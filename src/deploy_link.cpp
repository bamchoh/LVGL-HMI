#include "deploy_link.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using dsock_t = SOCKET;
static const dsock_t kNoSock = INVALID_SOCKET;
static void dsock_close(dsock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <unistd.h>
using dsock_t = int;
static const dsock_t kNoSock = -1;
static void dsock_close(dsock_t s) { ::close(s); }
#endif

// (a socket the other end has closed must give an error, not end the program: SIGPIPE)
#ifdef MSG_NOSIGNAL
static const int kSendFlags = MSG_NOSIGNAL;
#else
static const int kSendFlags = 0;
#endif

namespace fs = std::filesystem;

namespace hml {
namespace {

using Clock = std::chrono::steady_clock;

bool fail(std::string* error, const std::string& msg)
{
    if(error) *error = msg;
    return false;
}

std::string tls_error(int rc)
{
    char buf[160] = "";
    mbedtls_strerror(rc, buf, sizeof(buf));
    return buf[0] ? std::string(buf) : "error " + std::to_string(rc);
}

void sockets_init()
{
#ifdef _WIN32
    static const bool once = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    (void)once;
#endif
    static const bool psa = (psa_crypto_init() == PSA_SUCCESS);
    (void)psa;
}

// > 0 readable, 0 not within the time
int wait_readable(dsock_t s, int ms)
{
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    timeval tv{ ms / 1000, (ms % 1000) * 1000 };
    return select(static_cast<int>(s) + 1, &r, nullptr, nullptr, &tv);
}

void set_timeouts(dsock_t s, int send_ms)
{
#ifdef _WIN32
    DWORD v = static_cast<DWORD>(send_ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&v), sizeof(v));
#else
    timeval tv{ send_ms / 1000, (send_ms % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
}

// A random source for one piece of work.
struct Rng {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    bool ok = false;
    Rng()
    {
        sockets_init();
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        static const char pers[] = "hml-deploy";
        ok = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char*>(pers), sizeof(pers) - 1) == 0;
    }
    ~Rng()
    {
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
};

bool read_text(const fs::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if(!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_text(const fs::path& p, const std::string& data, bool secret)
{
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if(!f) return false;
#ifndef _WIN32
        if(secret) ::chmod(p.c_str(), 0600);                       // before the key is in it
#else
        (void)secret;
#endif
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if(!f.good()) return false;
    }
    return true;
}

bool fingerprint_of_pem(const std::string& cert_pem, std::string& out)
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    const bool ok = mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(cert_pem.c_str()), cert_pem.size() + 1) == 0;
    if(ok) out = deploy_fingerprint(crt.raw.p, crt.raw.len);
    mbedtls_x509_crt_free(&crt);
    return ok;
}

} // namespace

std::string deploy_fingerprint(const unsigned char* der, size_t len)
{
    unsigned char hash[32];
    mbedtls_sha256(der, len, hash, 0);
    unsigned char b64[64];
    size_t n = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &n, hash, sizeof(hash));
    std::string s(reinterpret_cast<const char*>(b64), n);
    while(!s.empty() && s.back() == '=') s.pop_back();
    return "SHA256:" + s;
}

std::string deploy_file_sha256(const fs::path& file)
{
    std::ifstream f(file, std::ios::binary);
    if(!f) return "";
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    std::vector<char> buf(64 * 1024);
    while(f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if(n > 0) mbedtls_sha256_update(&c, reinterpret_cast<const unsigned char*>(buf.data()), static_cast<size_t>(n));
    }
    unsigned char hash[32];
    mbedtls_sha256_finish(&c, hash);
    mbedtls_sha256_free(&c);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for(unsigned char b : hash) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

bool deploy_make_identity(const std::string& common_name, DeployIdentity& out, std::string* error)
{
    Rng rng;
    if(!rng.ok) return fail(error, "no random numbers");
    std::string cn;
    for(char c : common_name)
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') cn += c;
    if(cn.empty()) cn = "hml";
    if(cn.size() > 48) cn.resize(48);
    const std::string name = "CN=" + cn + ",O=Tsukuraku";

    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    std::vector<unsigned char> buf(8192);
    int rc = 0;
    bool ok = false;
    do {
        if((rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) != 0) break;
        if((rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &rng.drbg)) != 0) break;
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);
        if((rc = mbedtls_x509write_crt_set_subject_name(&crt, name.c_str())) != 0) break;
        if((rc = mbedtls_x509write_crt_set_issuer_name(&crt, name.c_str())) != 0) break;
        unsigned char serial[16];
        if((rc = mbedtls_ctr_drbg_random(&rng.drbg, serial, sizeof(serial))) != 0) break;
        serial[0] = static_cast<unsigned char>((serial[0] & 0x7f) | 0x01);
        if((rc = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial))) != 0) break;
        if((rc = mbedtls_x509write_crt_set_validity(&crt, "20240101000000", "20991231235959")) != 0) break;
        if((rc = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1)) != 0) break;
        if((rc = mbedtls_x509write_crt_pem(&crt, buf.data(), buf.size(), mbedtls_ctr_drbg_random, &rng.drbg)) != 0) break;
        out.cert_pem = reinterpret_cast<const char*>(buf.data());
        std::fill(buf.begin(), buf.end(), static_cast<unsigned char>(0));
        if((rc = mbedtls_pk_write_key_pem(&key, buf.data(), buf.size())) != 0) break;
        out.key_pem = reinterpret_cast<const char*>(buf.data());
        ok = fingerprint_of_pem(out.cert_pem, out.fingerprint);
    } while(false);
    std::fill(buf.begin(), buf.end(), static_cast<unsigned char>(0));
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    if(!ok) return fail(error, "cannot make a certificate: " + (rc ? tls_error(rc) : std::string("it cannot be read back")));
    return true;
}

bool deploy_identity(const fs::path& dir, const std::string& base, const std::string& common_name, DeployIdentity& out, std::string* error)
{
    std::error_code ec;
    const fs::path crt = dir / (base + ".crt"), key = dir / (base + ".key");
    const bool has_crt = fs::exists(crt, ec), has_key = fs::exists(key, ec);
    if(has_crt != has_key) return fail(error, "only one of " + crt.u8string() + " and " + key.u8string() + " is there: delete it to get a new pair (the other end has to accept the new one)");
    if(has_crt) {
        if(!read_text(crt, out.cert_pem) || !read_text(key, out.key_pem)) return fail(error, "cannot read " + crt.u8string() + " / " + key.u8string());
        if(!fingerprint_of_pem(out.cert_pem, out.fingerprint)) return fail(error, crt.u8string() + " is not a certificate");
        // (the key is looked at when it is first used: a wrong one fails the TLS setup with a message)
        return true;
    }
    fs::create_directories(dir, ec);
    if(!fs::is_directory(dir, ec)) return fail(error, "cannot create the folder " + dir.u8string());
    if(!deploy_make_identity(common_name, out, error)) return false;
    if(!write_text(key, out.key_pem, true) || !write_text(crt, out.cert_pem, false)) {
        fs::remove(key, ec);
        fs::remove(crt, ec);
        return fail(error, "cannot write into " + dir.u8string());
    }
    return true;
}

// ================================================================ one connection

struct DeployConn::Impl {
    dsock_t sock = kNoSock;
    bool    own_sock = false;                // connect(): ours to close
    bool    up = false;
    const std::atomic<bool>* cancel = nullptr;
    std::string peer;
    std::string pending;                     // read but not yet handed out (read_line)

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config  conf;
    mbedtls_x509_crt    cert;
    mbedtls_pk_context  key;
    Rng                 rng;

    Impl()
    {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&cert);
        mbedtls_pk_init(&key);
    }
    ~Impl()
    {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&key);
    }
    bool cancelled() const { return cancel && cancel->load(); }

    static int bio_send(void* ctx, const unsigned char* buf, size_t len)
    {
        Impl* m = static_cast<Impl*>(ctx);
        const int n = static_cast<int>(::send(m->sock, reinterpret_cast<const char*>(buf), static_cast<int>(std::min<size_t>(len, 1 << 20)), kSendFlags));
        return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : n;
    }
    // (timeout 0 = no limit, as mbedTLS says; every 200 ms the cancel flag is looked at)
    static int bio_recv(void* ctx, unsigned char* buf, size_t len, uint32_t timeout)
    {
        Impl* m = static_cast<Impl*>(ctx);
        const auto until = Clock::now() + std::chrono::milliseconds(timeout);
        for(;;) {
            if(m->cancelled()) return MBEDTLS_ERR_SSL_TIMEOUT;
            int slice = 200;
            if(timeout) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
                if(left <= 0) return MBEDTLS_ERR_SSL_TIMEOUT;
                slice = static_cast<int>(std::min<long long>(200, left));
            }
            const int r = wait_readable(m->sock, slice);
            if(r > 0) break;
            if(r < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        }
        const int n = static_cast<int>(::recv(m->sock, reinterpret_cast<char*>(buf), static_cast<int>(std::min<size_t>(len, 1 << 20)), 0));
        if(n == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
        return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : n;
    }

    bool handshake(bool server, const DeployIdentity& me, int timeout_ms, std::string* error)
    {
        if(!rng.ok) return fail(error, "no random numbers");
        int rc = 0;
        if((rc = mbedtls_x509_crt_parse(&cert, reinterpret_cast<const unsigned char*>(me.cert_pem.c_str()), me.cert_pem.size() + 1)) != 0)
            return fail(error, "own certificate: " + tls_error(rc));
        if((rc = mbedtls_pk_parse_key(&key, reinterpret_cast<const unsigned char*>(me.key_pem.c_str()), me.key_pem.size() + 1, nullptr, 0, mbedtls_ctr_drbg_random, &rng.drbg)) != 0)
            return fail(error, "own key: " + tls_error(rc));
        if((rc = mbedtls_ssl_config_defaults(&conf, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
            return fail(error, "TLS: " + tls_error(rc));
        mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &rng.drbg);
        // Nobody signs these certificates: the chain is not what is checked. The other end must
        // show one (TLS proves it holds the key), and its fingerprint is compared by the caller.
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        if((rc = mbedtls_ssl_conf_own_cert(&conf, &cert, &key)) != 0) return fail(error, "own certificate and key: " + tls_error(rc));
        mbedtls_ssl_conf_read_timeout(&conf, static_cast<uint32_t>(timeout_ms));
        if((rc = mbedtls_ssl_setup(&ssl, &conf)) != 0) return fail(error, "TLS: " + tls_error(rc));
        if(!server) mbedtls_ssl_set_hostname(&ssl, nullptr);            // (no name to compare: the fingerprint is)
        mbedtls_ssl_set_bio(&ssl, this, bio_send, nullptr, bio_recv);
        while((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
            if(rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            return fail(error, cancelled() ? "stopped" : rc == MBEDTLS_ERR_SSL_TIMEOUT ? "no answer (TLS)" : "TLS: " + tls_error(rc));
        }
        const mbedtls_x509_crt* other = mbedtls_ssl_get_peer_cert(&ssl);
        if(!other || !other->raw.p || other->raw.len == 0) return fail(error, "the other end showed no certificate");
        peer = deploy_fingerprint(other->raw.p, other->raw.len);
        up = true;
        return true;
    }
};

DeployConn::DeployConn() : impl_(std::make_unique<Impl>()) {}
DeployConn::~DeployConn() { close(); }
void DeployConn::set_cancel(const std::atomic<bool>* cancel) { impl_->cancel = cancel; }
const std::string& DeployConn::peer_fingerprint() const { return impl_->peer; }

bool DeployConn::accept(std::intptr_t sock, const DeployIdentity& me, int timeout_ms, std::string* error)
{
    Impl& m = *impl_;
    m.sock = static_cast<dsock_t>(sock);
    m.own_sock = false;
    set_timeouts(m.sock, 30000);
    return m.handshake(true, me, timeout_ms, error);
}

bool DeployConn::connect(const std::string& host, int port, const DeployIdentity& me, int timeout_ms, std::string* error)
{
    Impl& m = *impl_;
    sockets_init();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return fail(error, "unknown host '" + host + "'");
    dsock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(s == kNoSock) { freeaddrinfo(res); return fail(error, "cannot create a socket"); }
    // connect with a time limit: not blocking, wait until it can be written to
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    const int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
    bool ok = ::connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0;
    freeaddrinfo(res);
    if(!ok) {
        const auto until = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while(!ok && Clock::now() < until && !m.cancelled()) {
            fd_set w, e;
            FD_ZERO(&w); FD_ZERO(&e);
            FD_SET(s, &w); FD_SET(s, &e);
            timeval tv{ 0, 200 * 1000 };
            if(select(static_cast<int>(s) + 1, nullptr, &w, &e, &tv) <= 0) continue;
            int so_err = 0;
#ifdef _WIN32
            int len = sizeof(so_err);
#else
            socklen_t len = sizeof(so_err);
#endif
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_err), &len);
            if(so_err != 0 || FD_ISSET(s, &e)) break;
            ok = FD_ISSET(s, &w) != 0;
        }
    }
    if(!ok) { dsock_close(s); return fail(error, "cannot connect to " + host + ":" + std::to_string(port) + " (is \"Receive projects\" on at the device, System > Deploy?)"); }
#ifdef _WIN32
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fl);
#endif
    m.sock = s;
    m.own_sock = true;
    set_timeouts(s, 30000);
    return m.handshake(false, me, timeout_ms, error);
}

void DeployConn::close()
{
    Impl& m = *impl_;
    if(m.up) { mbedtls_ssl_close_notify(&m.ssl); m.up = false; }
    if(m.own_sock && m.sock != kNoSock) dsock_close(m.sock);
    m.sock = kNoSock;
}

bool DeployConn::write(const void* data, size_t len)
{
    Impl& m = *impl_;
    if(!m.up) return false;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    while(len > 0) {
        if(m.cancelled()) return false;
        const int rc = mbedtls_ssl_write(&m.ssl, p, len);
        if(rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if(rc <= 0) { m.up = false; return false; }
        p += rc;
        len -= static_cast<size_t>(rc);
    }
    return true;
}

bool DeployConn::write_line(const std::string& line)
{
    const std::string s = line + "\n";
    return write(s.data(), s.size());
}

int DeployConn::read_some(void* buf, size_t max, int timeout_ms)
{
    Impl& m = *impl_;
    if(!m.up) return -1;
    if(!m.pending.empty()) {
        const size_t n = std::min(max, m.pending.size());
        std::memcpy(buf, m.pending.data(), n);
        m.pending.erase(0, n);
        return static_cast<int>(n);
    }
    // nothing decrypted is waiting: wait for the socket first, so that "nothing came" leaves
    // the TLS state as it is
    if(mbedtls_ssl_get_bytes_avail(&m.ssl) == 0) {
        const auto until = Clock::now() + std::chrono::milliseconds(timeout_ms);
        for(;;) {
            if(m.cancelled()) return -1;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
            const int r = wait_readable(m.sock, static_cast<int>(std::clamp<long long>(left, 0, 200)));
            if(r > 0) break;
            if(r < 0) { m.up = false; return -1; }
            if(left <= 0) return 0;
        }
    }
    mbedtls_ssl_conf_read_timeout(&m.conf, 15000);                  // (a record that started must end)
    for(;;) {
        const int rc = mbedtls_ssl_read(&m.ssl, static_cast<unsigned char*>(buf), max);
        if(rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if(rc <= 0) { m.up = false; return -1; }                    // closed (also a clean close) or broken
        return rc;
    }
}

bool DeployConn::read_line(std::string& out, size_t max, int timeout_ms)
{
    Impl& m = *impl_;
    out.clear();
    const auto until = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string acc;
    acc.swap(m.pending);
    for(;;) {
        const size_t nl = acc.find('\n');
        if(nl != std::string::npos) {
            out = acc.substr(0, nl);
            if(!out.empty() && out.back() == '\r') out.pop_back();
            m.pending = acc.substr(nl + 1);
            return true;
        }
        if(acc.size() > max) return false;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
        if(left <= 0) return false;
        char buf[1024];
        const int n = read_some(buf, sizeof(buf), static_cast<int>(std::min<long long>(left, 500)));
        if(n < 0) return false;
        if(n > 0) acc.append(buf, static_cast<size_t>(n));
    }
}

// ================================================================ the designer's side

DeployOutcome deploy_send(const DeploySend& req, const DeployIdentity& me)
{
    DeployOutcome out;
    auto state = [&](const std::string& s) { if(req.on_state) req.on_state(s); };
    auto stop = [&](const std::string& why) { out.error = why; return out; };

    struct Item { const char* verb; bool runtime; std::string path, hash; std::uintmax_t size = 0; };
    std::vector<Item> items;
    if(!req.probe) {
        if(!req.runtime_file.empty()) items.push_back({ "RUNTIME", true, req.runtime_file, "", 0 });
        if(!req.file.empty()) items.push_back({ "PUT", false, req.file, "", 0 });
        if(items.empty()) return stop("nothing to send");
        for(Item& it : items) {
            std::error_code ec;
            it.size = fs::file_size(fs::u8path(it.path), ec);
            if(ec || it.size == 0) return stop("cannot read " + it.path);
            it.hash = deploy_file_sha256(fs::u8path(it.path));
            if(it.hash.empty()) return stop("cannot read " + it.path);
        }
    }

    DeployConn c;
    c.set_cancel(req.cancel);
    std::string err;
    if(!c.connect(req.host, req.port, me, req.connect_timeout_ms, &err)) return stop(err);
    out.device_fingerprint = c.peer_fingerprint();
    if(req.probe) { out.ok = true; return out; }
    if(req.expect_fingerprint.empty()) return stop("the device is not known yet: its fingerprint has to be confirmed first");
    if(out.device_fingerprint != req.expect_fingerprint)
        return stop("the device at " + req.host + " is not the one that was confirmed before (its certificate changed: reinstalled, or another device). Nothing was sent");
    state("connected");

    std::string name;
    for(char ch : req.client_name) if(ch > 0x20 && ch < 0x7f) name += ch;
    if(!c.write_line("HML-DEPLOY 1 " + (name.empty() ? std::string("designer") : name.substr(0, 60)))) return stop("the device closed the connection");
    const auto until = Clock::now() + std::chrono::milliseconds(req.approve_timeout_ms);
    for(;;) {
        std::string line;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
        if(left <= 0) return stop("the device did not answer");
        if(!c.read_line(line, 1024, static_cast<int>(left))) return stop(req.cancel && req.cancel->load() ? "stopped" : "the device closed the connection");
        if(line == "READY") break;
        if(line.rfind("PENDING", 0) == 0) { state("pending " + me.fingerprint); continue; }
        if(line.rfind("REJECTED", 0) == 0) return stop(line.size() > 9 ? "the device refused: " + line.substr(9) : "the operator of the device refused this designer");
        if(line == "TIMEOUT") return stop("nobody accepted this designer at the device in time");
        return stop(line.rfind("ERROR ", 0) == 0 ? "the device: " + line.substr(6) : "unexpected answer from the device");
    }

    for(const Item& it : items) {
        const std::string what = it.runtime ? "the runtime" : "the project";
        std::string base = fs::u8path(it.path).filename().u8string();
        for(char& ch : base) if(static_cast<unsigned char>(ch) <= 0x20 || ch == 0x7f) ch = '_';
        state(it.runtime ? "sending runtime" : "sending");
        if(!c.write_line(std::string(it.verb) + " " + std::to_string(it.size) + " " + it.hash + " " + base)) return stop("the device closed the connection");
        {
            std::string go;
            if(!c.read_line(go, 1024, 30000)) return stop("the device did not answer");
            if(go != "SEND") return stop(go.rfind("ERROR ", 0) == 0 ? "the device: " + go.substr(6) : "unexpected answer from the device");
        }
        {
            std::ifstream f(fs::u8path(it.path), std::ios::binary);
            std::vector<char> buf(64 * 1024);
            std::uintmax_t sent = 0;
            int shown = -1;
            while(sent < it.size) {
                f.read(buf.data(), static_cast<std::streamsize>(std::min<std::uintmax_t>(buf.size(), it.size - sent)));
                const std::streamsize n = f.gcount();
                if(n <= 0) return stop("cannot read " + it.path);
                if(!c.write(buf.data(), static_cast<size_t>(n))) return stop(req.cancel && req.cancel->load() ? "stopped" : "the connection broke while sending " + what);
                sent += static_cast<std::uintmax_t>(n);
                const int pct = static_cast<int>(sent * 100 / it.size);
                if(!it.runtime && pct / 10 != shown / 10) { shown = pct; state("sent " + std::to_string(pct)); }
            }
        }
        std::string line;
        if(!c.read_line(line, 1024, 60000)) return stop("the device did not confirm " + what);
        if(line != "RECEIVED") return stop(line.rfind("ERROR ", 0) == 0 ? "the device: " + line.substr(6) : "unexpected answer from the device");
        state(it.runtime ? "runtime received" : "received");
        const auto answer_by = Clock::now() + std::chrono::milliseconds(req.install_timeout_ms);
        for(;;) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(answer_by - Clock::now()).count();
            if(left <= 0 || !c.read_line(line, 2048, static_cast<int>(left))) return stop(what + " arrived, but the device did not say whether it was installed");
            if(line != "ASKING") break;
            state("asking");
        }
        if(line != "INSTALLED") return stop(line.rfind("ERROR ", 0) == 0 ? what + " was not installed: " + line.substr(6) : "unexpected answer from the device");
        if(it.runtime) state("runtime installed");
    }
    out.ok = true;
    return out;
}

} // namespace hml
