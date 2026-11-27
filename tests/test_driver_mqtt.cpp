// MQTT client: packets byte by byte (mqtt.hpp), and the driver end to end against an in-process
// fake broker, plain TCP and TLS (mbedTLS server, test certificates in tests/assets/mqtt).
// Grouped by ISO/IEC 25010 characteristic.
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "mqtt.hpp"
#include "tag.hpp"
#include "util.hpp"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef HML_TEST_SOURCE_DIR
#  error "HML_TEST_SOURCE_DIR must point at the repository root"
#endif

using namespace hml;
using hmltest::Runtime;

namespace {

using Bytes = std::vector<uint8_t>;

std::string cert(const char* name)
{
    const char* env = std::getenv("HML_TEST_SOURCE_DIR");
    return std::string(env && *env ? env : HML_TEST_SOURCE_DIR) + "/tests/assets/mqtt/" + name;
}

Bytes bytes(std::initializer_list<int> l) { Bytes b; for(int v : l) b.push_back(static_cast<uint8_t>(v)); return b; }
Bytes cat(Bytes a, const std::string& s) { a.insert(a.end(), s.begin(), s.end()); return a; }

// ---------------------------------------------------------------- log capture

std::mutex g_log_mu;
std::vector<std::string> g_log;
void capture(LogLevel, const std::string& m) { std::lock_guard<std::mutex> l(g_log_mu); g_log.push_back(m); }
struct LogCapture {
    LogCapture() { { std::lock_guard<std::mutex> l(g_log_mu); g_log.clear(); } set_log_sink(capture); }
    ~LogCapture() { set_log_sink(nullptr); }
    bool has(const std::string& part) const
    {
        std::lock_guard<std::mutex> l(g_log_mu);
        for(const auto& m : g_log) if(m.find(part) != std::string::npos) return true;
        return false;
    }
};

// ---------------------------------------------------------------- fake broker

struct Got { std::string topic, payload; uint8_t qos; bool retain, dup; uint16_t id; };
struct ConnectInfo { std::string client_id, username, password, will_topic, will_payload; bool has_user = false, has_pass = false, will_retain = false; uint16_t keepalive = 0; };

class FakeBroker {
public:
    explicit FakeBroker(bool tls, const char* server_cert = "server") : tls_(tls)
    {
        psa_crypto_init();
        mbedtls_net_init(&listen_);
        // TLS: on "localhost" (the name in the certificate), the first address it resolves to, which is
        // the one the client tries first (no wait for a refused ::1 on Windows)
        REQUIRE(mbedtls_net_bind(&listen_, tls ? "localhost" : "127.0.0.1", "0", MBEDTLS_NET_PROTO_TCP) == 0);
        sockaddr_storage a{};
        socklen_t al = sizeof(a);
        REQUIRE(getsockname(listen_.fd, reinterpret_cast<sockaddr*>(&a), &al) == 0);
        port = ntohs(reinterpret_cast<sockaddr_in*>(&a)->sin_port);      // (same place in sockaddr_in6)
        mbedtls_net_set_nonblock(&listen_);
        if(tls_) {
            mbedtls_ssl_config_init(&conf_);
            mbedtls_x509_crt_init(&crt_);
            mbedtls_pk_init(&key_);
            mbedtls_entropy_init(&entropy_);
            mbedtls_ctr_drbg_init(&drbg_);
            REQUIRE(mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func, &entropy_, nullptr, 0) == 0);
            REQUIRE(mbedtls_x509_crt_parse_file(&crt_, cert((std::string(server_cert) + ".pem").c_str()).c_str()) == 0);
            REQUIRE(mbedtls_pk_parse_keyfile(&key_, cert((std::string(server_cert) + ".key").c_str()).c_str(), nullptr, mbedtls_ctr_drbg_random, &drbg_) == 0);
            REQUIRE(mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
            mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);
            REQUIRE(mbedtls_ssl_conf_own_cert(&conf_, &crt_, &key_) == 0);
            mbedtls_ssl_conf_read_timeout(&conf_, 20);
        }
        thread_ = std::thread([this] { run(); });
    }
    ~FakeBroker()
    {
        running_ = false;
        thread_.join();
        mbedtls_net_free(&listen_);
        if(tls_) {
            mbedtls_ssl_config_free(&conf_);
            mbedtls_x509_crt_free(&crt_);
            mbedtls_pk_free(&key_);
            mbedtls_ctr_drbg_free(&drbg_);
            mbedtls_entropy_free(&entropy_);
        }
    }

    uint16_t port = 0;
    std::atomic<int> connects{ 0 };            // CONNECT packets received
    std::atomic<int> tcp_accepts{ 0 };
    std::atomic<int> handshake_failures{ 0 };
    std::atomic<int> pubacks{ 0 };
    std::atomic<int> connack_code{ 0 };
    std::atomic<bool> ack_qos1{ true };
    std::atomic<bool> accepting{ true };       // false: every connection is closed at once

    std::vector<Got> got() { std::lock_guard<std::mutex> l(mu_); return got_; }
    std::vector<Got> got(const std::string& topic)
    {
        std::vector<Got> r;
        for(auto& g : got()) if(g.topic == topic) r.push_back(g);
        return r;
    }
    std::vector<std::string> subscriptions() { std::lock_guard<std::mutex> l(mu_); return subs_; }
    ConnectInfo last_connect() { std::lock_guard<std::mutex> l(mu_); return connect_; }
    void send(const Bytes& b) { std::lock_guard<std::mutex> l(mu_); out_.push_back(b); }
    void drop() { drop_ = true; }
    bool online() const { return online_; }

private:
    void run()
    {
        while(running_) {
            mbedtls_net_context c;
            mbedtls_net_init(&c);
            const int rc = mbedtls_net_accept(&listen_, &c, nullptr, 0, nullptr);
            if(rc != 0) { mbedtls_net_free(&c); sleep_ms(5); continue; }
            tcp_accepts++;
            mbedtls_net_set_block(&c);
            if(accepting) serve(c);
            mbedtls_net_free(&c);
        }
    }

    void serve(mbedtls_net_context& c)
    {
        mbedtls_ssl_context ssl;
        mbedtls_ssl_init(&ssl);
        if(tls_) {
            mbedtls_ssl_setup(&ssl, &conf_);
            mbedtls_ssl_set_bio(&ssl, &c, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            int rc;
            while((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
                if((rc == MBEDTLS_ERR_SSL_TIMEOUT || rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
                   && running_ && std::chrono::steady_clock::now() < until) continue;
                handshake_failures++;
                mbedtls_ssl_free(&ssl);
                return;
            }
        }
        auto io_read = [&](uint8_t* b, size_t n) -> int {
            int r = tls_ ? mbedtls_ssl_read(&ssl, b, n) : mbedtls_net_recv_timeout(&c, b, n, 20);
            if(r == MBEDTLS_ERR_SSL_TIMEOUT || r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
            return r > 0 ? r : -1;
        };
        auto io_write = [&](const Bytes& b) {
            size_t done = 0;
            while(done < b.size()) {
                int r = tls_ ? mbedtls_ssl_write(&ssl, b.data() + done, b.size() - done) : mbedtls_net_send(&c, b.data() + done, b.size() - done);
                if(r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_READ) continue;
                if(r <= 0) return false;
                done += static_cast<size_t>(r);
            }
            return true;
        };
        Bytes in;
        drop_ = false;
        while(running_ && !drop_) {
            {
                std::vector<Bytes> o;
                { std::lock_guard<std::mutex> l(mu_); o.swap(out_); }
                for(auto& b : o) if(!io_write(b)) goto done;
            }
            uint8_t buf[4096];
            const int n = io_read(buf, sizeof(buf));
            if(n < 0) break;
            in.insert(in.end(), buf, buf + n);
            while(in.size() >= 2) {
                size_t len = 0;
                const int lb = mqtt::remaining_length(in.data() + 1, in.size() - 1, &len);
                if(lb <= 0 || in.size() < 1 + lb + len) break;
                Bytes pk(in.begin(), in.begin() + 1 + lb + static_cast<long>(len));
                in.erase(in.begin(), in.begin() + static_cast<long>(pk.size()));
                if(!handle(pk.data()[0], pk.data() + 1 + lb, len, io_write)) goto done;
            }
        }
    done:
        online_ = false;
        if(tls_) mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_free(&ssl);
    }

    template <class W> bool handle(uint8_t first, const uint8_t* b, size_t len, W& write)
    {
        auto u16 = [&](size_t at) { return static_cast<uint16_t>(b[at] << 8 | b[at + 1]); };
        auto str = [&](size_t& at) { const size_t n = u16(at); std::string s(reinterpret_cast<const char*>(b + at + 2), n); at += 2 + n; return s; };
        switch(first >> 4) {
            case mqtt::CONNECT: {
                size_t at = 0;
                str(at);                                     // "MQTT"
                at++;                                        // level
                const uint8_t flags = b[at++];
                ConnectInfo ci;
                ci.keepalive = u16(at); at += 2;
                ci.client_id = str(at);
                if(flags & 0x04) { ci.will_topic = str(at); ci.will_payload = str(at); ci.will_retain = flags & 0x20; }
                if(flags & 0x80) { ci.username = str(at); ci.has_user = true; }
                if(flags & 0x40) { ci.password = str(at); ci.has_pass = true; }
                { std::lock_guard<std::mutex> l(mu_); connect_ = ci; }
                connects++;
                const int code = connack_code;
                if(!write(bytes({ 0x20, 2, 0, code }))) return false;
                if(code) return false;
                online_ = true;
                return true;
            }
            case mqtt::PUBLISH: {
                Got g{};
                g.qos = (first >> 1) & 3;
                g.retain = first & 1;
                g.dup = first & 8;
                size_t at = 0;
                g.topic = str(at);
                if(g.qos) { g.id = u16(at); at += 2; }
                g.payload.assign(reinterpret_cast<const char*>(b + at), len - at);
                { std::lock_guard<std::mutex> l(mu_); got_.push_back(g); }
                if(g.qos == 1 && ack_qos1) return write(mqtt::puback(g.id));
                return true;
            }
            case mqtt::PUBACK: pubacks++; return true;
            case mqtt::SUBSCRIBE: {
                const uint16_t id = u16(0);
                size_t at = 2;
                Bytes ack = bytes({ 0x90, 0, id >> 8, id & 0xFF });
                while(at < len) { std::string t = str(at); at++; { std::lock_guard<std::mutex> l(mu_); subs_.push_back(t); } ack.push_back(1); }
                ack[1] = static_cast<uint8_t>(ack.size() - 2);
                return write(ack);
            }
            case mqtt::PINGREQ: return write(bytes({ 0xD0, 0 }));
            case mqtt::DISCONNECT: return false;
            default: return false;
        }
    }

    bool tls_;
    mbedtls_net_context listen_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt crt_;
    mbedtls_pk_context key_;
    mbedtls_entropy_context entropy_;
    mbedtls_ctr_drbg_context drbg_;
    std::thread thread_;
    std::atomic<bool> running_{ true }, drop_{ false }, online_{ false };
    std::mutex mu_;
    std::vector<Got> got_;
    std::vector<std::string> subs_;
    std::vector<Bytes> out_;
    ConnectInfo connect_;
};

int start(Runtime& rt, const json& cfg) { json arr = json::array({ cfg }); return rt.drivers.load(&arr); }

// Wait for cond, dispatching tag changes (the driver sees them on the UI thread).
bool wait_for(Runtime& rt, const std::function<bool()>& cond, uint32_t ms = 5000)
{
    const uint32_t t0 = millis();
    while(millis() - t0 < ms) {
        rt.pump();
        if(cond()) return true;
        sleep_ms(5);
    }
    rt.pump();
    return cond();
}

json plain_cfg(uint16_t port)
{
    return json{ { "type", "mqtt" }, { "host", "127.0.0.1" }, { "port", port }, { "tls", false }, { "reconnect_ms", 100 },
                 { "client_id", "hml-test" }, { "topic_prefix", "plant/p1/" }, { "status_topic", "status" }, { "status_tag", "online" } };
}

json tls_cfg(uint16_t port, const char* ca = "ca.pem")
{
    json c = plain_cfg(port);
    c["host"] = "localhost";
    c["tls"] = true;
    c["ca_file"] = cert(ca);
    c["timeout_ms"] = 3000;
    return c;
}

} // namespace

// ================================================================ functional suitability: packets

TEST_CASE("mqtt: packets byte by byte (MQTT 3.1.1)")
{
    mqtt::Connect c;
    c.client_id = "c1";
    Bytes want = cat(bytes({ 0x10, 14, 0, 4 }), "MQTT");
    for(int v : { 4, 0x02, 0, 30, 0, 2 }) want.push_back(static_cast<uint8_t>(v));
    want = cat(want, "c1");
    CHECK(mqtt::connect(c) == want);

    c.username = "u"; c.has_username = true;
    c.password = "p"; c.has_password = true;
    c.will_topic = "s"; c.will_payload = "offline";
    const Bytes full = mqtt::connect(c);
    CHECK(full[9] == 0xE6);                                 // clean, will, will retain, user, password
    // a password without a user name is not sent (3.1.1)
    c.has_username = false;
    CHECK((mqtt::connect(c)[9] & 0xC0) == 0);

    CHECK(mqtt::publish("a/b", "1", 0, true, 0) == cat(cat(bytes({ 0x31, 6, 0, 3 }), "a/b"), "1"));
    CHECK(mqtt::publish("a/b", "1", 1, false, 10) == cat(cat(cat(bytes({ 0x32, 8, 0, 3 }), "a/b"), std::string("\0\x0A", 2)), "1"));
    CHECK(mqtt::publish("a", "", 1, false, 7, true)[0] == 0x3A);                    // DUP
    CHECK(mqtt::subscribe(1, { { "x", 1 } }) == bytes({ 0x82, 6, 0, 1, 0, 1, 'x', 1 }));
    CHECK(mqtt::puback(0x1234) == bytes({ 0x40, 2, 0x12, 0x34 }));
    CHECK(mqtt::pingreq() == bytes({ 0xC0, 0 }));
    CHECK(mqtt::disconnect() == bytes({ 0xE0, 0 }));
    // a long payload: two length bytes
    const Bytes big = mqtt::publish("t", std::string(200, 'x'), 0, false, 0);
    CHECK(big[1] == (203 % 128 | 0x80));
    CHECK(big[2] == 203 / 128);
}

TEST_CASE("mqtt: remaining length, the edges")
{
    size_t v = 0;
    auto rl = [&](Bytes b, size_t have) { return mqtt::remaining_length(b.data(), have, &v); };
    CHECK(rl(bytes({ 0x00 }), 1) == 1); CHECK(v == 0);
    CHECK(rl(bytes({ 0x7F }), 1) == 1); CHECK(v == 127);
    CHECK(rl(bytes({ 0x80, 0x01 }), 2) == 2); CHECK(v == 128);
    CHECK(rl(bytes({ 0xFF, 0x7F }), 2) == 2); CHECK(v == 16383);
    CHECK(rl(bytes({ 0x80, 0x80, 0x01 }), 3) == 3); CHECK(v == 16384);
    CHECK(rl(bytes({ 0x80 }), 1) == 0);                                 // more bytes needed
    CHECK(rl(bytes({ 0x80, 0x80, 0x04 }), 3) == 3); CHECK(v == mqtt::kMaxIncoming);   // 64 KiB: the largest taken
    CHECK(rl(bytes({ 0x81, 0x80, 0x04 }), 3) == -1);                    // one more: refused
    CHECK(rl(bytes({ 0xFF, 0xFF, 0xFF, 0x7F }), 4) == -1);              // 256 MiB
    CHECK(rl(bytes({ 0x80, 0x80, 0x80, 0x80, 0x01 }), 5) == -1);        // five bytes
}

TEST_CASE("mqtt: packets a client receives are parsed, broken ones refused")
{
    auto parse = [](const Bytes& b) { return mqtt::parse(b.data(), b.size()); };
    auto ca = parse(bytes({ 0x20, 2, 1, 5 }));
    REQUIRE(ca);
    CHECK(ca->session_present);
    CHECK(ca->return_code == 5);
    CHECK(mqtt::connack_text(5) == "not authorized");
    auto pub = parse(cat(cat(bytes({ 0x33, 8, 0, 3 }), "a/b"), std::string("\0\x07" "v", 3)));
    REQUIRE(pub);
    CHECK(pub->topic == "a/b");
    CHECK(pub->payload == "v");
    CHECK(pub->qos == 1);
    CHECK(pub->retain);
    CHECK(pub->packet_id == 7);
    auto sa = parse(bytes({ 0x90, 4, 0, 1, 1, 0x80 }));
    REQUIRE(sa);
    CHECK(sa->granted == std::vector<uint8_t>{ 1, 0x80 });

    std::string err;
    auto refused = [&](const Bytes& b) { return !mqtt::parse(b.data(), b.size(), &err); };
    CHECK(refused(bytes({ 0x20, 2, 0 })));                                       // shorter than it says
    CHECK(refused(bytes({ 0x20, 3, 0, 0, 0 })));                                 // CONNACK of 3 bytes
    CHECK(refused(bytes({ 0x21, 2, 0, 0 })));                                    // flags
    CHECK(refused(cat(bytes({ 0x34, 4, 0, 1, 'a' }), "v")));                     // QoS 2
    CHECK(refused(cat(bytes({ 0x36, 4, 0, 1, 'a' }), "v")));                     // QoS 3
    CHECK(refused(cat(bytes({ 0x30, 4, 0, 1, '#' }), "v")));                     // wildcard in a received topic
    CHECK(refused(cat(bytes({ 0x30, 4, 0, 1, '+' }), "v")));
    CHECK(refused(bytes({ 0x30, 3, 0, 1, 0x00 })));                              // NUL
    CHECK(refused(bytes({ 0x30, 3, 0, 1, 0xC0 })));                              // not UTF-8
    CHECK(refused(bytes({ 0x30, 4, 0, 2, 0xED, 0xA0 })));                        // truncated
    CHECK(refused(bytes({ 0x30, 2, 0, 0 })));                                    // empty topic
    CHECK(refused(bytes({ 0x30, 3, 0, 9, 'a' })));                               // topic longer than the packet
    CHECK(refused(bytes({ 0x32, 5, 0, 1, 'a', 0, 0 })));                         // packet id 0
    CHECK(refused(bytes({ 0x90, 3, 0, 1, 7 })));                                 // SUBACK code 7
    CHECK(refused(bytes({ 0x10, 0 })));                                          // a CONNECT (server side packet)
    CHECK(refused(bytes({ 0x00, 0 })));
    CHECK(refused(bytes({ 0xF0, 0 })));
    CHECK(refused(bytes({ 0xD0, 1, 0 })));                                       // PINGRESP with a body
    CHECK_FALSE(err.empty());

    CHECK(mqtt::valid_utf8("温度/℃"));
    CHECK_FALSE(mqtt::valid_utf8(std::string("\xC0\xAF", 2)));                   // overlong "/"
    CHECK_FALSE(mqtt::valid_utf8(std::string("\xED\xA0\x80", 3)));               // surrogate
    CHECK_FALSE(mqtt::valid_topic(std::string(mqtt::kMaxTopic + 1, 'a')));
}

// ================================================================ security: fuzzing the parser

TEST_CASE("mqtt: random packets never crash the parser")
{
    std::mt19937 rng(12345);
    int accepted = 0;
    for(int i = 0; i < 100000; i++) {
        Bytes b(2 + rng() % 40);
        for(auto& x : b) x = static_cast<uint8_t>(rng());
        if(i % 2) {                                               // a plausible header: the length fits
            b[1] = static_cast<uint8_t>(b.size() - 2);
            b[0] = static_cast<uint8_t>((rng() % 16) << 4 | (rng() % 4 == 0 ? rng() % 16 : 0));
        }
        size_t v;
        mqtt::remaining_length(b.data() + 1, b.size() - 1, &v);
        if(mqtt::parse(b.data(), b.size())) accepted++;
    }
    MESSAGE("random packets accepted as well-formed: " << accepted);
    CHECK(accepted < 100000);
}

// ================================================================ functional suitability: driver

TEST_CASE("mqtt: values go out, online status and last will, commands come in")
{
    FakeBroker broker(false);
    Runtime rt;
    Tag* temp = rt.tag("temp", TagType::Float, 21.5);
    Tag* sp = rt.tag("speed_sp", TagType::Int, 10, 0, 100);
    Tag* run = rt.tag("run", TagType::Bool, 0);
    Tag* online = rt.tag("online", TagType::Bool, 0);
    rt.tag("label", "x");
    json cfg = plain_cfg(broker.port);
    cfg["publish"] = json::array({ "temp", json{ { "tag", "speed_sp" }, { "retain", true } } });
    cfg["subscribe"] = json::array({ json{ { "tag", "speed_sp" }, { "topic", "cmd/speed_sp" } }, "run", "label" });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return broker.online() && broker.subscriptions().size() == 3; }));
    CHECK(wait_for(rt, [&] { return online->get_number() == 1; }));

    const ConnectInfo ci = broker.last_connect();
    CHECK(ci.client_id == "hml-test");
    CHECK(ci.will_topic == "plant/p1/status");
    CHECK(ci.will_payload == "offline");
    CHECK(ci.will_retain);
    CHECK(ci.keepalive == 30);
    CHECK(broker.subscriptions() == std::vector<std::string>{ "plant/p1/cmd/speed_sp", "plant/p1/run", "plant/p1/label" });
    CHECK(wait_for(rt, [&] { return !broker.got("plant/p1/status").empty(); }));
    CHECK(broker.got("plant/p1/status")[0].payload == "online");
    CHECK(broker.got("plant/p1/status")[0].retain);

    // the values at start, then changes
    REQUIRE(wait_for(rt, [&] { return !broker.got("plant/p1/temp").empty() && !broker.got("plant/p1/speed_sp").empty(); }));
    CHECK(broker.got("plant/p1/temp")[0].payload == temp->get().format());
    CHECK(broker.got("plant/p1/speed_sp")[0].retain);
    temp->set_number(22.25, nullptr);
    CHECK(wait_for(rt, [&] { auto g = broker.got("plant/p1/temp"); return g.size() >= 2 && g.back().payload == "22.25"; }));

    // commands
    broker.send(mqtt::publish("plant/p1/cmd/speed_sp", "55", 0, false, 0));
    CHECK(wait_for(rt, [&] { return sp->get_number() == 55; }));
    broker.send(mqtt::publish("plant/p1/run", "true", 1, false, 3));
    CHECK(wait_for(rt, [&] { return run->get_number() == 1 && broker.pubacks == 1; }));     // QoS 1 acknowledged
    broker.send(mqtt::publish("plant/p1/label", "{\"v\":\"auto\",\"ts\":1}", 0, false, 0));
    CHECK(wait_for(rt, [&] { return tags().find("label")->get().str == "auto"; }));
    // a command is not sent back as a value
    sleep_ms(200); rt.pump(); sleep_ms(100);
    for(const Got& g : broker.got("plant/p1/speed_sp")) CHECK(g.payload != "55");
}

TEST_CASE("mqtt: JSON payload and publishing at an interval")
{
    FakeBroker broker(false);
    Runtime rt;
    rt.tag("level", TagType::Float, 3.5);
    rt.tag("on", TagType::Bool, 1);
    json cfg = plain_cfg(broker.port);
    cfg["payload"] = "json";
    cfg["publish"] = json::array({ json{ { "tag", "level" }, { "mode", "interval" }, { "interval_ms", 100 } }, "on" });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return broker.got("plant/p1/level").size() >= 4 && !broker.got("plant/p1/on").empty(); }));
    json j = json::parse(broker.got("plant/p1/level")[0].payload);
    CHECK(j["v"] == 3.5);
    CHECK(j["ts"].is_number_integer());
    CHECK(json::parse(broker.got("plant/p1/on")[0].payload)["v"] == true);
}

// ================================================================ security

TEST_CASE("mqtt: commands are checked before they are written")
{
    LogCapture log;
    FakeBroker broker(false);
    Runtime rt;
    Tag* sp = rt.tag("speed_sp", TagType::Int, 10, 0, 100);
    Tag* other = rt.tag("other", TagType::Int, 1);
    Tag* name = rt.tag("name", "a");
    json cfg = plain_cfg(broker.port);
    cfg["subscribe"] = json::array({ "speed_sp", "name", json{ { "tag", "other" }, { "topic", "cmd/#" } } });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return broker.subscriptions().size() == 2; }));       // the wildcard one was not taken
    CHECK(log.has("wildcards are not allowed"));

    for(const char* bad : { "101", "-1", "abc", "1e999", "nan", "", "12abc" })
        broker.send(mqtt::publish("plant/p1/speed_sp", bad, 0, false, 0));
    broker.send(mqtt::publish("plant/p1/name", std::string(300, 'x'), 0, false, 0));
    broker.send(mqtt::publish("plant/p1/name", std::string("\xFF\xFE", 2), 0, false, 0));
    broker.send(mqtt::publish("plant/p1/other", "5", 0, false, 0));                   // a topic nobody listed
    broker.send(mqtt::publish("plant/p1/speed_sp", "40", 0, false, 0));               // and finally a good one
    REQUIRE(wait_for(rt, [&] { return sp->get_number() == 40; }));
    CHECK(name->get().str == "a");
    CHECK(other->get_number() == 1);
    CHECK(log.has("101 is outside 0..100"));
    CHECK(log.has("refused: not a number"));
    CHECK(log.has("longer than 256 bytes"));
    CHECK(log.has("not UTF-8"));
    CHECK(broker.online());                                       // bad values do not cost the connection
}

TEST_CASE("mqtt: a broken packet from the broker ends the connection; nothing is written")
{
    LogCapture log;
    FakeBroker broker(false);
    Runtime rt;
    Tag* sp = rt.tag("sp", TagType::Int, 1);
    json cfg = plain_cfg(broker.port);
    cfg["subscribe"] = json::array({ "sp" });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return broker.subscriptions().size() == 1; }));
    broker.send(bytes({ 0x34, 7, 0, 4, 'p', 'l', 'a', 'n', '9' }));               // QoS 2 (and a short topic)
    CHECK(wait_for(rt, [&] { return broker.connects >= 2; }));                    // dropped, connected again
    CHECK(log.has("broken packet"));
    broker.send(bytes({ 0x30, 0xFF, 0xFF, 0xFF, 0x7F }));                         // 256 MiB announced
    CHECK(wait_for(rt, [&] { return broker.connects >= 3; }));
    CHECK(log.has("too large"));
    CHECK(sp->get_number() == 1);
}

TEST_CASE("mqtt: TLS, the broker's certificate is checked")
{
    LogCapture log;
    SUBCASE("signed by the CA in ca_file: connects") {
        FakeBroker broker(true);
        Runtime rt;
        rt.tag("online", TagType::Bool, 0);
        rt.tag("v", TagType::Int, 7);
        json cfg = tls_cfg(broker.port);
        cfg["publish"] = json::array({ "v" });
        cfg["username"] = "op";
        cfg["password"] = "s3cret-pw";
        REQUIRE(start(rt, cfg) == 1);
        REQUIRE(wait_for(rt, [&] { return !broker.got("plant/p1/v").empty(); }, 10000));
        CHECK(broker.got("plant/p1/v")[0].payload == "7");
        CHECK(broker.handshake_failures == 0);
        const ConnectInfo ci = broker.last_connect();
        CHECK(ci.username == "op");
        CHECK(ci.password == "s3cret-pw");
        CHECK(log.has("(TLS) connected"));
        CHECK_FALSE(log.has("s3cret-pw"));                       // never in the log
    }
    SUBCASE("another CA: refused before anything is sent") {
        FakeBroker broker(true);
        Runtime rt;
        rt.tag("online", TagType::Bool, 0);
        REQUIRE(start(rt, tls_cfg(broker.port, "other_ca.pem")) == 1);
        REQUIRE(wait_for(rt, [&] { return broker.tcp_accepts >= 2; }, 10000));
        CHECK(broker.connects == 0);
        CHECK(log.has("TLS handshake"));
        CHECK(tags().find("online")->get_number() == 0);
    }
    SUBCASE("a certificate for another host name: refused") {
        FakeBroker broker(true, "wrong_name");
        Runtime rt;
        rt.tag("online", TagType::Bool, 0);
        REQUIRE(start(rt, tls_cfg(broker.port)) == 1);
        REQUIRE(wait_for(rt, [&] { return broker.tcp_accepts >= 2; }, 10000));
        CHECK(broker.connects == 0);
        CHECK(log.has("expected CN"));
    }
    SUBCASE("verify false: connects, with a warning") {
        FakeBroker broker(true, "wrong_name");
        Runtime rt;
        rt.tag("online", TagType::Bool, 0);
        json cfg = tls_cfg(broker.port, "other_ca.pem");
        cfg["verify"] = false;
        REQUIRE(start(rt, cfg) == 1);
        CHECK(wait_for(rt, [&] { return broker.connects >= 1; }, 10000));
        CHECK(log.has("certificate is not checked"));
    }
    SUBCASE("plaintext against a TLS broker: no connection") {
        FakeBroker broker(true);
        Runtime rt;
        rt.tag("online", TagType::Bool, 0);
        json cfg = tls_cfg(broker.port);
        cfg["tls"] = false;
        REQUIRE(start(rt, cfg) == 1);
        REQUIRE(wait_for(rt, [&] { return broker.handshake_failures >= 1; }, 10000));
        CHECK(broker.connects == 0);
        CHECK(log.has("plaintext"));
    }
}

// ================================================================ reliability

TEST_CASE("mqtt: refused by the broker: logged, tried again")
{
    LogCapture log;
    FakeBroker broker(false);
    broker.connack_code = 5;
    Runtime rt;
    rt.tag("online", TagType::Bool, 0);
    REQUIRE(start(rt, plain_cfg(broker.port)) == 1);
    REQUIRE(wait_for(rt, [&] { return broker.connects >= 2; }));
    CHECK(log.has("not authorized"));
    CHECK(tags().find("online")->get_number() == 0);
    broker.connack_code = 0;
    CHECK(wait_for(rt, [&] { return tags().find("online")->get_number() == 1; }));
}

TEST_CASE("mqtt: while the broker is away only the newest value is kept; QoS 1 is sent again")
{
    FakeBroker broker(false);
    Runtime rt;
    Tag* online = rt.tag("online", TagType::Bool, 0);
    Tag* v = rt.tag("v", TagType::Int, 0);
    Tag* q = rt.tag("q", TagType::Int, 0);
    json cfg = plain_cfg(broker.port);
    cfg["publish"] = json::array({ "v", json{ { "tag", "q" }, { "qos", 1 } } });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return !broker.got("plant/p1/v").empty() && !broker.got("plant/p1/q").empty(); }));

    // away
    broker.accepting = false;
    broker.drop();
    REQUIRE(wait_for(rt, [&] { return online->get_number() == 0; }));
    const size_t before = broker.got("plant/p1/v").size();
    for(int i = 1; i <= 5; i++) { v->set_number(i, nullptr); rt.pump(); sleep_ms(20); }
    broker.accepting = true;
    REQUIRE(wait_for(rt, [&] { return broker.got("plant/p1/v").size() > before; }, 8000));
    sleep_ms(200);
    auto g = broker.got("plant/p1/v");
    CHECK(g.size() == before + 1);                                // 1..4 dropped
    CHECK(g.back().payload == "5");

    // a QoS 1 message the broker did not acknowledge comes again after a reconnect, as a duplicate
    broker.ack_qos1 = false;
    q->set_number(42, nullptr);
    REQUIRE(wait_for(rt, [&] { auto x = broker.got("plant/p1/q"); return !x.empty() && x.back().payload == "42"; }));
    const uint16_t id = broker.got("plant/p1/q").back().id;
    CHECK_FALSE(broker.got("plant/p1/q").back().dup);
    broker.ack_qos1 = true;
    broker.drop();
    REQUIRE(wait_for(rt, [&] { auto x = broker.got("plant/p1/q"); return x.back().dup; }, 8000));
    CHECK(broker.got("plant/p1/q").back().id == id);
    CHECK(broker.got("plant/p1/q").back().payload == "42");
}

TEST_CASE("mqtt: no broker: the driver stops quickly; a bad configuration is refused")
{
    Runtime rt;
    rt.tag("online", TagType::Bool, 0);
    json cfg = plain_cfg(1);                                       // nothing listens on port 1
    cfg["host"] = "127.0.0.1";
    REQUIRE(start(rt, cfg) == 1);
    sleep_ms(300);
    const uint32_t t0 = millis();
    rt.drivers.stop_all();
    CHECK(millis() - t0 < 500);

    CHECK(start(rt, json{ { "type", "mqtt" } }) == 0);            // no host
    CHECK(start(rt, json{ { "type", "mqtt" }, { "host", "h" }, { "cert_file", "c.pem" } }) == 0);   // no key_file
}

TEST_CASE("mqtt: JSON messages: several tags in one message out, fields of a message in")
{
    LogCapture log;
    FakeBroker broker(false);
    Runtime rt;
    Tag* temp = rt.tag("temp", TagType::Float, 211.5);
    rt.tag("count", TagType::Int, 3420);
    rt.tag("run", TagType::Bool, 1);
    rt.tag("product", "White bread");
    Tag* sp = rt.tag("oven_sp", TagType::Int, 200, 150, 260);
    Tag* rpm = rt.tag("rpm_sp", TagType::Float, 100);
    Tag* mode = rt.tag("mode", "auto");
    json cfg = plain_cfg(broker.port);
    cfg["publish"] = json::parse(R"([
        { "topic": "line1/status", "fields": { "/oven/temp": "temp", "/count": "count", "/run": "run", "/product": "product" },
          "const": { "/device": "line1" }, "ts": "/ts" },
        { "topic": "line1/beat", "fields": { "/count": "count" }, "mode": "interval", "interval_ms": 100 } ])");
    cfg["subscribe"] = json::parse(R"([ { "topic": "line1/cmd", "fields": { "/oven/setpoint": "oven_sp", "/mixer/rpm": "rpm_sp", "/mode": "mode" } } ])");
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return broker.online() && broker.subscriptions().size() == 1; }));
    CHECK(broker.subscriptions()[0] == "plant/p1/line1/cmd");

    // out: one message of all the fields, with the constants and the time; again when one changes
    REQUIRE(wait_for(rt, [&] { return !broker.got("plant/p1/line1/status").empty(); }));
    json m = json::parse(broker.got("plant/p1/line1/status")[0].payload);
    CHECK(m["oven"]["temp"] == 211.5);
    CHECK(m["count"] == 3420);
    CHECK(m["count"].is_number_integer());
    CHECK(m["run"] == true);
    CHECK(m["product"] == "White bread");
    CHECK(m["device"] == "line1");
    CHECK(m["ts"].is_number_integer());
    temp->set_number(212.0, nullptr);
    CHECK(wait_for(rt, [&] { auto g = broker.got("plant/p1/line1/status"); return g.size() >= 2 && json::parse(g.back().payload)["oven"]["temp"] == 212.0; }));
    CHECK(wait_for(rt, [&] { return broker.got("plant/p1/line1/beat").size() >= 3; }));

    // in: the fields that are there, checked like a command; the others stay
    broker.send(mqtt::publish("plant/p1/line1/cmd", R"({"oven":{"setpoint":220},"mixer":{"rpm":"135.5"},"other":1})", 0, false, 0));
    REQUIRE(wait_for(rt, [&] { return sp->get_number() == 220 && rpm->get_number() == 135.5; }));
    CHECK(mode->get().str == "auto");
    broker.send(mqtt::publish("plant/p1/line1/cmd", R"({"oven":{"setpoint":999},"mode":"manual","mixer":{"rpm":null}})", 0, false, 0));
    REQUIRE(wait_for(rt, [&] { return mode->get().str == "manual"; }));
    CHECK(sp->get_number() == 220);                                     // 999 is outside 150..260: refused
    CHECK(rpm->get_number() == 135.5);                                  // null: unchanged
    CHECK(wait_for(rt, [&] { return log.has("999 is outside 150..260"); }));   // (the line is written by the driver's thread: it may come a moment after the tag)
    broker.send(mqtt::publish("plant/p1/line1/cmd", "not json", 0, false, 0));
    broker.send(mqtt::publish("plant/p1/line1/cmd", R"({"oven":{"setpoint":{"v":1}}})", 0, false, 0));
    CHECK(wait_for(rt, [&] { return log.has("not a JSON object") && log.has("refused: not a value"); }));
    CHECK(sp->get_number() == 220);
    CHECK(broker.online());
}

TEST_CASE("mqtt: JSON messages: wrong settings are left out")
{
    LogCapture log;
    FakeBroker broker(false);
    Runtime rt;
    rt.tag("a", TagType::Int, 1);
    json cfg = plain_cfg(broker.port);
    cfg["publish"] = json::parse(R"([ { "fields": { "/a": "a" } }, { "topic": "t", "fields": { "a": "a", "/b": "nope", "": "a" } },
                                      { "topic": "ok", "fields": { "/a": "a", "/x": "nope" } } ])");
    cfg["subscribe"] = json::parse(R"([ { "topic": "c/#", "fields": { "/a": "a" } } ])");
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return !broker.got("plant/p1/ok").empty(); }));
    CHECK(json::parse(broker.got("plant/p1/ok")[0].payload) == json::parse(R"({"a":1})"));
    CHECK(log.has("a message needs a \"topic\""));
    CHECK(log.has("'a' is not a JSON pointer"));
    CHECK(log.has("unknown tag 'nope'"));
    CHECK(log.has("wildcards are not allowed"));
    CHECK(broker.subscriptions().empty());
}
