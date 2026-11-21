// Uploads (upload.hpp): the logs written to files and handed to a server; what waits, trying
// again, what is kept where. Grouped by ISO/IEC 25010 characteristic (issue #67). No network and
// no log databases: the "server" and the "logs" are functions of the test; so is the clock.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "check.hpp"
#include "operation_log.hpp"
#include "system_settings.hpp"
#include "ui.hpp"
#include "upload.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct Sent { std::string url, name, content, username, password; bool post; };

struct Fixture {
    Runtime rt;
    fs::path dir;
    int64_t now = 1795000000000;
    std::vector<Sent> got;
    std::deque<NetResult> results;              // what the "server" answers, in turn (none left: ok)
    std::vector<std::string> exported;          // the logs that were asked for
    std::string missing;                        // a log that is not there

    Fixture()
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_upload_test_" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir);
        uploads().set_clock([this] { return now; });
        uploads().set_exporter([this](const std::string& log, const std::string& path) -> int64_t {
            exported.push_back(log);
            if(log == missing) return -1;
            std::ofstream f(fs::u8path(path), std::ios::binary);
            f << "time,value\r\n" << log << "," << exported.size() << "\r\n";
            return 1;
        });
        uploads().set_sender([this](const NetUpload& u, const NetTls&) {
            Sent s;
            s.url = u.url.shown(); s.name = u.name; s.username = u.username; s.password = u.password; s.post = u.post;
            std::ifstream f(fs::u8path(u.file), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            s.content = ss.str();
            got.push_back(s);
            if(results.empty()) { NetResult ok; ok.ok = true; return ok; }
            NetResult r = results.front();
            results.pop_front();
            return r;
        });
    }
    ~Fixture()
    {
        uploads().clear();
        uploads().set_sender(nullptr);
        uploads().set_exporter(nullptr);
        uploads().set_clock(nullptr);
        uploads().set_read_only(false);
        uploads().start(false);
        operation_log().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string store() const { return (dir / "project.local.json").generic_string(); }
    void configure(const std::string& cfg, const json* local = nullptr)
    {
        const json c = json::parse(cfg);
        uploads().configure(&c, local, dir.generic_string(), store(), "Line 1");
    }
    void run(int64_t ms) { for(int64_t t = 0; t < ms; t += 250) { now += 250; uploads().poll(); } }
    static NetResult fail(const char* why, bool permanent = false) { NetResult r; r.error = why; r.permanent = permanent; return r; }
    std::vector<std::string> files(const char* target) const
    {
        std::vector<std::string> out;
        std::error_code ec;
        for(const auto& e : fs::directory_iterator(dir / "logs" / "upload" / target, ec)) out.push_back(e.path().filename().generic_string());
        std::sort(out.begin(), out.end());
        return out;
    }
    json read_json(const std::string& file) const
    {
        std::ifstream f(fs::u8path(file), std::ios::binary);
        if(!f) return json();
        std::stringstream ss;
        ss << f.rdbuf();
        return json::parse(ss.str(), nullptr, false);
    }
};

const char* kCfg = R"~([ { "name": "nightly", "url": "https://files.plant.example/hml/", "username": "hmi", "password": "s3cret-pw",
                          "logs": ["data:line1", "alarms", "operation"], "retry_s": 60, "retries": 2, "keep": 4 } ])~";

} // namespace

// ================================================================ functional suitability

TEST_CASE("upload: an entry as it is written; what the device changed on top; what is none")
{
    UploadTarget t;
    std::string err;
    REQUIRE_MESSAGE(upload_parse(json::parse(R"({ "name": "nightly", "url": "https://files.plant.example/hml/", "logs": ["data:line1", "alarms", "alarms"] })"), nullptr, t, &err), err);
    CHECK(t.url.shown() == "https://files.plant.example/hml/");
    CHECK_FALSE(t.post);
    CHECK(t.logs == std::vector<std::string>{ "data:line1", "alarms" });
    CHECK(t.tls.verify);
    CHECK(t.enabled);
    CHECK(t.retries == 3);
    CHECK(t.keep == 20);
    REQUIRE(upload_parse(json::parse(R"({ "name": "api", "url": "http://10.0.0.5:8080/api/upload", "method": "post", "logs": ["system"], "username": "u", "password": "p", "retries": 0, "keep": 1 })"), nullptr, t));
    CHECK(t.post);
    CHECK(t.username == "u");
    // project.local.json: the login and on / off - not where the files go
    const json local = json::parse(R"({ "username": "u2", "password": "p2", "enabled": false, "url": "https://evil.example/", "verify": false, "logs": ["system"] })");
    REQUIRE(upload_parse(json::parse(R"({ "name": "n", "url": "https://files.plant.example/hml/", "logs": ["alarms"] })"), &local, t));
    CHECK(t.username == "u2");
    CHECK(t.password == "p2");
    CHECK_FALSE(t.enabled);
    CHECK(t.url.host == "files.plant.example");
    CHECK(t.tls.verify);
    CHECK(t.logs == std::vector<std::string>{ "alarms" });
    for(const char* bad : { R"(5)", R"({})", R"({ "url": "https://h/", "logs": ["alarms"] })", R"({ "name": "a b", "url": "https://h/", "logs": ["alarms"] })",
                            R"({ "name": "../x", "url": "https://h/", "logs": ["alarms"] })", R"({ "name": "n", "logs": ["alarms"] })", R"({ "name": "n", "url": "files.example", "logs": ["alarms"] })",
                            R"({ "name": "n", "url": "https://user:pw@h/", "logs": ["alarms"] })", R"({ "name": "n", "url": "sftp://h/", "logs": ["alarms"] })",
                            R"({ "name": "n", "url": "https://h/" })", R"({ "name": "n", "url": "https://h/", "logs": [] })", R"({ "name": "n", "url": "https://h/", "logs": ["everything"] })",
                            R"({ "name": "n", "url": "https://h/", "logs": ["data:"] })", R"({ "name": "n", "url": "https://h/", "logs": [5] })", R"({ "name": "n", "url": "https://h/", "logs": "alarms" })",
                            R"({ "name": "n", "url": "https://h/", "logs": ["alarms"], "method": "delete" })", R"({ "name": "n", "url": "ftp://h/", "logs": ["alarms"], "method": "post" })" }) {
        CAPTURE(bad);
        err.clear();
        CHECK_FALSE(upload_parse(json::parse(bad), nullptr, t, &err));
        CHECK(err != "");
    }
}

TEST_CASE("upload: a run writes each log to a file and the files are sent one by one; a file the server has is deleted")
{
    Fixture f;
    f.configure(kCfg);
    CHECK(uploads().configured());
    CHECK(tags().find("sys.upload.waiting")->get_number() == 0);
    f.run(5000);
    CHECK(f.got.empty());                                        // nothing is sent by itself
    CHECK(uploads().run("nightly") == 3);
    CHECK(f.exported == std::vector<std::string>{ "data:line1", "alarms", "operation" });
    CHECK(uploads().waiting("nightly") == 3);
    CHECK(tags().find("sys.upload.waiting")->get_number() == 3);
    const std::vector<std::string> files = f.files("nightly");
    REQUIRE(files.size() == 3);
    for(const std::string& n : files) { CHECK(n.rfind("Line_1_", 0) == 0); CHECK(n.size() > 23); CHECK(n.substr(n.size() - 4) == ".csv"); CHECK(net_file_name(n) == n); }
    f.run(250);
    CHECK(f.got.size() == 1);                                    // one at a time
    f.run(1000);
    REQUIRE(f.got.size() == 3);
    CHECK(f.got[0].url == "https://files.plant.example/hml/");
    CHECK(f.got[0].username == "hmi");
    CHECK(f.got[0].password == "s3cret-pw");
    CHECK(f.got[0].name.find("_data-line1_") != std::string::npos);
    CHECK(f.got[0].content == "time,value\r\ndata:line1,1\r\n");
    CHECK(f.got[1].name.find("_alarms_") != std::string::npos);
    CHECK(f.got[2].name.find("_operation_") != std::string::npos);
    CHECK(uploads().waiting() == 0);
    CHECK(f.files("nightly").empty());
    CHECK(uploads().sent() == 3);
    CHECK(uploads().status("nightly").find("sent ") == 0);
    CHECK(tags().find("sys.upload.error")->get_number() == 0);
    // a log that is not there (switched off): the others go
    f.missing = "alarms";
    f.now += 60000;
    CHECK(uploads().run("") == 2);                               // "" = every upload
    f.run(2000);
    CHECK(f.got.size() == 5);
    // a second run in the same second does not make the same files twice
    f.missing.clear();
    CHECK(uploads().run("nightly") == 3);
    CHECK(uploads().run("nightly") == 0);
    CHECK(uploads().waiting() == 3);
    std::string why;
    CHECK(uploads().run("nope", &why) == -1);
    CHECK(why == "no upload 'nope'");
}

// ================================================================ reliability

TEST_CASE("upload: a server that cannot be reached - tried again, then left for the next run; nothing is lost")
{
    Fixture f;
    f.configure(kCfg);                                           // "retry_s": 60, "retries": 2
    for(int i = 0; i < 3; i++) f.results.push_back(Fixture::fail("cannot connect to files.plant.example:443"));
    CHECK(uploads().run("nightly") == 3);
    f.run(250);
    CHECK(f.got.size() == 1);
    CHECK(uploads().status("nightly").find("waiting (try 1 of 3 failed): cannot connect") == 0);
    CHECK(tags().find("sys.upload.error")->get_number() == 1);
    f.run(58000);
    CHECK(f.got.size() == 1);
    f.run(2500);
    CHECK(f.got.size() == 2);                                    // a minute later
    f.run(118000);
    CHECK(f.got.size() == 2);
    while(f.got.size() < 3) f.run(250);                          // two minutes after that: the third try fails too
    CHECK(uploads().status("nightly").find("given up after 3 tries") == 0);
    f.run(3600000);
    CHECK(f.got.size() == 3);                                    // no more tries by itself
    CHECK(uploads().waiting("nightly") == 3);                    // ... but every file is still there
    CHECK(f.files("nightly").size() == 3);
    // the next night: the old files go first, then the new ones
    const std::string oldest = f.files("nightly")[0];
    f.now += 86400000;
    CHECK(uploads().run("nightly") == 3);
    CHECK(uploads().waiting("nightly") == 4);                    // "keep": 4 - the two oldest of six gave way
    CHECK(uploads().dropped() == 2);
    f.run(5000);
    CHECK(f.got.size() == 7);
    CHECK(uploads().waiting() == 0);
    CHECK(f.files("nightly").empty());
    CHECK(tags().find("sys.upload.error")->get_number() == 0);
    (void)oldest;
}

TEST_CASE("upload: refused for good - the file is kept all the same; a new password lets it go")
{
    Fixture f;
    f.configure(kCfg);
    f.results.push_back(Fixture::fail("the server does not let us in (401 Unauthorized): the user name or the password?", true));
    CHECK(uploads().run("nightly") == 3);
    f.run(2000);
    CHECK(f.got.size() == 1);                                    // no second try with a login that is wrong
    CHECK(uploads().status("nightly").find("not sent: the server does not let us in") == 0);
    CHECK(uploads().waiting("nightly") == 3);
    f.run(600000);
    CHECK(f.got.size() == 1);
    // the operator sets the password on the device: what waits is sent
    CHECK(uploads().set_local("nightly", true, "hmi", "the-right-one") == "");
    f.run(2000);
    REQUIRE(f.got.size() == 4);
    CHECK(f.got[1].password == "the-right-one");
    CHECK(uploads().waiting() == 0);
}

TEST_CASE("upload: what waits survives a restart; files that are not ours in the folder are left alone")
{
    Fixture f;
    f.configure(kCfg);
    f.results.push_back(Fixture::fail("no route"));
    CHECK(uploads().run("nightly") == 3);
    f.run(250);
    const std::vector<std::string> before = f.files("nightly");
    REQUIRE(before.size() == 3);
    // somebody else's files in the folder
    { std::ofstream o(f.dir / "logs" / "upload" / "nightly" / "notes.txt"); o << "x"; }
    { std::ofstream o(f.dir / "logs" / "upload" / "nightly" / "evil name;rm.csv"); o << "x"; }
    // the power goes; the next start
    f.results.clear();
    f.got.clear();
    f.configure(kCfg);
    CHECK(uploads().waiting("nightly") == 3);
    f.run(2000);
    REQUIRE(f.got.size() == 3);
    CHECK(f.got[0].name == before[0]);
    CHECK(f.got[0].content.find("time,value") == 0);
    CHECK(f.files("nightly") == std::vector<std::string>{ "evil name;rm.csv", "notes.txt" });      // not sent, not deleted
    // 100 files from long ago: the newest "keep" stay
    for(int i = 0; i < 100; i++) { std::ofstream o(f.dir / "logs" / "upload" / "nightly" / vformat("Line_1_alarms_202611%02d-000000.csv", i % 28 + 1).c_str()); o << i; }
    f.got.clear();
    f.configure(kCfg);
    CHECK(uploads().waiting("nightly") == 4);
    f.run(5000);
    CHECK(f.got.size() == 4);
    CHECK(f.got[3].name == "Line_1_alarms_20261128-000000.csv");
}

TEST_CASE("upload: without \"uploads\", switched off, and while a project is only checked - nothing is written, nothing is sent")
{
    Fixture f;
    uploads().configure(nullptr, nullptr, f.dir.generic_string(), f.store(), "x");
    CHECK_FALSE(uploads().configured());
    std::string why;
    CHECK(uploads().run("", &why) == -1);
    CHECK(tags().find("sys.upload.waiting") == nullptr);
    f.run(1000);
    const json not_list = json::parse(R"({ "name": "x" })");
    uploads().configure(&not_list, nullptr, f.dir.generic_string(), f.store(), "x");
    CHECK_FALSE(uploads().configured());
    // entries that are none, two with one name, more than ten
    std::string many = R"([ 5, null, {}, { "name": "a", "url": "nope", "logs": ["alarms"] }, { "name": "a", "url": "https://h/", "logs": ["alarms"] }, { "name": "a", "url": "https://other/", "logs": ["alarms"] })";
    for(int i = 0; i < 15; i++) many += ", { \"name\": \"t" + std::to_string(i) + "\", \"url\": \"https://h/\", \"logs\": [\"alarms\"] }";
    many += "]";
    f.configure(many);
    REQUIRE(uploads().targets().size() == kUploadTargetsMax);
    CHECK(uploads().targets()[0].url.host == "h");
    // "ca_file": a file of the project, unless it says where else it is
    const std::string abs = (f.dir / "elsewhere.pem").generic_string();
    f.configure("[ { \"name\": \"a\", \"url\": \"https://h/\", \"logs\": [\"alarms\"], \"ca_file\": \"certs/ca.pem\" }, "
                "{ \"name\": \"b\", \"url\": \"https://h/\", \"logs\": [\"alarms\"], \"ca_file\": \"" + abs + "\" }, { \"name\": \"c\", \"url\": \"https://h/\", \"logs\": [\"alarms\"] } ]");
    REQUIRE(uploads().targets().size() == 3);
    CHECK(uploads().targets()[0].tls.ca_file == f.dir.generic_string() + "/certs/ca.pem");
    CHECK(uploads().targets()[1].tls.ca_file == abs);
    CHECK(uploads().targets()[2].tls.ca_file == "");
    // switched off on the device
    f.configure(kCfg);
    CHECK(uploads().set_local("nightly", false, "hmi", "") == "");
    CHECK(uploads().run("nightly", &why) == -1);
    CHECK(why == "the upload 'nightly' is switched off");
    f.run(2000);
    CHECK(f.got.empty());
    // --check
    f.configure(kCfg);
    uploads().set_read_only(true);
    CHECK(uploads().run("nightly", &why) == -1);
    f.run(2000);
    CHECK(f.got.empty());
    CHECK(f.exported.empty());
    CHECK_FALSE(fs::exists(f.dir / "logs"));
}

// ================================================================ security

TEST_CASE("upload: the password is kept in project.local.json only; names cannot leave the folder of the upload")
{
    Fixture f;
    const json ocfg = json::parse(R"({ "path": "op.db" })");
    REQUIRE(operation_log().load(&ocfg, f.dir.generic_string()));
    f.configure(R"~([ { "name": "nightly", "url": "https://files.plant.example/hml/", "username": "hmi", "logs": ["alarms"] },
                      { "name": "nas", "url": "ftp://nas.local/in/", "logs": ["system"] } ])~");
    CHECK(uploads().set_local("nightly", true, "hmi2", "t0p-secret") == "");
    CHECK(f.read_json(f.store())["uploads"] == json{ { "nightly", { { "username", "hmi2" }, { "password", "t0p-secret" } } } });
    CHECK(uploads().set_local("nas", true, "ftpuser", "ftp-pw") == "");
    CHECK(f.read_json(f.store())["uploads"]["nas"]["password"] == "ftp-pw");
    CHECK(f.read_json(f.store())["uploads"]["nightly"]["password"] == "t0p-secret");     // the other one stays
    // an empty password field leaves the password
    CHECK(uploads().set_local("nightly", true, "hmi2", "") == "");
    CHECK(uploads().targets()[0].password == "t0p-secret");
    CHECK(f.read_json(f.store())["uploads"]["nightly"]["password"] == "t0p-secret");
    // not in the log of operations, not in the state, not on the page
    for(const Operation& o : operation_log().query()) { CHECK(o.old_value.find("t0p-secret") == std::string::npos); CHECK(o.new_value.find("t0p-secret") == std::string::npos); }
    f.results.push_back(Fixture::fail("the server does not let us in (401)", true));
    uploads().run("nightly");
    f.run(500);
    CHECK(uploads().status("nightly").find("t0p-secret") == std::string::npos);
    SystemSettings& sys = system_settings();
    sys.add_page(make_upload_page());
    sys.create_tags();
    CHECK(tags().find("sys.upload.password")->get().format() == "");
    for(Tag* t : tags().all()) CHECK(t->get().format().find("t0p-secret") == std::string::npos);
    CHECK(tags().find("sys.upload.url")->get().format() == "https://files.plant.example/hml/");
    // what is refused
    CHECK(uploads().set_local("nope", true, "u", "p") == "No such upload.");
    CHECK(uploads().set_local("nightly", true, "u\r\nX: y", "p") != "");
    CHECK(uploads().set_local("nightly", true, "u", "p\nq") != "");
    CHECK(uploads().set_local("nightly", true, "u:v", "p") != "");
    // back to what the project says: only the password is left
    CHECK(uploads().set_local("nightly", true, "hmi", "") == "");
    CHECK(f.read_json(f.store())["uploads"]["nightly"] == json{ { "password", "t0p-secret" } });
    // a title that tries to leave the folder: the file name is made of safe characters
    const json c = json::parse(R"~([ { "name": "n", "url": "https://h/", "logs": ["data:../../etc"] } ])~");
    uploads().configure(&c, nullptr, f.dir.generic_string(), f.store(), "../../../evil\\title\r\n");
    CHECK(uploads().run("n") == 1);
    const std::vector<std::string> files = f.files("n");
    REQUIRE(files.size() == 1);
    CHECK(files[0].find("..") == std::string::npos);
    CHECK(files[0].find('/') == std::string::npos);
    CHECK(net_file_name(files[0]) == files[0]);
    CHECK(fs::exists(f.dir / "logs" / "upload" / "n" / files[0]));
    sys.clear();
}

// ================================================================ performance efficiency

TEST_CASE("upload: a slow server does not hold up the screen; a project that is unloaded meanwhile is safe")
{
    Fixture f;
    f.configure(kCfg);
    std::atomic<int> calls{ 0 };
    std::atomic<bool> release{ false };
    uploads().set_clock(nullptr);
    uploads().set_sender([&](const NetUpload&, const NetTls&) {
        calls++;
        for(int i = 0; i < 1000 && !release; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        NetResult ok;
        ok.ok = true;
        return ok;
    });
    uploads().start();
    CHECK(uploads().run("nightly") == 3);
    const uint32_t t0 = millis();
    for(int i = 0; i < 200; i++) uploads().poll();
    CHECK(millis() - t0 < 500);
    for(int i = 0; i < 200 && calls == 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(calls == 1);                                           // one at a time
    CHECK(uploads().waiting() == 3);
    release = true;
    for(int i = 0; i < 600 && uploads().waiting() > 0; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); uploads().poll(); }
    CHECK(uploads().waiting() == 0);
    CHECK(uploads().sent() == 3);
    // unloaded while a file is on its way: it stays on disk for the next start
    release = false;
    f.now += 5000;
    uploads().set_clock([&f] { return f.now; });
    CHECK(uploads().run("nightly") == 3);
    uploads().poll();
    for(int i = 0; i < 200 && calls < 4; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uploads().clear();
    release = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(f.files("nightly").size() == 3);
}

// ================================================================ usability: the page and the action

TEST_CASE("System > Upload: the login is set, \"send now\" sends, and what came of it is said")
{
    Fixture f;
    f.configure(R"~([ { "name": "nightly", "url": "https://files.plant.example/hml/", "username": "hmi", "logs": ["alarms"] },
                      { "name": "nas", "url": "ftp://nas.local:2121/in/", "logs": ["system", "operation"] } ])~");
    SystemSettings& sys = system_settings();
    sys.add_page(make_upload_page());
    sys.create_tags();
    auto text = [](const char* key) { return tags().find(std::string("sys.upload.") + key)->get().format(); };
    CHECK(text("target") == "0");                                // (a list: the number of the entry)
    CHECK(text("url") == "https://files.plant.example/hml/");
    CHECK(text("username") == "hmi");
    CHECK(text("state") == "nothing sent yet");
    CHECK(text("waiting") == "0");
    // send now: fails for a reason that is shown
    f.results.push_back(Fixture::fail("the server does not let us in (401 Unauthorized): the user name or the password?", true));
    sys.command("upload", "run");
    CHECK(text("status").find("1 file(s) are on their way") == 0);
    f.run(500);
    sys.poll();
    CHECK(text("state").find("not sent: the server does not let us in") == 0);
    CHECK(text("waiting") == "1");
    // the password is typed and applied: the file goes
    tags().find("sys.upload.password")->set_string("right", nullptr);
    CHECK(sys.apply("upload") == "Saved.");
    CHECK(text("password") == "");
    f.run(500);
    sys.poll();
    CHECK(text("state").find("sent ") == 0);
    CHECK(text("waiting") == "0");
    REQUIRE(f.got.size() == 2);
    CHECK(f.got[1].password == "right");
    // the other upload is picked in the list: the fields follow
    tags().find("sys.upload.target")->set_number(1, nullptr);
    sys.poll();
    CHECK(text("url") == "ftp://nas.local:2121/in/");
    CHECK(text("username") == "");
    sys.command("upload", "run");
    f.run(1000);
    CHECK(f.got.size() == 4);
    CHECK(f.got[3].url == "ftp://nas.local:2121/in/");
    // the action, as a schedule would call it
    register_upload_actions();
    f.now += 5000;
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "upload.run", "arg": "nightly" })"));
    f.run(500);
    CHECK(f.got.size() == 5);
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "upload.run", "arg": "no such upload" })"));
    f.now += 5000;
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "upload.run" })"));         // all of them
    f.run(2000);
    CHECK(f.got.size() == 8);
    sys.command("upload", "explode");
    for(const json& scr : sys.screens(480, 320)) REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen(SystemSettings::page_screen("upload"), "none"));
    f.rt.advance(300);
    ui().unload_all();
    sys.clear();
}

// ================================================================ maintainability / security: the checker

TEST_CASE("upload: --check finds a URL that is none, logs that are not there, and what should not be in a project file")
{
    Fixture f;
    fs::create_directories(f.dir / "screens");
    { std::ofstream o(f.dir / "screens" / "s.json", std::ios::binary); o << "{ \"name\": \"s\", \"children\": [] }"; }
    auto problems = [&](const std::string& cfg) {
        { std::ofstream o(f.dir / "project.json", std::ios::binary);
          o << "{ \"start_screen\": \"s\", \"tags\": [ { \"name\": \"hot\", \"type\": \"bool\" }, { \"name\": \"t\", \"type\": \"float\" } ], "
               "\"alarms\": { \"items\": [ { \"name\": \"h\", \"tag\": \"hot\" } ] }, \"data_log\": { \"groups\": [ { \"name\": \"line1\", \"tags\": [\"t\"], \"interval_ms\": 1000 } ] }, "
               "\"operation_log\": {}, \"uploads\": " << cfg << ", \"screens\": [\"screens/s.json\"] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((f.dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems(R"([ { "name": "nightly", "url": "https://files.example.com/hml/", "username": "hmi", "logs": ["data:line1", "alarms", "operation"], "retries": 5, "retry_s": 120, "keep": 10, "timeout_ms": 20000 },
                        { "name": "api", "url": "https://files.example.com/api/upload", "method": "post", "logs": ["alarms"], "enabled": false } ])") == "");
    CHECK(problems("{}").find("uploads: a list is needed") != std::string::npos);
    const std::string out = problems(R"([ { "name": "a", "url": "files.example.com", "logs": ["alarms"] }, { "name": "b", "url": "http://h/dir/", "username": "u", "password": "hunter2", "logs": ["data:nope", "system"] },
        { "name": "b", "url": "ftp://h/dir/", "logs": ["alarms"] }, { "name": "c", "url": "https://h/dir/", "verify": false, "ca_file": "no_such.pem", "speed": 1, "logs": ["alarms"] }, 5 ])");
    CHECK(out.find("uploads[0] (a): \"url\":") != std::string::npos);
    CHECK(out.find("uploads[1] (b): the password is in the project file") != std::string::npos);
    CHECK(out.find("hunter2") == std::string::npos);
    CHECK(out.find("uploads[1] (b): http:// is not secured: the files, the user name and the password can be read") != std::string::npos);
    CHECK(out.find("uploads[1] (b): logs: the data log has no group \"nope\"") != std::string::npos);
    CHECK(out.find("uploads[1] (b): logs: the project has no \"system_log\"") != std::string::npos);
    CHECK(out.find("uploads[2] (b): the name is used twice") != std::string::npos);
    CHECK(out.find("uploads[2] (b): ftp:// is not secured: the files can be read") != std::string::npos);
    CHECK(out.find("uploads[3] (c): \"verify\": false") != std::string::npos);
    CHECK(out.find("uploads[3] (c): ca_file: no file \"no_such.pem\"") != std::string::npos);
    CHECK(out.find("uploads[3] (c): unknown key \"speed\"") != std::string::npos);
    CHECK(out.find("uploads[4]: an upload is") != std::string::npos);
    CHECK(f.got.empty());
    CHECK_FALSE(fs::exists(f.dir / "logs" / "upload"));
}
