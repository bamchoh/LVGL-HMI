// SSH key management: public key parsing and fingerprints, the authorized_keys file, the offer
// protocol (KeyService) and the system settings page "Keys".
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using tsock_t = SOCKET;
#  define TSOCK_CLOSE closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using tsock_t = int;
#  define TSOCK_CLOSE close
#endif

#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "key_service.hpp"
#include "project.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

// throwaway keys made with ssh-keygen; the fingerprints are what `ssh-keygen -lf` prints
const char* kEd = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBRm0k48XyK/FvaK+x10wbebCU98LkcEyfb3FsKg2WUI test one";
const char* kEdFp = "SHA256:TC3RPtOeC0UlCshZHRsZt0L00BUnedsWRq7z1VC2chI";
const char* kRsa = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDzfp09iyTmKzRHoc6CxXwQMzZzvwSOz9JpeYK9Rh1yLax4x3IBbneac02TlpD4P69RQvcLln+YnHoDX9+hVal+2HxtRrbfeZcqZDA0D/VXieRagxVFpBSea6K8qYx0GYqJR4opkv32WEKMMPv13HE0puKx2W1HzxUq1B7JV/ytEgBlJ0GG1lu2j4dPD6YM0RwvTdXUjeIXY6yBzwSUCwg1J2ZBUJXF9A2X1KqrJOGvunc2yC+vjB+g2l4vc/FCcg7tMwZf8wk9PCaOETU2mIgDRLeNjaoi4PTjtgbKzluxIHF47GkmcJ3IuHtr5pNaYvLYv+4q6bKJS0dy8YWa2qOr rsa@test";
const char* kRsaFp = "SHA256:4oMm9Ta6iAZ7+6kYx/k0cFo/QYxV6Yyq8Z8RdwiITiI";

struct TempDir {
    fs::path dir;
    TempDir()
    {
        dir = fs::temp_directory_path() / ("hml_keys_test_" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this))));
        fs::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string path(const char* name) const { return (dir / name).generic_string(); }
};

std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool wait_until(const std::function<bool()>& cond, int timeout_ms = 3000)
{
    for(int t = 0; t < timeout_ms; t += 10) {
        if(cond()) return true;
        sleep_ms(10);
    }
    return cond();
}

// The sender's side of the offer protocol.
struct Sender {
    tsock_t s = static_cast<tsock_t>(-1);
    std::string buf;

    bool connect_to(int port)
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        return ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
    }
    void send_text(const std::string& t) { ::send(s, t.data(), static_cast<int>(t.size()), 0); }
    // next line, "" on timeout / close
    std::string line(int timeout_ms = 3000)
    {
        for(int t = 0; t <= timeout_ms; t += 20) {
            size_t nl = buf.find('\n');
            if(nl != std::string::npos) { std::string l = buf.substr(0, nl); buf.erase(0, nl + 1); return l; }
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(s, &rd);
            timeval tv{ 0, 20000 };
            if(select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv) > 0) {
                char b[512];
                int n = static_cast<int>(recv(s, b, sizeof(b), 0));
                if(n <= 0) return "";
                buf.append(b, static_cast<size_t>(n));
            }
        }
        return "";
    }
    void close_now() { if(s != static_cast<tsock_t>(-1)) { TSOCK_CLOSE(s); s = static_cast<tsock_t>(-1); } }
    ~Sender() { close_now(); }
};

// a port nobody listens on right now
int start_on_free_port(KeyService& ks)
{
    for(int port = 48551; port < 48600; port++) if(ks.start(port)) return port;
    return 0;
}

} // namespace

TEST_CASE("keys: SHA-256 and fingerprints match the reference values")
{
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256_hex(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");

    PublicKey k;
    REQUIRE(parse_public_key(kEd, k));
    CHECK(k.type == "ssh-ed25519");
    CHECK(k.comment == "test one");
    CHECK(k.fingerprint == kEdFp);                 // same as ssh-keygen -lf
    CHECK(k.short_type() == "ed25519");
    CHECK(k.line() == kEd);
    REQUIRE(parse_public_key(kRsa, k));
    CHECK(k.fingerprint == kRsaFp);
    CHECK(k.short_type() == "rsa");
}

TEST_CASE("keys: only well-formed public keys are accepted")
{
    PublicKey k;
    std::string err;
    std::string ed = kEd, blob = ed.substr(ed.find(' ') + 1, ed.rfind(" test") - ed.find(' ') - 1);

    CHECK(parse_public_key("  ssh-ed25519 " + blob + "\r\n", k));
    CHECK(k.comment.empty());
    CHECK(parse_public_key("ssh-ed25519 " + blob + "   me@pc  \x01\x7f tail", k));
    CHECK(k.comment == "me@pc   tail");            // control characters are dropped

    CHECK_FALSE(parse_public_key("", k, &err));
    CHECK_FALSE(parse_public_key("hello world", k, &err));
    CHECK_FALSE(parse_public_key("ssh-dss " + blob, k, &err));                 // unsupported type
    CHECK_FALSE(parse_public_key("ssh-ed25519 not*base64", k, &err));
    CHECK_FALSE(parse_public_key("ssh-ed25519 " + blob.substr(0, blob.size() - 4), k, &err));   // too short for an ed25519 key
    CHECK_FALSE(parse_public_key("ssh-rsa " + blob, k, &err));                 // the data says ed25519
    CHECK(err.find("does not match") != std::string::npos);
    CHECK_FALSE(parse_public_key("ssh-ed25519 " + blob + "\nssh-ed25519 " + blob, k, &err));   // one key, one line
    CHECK_FALSE(parse_public_key("ssh-ed25519 " + std::string(9000, 'A'), k, &err));

    // an options field is only understood when reading authorized_keys
    std::string with_opts = "command=\"echo hi\",no-pty ssh-ed25519 " + blob + " limited";
    CHECK_FALSE(parse_public_key(with_opts, k, &err));
    REQUIRE(parse_public_key(with_opts, k, &err, true));
    CHECK(k.fingerprint == kEdFp);
    CHECK(k.comment == "limited");
}

TEST_CASE("keys: authorized_keys is listed, extended and pruned without touching other lines")
{
    TempDir tmp;
    KeyService ks;
    ks.set_file(tmp.path("sub/authorized_keys"));
    CHECK(ks.file() == tmp.path("sub/authorized_keys"));
    CHECK(ks.list().empty());                                    // no file yet

    PublicKey ed, rsa;
    REQUIRE(parse_public_key(kEd, ed));
    REQUIRE(parse_public_key(kRsa, rsa));
    std::string err;
    REQUIRE(ks.add(ed, &err));                                   // creates the folder and the file
    CHECK(slurp(ks.file()) == std::string(kEd) + "\n");
    CHECK(ks.add(ed));                                           // already there: nothing added
    CHECK(ks.list().size() == 1);

    // a file as people really have it: comments, options, something unreadable
    {
        std::ofstream f(ks.file(), std::ios::binary);
        f << "# my keys\n" << "from=\"10.0.0.*\" " << kEd << "\n" << "\n" << "garbage line\n";
    }
    REQUIRE(ks.list().size() == 1);
    CHECK(ks.list()[0].fingerprint == kEdFp);
    REQUIRE(ks.add(rsa));
    REQUIRE(ks.list().size() == 2);
    CHECK(ks.list()[1].fingerprint == kRsaFp);

    CHECK_FALSE(ks.remove("SHA256:nope", &err));
    CHECK(err == "no such key");
    REQUIRE(ks.remove(kEdFp, &err));
    CHECK(slurp(ks.file()) == std::string("# my keys\n\ngarbage line\n") + kRsa + "\n");
    REQUIRE(ks.remove(kRsaFp));
    CHECK(ks.list().empty());

    PublicKey bad = ed;
    bad.base64 = "AAAA";
    CHECK_FALSE(ks.add(bad, &err));                              // what is written is validated again
    CHECK(ks.list().empty());
}

TEST_CASE("keys: an offered key waits for the operator and is registered only when accepted")
{
    TempDir tmp;
    KeyService ks;
    ks.set_file(tmp.path("authorized_keys"));
    KeyService::Offer offer;
    CHECK_FALSE(ks.listening());
    CHECK_FALSE(ks.pending(offer));
    int port = start_on_free_port(ks);
    REQUIRE(port != 0);
    CHECK(ks.listening());
    CHECK(ks.port() == port);

    SUBCASE("accept") {
        Sender c;
        REQUIRE(c.connect_to(port));
        c.send_text(std::string("HML-KEY 1\n") + kEd + "\n");
        CHECK(c.line() == std::string("PENDING ") + kEdFp);
        REQUIRE(wait_until([&] { return ks.pending(offer); }));
        CHECK(offer.from == "127.0.0.1");
        CHECK(offer.key.fingerprint == kEdFp);
        CHECK(offer.key.comment == "test one");
        CHECK(ks.list().empty());                                // nothing is registered by the network side alone
        REQUIRE(ks.accept());
        CHECK(c.line() == "ACCEPTED");
        REQUIRE(ks.list().size() == 1);
        CHECK(ks.list()[0].fingerprint == kEdFp);
        CHECK(wait_until([&] { return !ks.pending(offer); }));

        // the same key again is answered at once
        Sender again;
        REQUIRE(again.connect_to(port));
        again.send_text(std::string("HML-KEY 1\r\n") + kEd + "\r\n");
        CHECK(again.line() == std::string("ALREADY ") + kEdFp);
        CHECK_FALSE(ks.pending(offer));
    }
    SUBCASE("reject") {
        Sender c;
        REQUIRE(c.connect_to(port));
        c.send_text(std::string("HML-KEY 1\n") + kRsa + "\n");
        CHECK(c.line() == std::string("PENDING ") + kRsaFp);
        REQUIRE(wait_until([&] { return ks.pending(offer); }));
        ks.reject();
        CHECK(c.line() == "REJECTED");
        CHECK(ks.list().empty());
        CHECK_FALSE(ks.accept());                                // nothing is waiting any more
    }
    SUBCASE("requests that are not a key offer are refused") {
        Sender a;
        REQUIRE(a.connect_to(port));
        a.send_text("GET / HTTP/1.1\nHost: x\n");
        CHECK(a.line() == "ERROR unknown request");
        Sender b;
        REQUIRE(b.connect_to(port));
        b.send_text("HML-KEY 1\nssh-ed25519 AAAA oops\n");
        CHECK(b.line().rfind("ERROR ", 0) == 0);
        CHECK_FALSE(ks.pending(offer));
        CHECK(ks.list().empty());
    }
    SUBCASE("an offer nobody answers times out, and one whose sender left is dropped") {
        ks.set_offer_timeout(1);
        Sender c;
        REQUIRE(c.connect_to(port));
        c.send_text(std::string("HML-KEY 1\n") + kEd + "\n");
        CHECK(c.line() == std::string("PENDING ") + kEdFp);
        CHECK(c.line(4000) == "TIMEOUT");
        CHECK(wait_until([&] { return !ks.pending(offer); }));

        ks.set_offer_timeout(60);
        Sender d;
        REQUIRE(d.connect_to(port));
        d.send_text(std::string("HML-KEY 1\n") + kEd + "\n");
        CHECK(d.line() == std::string("PENDING ") + kEdFp);
        REQUIRE(wait_until([&] { return ks.pending(offer); }));
        d.close_now();
        CHECK(wait_until([&] { return !ks.pending(offer); }));
        CHECK(ks.list().empty());
    }
    SUBCASE("switching the mode off answers a waiting offer and closes the port") {
        Sender c;
        REQUIRE(c.connect_to(port));
        c.send_text(std::string("HML-KEY 1\n") + kEd + "\n");
        CHECK(c.line() == std::string("PENDING ") + kEdFp);
        REQUIRE(wait_until([&] { return ks.pending(offer); }));
        ks.stop();
        CHECK(c.line() == "REJECTED");
        CHECK_FALSE(ks.listening());
        CHECK_FALSE(ks.pending(offer));
        Sender late;
        CHECK_FALSE(late.connect_to(port));                      // nothing listens with the mode off
        CHECK(ks.list().empty());
    }
    ks.stop();
}

TEST_CASE("keys page: mode switch, accepting an offered key, deleting a key with confirmation")
{
    Runtime rt;
    TempDir tmp;
    {
        std::ofstream f(tmp.path("project.json"), std::ios::binary);
        f << R"json({ "tags": [ { "name": "x" } ],
                      "screens": [ { "name": "main", "children": [ { "type": "label", "text": "hi" } ] } ],
                      "key_management": { "enabled": true, "port": 48620, "file": "authorized_keys" } })json";
    }
    {
        std::ofstream f(tmp.path("authorized_keys"), std::ios::binary);
        f << kRsa << "\n";
    }
    Project p;
    REQUIRE(p.load(tmp.path("project.json")));
    SystemSettings& sys = system_settings();
    sys.register_actions();
    REQUIRE(sys.page("keys") != nullptr);
    CHECK(key_service().file() == tmp.path("authorized_keys"));
    REQUIRE(p.build_screens() == 1);
    auto tagstr = [](const char* key) { Tag* t = tags().find(std::string("sys.keys.") + key); return t ? t->get().str : std::string("<none>"); };
    auto tagon = [](const char* key) { Tag* t = tags().find(std::string("sys.keys.") + key); return t && t->get().truthy(); };

    CHECK_FALSE(tagon("mode"));
    CHECK(tagstr("state").rfind("Off", 0) == 0);
    CHECK(tagon("k1_used"));
    CHECK(tagstr("k1").find(kRsaFp) != std::string::npos);
    CHECK_FALSE(tagon("k2_used"));
    CHECK_FALSE(tagon("pending"));
    CHECK_FALSE(tagon("confirm"));

    // ---- mode on: the port opens, the page says where to send the key
    sys.command("keys", "mode");
    REQUIRE(key_service().listening());
    CHECK(tagon("mode"));
    CHECK(tagstr("state").find("port 48620") != std::string::npos);

    // ---- an offer arrives while another screen is shown: the page comes up and asks
    CHECK(ui().current_screen().value_or("") == "main");
    Sender c;
    REQUIRE(c.connect_to(48620));
    c.send_text(std::string("HML-KEY 1\n") + kEd + "\n");
    CHECK(c.line() == std::string("PENDING ") + kEdFp);
    KeyService::Offer offer;
    REQUIRE(wait_until([&] { return key_service().pending(offer); }));
    sys.poll();
    CHECK(tagon("pending"));
    CHECK(tagstr("pending_text").find("127.0.0.1") != std::string::npos);
    CHECK(tagstr("pending_text").find(kEdFp) != std::string::npos);
    CHECK(ui().current_screen().value_or("") == "__system_keys");

    sys.command("keys", "accept");
    CHECK(c.line() == "ACCEPTED");
    CHECK(tagon("k2_used"));
    CHECK(tagstr("k2").find(kEdFp) != std::string::npos);
    REQUIRE(wait_until([&] { return !key_service().pending(offer); }));
    sys.poll();
    CHECK_FALSE(tagon("pending"));

    // ---- delete: asks first, Cancel keeps the key
    sys.command("keys", "delete", "1");
    CHECK(tagon("confirm"));
    CHECK(tagstr("confirm_text").find(kRsaFp) != std::string::npos);
    CHECK(key_service().list().size() == 2);
    sys.command("keys", "delete_no");
    CHECK_FALSE(tagon("confirm"));
    CHECK(key_service().list().size() == 2);
    sys.command("keys", "delete", "1");
    sys.command("keys", "delete_yes");
    REQUIRE(key_service().list().size() == 1);
    CHECK(key_service().list()[0].fingerprint == kEdFp);
    CHECK(tagstr("k1").find(kEdFp) != std::string::npos);
    CHECK_FALSE(tagon("k2_used"));
    sys.command("keys", "delete", "1");
    CHECK(tagstr("confirm_text").find("last key") != std::string::npos);   // a warning, not a refusal
    sys.command("keys", "delete_no");
    sys.command("keys", "delete", "7");                                    // an empty slot: nothing to confirm
    CHECK_FALSE(tagon("confirm"));

    // ---- mode off again
    sys.command("keys", "mode");
    CHECK_FALSE(key_service().listening());
    CHECK_FALSE(tagon("mode"));

    SUBCASE("the page can be switched off in project.json") {
        // (the default depends on the backend: on for fbdev / drm devices, off on a PC, where
        // the page would edit the developer's own keys)
        TempDir other;
        std::ofstream f(other.path("project.json"), std::ios::binary);
        f << R"json({ "tags": [ { "name": "x" } ], "key_management": { "enabled": false } })json";
        f.close();
        Project q;
        REQUIRE(q.load(other.path("project.json")));
        CHECK(system_settings().page("keys") == nullptr);
    }
    key_service().stop();
    key_service().set_file("");
}
