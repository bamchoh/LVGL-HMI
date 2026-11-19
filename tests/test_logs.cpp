// Data log, alarms and operation log (logdb.hpp, data_log.hpp, alarm.hpp, operation_log.hpp).
#include <doctest.h>
#include "alarm.hpp"
#include "data_log.hpp"
#include "lvgl_fixture.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "script.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_logs_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        project().stop_logs();              // close the databases before deleting them
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string base() const { return dir.generic_string(); }
    std::string file(const std::string& rel) const { return (dir / rel).generic_string(); }
};

json J(const char* text) { return json::parse(text); }

std::string read_all(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

lv_obj_t* build_screen(const char* name, const json& widgets)
{
    json scr = { { "name", name }, { "children", widgets } };
    lv_obj_t* s = ui().build_screen(scr, "/base");
    REQUIRE(s != nullptr);
    return s;
}

void click(lv_obj_t* obj) { lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr); }

} // namespace

// ================================================================ LogDb

TEST_CASE("logdb: rows, filters, limits and a second reader")
{
    TempDir t;
    LogDb db("test log", "item", { { "name", "TEXT NOT NULL" }, { "n", "INTEGER" }, { "x", "REAL" } }, "name, ts");
    LogDbConfig cfg;
    cfg.enabled = true;
    cfg.path = t.file("sub/test.db");

    LogDb reader("test log", "item", { { "name", "TEXT NOT NULL" }, { "n", "INTEGER" }, { "x", "REAL" } });
    CHECK_FALSE(reader.open(cfg, true));            // a reader never creates the file
    CHECK_FALSE(fs::exists(t.dir / "sub"));

    REQUIRE(db.open(cfg));
    db.add({ "a", 1, 1.5 });
    db.add(1700000000000LL, { "b", 2, 2.5 });
    db.add({ "a", 3, LogCell() });
    auto rows = db.query();
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].size() == 5);                   // id, ts, name, n, x
    CHECK(rows[0][2].s == "a");
    CHECK(rows[0][3].i == 1);
    CHECK(rows[0][4].d == doctest::Approx(1.5));
    CHECK(rows[0][1].i > 1700000000000LL);
    CHECK(rows[1][1].i == 1700000000000LL);
    CHECK(rows[2][4].type == LogCell::Null);
    CHECK(db.query("name = ?1", { "a" }).size() == 2);
    CHECK(db.query("n >= ?1 AND name = ?2", { 2, "a" }).size() == 1);
    rows = db.query("", {}, 2, true);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0][3].i == 3);

    REQUIRE(reader.open(cfg, true));
    CHECK(reader.query().size() == 3);
    reader.add({ "ignored", 0, 0.0 });
    reader.close();
    CHECK(db.query().size() == 3);

    // retention_days and max_rows are applied when the database is opened (and then once an hour)
    db.close();
    REQUIRE(db.open(cfg));
    rows = db.query();
    REQUIRE(rows.size() == 2);                      // "b" (dated 2023) is older than 30 days
    CHECK(rows[0][3].i == 1);
    db.close();
    cfg.max_rows = 1;
    REQUIRE(db.open(cfg));
    rows = db.query();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0][3].i == 3);

    cfg.enabled = false;
    CHECK_FALSE(db.open(cfg));
    db.add({ "dropped", 0, 0.0 });
    CHECK(db.query().empty());
}

TEST_CASE("logdb: max_size_mb removes the oldest rows when the data outgrows it")
{
    TempDir tmp;
    LogDbConfig cfg;
    cfg.enabled = true;
    cfg.path = (tmp.dir / "size.db").generic_string();
    cfg.retention_days = 0;
    cfg.max_rows = 0;
    cfg.max_size_mb = 0;                                // no limit while filling
    LogDb db("test log", "t", { { "name", "TEXT NOT NULL" }, { "n", "INTEGER NOT NULL" } }, "");
    REQUIRE(db.open(cfg));
    const std::string kilobyte(1000, 'x');
    for(int i = 1; i <= 4000; i++) db.add({ kilobyte, i });   // about 4 MB
    db.flush();
    CHECK(db.query().size() == 4000);
    db.close();
    CHECK(std::filesystem::file_size(cfg.path) > 3500000);

    cfg.max_size_mb = 1;                                // applied when the database is opened (and then every minute)
    REQUIRE(db.open(cfg));
    std::vector<LogRow> rows = db.query();
    CHECK(rows.size() < 1100);                          // fits into 1 MB now
    CHECK(rows.size() > 500);                           // ... without throwing away more than needed (a tenth at a time)
    REQUIRE(!rows.empty());
    CHECK(rows.back()[3].i == 4000);                    // the newest rows are the ones that stay
    CHECK(rows.front()[3].i == 4000 - static_cast<int64_t>(rows.size()) + 1);
    // the freed space is used again: writing as much as was removed does not grow the file
    const auto size_before = std::filesystem::file_size(cfg.path);
    for(int i = 4001; i <= 4400; i++) db.add({ kilobyte, i });
    db.flush();
    db.close();
    CHECK(std::filesystem::file_size(cfg.path) <= size_before);
}

TEST_CASE("logdb: parse_log_config and write_csv")
{
    CHECK_FALSE(parse_log_config(nullptr, "/p", "logs/x.db", 500).enabled);
    json j = J(R"~({})~");
    LogDbConfig c = parse_log_config(&j, "/p", "logs/x.db", 500);
    CHECK(c.enabled);
    CHECK(c.path == "/p/logs/x.db");
    CHECK(c.retention_days == 30);
    CHECK(c.max_rows == 500);
    CHECK(c.max_size_mb == 1000);
    j = J(R"~({ "enabled": false, "path": "/abs/y.db", "retention_days": 7, "max_rows": -1 })~");
    c = parse_log_config(&j, "/p", "logs/x.db", 500);
    CHECK_FALSE(c.enabled);
    CHECK(c.path == "/abs/y.db");
    CHECK(c.retention_days == 7);
    CHECK(c.max_rows == 0);

    TempDir t;
    std::string csv = t.file("out/a.csv");
    CHECK(LogDb::write_csv(csv, { "time", "v" }, { { "t1", "a,b" }, { "t2", "say \"hi\"" } }) == 2);
    CHECK(read_all(csv) == "\xEF\xBB\xBFtime,v\r\nt1,\"a,b\"\r\nt2,\"say \"\"hi\"\"\"\r\n");
    CHECK(LogDb::format_time(1700000000123LL).size() == 23);
}

// ================================================================ data log

TEST_CASE("data log: interval, change, tag and manual groups")
{
    Runtime rt;
    TempDir t;
    Tag* temp = rt.tag("temp", TagType::Float, 21.5);
    Tag* run = rt.tag("run", TagType::Bool, 0);
    Tag* mode = rt.tag("mode", TagType::Int, 2);
    rt.tag("state", std::string("Idle"));
    Tag* done = rt.tag("done", TagType::Bool, 0);

    json cfg = J(R"~({
      "groups": [
        { "name": "process", "trigger": "interval", "interval_ms": 100, "tags": ["temp", "run", "mode", "state"] },
        { "name": "states",  "trigger": "change", "tags": ["run", "mode"] },
        { "name": "batch",   "trigger": "tag", "trigger_tag": "done", "tags": ["temp"] },
        { "name": "report",  "trigger": "manual", "tags": ["mode", "nope"] },
        { "name": "off",     "enabled": false, "tags": ["temp"] },
        { "name": "empty",   "tags": ["nope"] },
        { "name": "process", "tags": ["temp"] },
        { "name": "weird",   "trigger": "sometimes", "tags": ["temp"] },
        { "tags": ["temp"] }
      ] })~");
    REQUIRE(data_log().load(&cfg, t.base()) == 4);
    CHECK(data_log().enabled());
    CHECK(data_log().groups() == std::vector<std::string>{ "process", "states", "batch", "report" });
    CHECK(fs::exists(t.dir / "logs" / "data.db"));

    // interval: one sample per period with every tag of the group, typed
    rt.advance(350);
    auto s = data_log().query("process");
    REQUIRE(s.size() == 3);
    CHECK(s[0].values == J(R"~({ "temp": 21.5, "run": false, "mode": 2, "state": "Idle" })~"));
    CHECK(s[0].values["mode"].is_number_integer());
    CHECK(s[0].ts_ms > 1700000000000LL);

    // change: nothing until a tag of the group changes; several changes at once are one sample
    CHECK(data_log().query("states").empty());
    temp->set_number(30, nullptr);          // not in the group
    rt.advance(100);
    CHECK(data_log().query("states").empty());
    run->set_number(1, nullptr);
    mode->set_number(5, nullptr);
    rt.advance(100);
    s = data_log().query("states");
    REQUIRE(s.size() == 1);
    CHECK(s[0].values == J(R"~({ "run": true, "mode": 5 })~"));
    mode->set_number(6, nullptr);
    rt.advance(100);
    CHECK(data_log().query("states").size() == 2);

    // tag: when trigger_tag goes from off to on
    CHECK(data_log().query("batch").empty());
    done->set_number(1, nullptr);
    rt.advance(100);
    done->set_number(0, nullptr);
    rt.advance(100);
    done->set_number(1, nullptr);
    rt.advance(100);
    s = data_log().query("batch");
    REQUIRE(s.size() == 2);
    CHECK(s[0].values == J(R"~({ "temp": 30.0 })~"));

    // manual
    CHECK(data_log().query("report").empty());
    CHECK(data_log().sample("report"));
    CHECK_FALSE(data_log().sample("nope"));
    s = data_log().query("report", 1, true);
    REQUIRE(s.size() == 1);
    CHECK(s[0].values == J(R"~({ "mode": 6 })~"));

    // CSV: time + one column per tag, booleans as 1 / 0
    std::string csv = t.file("states.csv");
    CHECK(data_log().export_csv("states", csv) == 2);
    std::string text = read_all(csv);
    CHECK(text.find("time,run,mode\r\n") == 3);
    CHECK(text.find(",1,5\r\n") != std::string::npos);
    CHECK(text.find(",1,6\r\n") != std::string::npos);
    CHECK(data_log().export_csv("unknown", t.file("x.csv")) == -1);
    CHECK(data_log().export_csv("report") == 1);            // default file name next to the database
    bool found = false;
    for(const auto& e : fs::directory_iterator(t.dir / "logs")) found = found || e.path().filename().string().rfind("data-report-", 0) == 0;
    CHECK(found);

    // a reader (second process) sees the groups and the rows without tags or timers
    DataLog reader;
    REQUIRE(reader.load(&cfg, t.base(), true) == 5);        // every valid group of the file (tags are not checked)
    CHECK(reader.query("states").size() == 2);
    CHECK(reader.export_csv("process", t.file("p.csv")) >= 3);
    CHECK(read_all(t.file("p.csv")).find("time,temp,run,mode,state\r\n") == 3);
    reader.clear();

    // after clear() nothing is recorded any more
    data_log().clear();
    CHECK_FALSE(data_log().enabled());
    run->set_number(0, nullptr);
    rt.advance(300);
    REQUIRE(data_log().load(&cfg, t.base()) == 4);
    CHECK(data_log().query("states").size() == 2);
}

TEST_CASE("data log: off unless configured")
{
    Runtime rt;
    TempDir t;
    rt.tag("temp", TagType::Float, 1);
    CHECK(data_log().load(nullptr, t.base()) == 0);
    json off = J(R"~({ "enabled": false, "groups": [ { "name": "g", "tags": ["temp"] } ] })~");
    CHECK(data_log().load(&off, t.base()) == 0);
    CHECK_FALSE(data_log().enabled());
    rt.advance(1200);
    CHECK_FALSE(fs::exists(t.dir / "logs"));
    CHECK_FALSE(data_log().sample("g"));
}

// ================================================================ alarms

TEST_CASE("alarms: raise and clear are recorded")
{
    Runtime rt;
    TempDir t;
    Tag* temp = rt.tag("temp", TagType::Float, 20);
    Tag* hi = rt.tag("temp_hi", TagType::Float, 30);
    Tag* estop = rt.tag("estop", TagType::Bool, 1);         // active from the start
    Tag* state = rt.tag("state", std::string("Idle"));
    rt.tag("door", TagType::Bool, 1);

    json cfg = J(R"~({
      "items": [
        { "name": "temp_high", "tag": "temp", "condition": "gt", "value_tag": "temp_hi", "message": "Temperature high", "severity": "warning" },
        { "name": "estop",     "tag": "estop", "message": "Emergency stop", "severity": "critical" },
        { "name": "fault",     "tag": "state", "condition": "==", "value": "Fault" },
        { "name": "door_open", "tag": "door", "condition": "false" },
        { "name": "temp_low",  "tag": "temp", "condition": "<=", "value": 5 },
        { "name": "off",       "tag": "temp", "enabled": false },
        { "name": "bad_tag",   "tag": "nope" },
        { "name": "bad_cond",  "tag": "temp", "condition": "almost" },
        { "name": "estop",     "tag": "temp" },
        { "tag": "temp" }
      ] })~");
    REQUIRE(alarms().load(&cfg, t.base()) == 5);
    CHECK(alarms().enabled());
    CHECK(alarms().count() == 5);
    CHECK(alarms().active_names() == std::vector<std::string>{ "estop" });

    auto ev = alarms().query();
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].name == "estop");
    CHECK(ev[0].event == "raised");
    CHECK(ev[0].severity == "critical");
    CHECK(ev[0].message == "Emergency stop");
    CHECK(ev[0].ts_ms > 1700000000000LL);

    temp->set_number(31, nullptr);
    rt.pump();
    CHECK(alarms().active("temp_high"));
    temp->set_number(35, nullptr);          // still active: no new row
    rt.pump();
    hi->set_number(40, nullptr);            // the limit moved above the value: cleared
    rt.pump();
    CHECK_FALSE(alarms().active("temp_high"));
    estop->set_number(0, nullptr);
    state->set_string("Fault", nullptr);
    temp->set_number(5, nullptr);
    rt.pump();

    ev = alarms().query();
    REQUIRE(ev.size() == 6);
    CHECK(ev[1].name == "temp_high");
    CHECK(ev[1].event == "raised");
    CHECK(ev[1].value == "31");
    CHECK(ev[2].name == "temp_high");
    CHECK(ev[2].event == "cleared");
    CHECK(ev[2].value == "35");
    CHECK(ev[3].name == "temp_low");        // (tags are delivered in creation order)
    CHECK(ev[4].name == "estop");
    CHECK(ev[4].event == "cleared");
    CHECK(ev[5].name == "fault");
    CHECK(ev[5].message == "fault");        // default message = the name
    CHECK(ev[5].severity == "warning");
    CHECK(ev[5].value == "Fault");
    CHECK(alarms().active_names() == std::vector<std::string>{ "fault", "temp_low" });
    CHECK_FALSE(alarms().active("door_open"));
    CHECK_FALSE(alarms().active("unknown"));

    std::string csv = t.file("alarms.csv");
    CHECK(alarms().export_csv(csv) == 6);
    std::string text = read_all(csv);
    CHECK(text.find("time,name,event,severity,message,value,user\r\n") == 3);
    CHECK(text.find(",temp_high,raised,warning,Temperature high,31,\r\n") != std::string::npos);

    alarms().clear();
    CHECK(alarms().count() == 0);
    temp->set_number(99, nullptr);
    rt.pump();                              // must not touch the removed alarms
    CHECK(alarms().load(nullptr, t.base()) == 0);
    CHECK_FALSE(alarms().enabled());
}

// ================================================================ operation log

TEST_CASE("operation log: actions and widget writes")
{
    Runtime rt;
    TempDir t;
    Tag* sp = rt.tag("sp", TagType::Int, 40, 0, 100);
    Tag* on = rt.tag("on", TagType::Bool, 0);
    Tag* pw = rt.tag("pw", std::string(""));
    Tag* user = rt.tag("operator", std::string("alice"));
    ui().register_action("demo.fn", [](const std::string&) {});

    json cfg = J(R"~({ "user_tag": "operator" })~");
    REQUIRE(operation_log().load(&cfg, t.base()));
    CHECK(operation_log().enabled());

    build_screen("other", J(R"~([ { "type": "label", "text": "other" } ])~"));
    lv_obj_t* s = build_screen("main", J(R"~([
        { "type": "button", "on_click": { "action": "inc", "tag": "sp", "by": 10 } },
        { "type": "button", "on_click": [ { "action": "set", "tag": "on", "value": true }, { "action": "call", "fn": "demo.fn", "arg": "x" },
                                           { "action": "script", "code": "tag.sp = 1" } ] },
        { "type": "switch", "bind": "on" },
        { "type": "textarea", "password": true, "bind": "pw" },
        { "type": "button", "on_click": { "action": "set", "tag": "sp", "value": 1 } },
        { "type": "button", "on_click": { "action": "goto", "screen": "other", "anim": "none" } }
    ])~"));
    REQUIRE(ui().show_screen("main", "none"));

    // three quick presses of "+10" are one operation: 40 -> 70
    click(lv_obj_get_child(s, 0));
    click(lv_obj_get_child(s, 0));
    click(lv_obj_get_child(s, 0));
    CHECK(sp->get_number() == 70);
    click(lv_obj_get_child(s, 1));
    CHECK(sp->get_number() == 1);           // (written by the script: not an operator write itself)

    user->set_string("bob", nullptr);
    lv_obj_t* sw = lv_obj_get_child(s, 2);
    lv_obj_remove_state(sw, LV_STATE_CHECKED);
    lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(on->get_number() == 0);

    lv_obj_t* ta = lv_obj_get_child(s, 3);
    lv_textarea_set_text(ta, "secret");
    lv_obj_send_event(ta, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(pw->get().str == "secret");

    click(lv_obj_get_child(s, 4));          // set sp = 1 while it is 1: no change, no row
    click(lv_obj_get_child(s, 5));
    CHECK(ui().current_screen().value_or("") == "other");

    auto ops = operation_log().query();
    REQUIRE(ops.size() == 7);
    CHECK(ops[0].kind == "write");
    CHECK(ops[0].target == "sp");
    CHECK(ops[0].old_value == "40");
    CHECK(ops[0].new_value == "70");
    CHECK(ops[0].user == "alice");
    CHECK(ops[0].screen == "main");
    CHECK(ops[0].ts_ms > 1700000000000LL);
    CHECK(ops[1].kind == "write");
    CHECK(ops[1].target == "on");
    CHECK(ops[2].kind == "call");
    CHECK(ops[2].target == "demo.fn");
    CHECK(ops[2].new_value == "x");
    CHECK(ops[3].kind == "script");
    CHECK(ops[3].target == "(inline)");
    CHECK(ops[4].kind == "write");
    CHECK(ops[4].target == "on");
    CHECK(ops[4].user == "bob");
    CHECK(ops[5].target == "pw");
    CHECK(ops[5].old_value == "***");
    CHECK(ops[5].new_value == "***");       // what was typed into a password field is never stored
    CHECK(ops[6].kind == "screen");
    CHECK(ops[6].target == "other");
    CHECK(ops[6].screen == "main");

    std::string csv = t.file("ops.csv");
    CHECK(operation_log().export_csv(csv) == 7);
    std::string text = read_all(csv);
    CHECK(text.find("time,user,screen,kind,target,old_value,new_value\r\n") == 3);
    CHECK(text.find(",alice,main,write,sp,40,70\r\n") != std::string::npos);
    CHECK(text.find("secret") == std::string::npos);

    // a write that is not followed by anything is stored after a moment (timer)
    sp->set_number(40, nullptr);
    click(lv_obj_get_child(s, 0));
    operation_log().clear();
    CHECK_FALSE(operation_log().enabled());
    click(lv_obj_get_child(s, 0));          // not recorded, must not crash
    REQUIRE(operation_log().load(&cfg, t.base()));
    CHECK(operation_log().query().size() == 8);
}

TEST_CASE("operation log: off unless configured")
{
    Runtime rt;
    TempDir t;
    CHECK_FALSE(operation_log().load(nullptr, t.base()));
    json off = J(R"~({ "enabled": false })~");
    CHECK_FALSE(operation_log().load(&off, t.base()));
    operation_log().record("write", "x", "1", "2");
    CHECK(operation_log().query().empty());
    CHECK_FALSE(fs::exists(t.dir / "logs"));
}

// ================================================================ project integration

TEST_CASE("logs: started from project.json, exported through call actions")
{
    Runtime rt;
    TempDir t;
    std::ofstream(t.file("project.json"), std::ios::binary) << R"~({
      "tags": [ { "name": "temp", "type": "float", "init": 20 }, { "name": "run", "type": "bool", "init": false } ],
      "data_log": { "groups": [ { "name": "g", "trigger": "manual", "tags": ["temp", "run"] } ] },
      "alarms": { "path": "db/alarm.db", "items": [ { "name": "hot", "tag": "temp", "condition": "gt", "value": 25 } ] },
      "operation_log": {}
    })~";
    REQUIRE(project().load(t.file("project.json")));
    project().start_logs();
    CHECK(data_log().groups() == std::vector<std::string>{ "g" });
    CHECK(alarms().count() == 1);
    CHECK(operation_log().enabled());
    CHECK(fs::exists(t.dir / "db" / "alarm.db"));

    tags().find("temp")->set_number(26, nullptr);
    rt.pump();
    CHECK(ui().call_action("data_log.sample", "g"));
    CHECK(scripts().exec("call('data_log.sample', 'g')", "t"));
    operation_log().record("screen", "x");
    CHECK(ui().call_action("data_log.export", "g," + t.file("d.csv")));
    CHECK(ui().call_action("alarm_log.export", t.file("a.csv")));
    CHECK(ui().call_action("operation_log.export", t.file("o.csv")));
    CHECK(read_all(t.file("d.csv")).find("time,temp,run\r\n") == 3);
    CHECK(read_all(t.file("d.csv")).find(",26,0\r\n") != std::string::npos);
    CHECK(read_all(t.file("a.csv")).find(",hot,raised,warning,hot,26,\r\n") != std::string::npos);
    CHECK(read_all(t.file("o.csv")).find(",screen,x,,\r\n") != std::string::npos);

    project().stop_logs();
    CHECK_FALSE(data_log().enabled());

    // --export-* in a second process: read only
    project().start_logs(true);
    CHECK(data_log().query("g").size() == 2);
    CHECK(alarms().query().size() == 1);
    CHECK(operation_log().query().size() == 1);
    CHECK_FALSE(operation_log().enabled());
    project().stop_logs();
    project().unload();
}
