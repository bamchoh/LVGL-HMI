// Alarm statistics (alarm_stats.hpp) and the "alarm_summary" widget. Grouped by ISO/IEC 25010
// characteristic (issue #68).
#include <doctest.h>
#include "alarm.hpp"
#include "alarm_stats.hpp"
#include "alarm_summary_view.hpp"
#include "check.hpp"
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

const int64_t S = 1000, MIN = 60 * S, H = 60 * MIN, DAY = 24 * H;
const int64_t T0 = 1'800'000'000'000LL;         // some day in 2027

AlarmEvent ev(int64_t ts, const char* name, const char* event, const char* severity = "warning", const char* message = nullptr)
{
    AlarmEvent e;
    e.ts_ms = ts; e.name = name; e.event = event; e.severity = severity; e.message = message ? message : std::string(name) + " message";
    return e;
}
const AlarmStat* find(const std::vector<AlarmStat>& v, const char* name)
{
    for(const AlarmStat& s : v) if(s.name == name) return &s;
    return nullptr;
}

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_alarm_stats_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        alarm_summary_wait_idle();
        alarms().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
std::string read_all(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("alarm stats: how often and how long, per alarm, in a span; most often first or longest first")
{
    const std::vector<AlarmEvent> log = {
        ev(T0 + 0 * MIN, "hot", "raised", "critical"), ev(T0 + 5 * MIN, "hot", "cleared", "critical"),
        ev(T0 + 6 * MIN, "door", "raised"), ev(T0 + 6 * MIN + 10 * S, "door", "cleared"),
        ev(T0 + 7 * MIN, "door", "raised"), ev(T0 + 7 * MIN + 20 * S, "door", "acked"), ev(T0 + 7 * MIN + 30 * S, "door", "cleared"),
        ev(T0 + 8 * MIN, "door", "raised"), ev(T0 + 8 * MIN + 5 * S, "door", "cleared"),
        ev(T0 + 10 * MIN, "hot", "raised", "critical"), ev(T0 + 40 * MIN, "hot", "cleared", "critical"),
        ev(T0 + 50 * MIN, "note", "raised", "info"),
    };
    const int64_t now = T0 + H;
    std::vector<AlarmStat> st = alarm_stats(log, 0, 0, now, { "note" });
    REQUIRE(st.size() == 3);
    CHECK(st[0].name == "door");                                  // three raises: first
    CHECK(st[0].count == 3);
    CHECK(st[0].active_ms == 45 * S);
    CHECK(st[0].longest_ms == 30 * S);
    CHECK(st[0].last_raised_ms == T0 + 8 * MIN);
    CHECK(st[0].message == "door message");
    CHECK(st[0].severity == "warning");
    CHECK_FALSE(st[0].active_now);
    CHECK(st[1].name == "hot");
    CHECK(st[1].count == 2);
    CHECK(st[1].active_ms == 35 * MIN);
    CHECK(st[1].longest_ms == 30 * MIN);
    CHECK(st[2].name == "note");                                  // active now: counted up to now
    CHECK(st[2].count == 1);
    CHECK(st[2].active_ms == 10 * MIN);
    CHECK(st[2].active_now);

    st = alarm_stats(log, 0, 0, now, { "note" }, AlarmSort::Time);
    CHECK(st[0].name == "hot");
    CHECK(st[1].name == "note");
    CHECK(st[2].name == "door");
    st = alarm_stats(log, 0, 0, now, { "note" }, AlarmSort::Count, "CRITICAL");      // one severity (any case)
    REQUIRE(st.size() == 1);
    CHECK(st[0].name == "hot");
    CHECK(alarm_stats({}, 0, 0, now, {}).empty());
    CHECK(alarm_stats(log, 0, 0, now, {}, AlarmSort::Count, "nothing").empty());
}

TEST_CASE("alarm stats: a span cuts the stretches at its ends; an alarm from before the span counts its time, not a raise")
{
    const std::vector<AlarmEvent> log = {
        ev(T0 - 2 * H, "long", "raised"),                         // raised before the span, cleared in it
        ev(T0 - H, "gone", "raised"), ev(T0 - 30 * MIN, "gone", "cleared"),      // all before the span
        ev(T0 + 10 * MIN, "long", "cleared"),
        ev(T0 + 20 * MIN, "in", "raised"), ev(T0 + 25 * MIN, "in", "cleared"),
        ev(T0 + 50 * MIN, "over", "raised"),                      // raised in the span, cleared after it
        ev(T0 + 2 * H, "over", "cleared"),
        ev(T0 + 3 * H, "later", "raised"), ev(T0 + 4 * H, "later", "cleared"),   // all after the span
    };
    const std::vector<AlarmStat> st = alarm_stats(log, T0, T0 + H, T0 + 5 * H, {});
    CHECK(find(st, "gone") == nullptr);
    CHECK(find(st, "later") == nullptr);
    REQUIRE(find(st, "long"));
    CHECK(find(st, "long")->count == 0);                          // not raised in the span ...
    CHECK(find(st, "long")->active_ms == 10 * MIN);               // ... but active in it
    CHECK(find(st, "long")->last_raised_ms == 0);
    REQUIRE(find(st, "in"));
    CHECK(find(st, "in")->count == 1);
    CHECK(find(st, "in")->active_ms == 5 * MIN);
    REQUIRE(find(st, "over"));
    CHECK(find(st, "over")->count == 1);
    CHECK(find(st, "over")->active_ms == 10 * MIN);               // up to the end of the span
    // the times of a span never add up to more than the span
    for(const AlarmStat& s : st) CHECK(s.active_ms <= H);
    // an alarm active over midnight: each day gets its part
    const std::vector<AlarmEvent> night = { ev(T0 + 23 * H, "n", "raised"), ev(T0 + DAY + 2 * H, "n", "cleared") };
    CHECK(alarm_stats(night, T0, T0 + DAY, T0 + 3 * DAY, {})[0].active_ms == H);
    const std::vector<AlarmStat> second = alarm_stats(night, T0 + DAY, T0 + 2 * DAY, T0 + 3 * DAY, {});
    REQUIRE(second.size() == 1);
    CHECK(second[0].active_ms == 2 * H);
    CHECK(second[0].count == 0);
}

// ================================================================ reliability

TEST_CASE("alarm stats: a log with holes and a clock that jumped never gives a negative or an endless time")
{
    const int64_t now = T0 + 10 * H;
    // the power went while "a" was active: no "cleared"; it is raised again two hours later.
    // The log last said anything at T0 + 12 min: that is taken as the end of the first stretch.
    std::vector<AlarmEvent> log = {
        ev(T0, "a", "raised"), ev(T0 + 12 * MIN, "b", "raised"), ev(T0 + 12 * MIN, "b", "cleared"),
        ev(T0 + 2 * H, "a", "raised"), ev(T0 + 2 * H + 5 * MIN, "a", "cleared"),
    };
    std::vector<AlarmStat> st = alarm_stats(log, 0, 0, now, {});
    REQUIRE(find(st, "a"));
    CHECK(find(st, "a")->count == 2);
    CHECK(find(st, "a")->active_ms == 12 * MIN + 5 * MIN);
    // raised and never cleared, and not active now: not counted up to now
    log = { ev(T0, "stuck", "raised"), ev(T0 + 30 * MIN, "other", "raised"), ev(T0 + 31 * MIN, "other", "cleared") };
    st = alarm_stats(log, 0, 0, now, {});
    CHECK(find(st, "stuck")->active_ms == 31 * MIN);
    st = alarm_stats(log, 0, 0, now, { "stuck" });               // ... but when it is active now, it is
    CHECK(find(st, "stuck")->active_ms == 10 * H);
    // a "cleared" without a "raised" (the log was cut by its retention): nothing
    log = { ev(T0, "cut", "cleared"), ev(T0 + MIN, "cut", "raised"), ev(T0 + 2 * MIN, "cut", "cleared") };
    st = alarm_stats(log, 0, 0, now, {});
    CHECK(find(st, "cut")->count == 1);
    CHECK(find(st, "cut")->active_ms == MIN);
    // the clock was set back an hour while "back" was active: the "cleared" is before the "raised"
    log = { ev(T0 + H, "back", "raised"), ev(T0 + 5 * MIN, "back", "cleared"), ev(T0 + 10 * MIN, "back", "raised"), ev(T0 + 12 * MIN, "back", "cleared") };
    st = alarm_stats(log, 0, 0, now, {});
    REQUIRE(find(st, "back"));
    CHECK(find(st, "back")->active_ms == 2 * MIN);
    CHECK(find(st, "back")->count == 2);
    // the clock was set far ahead: a "now" before the events, events in the year 9999, time 0
    for(int64_t n : { int64_t(0), int64_t(-5), T0 - DAY, INT64_MAX / 2 }) {
        for(const AlarmStat& s : alarm_stats(log, 0, 0, n, { "back" })) { CHECK(s.active_ms >= 0); CHECK(s.longest_ms >= 0); CHECK(s.count >= 0); }
    }
    log = { ev(INT64_MAX / 4, "far", "raised"), ev(INT64_MAX / 4 + H, "far", "cleared"), ev(-1000, "neg", "raised"), ev(0, "neg", "cleared") };
    for(const AlarmStat& s : alarm_stats(log, 0, 0, INT64_MAX / 2, {})) { CHECK(s.active_ms >= 0); CHECK(s.active_ms <= H); }
    // a span turned round, events that are no events
    CHECK(alarm_stats(log, T0 + H, T0, now, {}).empty());
    log = { ev(T0, "", "raised"), ev(T0 + MIN, "", "cleared"), ev(T0, "x", "exploded"), ev(T0, "x", "") };
    st = alarm_stats(log, 0, 0, now, {});
    CHECK(st.size() == 1);                                       // (an alarm without a name is still one; unknown events are none)
}

TEST_CASE("alarm stats: durations as text")
{
    CHECK(alarm_duration_text(0) == "0:00:00");
    CHECK(alarm_duration_text(7 * S) == "0:00:07");
    CHECK(alarm_duration_text(499) == "0:00:00");
    CHECK(alarm_duration_text(500) == "0:00:01");
    CHECK(alarm_duration_text(H + 2 * MIN + 3 * S) == "1:02:03");
    CHECK(alarm_duration_text(100 * H) == "100:00:00");          // hours do not stop at 24
    CHECK(alarm_duration_text(-5 * S) == "0:00:00");
}

// ================================================================ security

TEST_CASE("alarm stats: the CSV cannot run a formula in a spreadsheet, whatever a message says")
{
    CHECK(csv_safe_text("Temperature high") == "Temperature high");
    CHECK(csv_safe_text("=HYPERLINK(\"http://evil\")") == "'=HYPERLINK(\"http://evil\")");
    CHECK(csv_safe_text("+1+1") == "'+1+1");
    CHECK(csv_safe_text("-2+3") == "'-2+3");
    CHECK(csv_safe_text("@SUM(A1)") == "'@SUM(A1)");
    CHECK(csv_safe_text("\t=1") == "'\t=1");
    CHECK(csv_safe_text("\r=1") == "'\r=1");
    CHECK(csv_safe_text("a=b") == "a=b");                        // only at the start
    CHECK(csv_safe_text("") == "");

    TempDir t;
    std::vector<AlarmStat> st(3);
    st[0].name = "=cmd|' /C calc'!A0"; st[0].message = "=1+1\nsecond line\x01"; st[0].severity = "@warning"; st[0].count = 3; st[0].active_ms = 90 * S; st[0].longest_ms = 61 * S;
    st[1].name = "plain"; st[1].message = std::string(5000, 'm') + ",\"quoted\""; st[1].severity = "info"; st[1].count = 1; st[1].active_now = true;
    st[2].name = "-neg"; st[2].message = "\xff\xfe broken"; st[2].severity = "+x";
    const fs::path file = t.dir / "summary.csv";
    std::string written;
    REQUIRE(alarm_stats_export(st, file.generic_string(), &written) == 3);
    CHECK(written == file.generic_string());
    const std::string csv = read_all(file);
    CHECK(csv.find("name,message,severity,count,active_s,longest_s,active_now") != std::string::npos);
    // no field begins with a character a spreadsheet calculates
    std::string field;
    bool quoted = false, bad = false;
    auto check = [&] { const std::string f = !field.empty() && field[0] == '"' ? field.substr(1) : field; if(!f.empty() && std::string("=+-@\t\r").find(f[0]) != std::string::npos) bad = true; field.clear(); };
    for(size_t i = 3; i < csv.size(); i++) {                     // (after the byte order mark)
        const char ch = csv[i];
        if(ch == '"') quoted = !quoted;
        if(!quoted && (ch == ',' || ch == '\n' || ch == '\r')) { if(ch != '\n' || !field.empty()) check(); continue; }
        field += ch;
    }
    CHECK_FALSE(bad);
    CHECK(csv.find("'=cmd|") != std::string::npos);
    CHECK(csv.find("'=1+1 second line") != std::string::npos);  // one line, the control character gone
    CHECK(csv.find(",3,90,61,0") != std::string::npos);
    CHECK(csv.find(",1,0,0,1") != std::string::npos);
    CHECK(csv.size() < 2000);                                    // the endless message was cut
    // somewhere that cannot be written
    CHECK(alarm_stats_export(st, (file / "x.csv").generic_string()) == -1);        // (below a file)
}

// ================================================================ performance efficiency

TEST_CASE("alarm stats: a log of 100 000 events is counted quickly")
{
    std::vector<AlarmEvent> log;
    log.reserve(100000);
    for(int i = 0; i < 50000; i++) {
        const std::string name = "alarm" + std::to_string(i % 200);
        AlarmEvent r = ev(T0 + i * 10 * S, name.c_str(), "raised");
        AlarmEvent c = ev(T0 + i * 10 * S + 4 * S, name.c_str(), "cleared");
        log.push_back(r);
        log.push_back(c);
    }
    const uint32_t t0 = millis();
    const std::vector<AlarmStat> st = alarm_stats(log, 0, 0, T0 + 30 * DAY, {});
    const uint32_t ms = millis() - t0;
    REQUIRE(st.size() == 200);
    CHECK(st[0].count == 250);
    CHECK(st[0].active_ms == 250 * 4 * S);
    MESSAGE("100 000 events of 200 alarms counted in " << ms << " ms");
    CHECK(ms < 2000);                                            // (loose: a debug build on a busy CI machine)
}

// ================================================================ the alarm log and the widget

TEST_CASE("alarm_summary: counts from the alarm log of the project; span and order chosen on the widget; an export")
{
    Runtime rt;
    TempDir t;
    rt.tag("temp", TagType::Float, 20);
    rt.tag("door", TagType::Bool, 0);
    const json cfg = json::parse(R"~({ "items": [
        { "name": "hot",  "tag": "temp", "condition": "gt", "value": 50, "message": "Too hot", "severity": "critical" },
        { "name": "door", "tag": "door", "message": "Door open", "severity": "warning" } ] })~");
    REQUIRE(alarms().load(&cfg, t.dir.generic_string()) == 2);
    // a history written straight into the log: "door" five times for a minute each two days ago, "hot" once for three hours yesterday
    const int64_t now = LogDb::now_ms();
    for(int i = 0; i < 5; i++) {
        alarms().db().add(now - 2 * DAY + i * 10 * MIN, { std::string("door"), std::string("raised"), std::string("warning"), std::string("Door open"), std::string("1"), std::string("") });
        alarms().db().add(now - 2 * DAY + i * 10 * MIN + MIN, { std::string("door"), std::string("cleared"), std::string("warning"), std::string("Door open"), std::string("0"), std::string("") });
    }
    alarms().db().add(now - DAY - 6 * H, { std::string("hot"), std::string("raised"), std::string("critical"), std::string("Too hot"), std::string("60"), std::string("") });
    alarms().db().add(now - DAY - 3 * H, { std::string("hot"), std::string("cleared"), std::string("critical"), std::string("Too hot"), std::string("40"), std::string("") });
    alarms().db().flush();

    std::vector<AlarmStat> st = alarm_stats_recent(7, {});
    REQUIRE(st.size() == 2);
    CHECK(st[0].name == "door");
    CHECK(st[0].count == 5);
    CHECK(st[0].active_ms == 5 * MIN);
    CHECK(st[1].name == "hot");
    CHECK(st[1].active_ms == 3 * H);
    CHECK(alarm_stats_recent(1, {}).empty());                    // nothing in the last 24 hours
    CHECK(alarm_stats_recent(0, {}, AlarmSort::Time)[0].name == "hot");

    lv_obj_t* s = ui().build_screen(json::parse(R"({ "name": "sum", "style": { "pad": 0 }, "children": [
        { "type": "alarm_summary", "w": 320, "h": 240, "days": 7, "labels": { "alarm": "What", "empty": "Nothing" } } ] })"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen("sum", "none"));
    lv_obj_t* w = lv_obj_get_child(s, 0);
    lv_obj_t* bar = lv_obj_get_child(w, 0);
    lv_obj_t* body = lv_obj_get_child(w, 1);
    auto settle = [&] { for(int i = 0; i < 4; i++) { rt.advance(300); alarm_summary_wait_idle(); } rt.advance(300); };
    auto text = [&](uint32_t row, uint32_t col) { return std::string(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(body, static_cast<int32_t>(row)), static_cast<int32_t>(col)))); };
    settle();
    REQUIRE(lv_obj_get_child_count(body) == 3);                  // the titles and two alarms
    CHECK(text(0, 0) == "What");
    CHECK(text(0, 1) == "Count");
    CHECK(text(1, 0) == "Door open");
    CHECK(text(1, 1) == "5");
    CHECK(text(1, 2) == "0:05:00");
    CHECK(text(2, 0) == "Too hot");
    CHECK(text(2, 2) == "3:00:00");
    CHECK(lv_bar_get_value(lv_obj_get_child(lv_obj_get_child(body, 1), 3)) == 1000);     // the most often: a full bar
    CHECK(lv_bar_get_value(lv_obj_get_child(lv_obj_get_child(body, 2), 3)) == 200);      // 1 of 5
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(bar, 2))) == "6 / 3:05:00");    // all of them together
    // longest first
    lv_obj_t* sort = lv_obj_get_child(bar, 1);
    lv_dropdown_set_selected(sort, 1);
    lv_obj_send_event(sort, LV_EVENT_VALUE_CHANGED, nullptr);
    settle();
    CHECK(text(1, 0) == "Too hot");
    CHECK(lv_bar_get_value(lv_obj_get_child(lv_obj_get_child(body, 1), 3)) == 1000);
    // the last 24 hours: nothing
    lv_obj_t* period = lv_obj_get_child(bar, 0);
    lv_dropdown_set_selected(period, 0);
    lv_obj_send_event(period, LV_EVENT_VALUE_CHANGED, nullptr);
    settle();
    REQUIRE(lv_obj_get_child_count(body) == 2);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(body, 1))) == "Nothing");
    // an alarm is raised now: the widget follows by itself, the alarm is marked as active, its time grows
    tags().find("door")->set_number(1, nullptr);
    rt.advance(50);
    {   // (in the very millisecond of the raise it is already counted)
        const std::vector<AlarmStat> at_once = alarm_stats_recent(1, { "door" });
        REQUIRE(at_once.size() == 1);
        CHECK(at_once[0].count == 1);
    }
    settle();
    REQUIRE(lv_obj_get_child_count(body) == 2);
    CHECK(text(1, 0).find("Door open") != std::string::npos);
    CHECK(text(1, 0) != "Door open");                            // (the mark of an active alarm in front)
    CHECK(text(1, 1) == "1");
    // the buttons a finger needs
    lv_obj_update_layout(w);
    CHECK(lv_obj_get_height(period) >= 40);
    CHECK(lv_obj_get_height(sort) >= 40);

    // the action writes the CSV next to the alarm database
    register_alarm_summary_actions();
    ui().call_action("alarm.export_summary", "7");
    bool found = false;
    for(const auto& e : fs::recursive_directory_iterator(t.dir)) if(e.path().filename().string().rfind("alarm-summary-", 0) == 0) { found = true; CHECK(read_all(e.path()).find("door,Door open,warning,6,") != std::string::npos); }
    CHECK(found);
    // the widget reads only: the log has what it had (+ the raise of just now)
    CHECK(alarms().query().size() == 13);
    ui().unload_all();
}

TEST_CASE("alarm_summary: without alarms it says so; --check knows the widget")
{
    Runtime rt;
    alarms().clear();
    CHECK(alarm_stats_recent(7, {}).empty());
    CHECK(alarm_stats_export({}) == -1);                         // nowhere to write it
    lv_obj_t* s = ui().build_screen(json::parse(R"({ "name": "sum", "children": [ { "type": "alarm_summary", "rows": -5, "days": 99999, "sort": "sideways", "refresh_ms": 1, "bars": false,
                                                                                    "labels": { "day": 5, "empty": [] } },
                                                                                  { "type": "alarm_summary", "w": 3, "h": 3 } ] })"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen("sum", "none"));
    for(int i = 0; i < 4; i++) { rt.advance(300); alarm_summary_wait_idle(); }
    rt.advance(300);
    lv_obj_t* body = lv_obj_get_child(lv_obj_get_child(s, 0), 1);
    REQUIRE(lv_obj_get_child_count(body) == 2);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(body, 1))).find("no alarms") != std::string::npos);
    ui().unload_all();

    TempDir t;
    fs::create_directories(t.dir / "screens");
    auto problems = [&](const std::string& extra, const std::string& widgets) {
        { std::ofstream f(t.dir / "project.json", std::ios::binary);
          f << "{ \"start_screen\": \"s\", \"tags\": [ { \"name\": \"a\", \"type\": \"bool\" } ], " << extra << " \"screens\": [\"screens/s.json\"] }"; }
        { std::ofstream f(t.dir / "screens" / "s.json", std::ios::binary); f << "{ \"name\": \"s\", \"children\": [ " << widgets << " ] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((t.dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    const std::string with = R"("alarms": { "items": [ { "name": "x", "tag": "a" } ] },)";
    CHECK(problems(with, R"({ "type": "alarm_summary", "days": 30, "sort": "time", "rows": 5, "severity": "warning", "bars": false, "refresh_ms": 0, "labels": { "empty": "-" } })") == "");
    CHECK(problems("", R"({ "type": "alarm_summary" })").find("children[0] (alarm_summary): the project has no \"alarms\"") != std::string::npos);
    CHECK(problems(with, R"({ "type": "alarm_summary", "period": 7 })").find("unknown key \"period\"") != std::string::npos);
}
