// Deploying without SSH: the certificates, the encrypted connection, the receiving service
// (deploy_service.hpp) with a designer made of deploy_send(), and the page System > Deploy.
#include <doctest.h>
#include "deploy_link.hpp"
#include "active_project.hpp"
#include "deploy_service.hpp"
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "project_archive.hpp"
#include "system_settings.hpp"
#include "util.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using tsock_t = SOCKET;
static void tsock_close(tsock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <unistd.h>
using tsock_t = int;
static void tsock_close(tsock_t s) { close(s); }
#endif

#ifdef MSG_NOSIGNAL
static const int kTestSendFlags = MSG_NOSIGNAL;
#else
static const int kTestSendFlags = 0;
#endif

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct Temp {
    fs::path dir;
    Temp()
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_deploy_test_" + std::to_string(n++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Temp() { std::error_code ec; fs::remove_all(dir, ec); }
};

void write(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}
std::string read(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void make_project(const fs::path& dir, const std::string& title, int w = 800, int h = 480)
{
    write(dir / "project.json", "{ \"title\": \"" + title + "\", \"display\": { \"width\": " + std::to_string(w) + ", \"height\": " + std::to_string(h) +
          " }, \"start_screen\": \"s\", \"tags\": [ { \"name\": \"a\", \"type\": \"int\" } ], \"screens\": [\"screens/s.json\"] }");
    write(dir / "screens" / "s.json", "{ \"name\": \"s\", \"children\": [ { \"type\": \"label\", \"text\": \"" + title + "\" } ] }");
}
std::string title_of(const fs::path& dir)
{
    json j = json::parse(read(dir / "project.json"), nullptr, false);
    return j.is_object() ? jstr(j, "title", "") : "";
}

// Wait (at most 20 s) until something is true, doing `step` in between.
bool until(const std::function<bool()>& what, const std::function<void()>& step = nullptr)
{
    const uint32_t t0 = millis();
    while(millis() - t0 < 20000) {
        if(what()) return true;
        if(step) step();
        sleep_ms(10);
    }
    return what();
}

// A designer on a thread of its own.
struct Sender {
    DeploySend req;
    DeployIdentity me;
    DeployOutcome out;
    std::vector<std::string> states;
    std::mutex mu;
    std::thread th;
    std::atomic<bool> done{ false };

    void go()
    {
        req.on_state = [this](const std::string& s) { std::lock_guard<std::mutex> l(mu); states.push_back(s); };
        th = std::thread([this] { out = deploy_send(req, me); done = true; });
    }
    bool saw(const std::string& prefix)
    {
        std::lock_guard<std::mutex> l(mu);
        for(const std::string& s : states) if(s.rfind(prefix, 0) == 0) return true;
        return false;
    }
    void join() { if(th.joinable()) th.join(); }
    ~Sender() { join(); }
};

tsock_t tcp_connect(int port)
{
    tsock_t s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    return s;
}

// a service on a free port, with its data folder
struct Device {
    Temp tmp;
    DeployService svc;
    fs::path keys() const { return tmp.dir / "keys"; }
    Device()
    {
        write(keys() / "deploy.json", R"({ "enabled": false, "port": 1 })");
        svc.open(keys().u8string());
    }
    // (port 0 cannot be configured: listen on a port the system picks by asking for one first)
    void on()
    {
        tsock_t s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
#ifdef _WIN32
        int len = sizeof(a);
#else
        socklen_t len = sizeof(a);
#endif
        getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
        tsock_close(s);
        REQUIRE(svc.set_port(ntohs(a.sin_port)) == "");
        REQUIRE(svc.set_enabled(true) == "");
        REQUIRE(svc.listening());
    }
};

} // namespace

// ================================================================ the pieces

TEST_CASE("deploy: a certificate is made once, kept, and tells who an end is")
{
    Temp tmp;
    DeployIdentity a, b, c;
    std::string err;
    REQUIRE_MESSAGE(deploy_identity(tmp.dir / "k", "device", "hml device!", a, &err), err);
    CHECK(a.fingerprint.rfind("SHA256:", 0) == 0);
    CHECK(a.fingerprint.size() == 7 + 43);
    CHECK(a.cert_pem.find("BEGIN CERTIFICATE") != std::string::npos);
    CHECK(a.key_pem.find("PRIVATE KEY") != std::string::npos);
    CHECK(read(tmp.dir / "k" / "device.crt") == a.cert_pem);
    CHECK(read(tmp.dir / "k" / "device.crt").find("PRIVATE") == std::string::npos);
#ifndef _WIN32
    struct stat st{};
    REQUIRE(stat((tmp.dir / "k" / "device.key").c_str(), &st) == 0);
    CHECK((st.st_mode & 0077) == 0);                              // the key: its owner only
#endif
    // the same one the next time; another one somewhere else
    REQUIRE(deploy_identity(tmp.dir / "k", "device", "x", b, &err));
    CHECK(b.fingerprint == a.fingerprint);
    CHECK(b.key_pem == a.key_pem);
    REQUIRE(deploy_identity(tmp.dir / "other", "device", "x", c, &err));
    CHECK(c.fingerprint != a.fingerprint);

    // half a pair, or something that is no certificate: an error, never a silent new identity
    fs::remove(tmp.dir / "k" / "device.key");
    CHECK_FALSE(deploy_identity(tmp.dir / "k", "device", "x", b, &err));
    CHECK(err.find("only one of") != std::string::npos);
    CHECK(read(tmp.dir / "k" / "device.crt") == a.cert_pem);
    write(tmp.dir / "bad" / "device.crt", "hello");
    write(tmp.dir / "bad" / "device.key", "hello");
    CHECK_FALSE(deploy_identity(tmp.dir / "bad", "device", "x", b, &err));
    CHECK(err.find("not a certificate") != std::string::npos);

    write(tmp.dir / "f.bin", "abc");
    CHECK(deploy_file_sha256(tmp.dir / "f.bin") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(deploy_file_sha256(tmp.dir / "nope.bin") == "");
}

TEST_CASE("deploy: off by default - no port; the setting is the device's and is kept")
{
    Temp tmp;
    DeployService svc;
    CHECK_FALSE(svc.available());
    CHECK(svc.set_enabled(true).find("--data-dir") != std::string::npos);
    CHECK_FALSE(svc.listening());

    svc.open((tmp.dir / "keys").u8string());
    CHECK(svc.available());
    CHECK_FALSE(svc.enabled());
    CHECK_FALSE(svc.start());                                     // not switched on: nothing listens
    CHECK(svc.port() == 0);
    CHECK_FALSE(fs::exists(tmp.dir / "keys" / "device.key"));     // and nothing was made

    Device d;
    d.on();
    const int port = d.svc.port();
    CHECK(port > 0);
    CHECK(fs::exists(d.keys() / "device.key"));
    json j = json::parse(read(d.keys() / "deploy.json"));
    CHECK(j["enabled"] == true);
    CHECK(j["port"] == port);
    const std::string fp = d.svc.fingerprint();
    CHECK(fp.rfind("SHA256:", 0) == 0);

    // a restart: on again, the same certificate
    DeployService again;
    again.open(d.keys().u8string());
    CHECK(again.enabled());
    CHECK(again.configured_port() == port);
    CHECK(again.fingerprint() == fp);

    CHECK(d.svc.set_enabled(false) == "");
    CHECK_FALSE(d.svc.listening());
    CHECK(json::parse(read(d.keys() / "deploy.json"))["enabled"] == false);
    tsock_t s = tcp_connect(port);
    char b;
    CHECK(recv(s, &b, 1, 0) <= 0);                                // the port is closed
    tsock_close(s);
    CHECK(d.svc.set_port(0) != "");
    CHECK(d.svc.set_port(70000) != "");
}

// ================================================================ the conversation

TEST_CASE("deploy: a new designer is accepted on the device, then its project arrives unchanged")
{
    Device d;
    d.on();
    Temp pc;
    std::string big(300 * 1024, 'x');
    for(size_t i = 0; i < big.size(); i++) big[i] = static_cast<char>(i * 31 + (i >> 8));
    write(pc.dir / "line 1.hmlproj", big);

    Sender s;
    REQUIRE(deploy_make_identity("pc-1", s.me));
    s.req.host = "127.0.0.1";
    s.req.port = d.svc.port();
    s.req.file = (pc.dir / "line 1.hmlproj").u8string();
    s.req.client_name = "office pc";

    // the designer first asks who the device is (nothing is sent), and the user confirms it
    DeploySend probe = s.req;
    probe.probe = true;
    DeployOutcome who = deploy_send(probe, s.me);
    REQUIRE_MESSAGE(who.ok, who.error);
    CHECK(who.device_fingerprint == d.svc.fingerprint());
    s.req.expect_fingerprint = who.device_fingerprint;

    // "Add a designer" is off: an unknown designer is turned away and learns nothing
    {
        DeployOutcome out = deploy_send(s.req, s.me);
        CHECK_FALSE(out.ok);
        CHECK(out.error.find("Add a designer") != std::string::npos);
        DeployService::Offer o;
        CHECK_FALSE(d.svc.pending(o));
        CHECK(d.svc.designers().empty());
    }

    d.svc.set_adding(true);
    s.go();
    DeployService::Offer offer;
    REQUIRE(until([&] { return d.svc.pending(offer); }));
    CHECK(offer.fingerprint == s.me.fingerprint);                 // what the designer shows its user ...
    CHECK(until([&] { return s.saw("pending " + s.me.fingerprint); }));   // ... and what the device shows
    CHECK(offer.name == "officepc");                              // (no spaces, nothing that is not printable)
    CHECK(offer.from == "127.0.0.1");
    DeployService::Received r;
    CHECK_FALSE(d.svc.received(r));                               // nothing is taken before the operator decides
    CHECK(d.svc.accept() == "");

    REQUIRE(until([&] { return d.svc.received(r); }));
    CHECK(r.name == "line_1.hmlproj");
    CHECK(r.from == "127.0.0.1");
    CHECK(r.designer.find(s.me.fingerprint) != std::string::npos);
    CHECK(read(fs::u8path(r.file)) == big);
    CHECK(fs::u8path(r.file).parent_path() == d.keys());
    CHECK_FALSE(s.done);                                          // the designer waits for the installation
    d.svc.finish("");
    REQUIRE(until([&] { return s.done.load(); }));
    s.join();
    CHECK_MESSAGE(s.out.ok, s.out.error);
    CHECK(s.saw("sent 100"));
    CHECK(s.saw("received"));
    CHECK_FALSE(fs::exists(fs::u8path(r.file)));                  // the copy next to the keys is gone

    // it is known now (also after a restart): no question the next time; a failed install is told
    REQUIRE(d.svc.designers().size() == 1);
    CHECK(d.svc.designers()[0].fingerprint == s.me.fingerprint);
    CHECK(json::parse(read(d.keys() / "deploy.json"))["designers"][0]["fingerprint"] == s.me.fingerprint);
    d.svc.set_adding(false);
    Sender s2;
    s2.me = s.me;
    s2.req = s.req;
    s2.go();
    REQUIRE(until([&] { return d.svc.received(r); }));
    CHECK_FALSE(s2.saw("pending"));
    d.svc.finish("the project is for a display of 480 x 320");
    REQUIRE(until([&] { return s2.done.load(); }));
    s2.join();
    CHECK_FALSE(s2.out.ok);
    CHECK(s2.out.error.find("not installed: the project is for a display of 480 x 320") != std::string::npos);

    // removed on the device: turned away again
    CHECK(d.svc.remove(s.me.fingerprint) == "");
    CHECK(d.svc.remove(s.me.fingerprint) != "");
    DeployOutcome out = deploy_send(s.req, s.me);
    CHECK_FALSE(out.ok);
    CHECK(out.error.find("not accepted") != std::string::npos);
}

TEST_CASE("deploy: rejected, nobody decides, the device is not the one confirmed before")
{
    Device d;
    d.on();
    Temp pc;
    write(pc.dir / "p.hmlproj", "data");
    DeployIdentity me;
    REQUIRE(deploy_make_identity("pc", me));
    DeploySend req;
    req.host = "127.0.0.1";
    req.port = d.svc.port();
    req.file = (pc.dir / "p.hmlproj").u8string();
    req.expect_fingerprint = d.svc.fingerprint();

    d.svc.set_adding(true);
    {
        Sender s;
        s.me = me; s.req = req;
        s.go();
        DeployService::Offer o;
        REQUIRE(until([&] { return d.svc.pending(o); }));
        d.svc.reject();
        REQUIRE(until([&] { return s.done.load(); }));
        s.join();
        CHECK_FALSE(s.out.ok);
        CHECK(s.out.error.find("refused") != std::string::npos);
    }
    d.svc.set_offer_timeout(1);
    {
        DeployOutcome out = deploy_send(req, me);
        CHECK_FALSE(out.ok);
        CHECK(out.error.find("in time") != std::string::npos);
    }
    CHECK(d.svc.designers().empty());
    DeployService::Received r;
    CHECK_FALSE(d.svc.received(r));

    // another device answers at that address (or somebody in between): nothing is sent
    Device other;
    other.on();
    other.svc.set_adding(true);
    req.port = other.svc.port();
    DeployOutcome out = deploy_send(req, me);
    CHECK_FALSE(out.ok);
    CHECK(out.error.find("not the one that was confirmed") != std::string::npos);
    CHECK(out.device_fingerprint == other.svc.fingerprint());
    DeployService::Offer o;
    CHECK_FALSE(other.svc.pending(o));
    // a device that was never confirmed: nothing is sent either
    req.expect_fingerprint.clear();
    out = deploy_send(req, me);
    CHECK_FALSE(out.ok);
    CHECK(out.error.find("confirmed first") != std::string::npos);
    // nobody listens
    other.svc.set_enabled(false);
    req.connect_timeout_ms = 1500;
    out = deploy_send(req, me);
    CHECK_FALSE(out.ok);
    CHECK(out.error.find("cannot connect") != std::string::npos);
}

// ================================================================ security: a hostile sender

TEST_CASE("deploy: garbage, a damaged file, a wrong kind of file, a stalled sender never get anything installed")
{
    Device d;
    d.on();
    d.svc.set_install_timeout(2);
    DeployIdentity me;
    REQUIRE(deploy_make_identity("pc", me));
    const int port = d.svc.port();
    DeployService::Received r;

    // not TLS at all; a connection that says nothing
    {
        tsock_t s = tcp_connect(port);
        const char junk[] = "GET / HTTP/1.0\r\n\r\n\x16\x03\x01\xff\xff garbage garbage garbage";
        send(s, junk, sizeof(junk), kTestSendFlags);
        char b[64];
        CHECK(recv(s, b, sizeof(b), 0) <= 7);                     // at most a TLS alert: nothing about the device
        tsock_close(s);
        tsock_t idle = tcp_connect(port);
        tsock_close(idle);
    }
    CHECK(d.svc.listening());

    // accepted on the device, so that the file handling is reached
    auto talk = [&](const std::function<void(DeployConn&)>& f) {
        DeployConn c;
        std::string err;
        REQUIRE_MESSAGE(c.connect("127.0.0.1", port, me, 5000, &err), err);
        f(c);
    };
    d.svc.set_adding(true);
    std::thread accepter([&] { DeployService::Offer o; if(until([&] { return d.svc.pending(o); })) d.svc.accept(); });
    talk([&](DeployConn& c) {
        std::string line;
        REQUIRE(c.write_line("HML-DEPLOY 1 pc"));
        REQUIRE(c.read_line(line, 200, 5000));
        CHECK(line.rfind("PENDING ", 0) == 0);
        REQUIRE(c.read_line(line, 200, 10000));
        CHECK(line == "READY");
    });
    accepter.join();
    d.svc.set_adding(false);
    REQUIRE(d.svc.designers().size() == 1);

    auto put = [&](const std::string& request, const std::string& body, int wait_ms = 8000) {
        std::string answer = "(closed)";
        talk([&](DeployConn& c) {
            std::string line;
            REQUIRE(c.write_line("HML-DEPLOY 1 pc"));
            REQUIRE(c.read_line(line, 200, 5000));
            REQUIRE(line == "READY");
            c.write_line(request);
            if(!c.read_line(line, 400, wait_ms)) return;
            answer = line;
            if(line != "SEND") return;                            // (refused before a byte of the file)
            answer = "(closed)";
            if(!body.empty()) c.write(body.data(), body.size());
            if(c.read_line(line, 400, wait_ms)) answer = line;
        });
        return answer;
    };
    const std::string good = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";   // of "abc"
    CHECK(put("PUT 3 " + std::string(64, '0') + " p.hmlproj", "abc").find("damaged") != std::string::npos);
    CHECK(put("PUT 3 " + good + " p.exe", "abc").find("not a project file") != std::string::npos);
    CHECK(put("PUT 3 " + good + " ../../evil.hmlproj", "abc") == "RECEIVED");      // (only the last part of the name is looked at, and it names nothing on disk)
    REQUIRE(until([&] { return d.svc.received(r); }));
    CHECK(r.name == "evil.hmlproj");
    CHECK(fs::u8path(r.file) == d.keys() / "incoming.hmlproj");
    d.svc.finish("no");
    CHECK(until([&] { return !d.svc.received(r); }));
    CHECK(put("PUT 99999999999999 " + good + " p.hmlproj", "").find("too large") != std::string::npos);
    CHECK(put("PUT 0 " + good + " p.hmlproj", "").find("too large") != std::string::npos);
    CHECK(put("PUT three " + good + " p.hmlproj", "").find("unknown request") != std::string::npos);
    CHECK(put("DELETE everything", "").find("unknown request") != std::string::npos);
    CHECK(put(std::string(5000, 'A'), "") == "(closed)");
    // fewer bytes than promised, then the sender goes away: nothing is kept
    CHECK(put("PUT 1000 " + good + " p.hmlproj", "abc", 300) == "(closed)");
    CHECK(until([&] { return !fs::exists(d.keys() / "incoming.hmlproj.part"); }));
    CHECK_FALSE(fs::exists(d.keys() / "incoming.hmlproj"));
    CHECK_FALSE(d.svc.received(r));

    // received, but the UI never installs it: the sender is told, the file is not kept
    CHECK(put("PUT 3 " + good + " p.hmlproj", "abc") == "RECEIVED");
    CHECK(until([&] { return !fs::exists(d.keys() / "incoming.hmlproj"); }));
    CHECK_FALSE(d.svc.received(r));

    // and after all that an honest project still gets through
    Temp pc;
    write(pc.dir / "p.hmlproj", "abc");
    Sender s;
    s.me = me;
    s.req.host = "127.0.0.1";
    s.req.port = port;
    s.req.file = (pc.dir / "p.hmlproj").u8string();
    s.req.expect_fingerprint = d.svc.fingerprint();
    s.go();
    REQUIRE(until([&] { return d.svc.received(r); }));
    d.svc.finish("");
    REQUIRE(until([&] { return s.done.load(); }));
    s.join();
    CHECK_MESSAGE(s.out.ok, s.out.error);
    d.svc.stop();
    CHECK_FALSE(d.svc.listening());
}

// ================================================================ the page

TEST_CASE("deploy: System > Deploy - switched on, a designer accepted, the project installed, the runtime restarts")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", src = tmp.dir / "pc" / "new", keys = tmp.dir / "device" / "keys";
    make_project(proj, "Old");
    write(proj / "project.local.json", "{ \"drivers\": {} }");
    make_project(src, "New");
    const fs::path file = tmp.dir / "pc" / "new.hmlproj";
    std::string err;
    REQUIRE_MESSAGE(pack_project(src, file, &err), err);
    make_project(tmp.dir / "pc" / "small", "Small", 480, 320);
    REQUIRE(pack_project(tmp.dir / "pc" / "small", tmp.dir / "pc" / "small.hmlproj", &err));

    Runtime rt;
    DeployService& ds = deploy_service();
    write(keys / "deploy.json", R"({ "enabled": false })");
    ds.open(keys.u8string());
    Project p;
    REQUIRE(p.load((proj / "project.json").generic_u8string()));
    SystemSettings& sys = system_settings();
    REQUIRE(sys.page("deploy"));
    auto text = [](const char* key) { return tags().find(std::string("sys.deploy.") + key)->get().format(); };
    auto flag = [](const char* key) { return tags().find(std::string("sys.deploy.") + key)->get_number() != 0; };
    CHECK_FALSE(flag("receive"));
    CHECK(text("state").find("Off") == 0);
    CHECK(text("count") == "No designer is accepted.");

    sys.command("deploy", "adding");                              // not before it receives at all
    CHECK(text("status").find("Receive projects") != std::string::npos);
    CHECK_FALSE(ds.adding());
    {   // (a free port for the test)
        Device probe;
        probe.on();
        const int port = probe.svc.port();
        probe.svc.set_enabled(false);
        REQUIRE(ds.set_port(port) == "");
    }
    sys.command("deploy", "receive");
    REQUIRE(ds.listening());
    CHECK(flag("receive"));
    CHECK(text("state").find(fingerprint_for_reading(ds.fingerprint())) != std::string::npos);
    sys.command("deploy", "adding");
    CHECK(flag("adding"));

    Sender s;
    REQUIRE(deploy_make_identity("pc", s.me));
    s.req.host = "127.0.0.1";
    s.req.port = ds.port();
    s.req.file = file.u8string();
    s.req.client_name = "pc-7";
    s.req.expect_fingerprint = ds.fingerprint();
    s.go();
    REQUIRE(until([&] { return flag("pending"); }, [&] { sys.poll(); }));
    CHECK(text("pending_text").find(fingerprint_for_reading(s.me.fingerprint)) != std::string::npos);
    CHECK(text("pending_text").find("pc-7") != std::string::npos);
    CHECK(title_of(proj) == "Old");
    sys.command("deploy", "accept");
    CHECK_FALSE(flag("adding"));                                  // one designer per switch
    REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); }));
    s.join();
    CHECK_MESSAGE(s.out.ok, s.out.error);
    CHECK(title_of(proj) == "New");
    CHECK(read(proj / "project.local.json") == "{ \"drivers\": {} }");     // what the device owns stays
    CHECK(text("status").find("installed") != std::string::npos);
    CHECK(text("d1").find(fingerprint_for_reading(s.me.fingerprint)) != std::string::npos);
    CHECK(text("count") == "1 accepted designer(s)");
    for(int i = 0; i < 5; i++) sys.poll();
    CHECK(rt.restart_requests == 1);

    // a project the device cannot run is not installed, and the designer hears why
    Sender bad;
    bad.me = s.me;
    bad.req = s.req;
    bad.req.file = (tmp.dir / "pc" / "small.hmlproj").u8string();
    bad.go();
    REQUIRE(until([&] { return bad.done.load(); }, [&] { sys.poll(); }));
    bad.join();
    CHECK_FALSE(bad.out.ok);
    CHECK(bad.out.error.find("not installed") != std::string::npos);
    CHECK(title_of(proj) == "New");
    CHECK(rt.restart_requests == 1);

    // removed (asked first), switched off
    sys.command("deploy", "delete", "1");
    CHECK(flag("confirm"));
    sys.command("deploy", "delete_no");
    CHECK(ds.designers().size() == 1);
    sys.command("deploy", "delete", "1");
    sys.command("deploy", "delete_yes");
    CHECK(ds.designers().empty());
    sys.command("deploy", "receive");
    CHECK_FALSE(ds.listening());
    CHECK_FALSE(flag("receive"));

    ui().unload_all();
    p.stop_logs();
    p.unload();
    ds.open("");
}

// ================================================================ the runtime itself

namespace {

// this test program: something that starts here and answers "--help" with 0, like a runtime
fs::path self_exe()
{
#ifdef _WIN32
    return fs::u8path(install_dir()) / "hml_tests.exe";
#else
    return fs::u8path(install_dir()) / "hml_tests";
#endif
}
#ifdef _WIN32
const char* kExeName = "hml_runtime.exe";
#else
const char* kExeName = "hml_runtime";
#endif

// a device that already accepts the designer `me`
void accept_beforehand(Device& d, const DeployIdentity& me)
{
    write(d.keys() / "deploy.json", "{ \"enabled\": false, \"port\": 1, \"designers\": [ { \"fingerprint\": \"" + me.fingerprint + "\", \"name\": \"pc\", \"added\": \"2026-01-01 00:00\" } ] }");
    d.svc.open(d.keys().u8string());
    REQUIRE(d.svc.designers().size() == 1);
    CHECK_FALSE(d.svc.designers()[0].runtime);                    // never allowed by itself
    d.on();
}

} // namespace

TEST_CASE("deploy: the runtime is replaced only by a designer the device allows to, and only by one that starts here")
{
    REQUIRE(fs::exists(self_exe()));
    Device d;
    DeployIdentity me;
    REQUIRE(deploy_make_identity("pc", me));
    accept_beforehand(d, me);
    const fs::path exe = d.tmp.dir / "device" / kExeName, prev = fs::path(exe.u8string() + ".prev"), staged = fs::u8path(deploy_staged_runtime(exe.u8string()));
    write(exe, "OLD RUNTIME");
    CHECK(staged.parent_path() == exe.parent_path());             // one rename puts it in place
    CHECK(staged != exe);
    Temp pc;
    write(pc.dir / "junk_runtime", std::string(4000, 'j'));
    write(pc.dir / "p.hmlproj", "abc");

    auto request = [&](const fs::path& runtime, bool with_project) {
        DeploySend req;
        req.host = "127.0.0.1";
        req.port = d.svc.port();
        req.expect_fingerprint = d.svc.fingerprint();
        req.runtime_file = runtime.u8string();
        if(with_project) req.file = (pc.dir / "p.hmlproj").u8string();
        return req;
    };
    // what the page does on the UI thread, here: install what arrives until the sender is done
    auto serve = [&](Sender& s, std::vector<std::string>* kinds = nullptr) {
        REQUIRE(until([&] { return s.done.load(); }, [&] {
            DeployService::Received r;
            if(!d.svc.received(r)) return;
            if(kinds) kinds->push_back(r.runtime ? "runtime" : "project");
            d.svc.finish(r.runtime ? deploy_install_runtime(r.file, d.svc.runtime_path()) : std::string());
        }));
        s.join();
    };

    // the device does not know where its program is: nothing is taken
    {
        d.svc.set_runtime_allowed(me.fingerprint, true);
        DeployOutcome out = deploy_send(request(pc.dir / "junk_runtime", false), me);
        CHECK_FALSE(out.ok);
        CHECK(out.error.find("cannot replace its runtime") != std::string::npos);
        d.svc.set_runtime_allowed(me.fingerprint, false);
    }
    d.svc.set_runtime_path(exe.u8string());

    // accepted for projects, not allowed for the runtime: refused before a byte of it is kept,
    // and the project that would have followed is not installed either
    {
        Sender s;
        s.me = me;
        s.req = request(self_exe(), true);
        s.go();
        std::vector<std::string> kinds;
        serve(s, &kinds);
        CHECK_FALSE(s.out.ok);
        CHECK(s.out.error.find("may not replace the runtime") != std::string::npos);
        CHECK(kinds.empty());
        CHECK(read(exe) == "OLD RUNTIME");
        CHECK_FALSE(fs::exists(staged));
        CHECK_FALSE(fs::exists(fs::path(staged.u8string() + ".part")));
    }

    // allowed (kept in deploy.json, off again leaves no trace)
    CHECK(d.svc.set_runtime_allowed("SHA256:nobody", true) != "");
    CHECK(d.svc.set_runtime_allowed(me.fingerprint, true) == "");
    CHECK(d.svc.designers()[0].runtime);
    CHECK(json::parse(read(d.keys() / "deploy.json"))["designers"][0]["runtime"] == true);

    // a file that does not start on this device: nothing changes, the designer hears why
    {
        Sender s;
        s.me = me;
        s.req = request(pc.dir / "junk_runtime", true);
        s.go();
        std::vector<std::string> kinds;
        serve(s, &kinds);
        CHECK_FALSE(s.out.ok);
        CHECK(s.out.error.find("the runtime was not installed") != std::string::npos);
        CHECK(s.out.error.find("does not start on this device") != std::string::npos);
        CHECK(kinds == std::vector<std::string>{ "runtime" });   // the project was not even sent
        CHECK(read(exe) == "OLD RUNTIME");
        CHECK_FALSE(fs::exists(prev));
        CHECK_FALSE(fs::exists(staged));
    }

    // a damaged one (the checksum of something else)
    {
        DeployConn c;
        std::string err, line;
        REQUIRE_MESSAGE(c.connect("127.0.0.1", d.svc.port(), me, 5000, &err), err);
        REQUIRE(c.write_line("HML-DEPLOY 1 pc"));
        REQUIRE(c.read_line(line, 200, 5000));
        REQUIRE(line == "READY");
        c.write_line("RUNTIME 3 " + std::string(64, '0') + " hml_runtime");
        REQUIRE(c.read_line(line, 400, 8000));
        REQUIRE(line == "SEND");
        c.write("abc", 3);
        REQUIRE(c.read_line(line, 400, 8000));
        CHECK(line.find("damaged") != std::string::npos);
        CHECK(read(exe) == "OLD RUNTIME");
        CHECK_FALSE(fs::exists(staged));
    }

    // one that starts: put in place, the one before kept, and the project follows on the same connection
    {
        Sender s;
        s.me = me;
        s.req = request(self_exe(), true);
        s.go();
        std::vector<std::string> kinds;
        serve(s, &kinds);
        CHECK_MESSAGE(s.out.ok, s.out.error);
        CHECK(kinds == std::vector<std::string>{ "runtime", "project" });
        CHECK(s.saw("runtime installed"));
        CHECK(s.saw("received"));
        CHECK(fs::file_size(exe) == fs::file_size(self_exe()));
        CHECK(deploy_file_sha256(exe) == deploy_file_sha256(self_exe()));
        CHECK(read(prev) == "OLD RUNTIME");
        CHECK_FALSE(fs::exists(staged));
        CHECK(until([&] { return !d.svc.serving(); }));           // (the restart waits for this)
    }
    // the runtime alone
    {
        write(exe, "OLDER");
        Sender s;
        s.me = me;
        s.req = request(self_exe(), false);
        s.go();
        std::vector<std::string> kinds;
        serve(s, &kinds);
        CHECK_MESSAGE(s.out.ok, s.out.error);
        CHECK(kinds == std::vector<std::string>{ "runtime" });
        CHECK(read(prev) == "OLDER");
    }

    // taken away again: refused; the project alone still goes through
    CHECK(d.svc.set_runtime_allowed(me.fingerprint, false) == "");
    CHECK_FALSE(json::parse(read(d.keys() / "deploy.json"))["designers"][0].contains("runtime"));
    {
        DeployOutcome out = deploy_send(request(self_exe(), false), me);
        CHECK_FALSE(out.ok);
        CHECK(out.error.find("may not replace the runtime") != std::string::npos);
        Sender s;
        s.me = me;
        s.req = request("", true);
        s.go();
        serve(s);
        CHECK_MESSAGE(s.out.ok, s.out.error);
    }
}

TEST_CASE("deploy: System > Deploy - allowing a designer to replace the runtime asks first; runtime and project, one restart")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", src = tmp.dir / "pc" / "new", keys = tmp.dir / "device" / "keys";
    const fs::path exe = tmp.dir / "device" / kExeName;
    make_project(proj, "Old");
    make_project(src, "New");
    const fs::path file = tmp.dir / "pc" / "new.hmlproj";
    std::string err;
    REQUIRE_MESSAGE(pack_project(src, file, &err), err);
    write(exe, "OLD RUNTIME");
    DeployIdentity me;
    REQUIRE(deploy_make_identity("pc", me));
    int port = 0;
    {   // (a free port for the test)
        Device probe;
        probe.on();
        port = probe.svc.port();
        probe.svc.set_enabled(false);
    }
    write(keys / "deploy.json", "{ \"enabled\": true, \"port\": " + std::to_string(port) + ", \"designers\": [ { \"fingerprint\": \"" + me.fingerprint + "\", \"name\": \"pc-7\" } ] }");

    Runtime rt;
    DeployService& ds = deploy_service();
    ds.open(keys.u8string());
    ds.set_runtime_path(exe.u8string());
    REQUIRE(ds.start());
    Project p;
    REQUIRE(p.load((proj / "project.json").generic_u8string()));
    SystemSettings& sys = system_settings();
    auto text = [](const char* key) { return tags().find(std::string("sys.deploy.") + key)->get().format(); };
    auto flag = [](const char* key) { return tags().find(std::string("sys.deploy.") + key)->get_number() != 0; };
    CHECK(text("d1").find("may replace the runtime") == std::string::npos);

    // asked first; "Cancel" changes nothing
    sys.command("deploy", "runtime", "1");
    CHECK(flag("confirm_rt"));
    CHECK(text("confirm_rt_text").find(fingerprint_for_reading(me.fingerprint)) != std::string::npos);
    CHECK_FALSE(ds.designers()[0].runtime);
    sys.command("deploy", "runtime_no");
    CHECK_FALSE(flag("confirm_rt"));
    CHECK_FALSE(ds.designers()[0].runtime);
    sys.command("deploy", "runtime", "1");
    sys.command("deploy", "runtime_yes");
    CHECK(ds.designers()[0].runtime);
    CHECK(text("d1").find("may replace the runtime") != std::string::npos);

    Sender s;
    s.me = me;
    s.req.host = "127.0.0.1";
    s.req.port = ds.port();
    s.req.file = file.u8string();
    s.req.runtime_file = self_exe().u8string();
    s.req.expect_fingerprint = ds.fingerprint();
    s.go();
    REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); CHECK(rt.restart_requests == 0); }));   // not while the designer is still sending
    s.join();
    CHECK_MESSAGE(s.out.ok, s.out.error);
    CHECK(deploy_file_sha256(exe) == deploy_file_sha256(self_exe()));
    CHECK(read(fs::path(exe.u8string() + ".prev")) == "OLD RUNTIME");
    CHECK(title_of(proj) == "New");
    REQUIRE(until([&] { return rt.restart_requests > 0; }, [&] { sys.poll(); }));
    for(int i = 0; i < 6; i++) sys.poll();
    CHECK(rt.restart_requests == 1);                              // once, for both

    // taken away at once, without a question
    sys.command("deploy", "runtime", "1");
    CHECK_FALSE(flag("confirm_rt"));
    CHECK_FALSE(ds.designers()[0].runtime);

    ui().unload_all();
    p.stop_logs();
    p.unload();
    ds.stop();
    ds.set_runtime_path("");
    ds.open("");
}

// ================================================================ a project that can do more

namespace {

// a project with a list of what it can do: `extra` goes into project.json (", \"schedules\": ...")
void make_project_with(const fs::path& dir, const std::string& title, const std::string& extra)
{
    write(dir / "project.json", "{ \"title\": \"" + title + "\", \"display\": { \"width\": 800, \"height\": 480 }, \"start_screen\": \"s\", "
          "\"tags\": [ { \"name\": \"a\", \"type\": \"int\" } ], \"screens\": [\"screens/s.json\"]" + extra + " }");
    write(dir / "screens" / "s.json", "{ \"name\": \"s\", \"children\": [ { \"type\": \"label\", \"text\": \"" + title + "\" } ] }");
}
const char* kSchedule = R"(, "schedules": [ { "name": "start", "at": "06:00", "do": { "action": "set", "tag": "a", "value": 1 } } ])";
const char* kScript = R"(, "scripts": [ { "name": "auto", "trigger": "interval", "interval_ms": 500, "code": "tag.a = tag.a + 1" } ])";

} // namespace

TEST_CASE("deploy: a project that can do other things than the one before waits for the operator; the same list goes in by itself")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", keys = tmp.dir / "device" / "keys", pc = tmp.dir / "pc";
    make_project(proj, "Old");                                    // only draws screens
    std::string err;
    auto pack = [&](const char* name, const std::string& title, const std::string& extra) {
        make_project_with(pc / name, title, extra);
        const fs::path file = pc / (std::string(name) + ".hmlproj");
        REQUIRE_MESSAGE(pack_project(pc / name, file, &err), err);
        return file;
    };
    const fs::path plain = pack("plain", "Plain", ""), sched = pack("sched", "Sched", kSchedule), sched2 = pack("sched2", "Sched2", kSchedule), script = pack("script", "Script", kScript);

    DeployIdentity me;
    REQUIRE(deploy_make_identity("pc", me));
    int port = 0;
    {
        Device probe;
        probe.on();
        port = probe.svc.port();
        probe.svc.set_enabled(false);
    }
    write(keys / "deploy.json", "{ \"enabled\": true, \"port\": " + std::to_string(port) + ", \"designers\": [ { \"fingerprint\": \"" + me.fingerprint + "\", \"name\": \"pc-7\" } ] }");

    Runtime rt;
    DeployService& ds = deploy_service();
    ds.open(keys.u8string());
    CHECK(ds.ask());                                              // asking is the default
    CHECK(ds.approved().empty());
    REQUIRE(ds.start());
    Project p;
    REQUIRE(p.load((proj / "project.json").generic_u8string()));
    REQUIRE(p.build_screens() == 1);                              // (the operator is looking at a screen of the project)
    SystemSettings& sys = system_settings();
    auto text = [](const char* key) { return tags().find(std::string("sys.deploy.") + key)->get().format(); };
    auto flag = [](const char* key) { return tags().find(std::string("sys.deploy.") + key)->get_number() != 0; };
    auto send = [&](Sender& s, const fs::path& file) {
        s.me = me;
        s.req.host = "127.0.0.1";
        s.req.port = ds.port();
        s.req.file = file.u8string();
        s.req.client_name = "pc-7";
        s.req.expect_fingerprint = ds.fingerprint();
        s.go();
    };
    CHECK(flag("ask"));
    CHECK_FALSE(flag("arrived"));

    // a project that only draws screens: nothing to ask
    {
        Sender s;
        send(s, plain);
        REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); }));
        s.join();
        CHECK_MESSAGE(s.out.ok, s.out.error);
        CHECK_FALSE(s.saw("asking"));
        CHECK(title_of(proj) == "Plain");
    }
    const std::string before = ds.approved();                     // (what went in is what was agreed to: here, "nothing")

    // one that runs something by itself: not installed, the operator is asked, the designer waits
    {
        Sender s;
        send(s, sched);
        REQUIRE(until([&] { return flag("arrived"); }, [&] { sys.poll(); }));
        CHECK(text("arrived_text").find("sched.hmlproj") != std::string::npos);
        CHECK(text("arrived_text").find("pc-7") != std::string::npos);
        CHECK(text("arrived_text").find("By itself: schedule start at 06:00: writes a") != std::string::npos);
        CHECK(ui().current_screen().value_or("") == "__system_deploy");       // put in front of the operator
        CHECK(until([&] { return s.saw("asking"); }, [&] { sys.poll(); }));
        for(int i = 0; i < 20; i++) { sys.poll(); sleep_ms(10); }
        CHECK(title_of(proj) == "Plain");
        CHECK_FALSE(s.done);
        // "Reject": nothing changes, the designer hears why
        sys.command("deploy", "install_no");
        REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); }));
        s.join();
        CHECK_FALSE(s.out.ok);
        CHECK(s.out.error.find("did not allow this project") != std::string::npos);
        CHECK(title_of(proj) == "Plain");
        CHECK_FALSE(flag("arrived"));
        CHECK(ds.approved() == before);
    }
    // again, and "Install": in place, and what was agreed to is remembered
    {
        Sender s;
        send(s, sched);
        REQUIRE(until([&] { return flag("arrived"); }, [&] { sys.poll(); }));
        sys.command("deploy", "install_yes");
        REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); }));
        s.join();
        CHECK_MESSAGE(s.out.ok, s.out.error);
        CHECK(title_of(proj) == "Sched");
        CHECK_FALSE(flag("arrived"));
        CHECK(ds.approved().size() == 64);
        CHECK(ds.approved() != before);
        CHECK(json::parse(read(keys / "deploy.json"))["approved"] == ds.approved());
    }
    // another project with the same list (other screens, another title): no question
    {
        Sender s;
        send(s, sched2);
        REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); CHECK_FALSE(flag("arrived")); }));
        s.join();
        CHECK_MESSAGE(s.out.ok, s.out.error);
        CHECK_FALSE(s.saw("asking"));
        CHECK(title_of(proj) == "Sched2");
    }
    // another list again: asked again. Nobody answers and the designer gives up: the question goes away, nothing is installed
    {
        ds.set_install_timeout(2);
        Sender s;
        send(s, script);
        REQUIRE(until([&] { return flag("arrived"); }, [&] { sys.poll(); }));
        CHECK(text("arrived_text").find("1 script(s) run by themselves") != std::string::npos);
        REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); }));
        s.join();
        CHECK_FALSE(s.out.ok);
        CHECK(until([&] { return !flag("arrived"); }, [&] { sys.poll(); }));
        CHECK(title_of(proj) == "Sched2");
        ds.set_install_timeout(300);
    }
    // the device's own choice: do not ask (kept in deploy.json)
    {
        sys.command("deploy", "ask");
        CHECK_FALSE(ds.ask());
        CHECK(json::parse(read(keys / "deploy.json"))["ask"] == false);
        Sender s;
        send(s, script);
        REQUIRE(until([&] { return s.done.load(); }, [&] { sys.poll(); CHECK_FALSE(flag("arrived")); }));
        s.join();
        CHECK_MESSAGE(s.out.ok, s.out.error);
        CHECK(title_of(proj) == "Script");
        sys.command("deploy", "ask");
        CHECK(ds.ask());
    }

    ui().unload_all();
    p.stop_logs();
    p.unload();
    ds.stop();
    ds.open("");
}

TEST_CASE("deploy: a fingerprint is shown in groups of four, with nothing left out")
{
    CHECK(fingerprint_for_reading("SHA256:zleUE1/+rhCV") == "SHA256: zleU E1/+ rhCV");
    CHECK(fingerprint_for_reading("SHA256:abcde") == "SHA256: abcd e");
    CHECK(fingerprint_for_reading("nocolon") == "nocolon");
    std::string back;
    for(char c : fingerprint_for_reading("SHA256:TWpCGiFNpKNOfmI5WsFNWtfgNg8BCxfrNgAS39smkY4")) if(c != ' ') back += c;
    CHECK(back == "SHA256:TWpCGiFNpKNOfmI5WsFNWtfgNg8BCxfrNgAS39smkY4");
}
