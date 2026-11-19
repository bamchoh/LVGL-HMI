#include "mqtt.hpp"
#include <cstring>
#include "util.hpp"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <fcntl.h>
#  include <netdb.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

namespace hml {
namespace mqtt {
namespace {

void put16(MqttBytes& b, uint16_t v) { b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
void put_str(MqttBytes& b, const std::string& s)
{
    const size_t n = std::min<size_t>(s.size(), 0xFFFF);
    put16(b, static_cast<uint16_t>(n));
    b.insert(b.end(), s.begin(), s.begin() + static_cast<long>(n));
}
// fixed header + body
MqttBytes packet(uint8_t first, const MqttBytes& body)
{
    MqttBytes out{ first };
    size_t len = body.size();
    do {
        uint8_t d = static_cast<uint8_t>(len % 128);
        len /= 128;
        if(len) d |= 0x80;
        out.push_back(d);
    } while(len);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace

MqttBytes connect(const Connect& c)
{
    MqttBytes b;
    put_str(b, "MQTT");
    b.push_back(4);                                      // protocol level 3.1.1
    uint8_t flags = c.clean_session ? 0x02 : 0;
    if(!c.will_topic.empty()) flags |= static_cast<uint8_t>(0x04 | (c.will_qos & 3) << 3 | (c.will_retain ? 0x20 : 0));
    if(c.has_username) flags |= 0x80;
    if(c.has_password && c.has_username) flags |= 0x40;  // (3.1.1: a password needs a user name)
    b.push_back(flags);
    put16(b, c.keepalive_s);
    put_str(b, c.client_id);
    if(!c.will_topic.empty()) { put_str(b, c.will_topic); put_str(b, c.will_payload); }
    if(c.has_username) put_str(b, c.username);
    if(c.has_password && c.has_username) put_str(b, c.password);
    return packet(CONNECT << 4, b);
}

MqttBytes publish(const std::string& topic, const std::string& payload, uint8_t qos, bool retain, uint16_t packet_id, bool dup)
{
    MqttBytes b;
    put_str(b, topic);
    if(qos > 0) put16(b, packet_id);
    b.insert(b.end(), payload.begin(), payload.end());
    return packet(static_cast<uint8_t>(PUBLISH << 4 | (dup ? 0x08 : 0) | (qos & 3) << 1 | (retain ? 1 : 0)), b);
}

MqttBytes subscribe(uint16_t packet_id, const std::vector<std::pair<std::string, uint8_t>>& topics)
{
    MqttBytes b;
    put16(b, packet_id);
    for(const auto& [t, q] : topics) { put_str(b, t); b.push_back(q & 1); }
    return packet(SUBSCRIBE << 4 | 0x02, b);
}

MqttBytes puback(uint16_t id) { MqttBytes b; put16(b, id); return packet(PUBACK << 4, b); }
MqttBytes pingreq() { return { PINGREQ << 4, 0 }; }
MqttBytes disconnect() { return { DISCONNECT << 4, 0 }; }

int remaining_length(const uint8_t* p, size_t have, size_t* value)
{
    size_t v = 0, mult = 1;
    for(int i = 0; i < 4; i++) {
        if(static_cast<size_t>(i) >= have) return 0;
        v += (p[i] & 0x7F) * mult;
        if(!(p[i] & 0x80)) {
            if(v > kMaxIncoming) return -1;
            *value = v;
            return i + 1;
        }
        mult *= 128;
    }
    return -1;
}

bool valid_utf8(const std::string& s)
{
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0, n = s.size();
    while(i < n) {
        unsigned c = p[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if(!len || i + len > n) return false;
        uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for(size_t k = 1; k < len; k++) { if((p[i + k] & 0xC0) != 0x80) return false; cp = cp << 6 | (p[i + k] & 0x3F); }
        if((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF) || cp == 0) return false;
        i += len;
    }
    return true;
}

bool valid_topic(const std::string& t)
{
    return !t.empty() && t.size() <= kMaxTopic && valid_utf8(t) && t.find_first_of("+#") == std::string::npos;
}

std::string connack_text(uint8_t code)
{
    switch(code) {
        case 0: return "accepted";
        case 1: return "unacceptable protocol version";
        case 2: return "client id rejected";
        case 3: return "server unavailable";
        case 4: return "bad user name or password";
        case 5: return "not authorized";
        default: return "refused (" + std::to_string(code) + ")";
    }
}

std::optional<Packet> parse(const uint8_t* p, size_t n, std::string* err)
{
    auto bad = [&](const char* m) -> std::optional<Packet> { if(err) *err = m; return std::nullopt; };
    if(n < 2) return bad("too short");
    size_t len = 0;
    const int lb = remaining_length(p + 1, n - 1, &len);
    if(lb <= 0) return bad("bad remaining length");
    if(1 + static_cast<size_t>(lb) + len != n) return bad("length does not match");
    const uint8_t* b = p + 1 + lb;
    Packet k;
    k.type = p[0] >> 4;
    k.flags = p[0] & 0x0F;
    auto u16 = [&](size_t at) { return static_cast<uint16_t>(b[at] << 8 | b[at + 1]); };
    switch(k.type) {
        case CONNACK:
            if(k.flags || len != 2 || (b[0] & 0xFE)) return bad("bad CONNACK");
            k.session_present = b[0] & 1;
            k.return_code = b[1];
            return k;
        case PUBLISH: {
            k.qos = (k.flags >> 1) & 3;
            k.retain = k.flags & 1;
            if(k.qos > 1) return bad("QoS 2 is not supported");
            if(len < 2) return bad("bad PUBLISH");
            const size_t tl = u16(0);
            size_t at = 2 + tl;
            if(at > len) return bad("bad PUBLISH topic length");
            k.topic.assign(reinterpret_cast<const char*>(b + 2), tl);
            if(!valid_topic(k.topic)) return bad("PUBLISH topic is not valid");
            if(k.qos) {
                if(at + 2 > len) return bad("bad PUBLISH packet id");
                k.packet_id = u16(at);
                if(k.packet_id == 0) return bad("PUBLISH packet id 0");
                at += 2;
            }
            k.payload.assign(reinterpret_cast<const char*>(b + at), len - at);
            return k;
        }
        case PUBACK:
            if(k.flags || len != 2) return bad("bad PUBACK");
            k.packet_id = u16(0);
            return k;
        case SUBACK:
            if(k.flags || len < 3) return bad("bad SUBACK");
            k.packet_id = u16(0);
            for(size_t i = 2; i < len; i++) {
                if(b[i] != 0 && b[i] != 1 && b[i] != 2 && b[i] != 0x80) return bad("bad SUBACK return code");
                k.granted.push_back(b[i]);
            }
            return k;
        case PINGRESP:
            if(k.flags || len != 0) return bad("bad PINGRESP");
            return k;
        default:
            return bad("not a packet a client receives");
    }
}

} // namespace mqtt

// ================================================================ the connection

struct MqttLink::Impl {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca, cert;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    bool tls = false, open = false, inited = false;
    std::string host;
    uint16_t port = 0;

    void init()
    {
        mbedtls_net_init(&net);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&ca);
        mbedtls_x509_crt_init(&cert);
        mbedtls_pk_init(&key);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        inited = true;
    }
    void free_all()
    {
        if(!inited) return;
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&ca);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_net_free(&net);
        inited = false;
    }
};

namespace {

// TCP connect with a time limit (mbedtls_net_connect waits as long as the OS does): the
// socket is made non-blocking for the connect, then blocking again for mbedTLS.
int connect_with_timeout(const std::string& host, uint16_t port, uint32_t timeout_ms, const std::function<bool()>& keep_going, std::string& err)
{
#ifdef _WIN32
    static bool wsa = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    (void)wsa;
#endif
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) { err = "cannot resolve '" + host + "'"; return -1; }
    int fd = -1;
    for(addrinfo* a = res; a && fd < 0; a = a->ai_next) {
#ifdef _WIN32
        SOCKET so = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if(so == INVALID_SOCKET) continue;
        u_long nb = 1;
        ioctlsocket(so, FIONBIO, &nb);
        int rc = ::connect(so, a->ai_addr, static_cast<int>(a->ai_addrlen));
        bool ok = rc == 0;
        if(!ok && WSAGetLastError() == WSAEWOULDBLOCK) {
            for(uint32_t waited = 0; waited < timeout_ms && (!keep_going || keep_going()); waited += 50) {
                fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(so, &w); FD_SET(so, &e);
                timeval tv{ 0, 50 * 1000 };
                const int n = select(0, nullptr, &w, &e, &tv);
                if(n == 0) continue;
                ok = n > 0 && FD_ISSET(so, &w);
                break;
            }
        }
        if(ok) { nb = 0; ioctlsocket(so, FIONBIO, &nb); fd = static_cast<int>(so); }
        else closesocket(so);
#else
        int so = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if(so < 0) continue;
        const int fl = fcntl(so, F_GETFL, 0);
        fcntl(so, F_SETFL, fl | O_NONBLOCK);
        int rc = ::connect(so, a->ai_addr, a->ai_addrlen);
        bool ok = rc == 0;
        if(!ok && errno == EINPROGRESS) {
            for(uint32_t waited = 0; waited < timeout_ms && (!keep_going || keep_going()); waited += 50) {
                pollfd pf{ so, POLLOUT, 0 };
                const int n = poll(&pf, 1, 50);
                if(n == 0) continue;
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                ok = n == 1 && getsockopt(so, SOL_SOCKET, SO_ERROR, &soerr, &sl) == 0 && soerr == 0;
                break;
            }
        }
        if(ok) { fcntl(so, F_SETFL, fl); fd = so; }
        else ::close(so);
#endif
    }
    freeaddrinfo(res);
    if(fd < 0) err = "cannot connect to " + host + ":" + std::to_string(port) + " (no answer within " + std::to_string(timeout_ms) + " ms, or refused)";
    return fd;
}

std::string tls_error(int rc)
{
    char buf[160];
    mbedtls_strerror(rc, buf, sizeof(buf));
    return vformat("%s (-0x%04X)", buf, static_cast<unsigned>(-rc));
}
} // namespace

MqttLink::MqttLink() : impl_(std::make_unique<Impl>()) {}
MqttLink::~MqttLink() { close(); }
bool MqttLink::is_open() const { return impl_->open; }
std::string MqttLink::describe() const { return impl_->host + ":" + std::to_string(impl_->port) + (impl_->tls ? " (TLS)" : ""); }

bool MqttLink::open(const std::string& host, uint16_t port, const MqttTls& tls, uint32_t timeout_ms)
{
    close();
    Impl& m = *impl_;
    m.init();
    m.host = host;
    m.port = port;
    m.tls = tls.enabled;
    err_.clear();
    static bool psa = (psa_crypto_init() == PSA_SUCCESS);   // (mbedTLS 3.6 uses PSA for TLS 1.3)
    (void)psa;
    const int fd = connect_with_timeout(host, port, timeout_ms, keep_going_, err_);
    if(fd < 0) { close(); return false; }
    m.net.fd = fd;
    int rc = 0;
    if(!tls.enabled) { m.open = true; return true; }

    const char* pers = "hml-mqtt";
    if((rc = mbedtls_ctr_drbg_seed(&m.drbg, mbedtls_entropy_func, &m.entropy, reinterpret_cast<const unsigned char*>(pers), std::strlen(pers))) != 0) {
        err_ = "TLS: random generator: " + tls_error(rc); close(); return false;
    }
    if((rc = mbedtls_ssl_config_defaults(&m.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        err_ = "TLS: " + tls_error(rc); close(); return false;
    }
    mbedtls_ssl_conf_min_tls_version(&m.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_rng(&m.conf, mbedtls_ctr_drbg_random, &m.drbg);
    if(tls.verify) {
        if(tls.ca_file.empty()) { err_ = "TLS: no ca_file to check the broker's certificate with"; close(); return false; }
        if((rc = mbedtls_x509_crt_parse_file(&m.ca, tls.ca_file.c_str())) < 0) { err_ = "TLS: ca_file '" + tls.ca_file + "': " + tls_error(rc); close(); return false; }
        mbedtls_ssl_conf_ca_chain(&m.conf, &m.ca, nullptr);
        mbedtls_ssl_conf_authmode(&m.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    }
    else mbedtls_ssl_conf_authmode(&m.conf, MBEDTLS_SSL_VERIFY_NONE);
    if(!tls.cert_file.empty()) {
        if((rc = mbedtls_x509_crt_parse_file(&m.cert, tls.cert_file.c_str())) != 0) { err_ = "TLS: cert_file: " + tls_error(rc); close(); return false; }
        if((rc = mbedtls_pk_parse_keyfile(&m.key, tls.key_file.c_str(), nullptr, mbedtls_ctr_drbg_random, &m.drbg)) != 0) { err_ = "TLS: key_file: " + tls_error(rc); close(); return false; }
        if((rc = mbedtls_ssl_conf_own_cert(&m.conf, &m.cert, &m.key)) != 0) { err_ = "TLS: " + tls_error(rc); close(); return false; }
    }
    mbedtls_ssl_conf_read_timeout(&m.conf, timeout_ms);
    if((rc = mbedtls_ssl_setup(&m.ssl, &m.conf)) != 0) { err_ = "TLS: " + tls_error(rc); close(); return false; }
    if((rc = mbedtls_ssl_set_hostname(&m.ssl, host.c_str())) != 0) { err_ = "TLS: " + tls_error(rc); close(); return false; }
    // the handshake on a non-blocking socket, waited for in 50 ms steps (time limit, stop)
    mbedtls_net_set_nonblock(&m.net);
    mbedtls_ssl_set_bio(&m.ssl, &m.net, mbedtls_net_send, mbedtls_net_recv, nullptr);
    uint32_t waited = 0;
    while((rc = mbedtls_ssl_handshake(&m.ssl)) != 0) {
        if(rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if(waited >= timeout_ms || (keep_going_ && !keep_going_())) { err_ = "TLS handshake with " + describe() + ": no answer within " + std::to_string(timeout_ms) + " ms"; close(); return false; }
            mbedtls_net_poll(&m.net, rc == MBEDTLS_ERR_SSL_WANT_READ ? MBEDTLS_NET_POLL_READ : MBEDTLS_NET_POLL_WRITE, 50);
            waited += 50;
            continue;
        }
        err_ = "TLS handshake with " + describe() + ": " + tls_error(rc);
        const uint32_t flags = mbedtls_ssl_get_verify_result(&m.ssl);
        if(flags != 0 && flags != static_cast<uint32_t>(-1)) {
            char vb[256];
            mbedtls_x509_crt_verify_info(vb, sizeof(vb), "", flags);
            std::string v = vb;
            while(!v.empty() && (v.back() == '\n' || v.back() == ' ')) v.pop_back();
            err_ += ": " + v;
        }
        close();
        return false;
    }
    mbedtls_net_set_block(&m.net);
    mbedtls_ssl_set_bio(&m.ssl, &m.net, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
    m.open = true;
    return true;
}

void MqttLink::close()
{
    Impl& m = *impl_;
    if(m.open && m.tls) mbedtls_ssl_close_notify(&m.ssl);
    m.open = false;
    m.free_all();
}

bool MqttLink::write(const MqttBytes& b)
{
    Impl& m = *impl_;
    if(!m.open) return false;
    size_t done = 0;
    while(done < b.size()) {
        int rc = m.tls ? mbedtls_ssl_write(&m.ssl, b.data() + done, b.size() - done) : mbedtls_net_send(&m.net, b.data() + done, b.size() - done);
        if(rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if(rc <= 0) { err_ = "write: " + tls_error(rc); return false; }
        done += static_cast<size_t>(rc);
    }
    return true;
}

int MqttLink::read(uint8_t* buf, size_t max, uint32_t timeout_ms)
{
    Impl& m = *impl_;
    if(!m.open) return -1;
    int rc;
    if(m.tls) {
        // pending decrypted bytes first, else wait for the socket
        if(mbedtls_ssl_get_bytes_avail(&m.ssl) == 0) {
            mbedtls_ssl_conf_read_timeout(&m.conf, timeout_ms);
        }
        rc = mbedtls_ssl_read(&m.ssl, buf, max);
        if(rc == MBEDTLS_ERR_SSL_TIMEOUT || rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if(rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) return 0;     // (TLS 1.3 brokers send these after the handshake)
#endif
    }
    else {
        rc = mbedtls_net_recv_timeout(&m.net, buf, max, timeout_ms);
        if(rc == MBEDTLS_ERR_SSL_TIMEOUT || rc == MBEDTLS_ERR_SSL_WANT_READ) return 0;
    }
    if(rc <= 0) { err_ = rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ? "closed by the broker" : "read: " + tls_error(rc); return -1; }
    return rc;
}

} // namespace hml
