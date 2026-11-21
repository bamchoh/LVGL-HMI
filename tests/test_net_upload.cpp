// One file handed to a server (net_upload.hpp): HTTP PUT / POST and FTP, line by line against
// servers that are scripts, and over real sockets against small servers in this process.
// Grouped by ISO/IEC 25010 characteristic (issue #67).
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

#include <doctest.h>
#include "net_upload.hpp"
#include "smtp.hpp"
#include "util.hpp"

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace hml;
namespace fs = std::filesystem;

namespace {

// a stream that hands out what the "server" says and keeps what the client sent
class Script final : public NetStream {
public:
    std::string in, sent;
    bool broken_after = false;
    size_t break_at = 0;                    // write() fails once this many bytes went out (0 = never)
    explicit Script(std::string answer = "") : in(std::move(answer)) {}
    bool write(const char* d, size_t n) override
    {
        if(break_at && sent.size() + n > break_at) { sent.append(d, break_at > sent.size() ? break_at - sent.size() : 0); return false; }
        sent.append(d, n);
        return true;
    }
    int read(char* buf, size_t max, uint32_t) override
    {
        if(in == "<close>") return -1;
        if(in.empty()) return 0;
        const size_t n = std::min(max, in.size());
        std::memcpy(buf, in.data(), n);
        in.erase(0, n);
        return static_cast<int>(n);
    }
};

struct TempFile {
    fs::path dir, path;
    explicit TempFile(const std::string& content, const char* name = "log.csv")
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_net_upload_test_" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir);
        path = dir / name;
        std::ofstream f(path, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    ~TempFile() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string str() const { return path.generic_string(); }
};

NetUpload upload(const char* url, const TempFile& f, const char* name = "line1_data_20261121-235500.csv")
{
    NetUpload u;
    std::string err;
    REQUIRE_MESSAGE(net_url_parse(url, u.url, &err), err);
    u.file = f.str();
    u.name = name;
    u.timeout_ms = 3000;
    return u;
}

const std::string kCsv = "\xEF\xBB\xBFtime,temp\r\n2026-11-21 23:00:00.000,21.5\r\n2026-11-21 23:01:00.000,21.7\r\n";

} // namespace

// ================================================================ functional suitability

TEST_CASE("net upload: URLs - what is taken apart, what is refused")
{
    NetUrl u;
    std::string err;
    REQUIRE(net_url_parse("https://files.plant.example/hml/logs/", u));
    CHECK(u.scheme == "https");
    CHECK(u.host == "files.plant.example");
    CHECK(u.port == 443);
    CHECK(u.path == "/hml/logs/");
    CHECK(u.secure());
    CHECK(u.shown() == "https://files.plant.example/hml/logs/");
    REQUIRE(net_url_parse("HTTP://10.0.0.5:8080", u));
    CHECK(u.port == 8080);
    CHECK(u.path == "/");
    CHECK_FALSE(u.secure());
    CHECK(u.shown() == "http://10.0.0.5:8080/");
    REQUIRE(net_url_parse("ftp://nas.local/incoming", u));
    CHECK(u.port == 21);
    REQUIRE(net_url_parse("http://[::1]:8081/x", u));
    CHECK(u.host == "::1");
    CHECK(u.port == 8081);
    for(const char* bad : { "", "files.plant.example/logs", "file:///etc/passwd", "sftp://host/dir", "https://", "https:///path", "https://user:secret@host/dir/", "ftp://hmi@host/",
                            "https://host/dir/?token=abc", "https://host/dir/#x", "https://host/a/../b/", "https://host/..", "https://host:0/", "https://host:99999/", "https://host:80a/",
                            "https://ho st/", "https://host/pa th", "https://host/\r\nHost: evil", "https://host\r\n/", "https://h\xC3\xB6st/", "https://host;rm/", "https://[::1/", "gopher://host/" }) {
        CAPTURE(bad);
        err.clear();
        CHECK_FALSE(net_url_parse(bad, u, &err));
        CHECK(err != "");
    }
    CHECK_FALSE(net_url_parse("https://host/" + std::string(600, 'a'), u));
    CHECK(net_file_name("line1_data_20261121-235500.csv") == "line1_data_20261121-235500.csv");
    CHECK(net_file_name("../../etc/passwd") == "__.__etc_passwd");
    CHECK(net_file_name("a b\r\nSTOR x.csv") == "a_b__STOR_x.csv");
    CHECK(net_file_name("..") == "_");
    CHECK(net_file_name(".") == "file");
    CHECK(net_file_name("a..b...c") == "a._b._.c");
    CHECK(net_file_name("") == "file");
    CHECK(net_file_name("\xE3\x83\xAD\xE3\x82\xB0.csv") == "______.csv");
    CHECK(net_file_name(std::string(300, 'x') + ".csv").size() == 120);
}

TEST_CASE("net upload: HTTP - PUT to a directory, POST to a URL; the file is the body, its length is said")
{
    TempFile f(kCsv);
    {
        Script s("HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
        NetUpload u = upload("https://files.plant.example/hml/logs/", f);
        u.username = "hmi";
        u.password = "s3cret-pw";
        const NetResult r = http_upload(s, u);
        CHECK_MESSAGE(r.ok, r.error);
        const size_t head_end = s.sent.find("\r\n\r\n");
        REQUIRE(head_end != std::string::npos);
        const std::string head = s.sent.substr(0, head_end);
        CHECK(head.find("PUT /hml/logs/line1_data_20261121-235500.csv HTTP/1.1\r\n") == 0);
        CHECK(head.find("\r\nHost: files.plant.example\r\n") != std::string::npos);
        CHECK(head.find("\r\nContent-Length: " + std::to_string(kCsv.size()) + "\r\n") != std::string::npos);
        CHECK(head.find("\r\nContent-Type: text/csv; charset=utf-8\r\n") != std::string::npos);
        CHECK(head.find("\r\nAuthorization: Basic " + smtp_base64("hmi:s3cret-pw")) != std::string::npos);
        CHECK(head.find("\r\nConnection: close") != std::string::npos);
        CHECK(s.sent.substr(head_end + 4) == kCsv);
    }
    {
        Script s("HTTP/1.1 200 OK\r\n\r\n");
        NetUpload u = upload("http://10.0.0.5:8080/api/upload", f);
        u.post = true;
        CHECK(http_upload(s, u).ok);
        CHECK(s.sent.find("POST /api/upload HTTP/1.1\r\nHost: 10.0.0.5:8080\r\n") == 0);
        CHECK(s.sent.find("\r\nContent-Disposition: attachment; filename=\"line1_data_20261121-235500.csv\"\r\n") != std::string::npos);
        CHECK(s.sent.find("Authorization") == std::string::npos);
    }
    {   // a directory written without the "/" at its end; an address in brackets
        Script s("HTTP/1.1 204 No Content\r\n\r\n");
        CHECK(http_upload(s, upload("http://[::1]:8081/logs", f)).ok);
        CHECK(s.sent.find("PUT /logs/line1_data_20261121-235500.csv HTTP/1.1\r\nHost: [::1]:8081\r\n") == 0);
    }
    {   // a megabyte goes out whole, and in pieces
        TempFile big(std::string(1024 * 1024 + 7, 'x'));
        Script s("HTTP/1.1 200 OK\r\n\r\n");
        CHECK(http_upload(s, upload("http://h/", big)).ok);
        CHECK(s.sent.size() > 1024 * 1024 + 7);
        CHECK(s.sent.substr(s.sent.find("\r\n\r\n") + 4) == std::string(1024 * 1024 + 7, 'x'));
    }
    {   // an empty file is a file
        TempFile empty("");
        Script s("HTTP/1.1 200 OK\r\n\r\n");
        CHECK(http_upload(s, upload("http://h/", empty)).ok);
        CHECK(s.sent.find("Content-Length: 0\r\n") != std::string::npos);
    }
}

// ================================================================ reliability

TEST_CASE("net upload: HTTP - what the server says, in words, and whether trying again makes sense")
{
    TempFile f(kCsv);
    struct Case { const char* answer; const char* in_error; bool permanent; };
    const Case cases[] = {
        { "HTTP/1.1 401 Unauthorized\r\n\r\n", "does not let us in (401 Unauthorized)", true },
        { "HTTP/1.1 403 Forbidden\r\n\r\n", "does not let us in", true },
        { "HTTP/1.1 404 Not Found\r\n\r\n", "refused the file: 404 Not Found", true },
        { "HTTP/1.1 405 Method Not Allowed\r\n\r\n", "405", true },
        { "HTTP/1.1 413 Payload Too Large\r\n\r\n", "413", true },
        { "HTTP/1.1 301 Moved Permanently\r\nLocation: https://evil.example/\r\n\r\n", "sends us elsewhere", true },
        { "HTTP/1.1 429 Too Many Requests\r\n\r\n", "cannot take the file now", false },
        { "HTTP/1.1 500 Internal Server Error\r\n\r\n", "cannot take the file now: 500", false },
        { "HTTP/1.1 503 Service Unavailable\r\n\r\n", "503", false },
        { "", "no answer from the server", false },
        { "<close>", "closed the connection", false },
        { "220 mail.example ESMTP\r\n", "does not speak HTTP", false },
        { "SSH-2.0-OpenSSH\r\n", "does not speak HTTP", false },
        { "HTTP/1.1 xyz\r\n", "does not speak HTTP", false },
    };
    for(const Case& c : cases) {
        CAPTURE(c.answer);
        Script s(c.answer);
        const NetResult r = http_upload(s, upload("https://h/logs/", f));
        CHECK_FALSE(r.ok);
        CHECK_MESSAGE(r.error.find(c.in_error) != std::string::npos, r.error);
        CHECK(r.permanent == c.permanent);
        CHECK(r.error.find('\n') == std::string::npos);
    }
    {   // a redirect is not followed (the file, and the password, would go where the answer says)
        Script s("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://evil.example/steal\r\n\r\n");
        NetUpload u = upload("https://h/logs/", f);
        u.username = "hmi"; u.password = "pw";
        CHECK_FALSE(http_upload(s, u).ok);
        CHECK(s.sent.find("evil.example") == std::string::npos);
    }
    {   // the server answers 401 and hangs up while we still send: its answer is what is reported
        TempFile big(std::string(200000, 'x'));
        Script s("HTTP/1.1 401 Unauthorized\r\n\r\n");
        s.break_at = 50000;
        const NetResult r = http_upload(s, upload("https://h/logs/", big));
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK_MESSAGE(r.error.find("401") != std::string::npos, r.error);
    }
    {   // the connection breaks in the middle, nothing was said
        TempFile big(std::string(200000, 'x'));
        Script s("");
        s.break_at = 50000;
        const NetResult r = http_upload(s, upload("https://h/logs/", big));
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.permanent);
        CHECK(r.error.find("lost while the file was sent") != std::string::npos);
    }
    {   // a line without end, control characters in the status
        Script a(std::string(100000, 'x'));
        CHECK(http_upload(a, upload("https://h/logs/", f)).error.find("without end") != std::string::npos);
        Script b("HTTP/1.1 500 go\x1b[31m away\x07\r\n\r\n");
        for(char ch : http_upload(b, upload("https://h/logs/", f)).error) CHECK(static_cast<unsigned char>(ch) >= 0x20);
    }
    {   // the file is gone
        NetUpload u = upload("https://h/logs/", f);
        u.file = (f.dir / "gone.csv").generic_string();
        Script s("HTTP/1.1 200 OK\r\n\r\n");
        const NetResult r = http_upload(s, u);
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK(s.sent.empty());
    }
}

// ================================================================ security

TEST_CASE("net upload: a file name or a login cannot add a header or a command")
{
    TempFile f(kCsv);
    {
        Script s("HTTP/1.1 200 OK\r\n\r\n");
        NetUpload u = upload("https://h/logs/", f, "x.csv\r\nX-Evil: 1\r\n\r\nGET /secret HTTP/1.1");
        CHECK(http_upload(s, u).ok);
        const std::string head = s.sent.substr(0, s.sent.find("\r\n\r\n"));
        CHECK(head.find("\r\nX-Evil") == std::string::npos);      // (in the path, as text: not a header)
        CHECK(head.find("\r\nGET ") == std::string::npos);
        CHECK(head.find("PUT /logs/x.csv__X-Evil__1____GET__secret_HTTP_1.1 HTTP/1.1") == 0);
    }
    for(const char* user : { "hmi\r\nX-Evil: 1", "h:mi" }) {
        Script s("HTTP/1.1 200 OK\r\n\r\n");
        NetUpload u = upload("https://h/logs/", f);
        u.username = user;
        u.password = "pw";
        const NetResult r = http_upload(s, u);
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK(s.sent.empty());
    }
    {   // FTP: a name or a password with a line end
        NetConnect none = [](const std::string&, uint16_t, bool, std::string*) -> std::unique_ptr<NetStream> { FAIL("no connection may be opened"); return nullptr; };
        NetUpload u = upload("ftp://h/in/", f);
        u.username = "hmi";
        u.password = "pw\r\nDELE important.csv";
        CHECK_FALSE(ftp_upload(none, u).ok);
    }
}

// ================================================================ FTP, line by line

namespace {

// control and data connections of a scripted FTP server
struct FtpScript {
    std::string control_in;                 // everything the server says on the control connection, in order
    std::string control_sent, data_sent;
    std::vector<std::pair<std::string, uint16_t>> opened;
    bool data_fails = false;
    NetConnect connect()
    {
        return [this](const std::string& host, uint16_t port, bool tls, std::string* err) -> std::unique_ptr<NetStream> {
            CHECK_FALSE(tls);
            opened.emplace_back(host, port);
            if(opened.size() == 1) return std::make_unique<Tap>(control_in, control_sent);
            if(data_fails) { if(err) *err = "cannot connect to " + host; return nullptr; }
            return std::make_unique<Tap>(empty_, data_sent);
        };
    }
    struct Tap final : NetStream {
        std::string& in; std::string& out;
        Tap(std::string& i, std::string& o) : in(i), out(o) {}
        bool write(const char* d, size_t n) override { out.append(d, n); return true; }
        int read(char* buf, size_t max, uint32_t) override
        {
            if(in.empty()) return 0;
            // one line at a time: a real server answers when asked
            const size_t nl = in.find('\n');
            const size_t n = std::min(max, nl == std::string::npos ? in.size() : nl + 1);
            std::memcpy(buf, in.data(), n);
            in.erase(0, n);
            return static_cast<int>(n);
        }
    };
    std::string empty_;
};

} // namespace

TEST_CASE("net upload: FTP - login, binary mode, the directory, passive mode, the file, in that order")
{
    TempFile f(kCsv);
    {
        FtpScript s;
        s.control_in = "220-Welcome\r\n220 ready\r\n331 password please\r\n230 logged in\r\n200 binary\r\n250 ok\r\n229 Entering Extended Passive Mode (|||50123|)\r\n150 go\r\n226 done\r\n221 bye\r\n";
        NetUpload u = upload("ftp://nas.plant.example/incoming/hml/", f);
        u.username = "hmi";
        u.password = "s3cret-pw";
        const NetResult r = ftp_upload(s.connect(), u);
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(s.control_sent == "USER hmi\r\nPASS s3cret-pw\r\nTYPE I\r\nCWD /incoming/hml\r\nEPSV\r\nSTOR line1_data_20261121-235500.csv\r\nQUIT\r\n");
        CHECK(s.data_sent == kCsv);
        REQUIRE(s.opened.size() == 2);
        CHECK(s.opened[1] == std::make_pair(std::string("nas.plant.example"), static_cast<uint16_t>(50123)));
    }
    {   // an old server: no EPSV. PASV names an address - the data goes to the server we talk to all the same
        FtpScript s;
        s.control_in = "220 ready\r\n230 no password needed\r\n200 binary\r\n500 EPSV not understood\r\n227 Entering Passive Mode (203,0,113,66,195,80)\r\n125 go\r\n226 done\r\n";
        const NetResult r = ftp_upload(s.connect(), upload("ftp://10.0.0.9:2121/", f));
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(s.control_sent == "USER anonymous\r\nTYPE I\r\nEPSV\r\nPASV\r\nSTOR line1_data_20261121-235500.csv\r\nQUIT\r\n");
        REQUIRE(s.opened.size() == 2);
        CHECK(s.opened[0] == std::make_pair(std::string("10.0.0.9"), static_cast<uint16_t>(2121)));
        CHECK(s.opened[1] == std::make_pair(std::string("10.0.0.9"), static_cast<uint16_t>(195 * 256 + 80)));       // not 203.0.113.66
    }
    struct Case { const char* control; const char* in_error; bool permanent; };
    const Case cases[] = {
        { "", "no answer from the server", false },
        { "421 too many users\r\n", "does not take us now: 421", false },
        { "HTTP/1.1 400 Bad Request\r\n", "does not speak FTP", false },
        { "220 ready\r\n530 not you\r\n", "the user name was not accepted: 530", true },
        { "220 ready\r\n331 pw\r\n530 Login incorrect\r\n", "the user name or the password was not accepted: 530 Login incorrect", true },
        { "220 ready\r\n230 ok\r\n200 ok\r\n550 no such directory\r\n", "the directory is not there: 550", true },
        { "220 ready\r\n230 ok\r\n200 ok\r\n250 ok\r\n500 no\r\n502 no passive mode\r\n", "does not offer passive mode", true },
        { "220 ready\r\n230 ok\r\n200 ok\r\n250 ok\r\n500 no\r\n227 whatever\r\n", "answer to PASV is not understood", false },
        { "220 ready\r\n230 ok\r\n200 ok\r\n250 ok\r\n229 (|||5000|)\r\n553 no permission to write\r\n", "does not take the file: 553", true },
        { "220 ready\r\n230 ok\r\n200 ok\r\n250 ok\r\n229 (|||5000|)\r\n150 go\r\n452 disk full\r\n", "did not keep the file: 452", false },
        { "220 ready\r\n230 ok\r\n200 ok\r\n250 ok\r\n229 (|||5000|)\r\n150 go\r\n", "no answer", false },
    };
    for(const Case& c : cases) {
        CAPTURE(c.control);
        FtpScript s;
        s.control_in = c.control;
        NetUpload u = upload("ftp://h/in/", f);
        u.username = "hmi"; u.password = "pw";
        const NetResult r = ftp_upload(s.connect(), u);
        CHECK_FALSE(r.ok);
        CHECK_MESSAGE(r.error.find(c.in_error) != std::string::npos, r.error);
        CHECK(r.permanent == c.permanent);
        CHECK(r.error.find("pw\r") == std::string::npos);
    }
    {   // the data connection cannot be opened
        FtpScript s;
        s.control_in = "220 ready\r\n230 ok\r\n200 ok\r\n250 ok\r\n229 (|||5000|)\r\n";
        s.data_fails = true;
        const NetResult r = ftp_upload(s.connect(), upload("ftp://h/in/", f));
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("the data connection") == 0);
        CHECK(s.control_sent.find("STOR") == std::string::npos);
    }
    {   // nothing to connect to
        NetConnect none = [](const std::string&, uint16_t, bool, std::string* err) -> std::unique_ptr<NetStream> { if(err) *err = "cannot connect to h:21"; return nullptr; };
        const NetResult r = ftp_upload(none, upload("ftp://h/in/", f));
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.permanent);
    }
}

// ================================================================ over real sockets

namespace {

// a server on a free port of this machine that serves one kind of connection in a thread
class Listener {
public:
    Listener()
    {
        mbedtls_net_init(&listen_);
        REQUIRE(mbedtls_net_bind(&listen_, "127.0.0.1", "0", MBEDTLS_NET_PROTO_TCP) == 0);
        sockaddr_storage a{};
        socklen_t al = sizeof(a);
        REQUIRE(getsockname(listen_.fd, reinterpret_cast<sockaddr*>(&a), &al) == 0);
        port = ntohs(reinterpret_cast<sockaddr_in*>(&a)->sin_port);
        mbedtls_net_set_nonblock(&listen_);
    }
    ~Listener() { mbedtls_net_free(&listen_); }
    uint16_t port = 0;
    // one connection, or false when told to stop / nobody came within 5 s
    bool accept(mbedtls_net_context& c, const std::atomic<bool>& running)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(running && std::chrono::steady_clock::now() < until) {
            mbedtls_net_init(&c);
            if(mbedtls_net_accept(&listen_, &c, nullptr, 0, nullptr) == 0) { mbedtls_net_set_block(&c); return true; }
            mbedtls_net_free(&c);
            sleep_ms(5);
        }
        return false;
    }

private:
    mbedtls_net_context listen_;
};

bool send_all(mbedtls_net_context& c, const std::string& s)
{
    size_t done = 0;
    while(done < s.size()) {
        const int r = mbedtls_net_send(&c, reinterpret_cast<const unsigned char*>(s.data()) + done, s.size() - done);
        if(r <= 0) return false;
        done += static_cast<size_t>(r);
    }
    return true;
}
// reads until `until` is in the text, the peer closes, or 5 s pass
std::string read_until(mbedtls_net_context& c, const std::string& until, size_t more = 0)
{
    std::string in;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    size_t at = std::string::npos;
    while(std::chrono::steady_clock::now() < deadline) {
        if(!until.empty() && at == std::string::npos) at = in.find(until);
        if(at != std::string::npos && in.size() >= at + until.size() + more) break;
        unsigned char buf[4096];
        const int r = mbedtls_net_recv_timeout(&c, buf, sizeof(buf), 50);
        if(r == MBEDTLS_ERR_SSL_TIMEOUT || r == MBEDTLS_ERR_SSL_WANT_READ) continue;
        if(r <= 0) break;
        in.append(reinterpret_cast<char*>(buf), static_cast<size_t>(r));
    }
    return in;
}

} // namespace

TEST_CASE("net upload: over real sockets - an HTTP server and an FTP server in this process get the file byte for byte")
{
    std::string content = kCsv;
    for(int i = 0; i < 5000; i++) content += "2026-11-21 23:02:00.000," + std::to_string(i) + "\r\n";
    TempFile f(content);
    {   // HTTP
        Listener l;
        std::atomic<bool> running{ true };
        std::string request;
        std::thread server([&] {
            mbedtls_net_context c;
            if(!l.accept(c, running)) return;
            std::string head = read_until(c, "\r\n\r\n");
            const size_t cl = head.find("Content-Length: ");
            const size_t len = cl == std::string::npos ? 0 : static_cast<size_t>(std::atoll(head.c_str() + cl + 16));
            const size_t body_at = head.find("\r\n\r\n") + 4;
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while(head.size() < body_at + len && std::chrono::steady_clock::now() < until) {
                unsigned char buf[8192];
                const int n = mbedtls_net_recv_timeout(&c, buf, sizeof(buf), 50);
                if(n == MBEDTLS_ERR_SSL_TIMEOUT || n == MBEDTLS_ERR_SSL_WANT_READ) continue;
                if(n <= 0) break;
                head.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
            }
            request = head;
            send_all(c, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            mbedtls_net_free(&c);
        });
        NetUpload u = upload(("http://127.0.0.1:" + std::to_string(l.port) + "/logs/").c_str(), f);
        const NetResult r = net_upload(u, NetTls{});
        running = false;
        server.join();
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(request.find("PUT /logs/line1_data_20261121-235500.csv HTTP/1.1\r\n") == 0);
        CHECK(request.substr(request.find("\r\n\r\n") + 4) == content);
    }
    {   // FTP: a control connection and a data connection
        Listener control, data;
        std::atomic<bool> running{ true };
        std::string commands, stored;
        std::thread server([&] {
            mbedtls_net_context c;
            if(!control.accept(c, running)) return;
            send_all(c, "220 fake ftp\r\n");
            auto next = [&] { const std::string l = read_until(c, "\r\n"); commands += l; return l; };
            next(); send_all(c, "331 password\r\n");
            next(); send_all(c, "230 in\r\n");
            next(); send_all(c, "200 binary\r\n");
            next(); send_all(c, "250 ok\r\n");
            next(); send_all(c, "229 Entering Extended Passive Mode (|||" + std::to_string(data.port) + "|)\r\n");
            mbedtls_net_context d;
            const bool got = data.accept(d, running);
            next(); send_all(c, "150 go\r\n");
            if(got) { stored = read_until(d, "\x01never\x01"); mbedtls_net_free(&d); }      // until the client closes
            send_all(c, "226 kept\r\n");
            next();
            mbedtls_net_free(&c);
        });
        NetUpload u = upload(("ftp://127.0.0.1:" + std::to_string(control.port) + "/incoming/").c_str(), f);
        u.username = "hmi";
        u.password = "s3cret-pw";
        const NetResult r = net_upload(u, NetTls{});
        running = false;
        server.join();
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(commands == "USER hmi\r\nPASS s3cret-pw\r\nTYPE I\r\nCWD /incoming\r\nEPSV\r\nSTOR line1_data_20261121-235500.csv\r\nQUIT\r\n");
        CHECK(stored == content);
    }
    {   // nothing listens; a server that never answers
        NetUpload u = upload("http://127.0.0.1:1/logs/", f);
        u.timeout_ms = 1500;
        const NetResult r = net_upload(u, NetTls{});
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.permanent);
        Listener l;
        std::atomic<bool> running{ true };
        std::thread server([&] { mbedtls_net_context c; if(l.accept(c, running)) { read_until(c, "\x01never\x01"); mbedtls_net_free(&c); } });
        NetUpload v = upload(("http://127.0.0.1:" + std::to_string(l.port) + "/logs/").c_str(), f);
        v.timeout_ms = 1000;
        const uint32_t t0 = millis();
        const NetResult r2 = net_upload(v, NetTls{});
        CHECK_FALSE(r2.ok);
        CHECK(r2.error.find("no answer") != std::string::npos);
        CHECK(millis() - t0 < 4000);
        running = false;
        server.join();
    }
    {   // https:// against a server whose certificate the system does not know: nothing is sent
        NetUpload u = upload("https://127.0.0.1:1/logs/", f);
        u.timeout_ms = 1500;
        CHECK_FALSE(net_upload(u, NetTls{}).ok);
    }
}
