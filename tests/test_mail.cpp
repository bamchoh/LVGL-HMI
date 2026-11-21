// Mail (mail.hpp): alarms sent by e-mail, the queue, trying again, what is kept where.
// Grouped by ISO/IEC 25010 characteristic (issue #67). No network: the "server" is a function
// that says what came of each mail; the clock is the test's.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "alarm.hpp"
#include "check.hpp"
#include "mail.hpp"
#include "operation_log.hpp"
#include "system_settings.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    Runtime rt;
    fs::path dir;
    int64_t now = 1795000000000;                    // ms
    std::vector<SmtpMessage> got;                   // what the "server" was handed
    std::vector<SmtpAccount> accounts;
    std::deque<SmtpResult> results;                 // what it answers, in turn (none left: ok)
    Tag* hot;
    Tag* level;

    Fixture()
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_mail_test_" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir);
        hot = rt.tag("hot", TagType::Bool, 0);
        level = rt.tag("level", TagType::Float, 42.5);
        mail().set_clock([this] { return now; });
        mail().set_sender([this](const SmtpAccount& a, const SmtpTls&, const SmtpMessage& m) {
            accounts.push_back(a);
            got.push_back(m);
            if(results.empty()) { SmtpResult ok; ok.ok = true; return ok; }
            SmtpResult r = results.front();
            results.pop_front();
            return r;
        });
    }
    ~Fixture()
    {
        alarms().set_listener(nullptr);
        mail().clear();
        mail().set_sender(nullptr);
        mail().set_clock(nullptr);
        mail().set_read_only(false);
        mail().start(false);
        alarms().clear();
        operation_log().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string store() const { return (dir / "project.local.json").generic_string(); }
    std::string queue_file() const { return (dir / "logs" / "mail_queue.json").generic_string(); }
    void configure(const std::string& cfg, const json* local = nullptr)
    {
        const json c = json::parse(cfg);
        mail().configure(&c, local, dir.generic_string(), store(), "Line 1");
        alarms().set_listener([](const AlarmEvent& e) { mail().on_alarm(e); });
    }
    void with_alarms(const char* items = R"~([ { "name": "hot", "tag": "hot", "message": "Tank too hot", "severity": "critical" } ])~")
    {
        const json a = json{ { "items", json::parse(items) } };
        REQUIRE(alarms().load(&a, dir.generic_string()) >= 1);
    }
    void raise(Tag* t, bool on) { t->set_number(on ? 1 : 0, nullptr); rt.pump(); }
    // the main loop for a while (it looks at the mail every 250 ms)
    void run(int64_t ms) { for(int64_t t = 0; t < ms; t += 250) { now += 250; mail().poll(); } }
    static SmtpResult fail(const char* why, bool permanent = false) { SmtpResult r; r.error = why; r.permanent = permanent; return r; }
    json read_json(const std::string& file) const
    {
        std::ifstream f(fs::u8path(file), std::ios::binary);
        if(!f) return json();
        std::stringstream ss;
        ss << f.rdbuf();
        return json::parse(ss.str(), nullptr, false);
    }
    std::string read_text(const std::string& file) const
    {
        std::ifstream f(fs::u8path(file), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};

const char* kCfg = R"~({ "host": "smtp.plant.example", "username": "hmi", "password": "s3cret-pw", "from": "hmi@plant.example", "to": ["shift@plant.example", "boss@plant.example"],
                         "retry_s": 60, "retries": 3, "queue": 5 })~";

} // namespace

// ================================================================ functional suitability

TEST_CASE("mail: the settings as they are written - defaults, what the device changed on top, what is wrong")
{
    MailConfig c;
    CHECK(mail_parse(json::parse(kCfg), nullptr, c).empty());
    CHECK(c.account.host == "smtp.plant.example");
    CHECK(c.account.port == 587);
    CHECK(c.account.security == SmtpSecurity::StartTls);
    CHECK(c.tls.verify);
    CHECK(c.enabled);
    CHECK(c.to.size() == 2);
    CHECK(c.on_raised);
    CHECK_FALSE(c.on_cleared);
    CHECK(c.min_interval_s == 60);
    CHECK(mail_parse(json::parse(R"({ "host": "h", "security": "tls", "from": "a@b.c", "to": "x@y.z" })"), nullptr, c).empty());
    CHECK(c.account.port == 465);
    CHECK(c.to == std::vector<std::string>{ "x@y.z" });
    CHECK(mail_parse(json::parse(R"({ "host": "h", "security": "none", "from": "a@b.c", "to": "x@y.z; q@y.z  r@y.z", "port": 2525,
        "alarms": { "events": ["raised", "cleared"], "severities": ["critical"], "min_interval_s": 0, "subject": "S {alarm.name}", "body": "B" } })"), nullptr, c).empty());
    CHECK(c.account.port == 2525);
    CHECK(c.to.size() == 3);
    CHECK(c.on_cleared);
    CHECK(c.severities == std::vector<std::string>{ "critical" });
    CHECK(c.subject == "S {alarm.name}");
    // project.local.json on top: the recipients, the login, on / off - and nothing else
    const json local = json::parse(R"({ "to": ["night@plant.example"], "username": "u2", "password": "p2", "enabled": false, "host": "evil.example", "security": "none", "verify": false, "from": "ceo@plant.example" })");
    CHECK(mail_parse(json::parse(kCfg), &local, c).empty());
    CHECK(c.to == std::vector<std::string>{ "night@plant.example" });
    CHECK(c.account.username == "u2");
    CHECK(c.account.password == "p2");
    CHECK_FALSE(c.enabled);
    CHECK(c.account.host == "smtp.plant.example");               // where the mail goes and how it is secured is the project's
    CHECK(c.account.security == SmtpSecurity::StartTls);
    CHECK(c.tls.verify);
    CHECK(c.from == "hmi@plant.example");

    for(const char* bad : { R"(5)", R"({})", R"({ "host": "h", "from": "a@b.c" })", R"({ "host": "h", "to": "x@y.z" })", R"({ "from": "a@b.c", "to": "x@y.z" })",
                            R"({ "host": "h", "from": "a@b.c", "to": "not an address" })", R"({ "host": "h", "from": "a@b.c", "to": ["x@y.z", 5] })",
                            R"({ "host": "h", "from": "Name <a@b.c>", "to": "x@y.z" })", R"({ "host": "h", "from": "a@b.c", "to": "x@y.z", "security": "ssl" })",
                            R"({ "host": "h", "from": "a@b.c", "to": "x@y.z", "port": 70000 })", R"({ "host": "h; rm -rf /", "from": "a@b.c", "to": "x@y.z" })",
                            R"({ "host": "h", "from": "a@b.c", "to": "x@y.z", "alarms": { "events": ["exploded"] } })", R"({ "host": "h", "from": "a@b.c", "to": "x@y.z", "alarms": 5 })",
                            R"({ "host": "h", "from": "a@b.c\r\nBcc: x@evil.example", "to": "x@y.z" })" }) {
        CAPTURE(bad);
        CHECK_FALSE(mail_parse(json::parse(bad), nullptr, c).empty());
    }
    std::vector<std::string> to;
    CHECK(mail_parse_recipients(json("a@b.c,c@d.e ; f@g.h"), to));
    CHECK(to.size() == 3);
    CHECK(mail_parse_recipients(json(""), to));
    CHECK(to.empty());
    CHECK_FALSE(mail_parse_recipients(json("a@b.c, nope"), to));
    CHECK_FALSE(mail_parse_recipients(json(5), to));
    std::string many;
    for(int i = 0; i < 30; i++) many += "a" + std::to_string(i) + "@b.c,";
    CHECK_FALSE(mail_parse_recipients(json(many), to));
}

TEST_CASE("mail: an alarm is mailed with its text, its value and values of tags; the server gets the account of the project")
{
    Fixture f;
    f.with_alarms();
    f.configure(R"~({ "host": "smtp.plant.example", "username": "hmi", "password": "s3cret-pw", "from": "hmi@plant.example", "to": ["shift@plant.example"],
        "alarms": { "subject": "[{project}] {alarm.severity}: {alarm.message}", "body": "{alarm.time} {alarm.name} is {alarm.event}, level {level} {nothing} {" } })~");
    CHECK(mail().configured());
    CHECK(tags().find("sys.mail.waiting")->get_number() == 0);
    f.run(1000);
    CHECK(f.got.empty());
    f.raise(f.hot, true);
    CHECK(mail().waiting() == 1);
    CHECK(tags().find("sys.mail.waiting")->get_number() == 1);
    f.run(250);
    REQUIRE(f.got.size() == 1);
    CHECK(f.got[0].subject == "[Line 1] critical: Tank too hot");
    CHECK(f.got[0].body.find(" hot is raised, level 42.5 {nothing} {") != std::string::npos);     // what names nothing stays as written
    CHECK(f.got[0].from == "hmi@plant.example");
    CHECK(f.got[0].to == std::vector<std::string>{ "shift@plant.example" });
    CHECK(f.accounts[0].host == "smtp.plant.example");
    CHECK(f.accounts[0].password == "s3cret-pw");
    CHECK(mail().waiting() == 0);
    CHECK(mail().sent() == 1);
    CHECK(mail().status().find("sent ") == 0);
    CHECK(tags().find("sys.mail.error")->get_number() == 0);
    // it clears: not mailed (only "raised" by default); it comes back within the minute: not mailed again
    f.raise(f.hot, false);
    f.run(5000);
    f.raise(f.hot, true);
    f.run(5000);
    CHECK(f.got.size() == 1);
    // ... a minute later it is
    f.raise(f.hot, false);
    f.run(60000);
    f.raise(f.hot, true);
    f.run(500);
    CHECK(f.got.size() == 2);
}

TEST_CASE("mail: which alarms - by severity and by event; \"alarms\": false mails none")
{
    Fixture f;
    Tag* warn = f.rt.tag("warn", TagType::Bool, 0);
    f.with_alarms(R"~([ { "name": "hot", "tag": "hot", "message": "hot", "severity": "critical" }, { "name": "warn", "tag": "warn", "message": "warm", "severity": "warning" } ])~");
    f.configure(R"~({ "host": "h", "from": "a@b.c", "to": "x@y.z", "alarms": { "severities": ["critical"], "events": ["raised", "cleared", "acked"], "min_interval_s": 0, "subject": "{alarm.name} {alarm.event} {alarm.user}" } })~");
    f.raise(warn, true);
    f.raise(f.hot, true);
    f.raise(f.hot, false);
    CHECK(alarms().ack("hot", "op1"));
    f.run(2000);
    REQUIRE(f.got.size() == 3);
    CHECK(f.got[0].subject == "hot raised ");
    CHECK(f.got[1].subject == "hot cleared ");
    CHECK(f.got[2].subject == "hot acked op1");
    f.got.clear();
    f.configure(R"~({ "host": "h", "from": "a@b.c", "to": "x@y.z", "alarms": false })~");
    f.raise(f.hot, true);
    f.run(1000);
    CHECK(f.got.empty());
    // ... but an action can still send
    CHECK(mail().queue("by hand", "body"));
    f.run(500);
    CHECK(f.got.size() == 1);
}

// ================================================================ security

TEST_CASE("mail: a value that goes into the subject cannot add a header; the texts are bounded")
{
    Fixture f;
    Tag* name = f.rt.tag("batch", "B1");
    f.with_alarms(R"~([ { "name": "hot", "tag": "hot", "message": "hot\r\nBcc: thief@evil.example", "severity": "critical" } ])~");
    f.configure(R"~({ "host": "h", "from": "a@b.c", "to": "x@y.z", "alarms": { "subject": "{alarm.message} batch {batch}", "body": "batch {batch}" } })~");
    name->set_string("X\r\nTo: all@evil.example\r\n\r\nfake body", nullptr);
    f.raise(f.hot, true);
    f.run(500);
    REQUIRE(f.got.size() == 1);
    CHECK(f.got[0].subject.find('\r') == std::string::npos);
    CHECK(f.got[0].subject.find('\n') == std::string::npos);
    CHECK(f.got[0].subject == "hot  Bcc: thief@evil.example batch X  To: all@evil.example    fake body");
    const std::string data = smtp_build_message(f.got[0]);
    const std::string head = data.substr(0, data.find("\r\n\r\n"));
    CHECK(head.find("\r\nBcc:") == std::string::npos);
    CHECK(head.find("\r\nTo: all@evil") == std::string::npos);
    CHECK(f.got[0].to == std::vector<std::string>{ "x@y.z" });   // the recipients are what the settings say, whatever a value holds
    // a subject and a body of megabytes
    CHECK(mail().queue(std::string(100000, 's'), std::string(3 * 1024 * 1024, 'b')));
    f.run(500);
    REQUIRE(f.got.size() == 2);
    CHECK(f.got[1].subject.size() <= kMailSubjectMax);
    CHECK(f.got[1].body.size() <= kMailBodyMax);
    CHECK_FALSE(mail().queue("", ""));
    // mail_expand itself
    CHECK(mail_expand("{a} {b.c} {level} {sys.nothing} {} { x } {a", { { "a", "1" }, { "b.c", "{a}" } }) == "1 {a} 42.5 {sys.nothing} {} { x } {a");
    CHECK(mail_expand(std::string(50, '{') + "level" + std::string(50, '}'), {}) == std::string(49, '{') + "42.5" + std::string(49, '}'));
}

TEST_CASE("mail: the password is kept in project.local.json only - not in the queue file, not in the log of operations, not on the page")
{
    Fixture f;
    const json ocfg = json::parse(R"({ "path": "op.db" })");
    REQUIRE(operation_log().load(&ocfg, f.dir.generic_string()));
    f.configure(R"~({ "host": "smtp.plant.example", "username": "hmi", "from": "hmi@plant.example", "to": ["shift@plant.example"] })~");
    // set on the device: recipients, the user name, the password
    CHECK(mail().set_local(true, "night@plant.example, boss@plant.example", "hmi2", "t0p-secret") == "");
    CHECK(mail().config().to.size() == 2);
    CHECK(mail().config().account.password == "t0p-secret");
    json local = f.read_json(f.store());
    CHECK(local["mail"] == json{ { "to", { "night@plant.example", "boss@plant.example" } }, { "username", "hmi2" }, { "password", "t0p-secret" } });
    // an empty password field leaves the password
    CHECK(mail().set_local(true, "night@plant.example", "hmi2", "") == "");
    CHECK(mail().config().account.password == "t0p-secret");
    CHECK(f.read_json(f.store())["mail"]["password"] == "t0p-secret");
    // a mail that waits: the queue file knows the mail, not the account
    f.results.push_back(Fixture::fail("no route"));
    CHECK(mail().queue("waiting", "body"));
    f.run(250);
    REQUIRE(fs::exists(f.queue_file()));
    const std::string q = f.read_text(f.queue_file());
    CHECK(q.find("waiting") != std::string::npos);
    CHECK(q.find("t0p-secret") == std::string::npos);
    CHECK(q.find("hmi2") == std::string::npos);
    // who changed what is logged - without the password
    bool to_logged = false;
    for(const Operation& o : operation_log().query()) {
        CHECK(o.old_value.find("t0p-secret") == std::string::npos);
        CHECK(o.new_value.find("t0p-secret") == std::string::npos);
        if(o.kind == "mail" && o.target == "to") to_logged = true;
    }
    CHECK(to_logged);
    CHECK(mail().status().find("t0p-secret") == std::string::npos);
    // the page: the password field is empty, whatever the password is
    SystemSettings& sys = system_settings();
    sys.add_page(make_mail_page());
    sys.create_tags();
    CHECK(tags().find("sys.mail.password")->get().format() == "");
    CHECK(tags().find("sys.mail.username")->get().format() == "hmi2");
    for(Tag* t : tags().all()) CHECK(t->get().format().find("t0p-secret") == std::string::npos);
    // what is refused on the device
    CHECK(mail().set_local(true, "not an address", "u", "") != "");
    CHECK(mail().set_local(true, "", "u", "") == "No recipient.");
    CHECK(mail().set_local(true, "a@b.c\r\nRCPT TO:<x@evil.example>", "u", "") != "");
    CHECK(mail().set_local(true, "a@b.c", "u\r\nx", "") != "");
    CHECK(mail().config().to == std::vector<std::string>{ "night@plant.example" });
    // back to what the project says: nothing of the recipients is left; the password stays (the project has none)
    CHECK(mail().set_local(true, "shift@plant.example", "hmi", "") == "");
    CHECK(f.read_json(f.store())["mail"] == json{ { "password", "t0p-secret" } });
    // switched off: nothing is queued
    CHECK(mail().set_local(false, "shift@plant.example", "hmi", "") == "");
    std::string why;
    CHECK_FALSE(mail().queue("x", "y", &why));
    CHECK(why == "mail is switched off");
    sys.clear();
}

// ================================================================ reliability

TEST_CASE("mail: a server that cannot be reached - tried again, a little later each time, then given up; a refusal for good is dropped at once")
{
    Fixture f;
    f.configure(kCfg);
    for(int i = 0; i < 4; i++) f.results.push_back(Fixture::fail("cannot connect to smtp.plant.example:587"));
    CHECK(mail().queue("first", "1"));
    CHECK(mail().queue("second", "2"));
    f.run(250);
    CHECK(f.got.size() == 1);
    CHECK(mail().waiting() == 2);
    CHECK(mail().status().find("waiting (try 1 of 4 failed): cannot connect") == 0);
    CHECK(tags().find("sys.mail.error")->get_number() == 1);
    f.run(59000);
    CHECK(f.got.size() == 1);                                    // not before a minute is over
    f.run(1500);
    CHECK(f.got.size() == 2);
    f.run(119000);
    CHECK(f.got.size() == 2);                                    // two minutes the second time
    f.run(1500);
    CHECK(f.got.size() == 3);
    f.run(177000);
    CHECK(f.got.size() == 3);                                    // three minutes the third time
    while(f.got.size() < 4) f.run(250);
    CHECK(f.got.size() == 4);                                    // the fourth try fails too: given up
    CHECK(mail().dropped() == 1);
    CHECK(mail().waiting() == 1);
    CHECK(mail().status().find("given up after 4 tries") == 0);
    // the next mail is tried on its own account, at once - and goes through
    f.run(500);
    REQUIRE(f.got.size() == 5);
    CHECK(f.got[4].subject == "second");
    CHECK(mail().sent() == 1);
    CHECK(tags().find("sys.mail.error")->get_number() == 0);
    // refused for good (a wrong password): no second try, the mail behind it is not held up
    f.results.push_back(Fixture::fail("the user name or the password was not accepted: 535", true));
    CHECK(mail().queue("third", "3"));
    CHECK(mail().queue("fourth", "4"));
    f.run(1000);
    CHECK(f.got.size() == 7);
    CHECK(mail().dropped() == 2);
    CHECK(mail().sent() == 2);
    CHECK(mail().waiting() == 0);
    f.run(600000);
    CHECK(f.got.size() == 7);
    // a recipient the server did not take: sent, and said
    SmtpResult part;
    part.ok = true;
    part.refused.push_back("boss@plant.example (550 no such user)");
    f.results.push_back(part);
    CHECK(mail().queue("fifth", "5"));
    f.run(500);
    CHECK(mail().status().find("1 recipient(s) refused") != std::string::npos);
}

TEST_CASE("mail: what waits survives a restart; a full queue gives up its oldest mail and says so")
{
    Fixture f;
    f.configure(kCfg);                                           // "queue": 5
    for(int i = 0; i < 50; i++) f.results.push_back(Fixture::fail("no route"));
    for(int i = 1; i <= 8; i++) CHECK(mail().queue("mail " + std::to_string(i), "body " + std::to_string(i)));
    CHECK(mail().waiting() == 5);
    CHECK(mail().dropped() == 3);
    json q = f.read_json(f.queue_file());
    REQUIRE(q["mails"].size() == 5);
    CHECK(q["mails"][0]["subject"] == "mail 4");                 // 1 to 3 gave way
    CHECK(q["mails"][4]["subject"] == "mail 8");
    f.run(250);                                                  // one try (it fails)
    CHECK(f.read_json(f.queue_file())["mails"][0]["attempts"] == 1);

    // the power goes. The next start finds the queue and sends it.
    f.results.clear();
    f.got.clear();
    f.configure(kCfg);
    CHECK(mail().waiting() == 5);
    f.run(60000 * 5);
    REQUIRE(f.got.size() == 5);
    CHECK(f.got[0].subject == "mail 4");
    CHECK(f.got[4].body == "body 8");
    CHECK(mail().waiting() == 0);
    CHECK_FALSE(fs::exists(f.queue_file()));                     // nothing waits: no file
    // a queue file that is nonsense, or huge, or somebody else's
    fs::create_directories(f.dir / "logs");
    for(const char* junk : { "", "not json", "[]", "{ \"mails\": 5 }", "{ \"mails\": [5, null, {}, { \"subject\": 7 }, { \"subject\": \"ok\\r\\nBcc: x@evil.example\", \"body\": \"b\", \"attempts\": -5, \"time\": \"x\" }] }" }) {
        CAPTURE(junk);
        { std::ofstream o(f.queue_file(), std::ios::binary); o << junk; }
        f.got.clear();
        f.configure(kCfg);
        f.run(1000);
        for(const SmtpMessage& m : f.got) CHECK(m.subject.find('\n') == std::string::npos);
    }
    CHECK(f.got.size() == 1);                                    // the one entry that is a mail
    std::string many = "{ \"mails\": [";
    for(int i = 0; i < 1000; i++) many += std::string(i ? "," : "") + "{ \"subject\": \"m" + std::to_string(i) + "\", \"body\": \"b\" }";
    many += "] }";
    { std::ofstream o(f.queue_file(), std::ios::binary); o << many; }
    f.configure(kCfg);
    CHECK(mail().waiting() == 5);
}

TEST_CASE("mail: without \"mail\", with settings that are wrong, and while a project is only checked - nothing is sent, nothing is written")
{
    Fixture f;
    f.with_alarms();
    mail().configure(nullptr, nullptr, f.dir.generic_string(), f.store(), "x");
    alarms().set_listener([](const AlarmEvent& e) { mail().on_alarm(e); });
    CHECK_FALSE(mail().configured());
    CHECK(tags().find("sys.mail.waiting") == nullptr);
    std::string why;
    CHECK_FALSE(mail().queue("a", "b", &why));
    CHECK(why.find("no \"mail\"") != std::string::npos);
    f.raise(f.hot, true);
    f.run(1000);
    f.raise(f.hot, false);
    CHECK(f.got.empty());
    // settings that are wrong: said once, the device runs
    f.configure(R"~({ "host": "smtp.plant.example", "from": "hmi@plant.example" })~");
    CHECK(mail().configured());
    CHECK(mail().status().find("not set up: no recipient") == 0);
    CHECK_FALSE(mail().queue("a", "b", &why));
    CHECK(why.find("not set up") != std::string::npos);
    f.raise(f.hot, true);
    f.run(1000);
    f.raise(f.hot, false);
    CHECK(f.got.empty());
    // --check: alarms of the check mail nobody, the folder stays as it is
    f.configure(kCfg);
    mail().set_read_only(true);
    f.raise(f.hot, true);
    CHECK_FALSE(mail().queue("a", "b", &why));
    f.run(1000);
    CHECK(f.got.empty());
    CHECK_FALSE(fs::exists(f.queue_file()));
    CHECK_FALSE(fs::exists(f.store()));
}

// ================================================================ performance efficiency

TEST_CASE("mail: a slow server does not hold up the screen; a project that is unloaded meanwhile is safe")
{
    Fixture f;
    f.configure(kCfg);
    std::atomic<int> calls{ 0 };
    std::atomic<bool> release{ false };
    mail().set_clock(nullptr);
    mail().set_sender([&](const SmtpAccount&, const SmtpTls&, const SmtpMessage&) {
        calls++;
        for(int i = 0; i < 1000 && !release; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));     // a server that takes its time
        SmtpResult ok;
        ok.ok = true;
        return ok;
    });
    mail().start();
    CHECK(mail().queue("slow", "body"));
    const uint32_t t0 = millis();
    for(int i = 0; i < 200; i++) mail().poll();                  // the main loop goes on
    CHECK(millis() - t0 < 500);
    for(int i = 0; i < 200 && calls == 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(calls == 1);
    CHECK(mail().waiting() == 1);                                // still on its way
    CHECK(mail().queue("behind", "body"));
    mail().poll();
    CHECK(calls == 1);                                           // one at a time
    release = true;
    for(int i = 0; i < 400 && mail().waiting() > 0; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); mail().poll(); }
    CHECK(mail().waiting() == 0);
    CHECK(mail().sent() == 2);
    // the project is unloaded while a mail is on its way
    release = false;
    CHECK(mail().queue("in flight", "body"));
    mail().poll();
    for(int i = 0; i < 200 && calls < 3; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    mail().clear();
    release = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(fs::exists(f.queue_file()));                           // not known to have arrived: kept for the next start
    // (back to sending without a thread for the fixture's end; the thread above has ended)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ================================================================ usability: the page and the actions

TEST_CASE("System > Mail: recipients and on / off are changed and saved; a test mail says what came of it")
{
    Fixture f;
    f.rt.tag("count", TagType::Int, 7);
    f.configure(kCfg);
    SystemSettings& sys = system_settings();
    sys.add_page(make_mail_page());
    sys.create_tags();
    auto text = [](const char* key) { return tags().find(std::string("sys.mail.") + key)->get().format(); };
    CHECK(text("to") == "shift@plant.example, boss@plant.example");
    CHECK(text("username") == "hmi");
    CHECK(text("password") == "");
    CHECK(text("server") == "smtp.plant.example:587 (starttls)");
    CHECK(text("state") == "nothing sent yet");
    // a test mail that fails: the reason is on the page
    f.results.push_back(Fixture::fail("TLS handshake with smtp.plant.example:587 (TLS): the certificate is not trusted", true));
    sys.command("mail", "test");
    CHECK(text("status").find("on its way") != std::string::npos);
    f.run(500);
    sys.poll();
    CHECK(text("state").find("not sent: TLS handshake") == 0);
    CHECK(text("waiting") == "0");
    // one that works
    sys.command("mail", "test");
    f.run(500);
    sys.poll();
    CHECK(text("state").find("sent ") == 0);
    REQUIRE(f.got.size() == 2);
    CHECK(f.got[1].subject == "Test mail");
    // the recipients are changed on the page (the operator typed into the field)
    tags().find("sys.mail.to")->set_string("night@plant.example", nullptr);
    tags().find("sys.mail.password")->set_string("new-pw", nullptr);
    CHECK(sys.apply("mail") == "Saved.");
    CHECK(mail().config().to == std::vector<std::string>{ "night@plant.example" });
    CHECK(mail().config().account.password == "new-pw");
    CHECK(text("password") == "");                               // gone from the field again
    tags().find("sys.mail.to")->set_string("nobody", nullptr);
    CHECK(sys.apply("mail").find("not a list of mail addresses") != std::string::npos);
    CHECK(mail().config().to == std::vector<std::string>{ "night@plant.example" });
    // the actions
    register_mail_actions();
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "mail.send", "arg": "Count is {count} on {project}\nThe count reached {count}.\nSecond line." })"));
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "mail.test" })"));
    f.run(1000);
    REQUIRE(f.got.size() == 4);
    CHECK(f.got[2].subject == "Count is 7 on Line 1");
    CHECK(f.got[2].body == "The count reached 7.\nSecond line.");
    CHECK(f.got[2].to == std::vector<std::string>{ "night@plant.example" });
    CHECK(f.got[3].subject == "Test mail");
    sys.command("mail", "explode");
    for(const json& scr : sys.screens(480, 320)) REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen(SystemSettings::page_screen("mail"), "none"));
    f.rt.advance(300);
    ui().unload_all();
    sys.clear();
}

// ================================================================ maintainability / security: the checker

TEST_CASE("mail: --check finds settings that cannot work, and what should not be in a project file")
{
    Fixture f;
    fs::create_directories(f.dir / "screens");
    { std::ofstream o(f.dir / "screens" / "s.json", std::ios::binary); o << "{ \"name\": \"s\", \"children\": [] }"; }
    auto problems = [&](const std::string& mail_cfg, bool with_alarms = true) {
        { std::ofstream o(f.dir / "project.json", std::ios::binary);
          o << "{ \"start_screen\": \"s\", \"tags\": [ { \"name\": \"hot\", \"type\": \"bool\" } ], "
            << (with_alarms ? "\"alarms\": { \"items\": [ { \"name\": \"h\", \"tag\": \"hot\" } ] }, " : "") << "\"mail\": " << mail_cfg << ", \"screens\": [\"screens/s.json\"] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((f.dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems(R"({ "host": "smtp.example.com", "from": "hmi@example.com", "to": ["a@example.com"], "username": "hmi", "alarms": { "severities": ["critical"], "subject": "{alarm.message}" } })") == "");
    CHECK(problems(R"({ "host": "h", "port": 465, "security": "tls", "from": "a@b.c", "to": "x@y.z", "retry_s": 30, "retries": 2, "queue": 10, "timeout_ms": 5000, "enabled": false })") == "");
    const std::string out = problems(R"({ "host": "h", "from": "nobody", "to": ["x@y.z", "bad"], "password": "hunter2", "security": "none", "verify": false, "ca_file": "no_such.pem", "speed": 1,
                                         "alarms": { "events": ["raised"], "colour": "red" } })");
    CHECK(out.find("mail: \"from\" is the mail address") != std::string::npos);
    CHECK(out.find("mail: \"to\" is a mail address") != std::string::npos);
    CHECK(out.find("mail: the password is in the project file") != std::string::npos);
    CHECK(out.find("hunter2") == std::string::npos);                                    // (the report does not repeat it)
    CHECK(out.find("mail: \"security\": \"none\"") != std::string::npos);
    CHECK(out.find("mail: \"verify\": false") != std::string::npos);
    CHECK(out.find("mail: ca_file: no file \"no_such.pem\"") != std::string::npos);
    CHECK(out.find("mail: unknown key \"speed\"") != std::string::npos);
    CHECK(out.find("mail: alarms: unknown key \"colour\"") != std::string::npos);
    CHECK(problems("5").find("mail: \"mail\" is an object") != std::string::npos);
    CHECK(problems(R"({ "host": "h", "from": "a@b.c", "to": "x@y.z", "alarms": { "events": ["raised"] } })", false).find("the project has no \"alarms\"") != std::string::npos);
    // the check itself sends nothing and writes nothing
    CHECK(f.got.empty());
    CHECK_FALSE(fs::exists(f.queue_file()));
}
