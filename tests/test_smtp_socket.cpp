// SMTP over real sockets (smtp_deliver): a small mail server in this process - plain, STARTTLS and
// TLS from the first byte - with the test certificates of tests/assets/mqtt. What smtp.hpp says
// line by line is tested in test_smtp.cpp; here it is the connection: the handshake in the middle
// of a conversation, certificates, a server that does not answer.
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

#include <doctest.h>
#include "smtp.hpp"
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
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef HML_TEST_SOURCE_DIR
#  error "HML_TEST_SOURCE_DIR must point at the repository root"
#endif

using namespace hml;

namespace {

std::string cert(const char* name)
{
    const char* env = std::getenv("HML_TEST_SOURCE_DIR");
    return std::string(env && *env ? env : HML_TEST_SOURCE_DIR) + "/tests/assets/mqtt/" + name;
}

enum class Mode { Plain, StartTls, Tls };

class FakeSmtpServer {
public:
    explicit FakeSmtpServer(Mode mode, const char* server_cert = "server") : mode_(mode)
    {
        psa_crypto_init();
        mbedtls_net_init(&listen_);
        // on "localhost" (the name in the certificate), the first address it resolves to
        REQUIRE(mbedtls_net_bind(&listen_, mode == Mode::Plain ? "127.0.0.1" : "localhost", "0", MBEDTLS_NET_PROTO_TCP) == 0);
        sockaddr_storage a{};
        socklen_t al = sizeof(a);
        REQUIRE(getsockname(listen_.fd, reinterpret_cast<sockaddr*>(&a), &al) == 0);
        port = ntohs(reinterpret_cast<sockaddr_in*>(&a)->sin_port);
        mbedtls_net_set_nonblock(&listen_);
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
        mbedtls_ssl_conf_read_timeout(&conf_, 50);
        thread_ = std::thread([this] { run(); });
    }
    ~FakeSmtpServer()
    {
        running_ = false;
        thread_.join();
        mbedtls_net_free(&listen_);
        mbedtls_ssl_config_free(&conf_);
        mbedtls_x509_crt_free(&crt_);
        mbedtls_pk_free(&key_);
        mbedtls_ctr_drbg_free(&drbg_);
        mbedtls_entropy_free(&entropy_);
    }

    uint16_t port = 0;
    std::atomic<int> connections{ 0 }, handshakes{ 0 }, handshake_failures{ 0 }, mails{ 0 };
    std::atomic<bool> silent{ false };         // accepts and never says a word
    std::atomic<bool> auth_in_tls{ false }, auth_seen{ false }, data_in_tls{ false };
    std::string data() { std::lock_guard<std::mutex> l(mu_); return data_; }
    std::string auth() { std::lock_guard<std::mutex> l(mu_); return auth_; }
    std::vector<std::string> plain_lines() { std::lock_guard<std::mutex> l(mu_); return plain_; }     // what came before any TLS

private:
    void run()
    {
        while(running_) {
            mbedtls_net_context c;
            mbedtls_net_init(&c);
            if(mbedtls_net_accept(&listen_, &c, nullptr, 0, nullptr) != 0) { mbedtls_net_free(&c); sleep_ms(5); continue; }
            connections++;
            mbedtls_net_set_block(&c);
            serve(c);
            mbedtls_net_free(&c);
        }
    }
    bool handshake(mbedtls_ssl_context& ssl, mbedtls_net_context& c)
    {
        mbedtls_ssl_setup(&ssl, &conf_);
        mbedtls_ssl_set_bio(&ssl, &c, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        int rc;
        while((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
            if((rc == MBEDTLS_ERR_SSL_TIMEOUT || rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) && running_ && std::chrono::steady_clock::now() < until) continue;
            handshake_failures++;
            return false;
        }
        handshakes++;
        return true;
    }
    void serve(mbedtls_net_context& c)
    {
        mbedtls_ssl_context ssl;
        mbedtls_ssl_init(&ssl);
        bool tls = false;
        if(mode_ == Mode::Tls) { if(!handshake(ssl, c)) { mbedtls_ssl_free(&ssl); return; } tls = true; }
        auto send = [&](const std::string& s) {
            size_t done = 0;
            while(done < s.size()) {
                const auto* p = reinterpret_cast<const unsigned char*>(s.data()) + done;
                int r = tls ? mbedtls_ssl_write(&ssl, p, s.size() - done) : mbedtls_net_send(&c, p, s.size() - done);
                if(r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_READ) continue;
                if(r <= 0) return false;
                done += static_cast<size_t>(r);
            }
            return true;
        };
        std::string in;
        // a line of the client ("" and false: it is gone, or we are told to stop)
        auto line = [&](std::string& out) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            for(;;) {
                const size_t nl = in.find("\r\n");
                if(nl != std::string::npos) { out = in.substr(0, nl); in.erase(0, nl + 2); return true; }
                if(!running_ || std::chrono::steady_clock::now() > until) return false;
                unsigned char buf[2048];
                int r = tls ? mbedtls_ssl_read(&ssl, buf, sizeof(buf)) : mbedtls_net_recv_timeout(&c, buf, sizeof(buf), 50);
                if(r == MBEDTLS_ERR_SSL_TIMEOUT || r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
                if(r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
                if(r <= 0) return false;
                in.append(reinterpret_cast<char*>(buf), static_cast<size_t>(r));
            }
        };
        if(silent) { std::string l; while(running_ && line(l)) {} mbedtls_ssl_free(&ssl); return; }
        send("220 fake.example ESMTP\r\n");
        std::string l;
        bool in_data = false;
        std::string body;
        while(line(l)) {
            if(!tls) { std::lock_guard<std::mutex> g(mu_); plain_.push_back(l); }
            if(in_data) {
                if(l == ".") { in_data = false; { std::lock_guard<std::mutex> g(mu_); data_ = body; } data_in_tls = tls; mails++; send("250 queued\r\n"); }
                else body += l + "\r\n";
                continue;
            }
            const std::string cmd = l.substr(0, l.find(' '));
            if(cmd == "EHLO") send(std::string("250-fake.example\r\n") + (mode_ == Mode::StartTls && !tls ? "250-STARTTLS\r\n" : "") + "250 AUTH PLAIN LOGIN\r\n");
            else if(cmd == "STARTTLS" && mode_ == Mode::StartTls && !tls) {
                send("220 go ahead\r\n");
                if(!handshake(ssl, c)) break;
                tls = true;
                in.clear();
            }
            else if(cmd == "AUTH") { auth_seen = true; auth_in_tls = tls; { std::lock_guard<std::mutex> g(mu_); auth_ = l; } send("235 ok\r\n"); }
            else if(cmd == "MAIL" || cmd == "RCPT") send("250 ok\r\n");
            else if(cmd == "DATA") { in_data = true; body.clear(); send("354 go\r\n"); }
            else if(cmd == "QUIT") { send("221 bye\r\n"); break; }
            else send("500 what?\r\n");
        }
        if(tls) mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_free(&ssl);
    }

    Mode mode_;
    mbedtls_net_context listen_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt crt_;
    mbedtls_pk_context key_;
    mbedtls_entropy_context entropy_;
    mbedtls_ctr_drbg_context drbg_;
    std::thread thread_;
    std::atomic<bool> running_{ true };
    std::mutex mu_;
    std::string data_, auth_;
    std::vector<std::string> plain_;
};

SmtpMessage mail()
{
    SmtpMessage m;
    m.from = "hmi@plant.example";
    m.to = { "shift@plant.example" };
    m.subject = "Alarm over a real socket";
    m.body = "Line one.\r\nLine two.";
    return m;
}
SmtpAccount account(uint16_t port, SmtpSecurity sec, const char* host = "localhost")
{
    SmtpAccount a;
    a.host = host;
    a.port = port;
    a.security = sec;
    a.username = "hmi";
    a.password = "s3cret-pw";
    a.timeout_ms = 4000;
    return a;
}
SmtpTls ca(const char* file = "ca.pem") { SmtpTls t; t.ca_file = cert(file); return t; }

} // namespace

// ================================================================ compatibility: the three ways to connect

TEST_CASE("smtp socket: STARTTLS - the handshake in the middle of the conversation; the login and the mail go through it")
{
    FakeSmtpServer srv(Mode::StartTls);
    const SmtpResult r = smtp_deliver(account(srv.port, SmtpSecurity::StartTls), ca(), mail());
    CHECK_MESSAGE(r.ok, r.error);
    CHECK(srv.handshakes == 1);
    CHECK(srv.mails == 1);
    CHECK(srv.auth_in_tls);
    CHECK(srv.data_in_tls);
    CHECK(srv.auth() == "AUTH PLAIN " + smtp_base64(std::string("\0hmi\0s3cret-pw", 14)));
    CHECK(srv.data().find("Subject: Alarm over a real socket\r\n") != std::string::npos);
    // in the clear: the greeting of the client and the request for TLS, nothing else
    const std::vector<std::string> plain = srv.plain_lines();
    REQUIRE(plain.size() == 2);
    CHECK(plain[0].rfind("EHLO ", 0) == 0);
    CHECK(plain[1] == "STARTTLS");
    // a second mail: a new connection, the same again
    CHECK(smtp_deliver(account(srv.port, SmtpSecurity::StartTls), ca(), mail()).ok);
    CHECK(srv.mails == 2);
}

TEST_CASE("smtp socket: TLS from the first byte, and a plain relay without a login")
{
    {
        FakeSmtpServer srv(Mode::Tls);
        const SmtpResult r = smtp_deliver(account(srv.port, SmtpSecurity::Tls), ca(), mail());
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(srv.handshakes == 1);
        CHECK(srv.auth_in_tls);
        CHECK(srv.plain_lines().empty());
        CHECK(srv.mails == 1);
    }
    {
        FakeSmtpServer srv(Mode::Plain);
        SmtpAccount a = account(srv.port, SmtpSecurity::None, "127.0.0.1");
        a.username.clear();
        const SmtpResult r = smtp_deliver(a, SmtpTls{}, mail());
        CHECK_MESSAGE(r.ok, r.error);
        CHECK_FALSE(srv.auth_seen);
        CHECK_FALSE(srv.data_in_tls);
        CHECK(srv.mails == 1);
    }
}

// ================================================================ security: certificates

TEST_CASE("smtp socket: a certificate that is not trusted, or of another name - nothing is sent, not the login, not the mail")
{
    SUBCASE("signed by somebody else") {
        FakeSmtpServer srv(Mode::StartTls);
        const SmtpResult r = smtp_deliver(account(srv.port, SmtpSecurity::StartTls), ca("other_ca.pem"), mail());
        CHECK_FALSE(r.ok);
        CHECK_MESSAGE(r.error.find("TLS handshake") != std::string::npos, r.error);
        CHECK_FALSE(srv.auth_seen);
        CHECK(srv.mails == 0);
    }
    SUBCASE("no ca_file: what the system trusts - and it does not trust a test CA") {
        FakeSmtpServer srv(Mode::Tls);
        const SmtpResult r = smtp_deliver(account(srv.port, SmtpSecurity::Tls), SmtpTls{}, mail());
        CHECK_FALSE(r.ok);
        CHECK_MESSAGE(r.error.find("TLS") != std::string::npos, r.error);
        CHECK_FALSE(srv.auth_seen);
        CHECK(srv.mails == 0);
    }
    SUBCASE("a certificate for another name") {
        FakeSmtpServer srv(Mode::StartTls, "wrong_name");
        const SmtpResult r = smtp_deliver(account(srv.port, SmtpSecurity::StartTls), ca(), mail());
        CHECK_FALSE(r.ok);
        CHECK_FALSE(srv.auth_seen);
        CHECK(srv.mails == 0);
    }
    SUBCASE("\"verify\": false takes any certificate (by choice)") {
        FakeSmtpServer srv(Mode::StartTls, "wrong_name");
        SmtpTls t;
        t.verify = false;
        const SmtpResult r = smtp_deliver(account(srv.port, SmtpSecurity::StartTls), t, mail());
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(srv.auth_in_tls);
    }
    SUBCASE("STARTTLS asked of a server that does not have it: the login never leaves") {
        FakeSmtpServer srv(Mode::Plain);
        const SmtpResult r = smtp_deliver(account(srv.port, SmtpSecurity::StartTls, "127.0.0.1"), ca(), mail());
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK_FALSE(srv.auth_seen);
        CHECK(srv.mails == 0);
        for(const std::string& l : srv.plain_lines()) CHECK(l.find("AUTH") == std::string::npos);
    }
}

// ================================================================ reliability / performance: servers that do not answer

TEST_CASE("smtp socket: a server that says nothing, a TLS port spoken to in the clear - given up within the time limit")
{
    {
        FakeSmtpServer srv(Mode::Plain);
        srv.silent = true;
        SmtpAccount a = account(srv.port, SmtpSecurity::None, "127.0.0.1");
        a.timeout_ms = 1000;
        const uint32_t t0 = millis();
        const SmtpResult r = smtp_deliver(a, SmtpTls{}, mail());
        const uint32_t ms = millis() - t0;
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.permanent);                                // worth another try later
        CHECK_MESSAGE(r.error.find("no answer") != std::string::npos, r.error);
        CHECK(ms < 4000);
    }
    {   // "starttls" against a port that wants TLS from the first byte (465 and 587 mixed up)
        FakeSmtpServer srv(Mode::Tls);
        SmtpAccount a = account(srv.port, SmtpSecurity::StartTls);
        a.timeout_ms = 1500;
        const uint32_t t0 = millis();
        const SmtpResult r = smtp_deliver(a, ca(), mail());
        CHECK_FALSE(r.ok);
        CHECK(millis() - t0 < 12000);
        CHECK(srv.mails == 0);
    }
    {   // "tls" against a port that speaks in the clear
        FakeSmtpServer srv(Mode::Plain);
        SmtpAccount a = account(srv.port, SmtpSecurity::Tls, "127.0.0.1");
        a.timeout_ms = 1500;
        const SmtpResult r = smtp_deliver(a, ca(), mail());
        CHECK_FALSE(r.ok);
        CHECK(srv.mails == 0);
        CHECK_FALSE(srv.auth_seen);
    }
}
