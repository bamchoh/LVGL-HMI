// Schedules (schedule.hpp): actions that run by the clock. Grouped by ISO/IEC 25010
// characteristic (issue #70). The clock is handed in: no test waits for a time of day.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "schedule.hpp"
#include "system_settings.hpp"
#include "check.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

// local date and time -> Unix seconds (as the machine's time zone has it)
int64_t at(int y, int mo, int d, int h, int mi, int s = 0)
{
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
    tm.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&tm));
}
ScheduleItem item(const char* text)
{
    ScheduleItem s;
    std::string err;
    REQUIRE_MESSAGE(schedule_parse(json::parse(text), s, &err), err);
    return s;
}
const char* kDo = R"("do": { "action": "set", "tag": "run", "value": true })";
std::string J(const std::string& fields) { return "{ " + fields + ", " + kDo + " }"; }

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_schedule_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir() { scheduler().clear(); std::error_code ec; fs::remove_all(dir, ec); }
    std::string store() const { return (dir / "project.local.json").generic_string(); }
    json local() const
    {
        std::ifstream f(dir / "project.local.json", std::ios::binary);
        if(!f) return json();
        std::stringstream ss;
        ss << f.rdbuf();
        return json::parse(ss.str(), nullptr, false);
    }
};

// a scheduler with a list of what ran instead of the user interface
struct Fixture {
    Runtime rt;
    TempDir t;
    std::vector<std::string> ran;
    int load(const std::string& schedules, const json* local = nullptr)
    {
        const json j = json::parse(schedules);
        const int n = scheduler().load(&j, local, t.store());
        scheduler().set_runner([this](const ScheduleItem& s) { ran.push_back(s.name); });
        return n;
    }
    // looks at the clock every second from a to b (both included)
    void run(int64_t a, int64_t b) { for(int64_t s = a; s <= b; s++) scheduler().check(s); }
};

// 2026-11-23 is a Monday
const int Y = 2026, MO = 11, MON = 23;

} // namespace

// ================================================================ functional suitability

TEST_CASE("schedule: \"at\" and \"days\" as they are written, and back")
{
    ScheduleItem s = item(J(R"("name": "a", "at": "08:00")").c_str());
    CHECK(s.hour == 8);
    CHECK(s.minute == 0);
    CHECK_FALSE(s.once);
    CHECK(s.days == 0x7F);
    CHECK(s.enabled);
    CHECK_FALSE(s.catch_up);
    CHECK(schedule_at_text(s) == "08:00");
    CHECK(schedule_days_text(s.days) == "daily");
    s = item(J(R"("name": "a", "at": " 23:59 ", "days": ["mon", "WED", "fri"], "enabled": false, "catch_up": true)").c_str());
    CHECK(s.days == ((1u << 1) | (1u << 3) | (1u << 5)));
    CHECK(schedule_days_text(s.days) == "mon wed fri");
    CHECK(schedule_days_json(s.days) == json::array({ "mon", "wed", "fri" }));
    CHECK_FALSE(s.enabled);
    CHECK(s.catch_up);
    CHECK(item(J(R"("name": "a", "at": "08:00", "days": "weekdays")").c_str()).days == 0x3E);
    CHECK(item(J(R"("name": "a", "at": "08:00", "days": "weekend")").c_str()).days == 0x41);
    CHECK(item(J(R"("name": "a", "at": "08:00", "days": "sat, sun")").c_str()).days == 0x41);
    CHECK(schedule_days_text(0x3E) == "weekdays");
    CHECK(schedule_days_text(0x41) == "weekend");
    CHECK(schedule_days_text(1u << 0) == "sun");
    CHECK(schedule_days_json(0x7F) == json("daily"));
    s = item(J(R"("name": "once", "at": "2026-12-01 09:30")").c_str());
    CHECK(s.once);
    CHECK(s.year == 2026);
    CHECK(s.month == 12);
    CHECK(s.day == 1);
    CHECK(schedule_at_text(s) == "2026-12-01 09:30");
    CHECK(item(J(R"("name": "leap", "at": "2028-02-29T00:00")").c_str()).day == 29);

    // what is no schedule says why
    ScheduleItem x;
    std::string err;
    for(const char* bad : { R"({ "at": "08:00", "do": {} })", R"({ "name": "a", "do": {} })", R"({ "name": "a", "at": "8:00", "do": {} })", R"({ "name": "a", "at": "24:00", "do": {} })",
                            R"({ "name": "a", "at": "12:60", "do": {} })", R"({ "name": "a", "at": "08:00:00", "do": {} })", R"({ "name": "a", "at": "2026-02-30 08:00", "do": {} })",
                            R"({ "name": "a", "at": "2026-13-01 08:00", "do": {} })", R"({ "name": "a", "at": "1969-12-31 08:00", "do": {} })", R"({ "name": "a", "at": "now", "do": {} })",
                            R"({ "name": "a", "at": "08:00" })", R"({ "name": "a", "at": "08:00", "do": "reboot" })", R"({ "name": "a", "at": "08:00", "do": 5 })",
                            R"({ "name": "a", "at": "08:00", "days": [], "do": {} })", R"({ "name": "a", "at": "08:00", "days": ["monday"], "do": {} })",
                            R"({ "name": "a", "at": "08:00", "days": 5, "do": {} })", R"({ "name": "a", "at": "08:00", "days": [1], "do": {} })", R"("08:00")", R"([])", R"(null)" }) {
        CAPTURE(bad);
        err.clear();
        CHECK_FALSE(schedule_parse(json::parse(bad), x, &err));
        CHECK(err != "");
    }
    CHECK_FALSE(schedule_parse(json::parse(R"({ "name": ")" + std::string(100, 'n') + R"(", "at": "08:00", "do": {} })"), x, &err));
}

TEST_CASE("schedule: the scheduled times of an item - a time of day on its days, once at a date")
{
    const ScheduleItem daily = item(J(R"("name": "d", "at": "08:00")").c_str());
    const int64_t mon8 = at(Y, MO, MON, 8, 0);
    CHECK(*schedule_latest_due(daily, mon8 - 1, mon8) == mon8);                 // the second it becomes 8:00
    CHECK_FALSE(schedule_latest_due(daily, mon8, mon8 + 3600));                 // it is behind us
    CHECK_FALSE(schedule_latest_due(daily, mon8 - 3600, mon8 - 1));             // not yet
    CHECK_FALSE(schedule_latest_due(daily, mon8, mon8));                        // an empty span
    CHECK(*schedule_latest_due(daily, mon8 - 3 * 86400, mon8 + 60) == mon8);    // the newest of several
    CHECK(*schedule_next_due(daily, mon8) == at(Y, MO, MON + 1, 8, 0));
    CHECK(*schedule_next_due(daily, mon8 - 1) == mon8);

    const ScheduleItem week = item(J(R"("name": "w", "at": "08:00", "days": "weekdays")").c_str());
    const int64_t fri8 = at(Y, MO, MON + 4, 8, 0), sat8 = at(Y, MO, MON + 5, 8, 0), next_mon8 = at(Y, MO, MON + 7, 8, 0);
    CHECK(*schedule_latest_due(week, fri8 - 1, fri8) == fri8);
    CHECK_FALSE(schedule_latest_due(week, sat8 - 1, sat8));                     // not on a Saturday
    CHECK_FALSE(schedule_latest_due(week, fri8, next_mon8 - 1));                // (nothing between Friday 8:00 and Monday 7:59:59)
    CHECK(*schedule_next_due(week, fri8) == next_mon8);                         // over the weekend
    const ScheduleItem sun = item(J(R"("name": "s", "at": "00:00", "days": ["sun"])").c_str());
    CHECK(*schedule_next_due(sun, mon8) == at(Y, MO, MON + 6, 0, 0));           // midnight at the start of Sunday
    CHECK(*schedule_latest_due(sun, mon8 - 2 * 86400, mon8) == at(Y, MO, MON - 1, 0, 0));
    CHECK_FALSE(schedule_latest_due(sun, mon8 - 86400, mon8));                  // (Sunday 0:00 is before Sunday 8:00)

    // a time just before midnight, over the end of a month and of a year
    const ScheduleItem late = item(J(R"("name": "l", "at": "23:59")").c_str());
    CHECK(*schedule_next_due(late, at(2026, 12, 31, 23, 59)) == at(2027, 1, 1, 23, 59));
    CHECK(*schedule_latest_due(late, at(2027, 2, 28, 12, 0), at(2027, 3, 1, 0, 0)) == at(2027, 2, 28, 23, 59));
    CHECK(*schedule_latest_due(late, at(2028, 2, 28, 23, 59), at(2028, 3, 1, 0, 0)) == at(2028, 2, 29, 23, 59));   // a leap day

    const ScheduleItem once = item(J(R"("name": "o", "at": "2026-12-01 09:30")").c_str());
    const int64_t t = at(2026, 12, 1, 9, 30);
    CHECK(*schedule_latest_due(once, t - 1, t) == t);
    CHECK_FALSE(schedule_latest_due(once, t, t + 86400 * 400));                 // never again
    CHECK(*schedule_next_due(once, t - 86400 * 30) == t);
    CHECK_FALSE(schedule_next_due(once, t));
}

TEST_CASE("schedule: each scheduled time runs once, on its days; a disabled one does not")
{
    Fixture f;
    REQUIRE(f.load("[" + J(R"("name": "start", "at": "08:00", "days": "weekdays")") + "," + J(R"("name": "stop", "at": "17:30")") + "," +
                   J(R"("name": "off", "at": "08:00", "enabled": false)") + "," + J(R"("name": "service", "at": "2026-11-25 12:00")") + "]") == 4);
    CHECK(scheduler().count() == 4);
    f.run(at(Y, MO, MON, 7, 59, 50), at(Y, MO, MON, 8, 0, 10));
    CHECK(f.ran == std::vector<std::string>{ "start" });        // once, not for each of the ten seconds after
    f.ran.clear();
    f.run(at(Y, MO, MON, 17, 29, 58), at(Y, MO, MON, 17, 30, 2));
    CHECK(f.ran == std::vector<std::string>{ "stop" });
    f.ran.clear();
    // Wednesday noon: the one with a date
    f.run(at(Y, MO, MON + 2, 11, 59, 59), at(Y, MO, MON + 2, 12, 0, 1));
    CHECK(f.ran == std::vector<std::string>{ "service" });
    f.ran.clear();
    // Saturday 8:00: "start" is for weekdays
    f.run(at(Y, MO, MON + 5, 7, 59, 59), at(Y, MO, MON + 5, 8, 0, 1));
    CHECK(f.ran.empty());
    // a year later the date is not run again
    f.run(at(2027, 11, 25, 11, 59, 59), at(2027, 11, 25, 12, 0, 1));
    CHECK(f.ran.empty());
}

// ================================================================ reliability: the clock

TEST_CASE("schedule: a second that is skipped loses nothing; a clock set back runs nothing twice")
{
    Fixture f;
    REQUIRE(f.load("[" + J(R"("name": "a", "at": "08:00")") + "]") == 1);
    const int64_t t = at(Y, MO, MON, 8, 0);
    // the look at 8:00:00 never happens (a busy moment, a correction of a few seconds by NTP)
    scheduler().check(t - 2);
    scheduler().check(t + 3);
    CHECK(f.ran.size() == 1);
    // the clock is set back 30 seconds: 8:00 comes a second time and does not run again
    for(int64_t s = t - 27; s <= t + 5; s++) scheduler().check(s);
    CHECK(f.ran.size() == 1);
    // ... and tomorrow it runs as ever
    f.run(t + 86400 - 2, t + 86400 + 2);
    CHECK(f.ran.size() == 2);
    // a correction forward of a minute over the scheduled time: it runs (once)
    scheduler().check(t + 2 * 86400 - 40);
    scheduler().check(t + 2 * 86400 + 20);
    scheduler().check(t + 2 * 86400 + 21);
    CHECK(f.ran.size() == 3);
    // the clock was wrong by a year and is set back: the schedules go on with the right date
    scheduler().check(t + 400 * 86400);
    f.ran.clear();
    for(int64_t s = t + 3 * 86400 - 2; s <= t + 3 * 86400 + 2; s++) scheduler().check(s);
    CHECK(f.ran.size() == 1);
    // nonsense from the clock
    scheduler().check(0);
    scheduler().check(-5);
    CHECK(f.ran.size() == 1);
}

TEST_CASE("schedule: a time that passed while nothing ran is skipped, unless the schedule asks to catch up")
{
    Fixture f;
    const std::string defs = "[" + J(R"("name": "plain", "at": "08:00")") + "," + J(R"("name": "must", "at": "08:00", "catch_up": true)") + "]";
    REQUIRE(f.load(defs) == 2);
    const int64_t t = at(Y, MO, MON, 8, 0);
    // the first start ever, at 9:00: nothing is known to have been missed
    scheduler().check(t + 3600);
    CHECK(f.ran.empty());
    // it runs on Tuesday; "must" writes down that it did
    f.run(t + 86400 - 1, t + 86400 + 1);
    CHECK(f.ran == std::vector<std::string>{ "plain", "must" });
    const json local = f.t.local();
    REQUIRE(local.is_object());
    CHECK(local["schedules"]["must"]["last"] == t + 86400);
    CHECK_FALSE(local["schedules"].contains("plain"));           // (nothing to remember for the other)

    // the device is off from Tuesday evening to Wednesday 10:00: the new start catches "must" up, once
    f.ran.clear();
    const json loc = local["schedules"];
    REQUIRE(f.load(defs, &loc) == 2);
    scheduler().check(t + 2 * 86400 + 7200);
    CHECK(f.ran == std::vector<std::string>{ "must" });
    scheduler().check(t + 2 * 86400 + 7201);
    CHECK(f.ran.size() == 1);
    CHECK(f.t.local()["schedules"]["must"]["last"] == t + 2 * 86400);
    // off for a week: only the newest missed time is caught up, and only if it is within a day
    f.ran.clear();
    const json loc2 = f.t.local()["schedules"];
    REQUIRE(f.load(defs, &loc2) == 2);
    scheduler().check(t + 9 * 86400 + 7200);
    CHECK(f.ran == std::vector<std::string>{ "must" });          // (of 8:00 that morning)
    f.ran.clear();
    const json loc3 = f.t.local()["schedules"];
    REQUIRE(f.load("[" + J(R"("name": "must", "at": "08:00", "days": ["mon"], "catch_up": true)") + "]", &loc3) == 1);
    scheduler().check(t + 10 * 86400 + 7200);                    // a Thursday: Monday's 8:00 is more than a day ago
    CHECK(f.ran.empty());

    // while running: the clock jumps ahead by three hours (set by hand, or the machine slept)
    Fixture g;
    REQUIRE(g.load(defs) == 2);
    scheduler().check(t - 3600);
    scheduler().check(t - 3599);
    scheduler().check(t + 7200);
    CHECK(g.ran == std::vector<std::string>{ "must" });          // "plain" is skipped like after a restart
}

TEST_CASE("schedule: switched off before a catch_up schedule ever ran - the next start still catches it up")
{
    Fixture h;
    const std::string defs = "[" + J(R"("name": "plain", "at": "08:00")") + "," + J(R"("name": "must", "at": "08:00", "catch_up": true)") + "]";
    const int64_t t = at(Y, MO, MON, 8, 0);
    REQUIRE(h.load(defs) == 2);
    scheduler().check(t - 7200);                                 // the first look ever, at 6:00: written down
    CHECK(h.ran.empty());
    const json seen = h.t.local()["schedules"];
    REQUIRE(seen.is_object());
    CHECK(seen["must"]["last"] == t - 7200);
    CHECK_FALSE(seen.contains("plain"));
    REQUIRE(h.load(defs, &seen) == 2);                           // off at 7:00, on again at 8:30
    scheduler().check(t + 1800);
    CHECK(h.ran == std::vector<std::string>{ "must" });
}

// ================================================================ security / usability: changes on the device

TEST_CASE("schedule: time, days and enabled can be changed on the device and are kept; what it does cannot be changed")
{
    Fixture f;
    const std::string defs = "[" + J(R"("name": "start", "at": "08:00", "days": "weekdays")") + "," + J(R"("name": "service", "at": "2026-12-01 09:00")") + "]";
    REQUIRE(f.load(defs) == 2);
    const json before = scheduler().items()[0].actions;
    CHECK(scheduler().change("start", "07:30", 0x7F, true) == "");
    CHECK(schedule_at_text(scheduler().items()[0]) == "07:30");
    CHECK(scheduler().items()[0].days == 0x7F);
    CHECK(scheduler().items()[0].actions == before);
    json local = f.t.local();
    CHECK(local["schedules"]["start"] == json{ { "at", "07:30" }, { "days", "daily" } });      // only what differs from the project
    // it runs at the new time, not at the old one
    f.run(at(Y, MO, MON, 7, 29, 58), at(Y, MO, MON, 8, 0, 2));
    CHECK(f.ran == std::vector<std::string>{ "start" });
    // switched off
    CHECK(scheduler().change("start", "07:30", 0x7F, false) == "");
    f.ran.clear();
    f.run(at(Y, MO, MON + 1, 7, 29, 58), at(Y, MO, MON + 1, 7, 30, 2));
    CHECK(f.ran.empty());
    CHECK(f.t.local()["schedules"]["start"]["enabled"] == false);
    // the next start has the changes
    const json loc = f.t.local()["schedules"];
    REQUIRE(f.load(defs, &loc) == 2);
    CHECK(schedule_at_text(scheduler().items()[0]) == "07:30");
    CHECK_FALSE(scheduler().items()[0].enabled);
    // back to what the project says: nothing is left in the file
    CHECK(scheduler().change("start", "08:00", 0x3E, true) == "");
    CHECK_FALSE(fs::exists(f.t.store()));

    // what is refused
    CHECK(scheduler().change("nope", "08:00", 0x7F, true).find("no schedule") != std::string::npos);
    CHECK(scheduler().change("start", "25:00", 0x7F, true).find("HH:MM") != std::string::npos);
    CHECK(scheduler().change("start", "8", 0x7F, true) != "");
    CHECK(scheduler().change("start", "08:00; rm -rf /", 0x7F, true) != "");
    CHECK(scheduler().change("start", "08:00", 0, true).find("day") != std::string::npos);
    CHECK(scheduler().change("start", "2026-12-01 09:00", 0x7F, true) != "");          // a daily one does not become a date
    CHECK(scheduler().change("service", "10:00", 0x7F, true) != "");                    // ... nor the other way
    CHECK(scheduler().change("service", "2026-12-02 10:00", 0x7F, true) == "");
    CHECK(schedule_at_text(scheduler().items()[1]) == "2026-12-02 10:00");
    // a local file with nonsense in it changes nothing and breaks nothing
    const json junk = json::parse(R"({ "start": { "at": "99:99", "days": ["someday"], "enabled": "maybe", "last": "x", "do": { "action": "exit" } }, "ghost": { "at": "01:00" }, "service": 5 })");
    REQUIRE(f.load(defs, &junk) == 2);
    CHECK(schedule_at_text(scheduler().items()[0]) == "08:00");
    CHECK(scheduler().items()[0].days == 0x3E);
    CHECK(scheduler().items()[0].actions == before);                                    // the local file cannot give a schedule something else to do
}

TEST_CASE("schedule: a list that is no list, entries that are no schedules, two with one name, hundreds")
{
    Fixture f;
    CHECK(scheduler().load(nullptr, nullptr, "") == 0);
    const json not_list = json::parse(R"({ "name": "a" })");
    CHECK(scheduler().load(&not_list, nullptr, "") == 0);
    CHECK(f.load("[ 5, null, \"x\", [], {}, " + J(R"("name": "ok", "at": "08:00")") + "," + J(R"("name": "ok", "at": "09:00")") + "," + J(R"("name": "bad", "at": "8 o'clock")") + "]") == 1);
    CHECK(schedule_at_text(scheduler().items()[0]) == "08:00");                         // the first of two with one name
    std::string many = "[";
    for(int i = 0; i < 300; i++) many += std::string(i ? "," : "") + J("\"name\": \"s" + std::to_string(i) + "\", \"at\": \"" + (i % 2 ? "08:00" : "09:00") + "\"");
    many += "]";
    CHECK(f.load(many) == 200);
    // 200 schedules, a look every second for an hour over 8:00: quick, and each one once
    const int64_t t = at(Y, MO, MON, 8, 0);
    const uint32_t t0 = millis();
    f.run(t - 1800, t + 1800);
    const uint32_t ms = millis() - t0;
    CHECK(f.ran.size() == 100);
    MESSAGE("200 schedules looked at 3600 times: " << ms << " ms (" << (ms / 3600.0) << " ms per look)");
    CHECK(ms < 20000);                                          // (loose: a debug build on a busy CI machine)
}

// ================================================================ functional suitability: what a schedule does

TEST_CASE("schedule: runs the actions of a screen - and nothing else - and is in the operation log")
{
    Runtime rt;
    TempDir t;
    rt.tag("run", TagType::Bool, 0);
    rt.tag("count", TagType::Int, 5);
    const json ocfg = json::parse(R"({ "path": "op.db" })");
    REQUIRE(operation_log().load(&ocfg, t.dir.generic_string()));
    const json defs = json::parse(R"([
        { "name": "start", "at": "08:00", "do": [ { "action": "set", "tag": "run", "value": true }, { "action": "inc", "tag": "count", "by": 2 } ] },
        { "name": "odd",   "at": "08:00", "do": [ { "action": "set", "tag": "no_such_tag", "value": 1 }, { "action": "launch", "program": "calc" }, { "action": "toggle", "tag": "run" } ] } ])");
    REQUIRE(scheduler().load(&defs, nullptr, t.store()) == 2);
    const int64_t t8 = at(Y, MO, MON, 8, 0);
    scheduler().check(t8 - 1);
    CHECK(tags().find("run")->get_number() == 0);
    const std::vector<std::string> ran = scheduler().check(t8);
    CHECK(ran == std::vector<std::string>{ "start", "odd" });
    CHECK(tags().find("count")->get_number() == 7);
    CHECK(tags().find("run")->get_number() == 0);                // set by "start", toggled back by "odd": the unknown actions were left out
    bool logged = false;
    for(const Operation& o : operation_log().query()) if(o.kind == "schedule" && o.target == "start" && o.new_value == "08:00") logged = true;
    CHECK(logged);
    CHECK(ui().run_actions(json::parse(R"({ "action": "set", "tag": "count", "value": 1 })")) == 1);
    CHECK(tags().find("count")->get_number() == 1);
    CHECK(ui().run_actions(json::parse("5")) == 0);
    CHECK(ui().run_actions(json::parse(R"([ "x", null, { "action": "nothing" } ])")) == 0);
    scheduler().clear();
    operation_log().clear();
}

// ================================================================ usability: the page of the device

TEST_CASE("System > Schedule: the schedules with their next run; time, days and on / off are changed and saved")
{
    Runtime rt;
    TempDir t;
    rt.tag("run", TagType::Bool, 0);
    const json defs = json::parse("[" + J(R"("name": "start", "at": "08:00", "days": "weekdays")") + "," + J(R"("name": "night", "at": "22:15", "enabled": false)") + "," +
                                  J(R"("name": "service", "at": "2031-12-01 09:00")") + "]");
    REQUIRE(scheduler().load(&defs, nullptr, t.store()) == 3);
    SystemSettings& sys = system_settings();
    sys.add_page(make_schedule_page());
    sys.create_tags();
    auto text = [](const char* key) { return tags().find(std::string("sys.schedule.") + key)->get().format(); };
    auto on = [](const char* key) { return tags().find(std::string("sys.schedule.") + key)->get_number() != 0; };
    auto set = [](const char* key, double v) { tags().find(std::string("sys.schedule.") + key)->set_number(v, nullptr); };

    CHECK(text("n1") == "start");
    CHECK(text("t1") == "08:00  weekdays");
    CHECK(text("x1").size() == 11);                              // the next run: MM-DD HH:MM
    CHECK(text("n2") == "night");
    CHECK(text("x2") == "off");
    CHECK(text("t3") == "2031-12-01 09:00");
    CHECK(text("x3") == "12-01 09:00");
    CHECK_FALSE(on("r4_used"));
    CHECK_FALSE(on("selected"));
    CHECK_FALSE(on("more"));

    sys.command("schedule", "save");                             // nothing picked
    CHECK(text("range") == "Pick a schedule first.");
    sys.command("schedule", "select", "1");
    CHECK(on("selected"));
    CHECK(on("daily"));
    CHECK(text("sel_name") == "start");
    CHECK(text("at") == "08:00");
    CHECK(on("d1"));                                             // Monday .. Friday
    CHECK(on("d5"));
    CHECK_FALSE(on("d6"));
    CHECK_FALSE(on("d0"));
    CHECK(on("enabled"));
    // the operator turns the rollers to 07:30, adds Saturday and saves
    tags().find("sys.schedule.at")->set_string("07:30", nullptr);
    set("d6", 1);
    sys.command("schedule", "save");
    CHECK(text("range").find("Saved: start  07:30") == 0);
    CHECK_FALSE(on("selected"));
    CHECK(schedule_at_text(scheduler().items()[0]) == "07:30");
    CHECK(scheduler().items()[0].days == 0x7E);
    CHECK(text("t1") == "07:30  mon tue wed thu fri sat");
    CHECK(t.local()["schedules"]["start"]["at"] == "07:30");
    // every day switched off: refused, the form stays
    sys.command("schedule", "select", "1");
    for(const char* d : { "d0", "d1", "d2", "d3", "d4", "d5", "d6" }) set(d, 0);
    sys.command("schedule", "save");
    CHECK(text("range").find("day") != std::string::npos);
    CHECK(on("selected"));
    CHECK(scheduler().items()[0].days == 0x7E);
    sys.command("schedule", "close");
    CHECK_FALSE(on("selected"));
    // switched on; a schedule with a date keeps being one
    sys.command("schedule", "select", "2");
    CHECK_FALSE(on("enabled"));
    set("enabled", 1);
    sys.command("schedule", "save");
    CHECK(scheduler().items()[1].enabled);
    CHECK(text("x2") != "off");
    sys.command("schedule", "select", "3");
    CHECK(on("once"));
    CHECK_FALSE(on("daily"));
    tags().find("sys.schedule.at")->set_string("10:00", nullptr);
    sys.command("schedule", "save");
    CHECK(text("range").find("runs once") != std::string::npos);
    tags().find("sys.schedule.at")->set_string("2031-12-02 10:00", nullptr);
    sys.command("schedule", "save");
    CHECK(schedule_at_text(scheduler().items()[2]) == "2031-12-02 10:00");
    // rows that are not there, commands that are none
    sys.command("schedule", "select", "6");
    sys.command("schedule", "select", "-1");
    sys.command("schedule", "select", "x");
    sys.command("schedule", "explode");
    CHECK_FALSE(on("selected"));
    // the page is built and shown (the small display: the form with its rollers and seven day buttons)
    for(const json& scr : sys.screens(480, 320)) REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen(SystemSettings::page_screen("schedule"), "none"));
    rt.advance(300);
    ui().unload_all();
    scheduler().clear();
}

// ================================================================ maintainability: the checker

TEST_CASE("schedule: --check finds a wrong time, wrong days, an action that is none and an unknown tag")
{
    TempDir t;
    fs::create_directories(t.dir / "screens");
    auto problems = [&](const std::string& schedules) {
        { std::ofstream f(t.dir / "project.json", std::ios::binary);
          f << "{ \"start_screen\": \"s\", \"tags\": [ { \"name\": \"run\", \"type\": \"bool\" } ], \"schedules\": " << schedules << ", \"screens\": [\"screens/s.json\"] }"; }
        { std::ofstream f(t.dir / "screens" / "s.json", std::ios::binary); f << "{ \"name\": \"s\", \"children\": [] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((t.dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems("[" + J(R"("name": "a", "at": "08:00", "days": ["mon", "fri"], "enabled": false, "catch_up": true)") + "," +
                   R"({ "name": "b", "at": "2026-12-01 09:00", "do": [ { "action": "goto", "screen": "s" }, { "action": "toggle", "tag": "run" } ] })" + "]") == "");
    CHECK(problems("{}").find("schedules: a list is needed") != std::string::npos);
    const std::string out = problems(R"([ { "name": "a", "at": "8", "do": {} }, { "name": "b", "at": "08:00", "days": ["someday"], "do": {} },
        { "name": "c", "at": "08:00", "do": { "action": "set", "tag": "nope", "value": 1 } }, { "name": "c", "at": "09:00", "do": { "action": "launch" } },
        { "name": "d", "at": "08:00", "do": { "action": "goto", "screen": "gone" }, "every": 5 }, { "name": "e", "at": "2026-12-01 09:00", "days": "weekdays", "do": { "action": "toggle", "tag": "run" } }, 5 ])");
    CHECK(out.find("schedules[0] (a): \"at\" is") != std::string::npos);
    CHECK(out.find("schedules[1] (b): \"days\" is") != std::string::npos);
    CHECK(out.find("schedules[2] (c): do") != std::string::npos);
    CHECK(out.find("unknown tag \"nope\"") != std::string::npos);
    CHECK(out.find("schedules[3] (c): the name is used twice") != std::string::npos);
    CHECK(out.find("schedules[3] (c): do") != std::string::npos);                       // an action that is none
    CHECK(out.find("schedules[4] (d): unknown key \"every\"") != std::string::npos);
    CHECK(out.find("unknown screen \"gone\"") != std::string::npos);
    CHECK(out.find("schedules[5] (e): \"days\" has no meaning") != std::string::npos);
    CHECK(out.find("schedules[6]: a schedule is") != std::string::npos);
}
