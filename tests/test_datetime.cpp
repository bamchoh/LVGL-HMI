// The "datetime_input" widget and its dates (datetime_view.hpp). Grouped by ISO/IEC 25010
// characteristic (issue #69).
#include <doctest.h>
#include "check.hpp"
#include "datetime_view.hpp"
#include "lvgl_fixture.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

const DateTimeMode DT = DateTimeMode::DateAndTime, D = DateTimeMode::Date, T = DateTimeMode::Time;

lv_obj_t* show(const char* screen, const std::string& widgets)
{
    lv_obj_t* s = ui().build_screen(json::parse(std::string("{ \"name\": \"") + screen + "\", \"style\": { \"pad\": 0 }, \"children\": [ " + widgets + " ] }"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen(screen, "none"));
    return s;
}
// what a roller shows
std::string sel(lv_obj_t* w, const char* part)
{
    lv_obj_t* r = datetime_roller(w, part);
    if(!r) return "(none)";
    char buf[16] = "";
    lv_roller_get_selected_str(r, buf, sizeof(buf));
    return buf;
}
// the operator turns a roller to this row
void turn(lv_obj_t* w, const char* part, uint32_t index)
{
    lv_obj_t* r = datetime_roller(w, part);
    REQUIRE(r);
    lv_roller_set_selected(r, index, LV_ANIM_OFF);
    lv_obj_send_event(r, LV_EVENT_VALUE_CHANGED, nullptr);
}
std::string str(const char* tag) { return tags().find(tag)->get().format(); }

} // namespace

// ================================================================ functional suitability: dates

TEST_CASE("datetime: months have 28 to 31 days, leap years as the calendar has them")
{
    CHECK(days_in_month(2026, 1) == 31);
    CHECK(days_in_month(2026, 4) == 30);
    CHECK(days_in_month(2026, 2) == 28);
    CHECK(days_in_month(2024, 2) == 29);
    CHECK(days_in_month(2000, 2) == 29);                       // divisible by 400
    CHECK(days_in_month(1900, 2) == 28);                       // by 100, not by 400
    CHECK(days_in_month(2100, 2) == 28);
    CHECK(days_in_month(2026, 12) == 31);
    CHECK(days_in_month(2026, 0) == 0);
    CHECK(days_in_month(2026, 13) == 0);
    CHECK(datetime_valid({ 2024, 2, 29, 23, 59, 59 }));
    CHECK_FALSE(datetime_valid({ 2025, 2, 29, 0, 0, 0 }));
    CHECK_FALSE(datetime_valid({ 2026, 4, 31, 0, 0, 0 }));
    CHECK_FALSE(datetime_valid({ 2026, 1, 1, 24, 0, 0 }));
    CHECK_FALSE(datetime_valid({ 2026, 1, 1, 0, 60, 0 }));
    CHECK_FALSE(datetime_valid({ 2026, 1, 1, 0, 0, 60 }));
    CHECK_FALSE(datetime_valid({ 1969, 12, 31, 0, 0, 0 }));
    CHECK_FALSE(datetime_valid({ 10000, 1, 1, 0, 0, 0 }));
    CHECK_FALSE(datetime_valid({ 2026, 1, 0, 0, 0, 0 }));
}

TEST_CASE("datetime: texts are read and written in one form; what is no date is refused")
{
    auto p = datetime_parse("2026-11-21 13:05", DT);
    REQUIRE(p);
    CHECK(*p == DateTime{ 2026, 11, 21, 13, 5, 0 });
    CHECK(*datetime_parse("2026-11-21 13:05:09", DT) == DateTime{ 2026, 11, 21, 13, 5, 9 });
    CHECK(*datetime_parse("  2026-11-21T13:05:09 ", DT) == DateTime{ 2026, 11, 21, 13, 5, 9 });
    CHECK(*datetime_parse("2026/1/2 3:04", DT) == DateTime{ 2026, 1, 2, 3, 4, 0 });
    CHECK(*datetime_parse("2026-11-21", DT) == DateTime{ 2026, 11, 21, 0, 0, 0 });       // no time: midnight
    CHECK(*datetime_parse("2026-11-21", D) == DateTime{ 2026, 11, 21, 0, 0, 0 });
    CHECK(*datetime_parse("2026-11-21 13:05", D) == DateTime{ 2026, 11, 21, 0, 0, 0 });  // a date tag that holds a time too
    CHECK(*datetime_parse("13:05", T) == DateTime{ 2000, 1, 1, 13, 5, 0 });
    CHECK(*datetime_parse("23:59:59", T) == DateTime{ 2000, 1, 1, 23, 59, 59 });

    CHECK(datetime_format({ 2026, 1, 2, 3, 4, 5 }, DT, false) == "2026-01-02 03:04");
    CHECK(datetime_format({ 2026, 1, 2, 3, 4, 5 }, DT, true) == "2026-01-02 03:04:05");
    CHECK(datetime_format({ 2026, 1, 2, 3, 4, 5 }, D, true) == "2026-01-02");
    CHECK(datetime_format({ 2026, 1, 2, 3, 4, 5 }, T, false) == "03:04");
    CHECK(datetime_format({ 2026, 1, 2, 3, 4, 5 }, T, true) == "03:04:05");

    for(const char* bad : { "", " ", "2026-02-30", "2025-02-29", "2026-13-01", "2026-00-10", "2026-04-31 00:00", "2026-11-21 24:00", "2026-11-21 12:60",
                            "2026-11-21 12:30:60", "26-11-21", "20261121", "2026-11-21 12", "2026-11-21 12:", "2026-11-21x12:30", "2026-11/21", "+2026-11-21",
                            "-2026-11-21", "2026-11-21 12:30 PM", "2026-11-21 12:30:15.5", "2026-111-21", "12026-11-21", "now", "0000-00-00 00:00", "1969-12-31",
                            "2026-11-21\n12:30", "2026-11-21 12:30; DROP", "%s%s%n", "\xff\xfe" }) {
        CAPTURE(bad);
        CHECK_FALSE(datetime_parse(bad, DT));
    }
    CHECK_FALSE(datetime_parse(std::string(5000, '9'), DT));
    CHECK_FALSE(datetime_parse("2026-11-21 13:05" + std::string(100, ' ') + "x", DT));
    for(const char* bad : { "24:00", "12:60", "12", "12:", ":30", "1:2:3:4", "2026-11-21", "12:30:99", "-1:30", "12.5:00" }) {
        CAPTURE(bad);
        CHECK_FALSE(datetime_parse(bad, T));
    }
    CHECK_FALSE(datetime_parse("13:05", D));
}

TEST_CASE("datetime: numbers are Unix seconds in local time, or seconds since midnight")
{
    CHECK(datetime_to_number({ 2000, 1, 1, 12, 34, 56 }, T) == 45296);
    CHECK(*datetime_from_number(45296, T) == DateTime{ 2000, 1, 1, 12, 34, 56 });
    CHECK(*datetime_from_number(0, T) == DateTime{ 2000, 1, 1, 0, 0, 0 });
    CHECK(*datetime_from_number(86399.9, T) == DateTime{ 2000, 1, 1, 23, 59, 59 });
    CHECK_FALSE(datetime_from_number(86400, T));
    // there and back, whatever the time zone of the machine (dates away from a change of summer time)
    for(const DateTime& t : { DateTime{ 2026, 11, 21, 13, 5, 9 }, DateTime{ 2024, 2, 29, 0, 0, 0 }, DateTime{ 2038, 1, 19, 12, 0, 0 }, DateTime{ 1999, 12, 31, 23, 59, 59 },
                              DateTime{ 2040, 6, 15, 6, 30, 0 } }) {
        const int64_t n = datetime_to_number(t, DT);
        CHECK(n > 0);
        auto back = datetime_from_number(static_cast<double>(n), DT);
        REQUIRE(back);
        CHECK(*back == t);
        // a date is its midnight
        auto day = datetime_from_number(static_cast<double>(n), D);
        REQUIRE(day);
        CHECK(*day == DateTime{ t.year, t.month, t.day, 0, 0, 0 });
        CHECK(datetime_to_number(t, D) == datetime_to_number({ t.year, t.month, t.day, 0, 0, 0 }, DT));
    }
    for(double bad : { -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1e18, 253402300800.0 * 10 }) CHECK_FALSE(datetime_from_number(bad, DT));
}

// ================================================================ functional suitability: the widget

TEST_CASE("datetime_input: the rollers show the tag, turning one writes it; the day follows the month")
{
    Runtime rt;
    rt.tag("at", std::string("2024-01-31 13:05"));
    lv_obj_t* s = show("dt", R"({ "type": "datetime_input", "bind": "at", "year_min": 2020, "year_max": 2030 })");
    lv_obj_t* w = lv_obj_get_child(s, 0);
    rt.advance(300);
    CHECK(sel(w, "year") == "2024");
    CHECK(sel(w, "month") == "01");
    CHECK(sel(w, "day") == "31");
    CHECK(sel(w, "hour") == "13");
    CHECK(sel(w, "minute") == "05");
    CHECK(datetime_roller(w, "second") == nullptr);
    CHECK(lv_roller_get_option_count(datetime_roller(w, "year")) == 11);
    CHECK(lv_roller_get_option_count(datetime_roller(w, "day")) == 31);

    turn(w, "minute", 30);
    CHECK(str("at") == "2024-01-31 13:30");
    turn(w, "hour", 0);
    CHECK(str("at") == "2024-01-31 00:30");
    // January 31 -> February: 2024 is a leap year, the 29th is the last day
    turn(w, "month", 1);
    CHECK(str("at") == "2024-02-29 00:30");
    CHECK(sel(w, "day") == "29");
    CHECK(lv_roller_get_option_count(datetime_roller(w, "day")) == 29);
    // ... and 2025 is not
    turn(w, "year", 5);
    CHECK(str("at") == "2025-02-28 00:30");
    CHECK(lv_roller_get_option_count(datetime_roller(w, "day")) == 28);
    turn(w, "month", 3);                                         // April
    CHECK(lv_roller_get_option_count(datetime_roller(w, "day")) == 30);
    turn(w, "day", 29);
    CHECK(str("at") == "2025-04-30 00:30");
    turn(w, "month", 11);
    CHECK(str("at") == "2025-12-30 00:30");
    CHECK(lv_roller_get_option_count(datetime_roller(w, "day")) == 31);

    // the tag changes elsewhere (a script, a PLC): the rollers follow
    tags().find("at")->set_string("2027-07-04 23:59:58", nullptr);
    rt.advance(300);
    CHECK(sel(w, "year") == "2027");
    CHECK(sel(w, "month") == "07");
    CHECK(sel(w, "day") == "04");
    CHECK(sel(w, "hour") == "23");
    CHECK(sel(w, "minute") == "59");
    CHECK(str("at") == "2027-07-04 23:59:58");                   // looking at it writes nothing
    // a year outside the years on offer: they grow to hold it
    tags().find("at")->set_string("2035-01-01 00:00", nullptr);
    rt.advance(300);
    CHECK(sel(w, "year") == "2035");
    CHECK(lv_roller_get_option_count(datetime_roller(w, "year")) == 16);
    // what is no date in the tag: the rollers stay, nothing is written
    tags().find("at")->set_string("not yet", nullptr);
    rt.advance(300);
    CHECK(sel(w, "year") == "2035");
    CHECK(str("at") == "not yet");
    turn(w, "day", 9);                                           // the operator enters one: now it is a date
    CHECK(str("at") == "2035-01-10 00:00");
    ui().unload_all();
}

TEST_CASE("datetime_input: date only, time with seconds, number tags")
{
    Runtime rt;
    rt.tag("day", std::string("2026-11-21"));
    rt.tag("tm", TagType::Int, 45296);
    rt.tag("at", TagType::Float, static_cast<double>(datetime_to_number({ 2026, 11, 21, 13, 5, 0 }, DT)));
    rt.tag("d", TagType::Int, static_cast<double>(datetime_to_number({ 2026, 11, 21, 0, 0, 0 }, D)));
    lv_obj_t* s = show("dt", R"({ "type": "datetime_input", "bind": "day", "mode": "date" },
                               { "type": "datetime_input", "bind": { "value": "tm" }, "mode": "time", "seconds": true },
                               { "type": "datetime_input", "bind": { "value": { "tag": "at" } } },
                               { "type": "datetime_input", "bind": "d", "mode": "date", "seconds": true })");
    rt.advance(300);
    lv_obj_t* day = lv_obj_get_child(s, 0);
    CHECK(datetime_roller(day, "hour") == nullptr);
    CHECK(sel(day, "day") == "21");
    turn(day, "day", 0);
    CHECK(str("day") == "2026-11-01");

    lv_obj_t* tm = lv_obj_get_child(s, 1);
    CHECK(datetime_roller(tm, "year") == nullptr);
    CHECK(sel(tm, "hour") == "12");
    CHECK(sel(tm, "minute") == "34");
    CHECK(sel(tm, "second") == "56");
    turn(tm, "second", 0);
    CHECK(tags().find("tm")->get_number() == 45240);
    turn(tm, "hour", 23);
    CHECK(tags().find("tm")->get_number() == 23 * 3600 + 34 * 60);

    lv_obj_t* at = lv_obj_get_child(s, 2);
    CHECK(sel(at, "hour") == "13");
    turn(at, "minute", 45);
    CHECK(tags().find("at")->get_number() == static_cast<double>(datetime_to_number({ 2026, 11, 21, 13, 45, 0 }, DT)));

    lv_obj_t* d = lv_obj_get_child(s, 3);
    CHECK(datetime_roller(d, "second") == nullptr);              // a date has no seconds
    turn(d, "month", 0);
    CHECK(tags().find("d")->get_number() == static_cast<double>(datetime_to_number({ 2026, 1, 21, 0, 0, 0 }, D)));
    ui().unload_all();
}

// ================================================================ usability

TEST_CASE("datetime_input: date and time fit on a 480 px display, every roller is wide enough for a finger")
{
    Runtime rt;
    rt.tag("at", std::string("2026-11-21 13:05:09"));
    lv_obj_t* s = show("dt", R"({ "type": "datetime_input", "bind": "at", "seconds": true }, { "type": "datetime_input", "bind": "at", "mode": "time" })");
    rt.advance(300);
    lv_obj_t* w = lv_obj_get_child(s, 0);
    lv_obj_update_layout(w);
    CHECK(lv_obj_get_width(w) <= 472);                           // the widest form: date, time and seconds
    for(const char* part : { "year", "month", "day", "hour", "minute", "second" }) {
        lv_obj_t* r = datetime_roller(w, part);
        REQUIRE(r);
        CHECK(lv_obj_get_width(r) >= 56);
        CHECK(lv_obj_get_height(r) >= 90);                       // three rows: the one chosen and its neighbours
    }
    CHECK(lv_obj_get_width(lv_obj_get_child(s, 1)) < 160);
    ui().unload_all();
}

// ================================================================ reliability

TEST_CASE("datetime_input: no tag, an unknown tag, a bool tag, absurd settings and values")
{
    Runtime rt;
    rt.tag("flag", TagType::Bool);
    rt.tag("n", TagType::Float, -5);
    rt.tag("text", std::string(5000, 'x'));
    lv_obj_t* s = show("dt", R"({ "type": "datetime_input" }, { "type": "datetime_input", "bind": "nope" }, { "type": "datetime_input", "bind": "flag" },
                               { "type": "datetime_input", "bind": "n" }, { "type": "datetime_input", "bind": "text" },
                               { "type": "datetime_input", "mode": "sometimes", "year_min": -5, "year_max": 99999999 },
                               { "type": "datetime_input", "year_min": 2030, "year_max": 2020 }, { "type": "datetime_input", "bind": 5, "seconds": "yes" },
                               { "type": "datetime_input", "bind": { "value": [] } }, { "type": "datetime_input", "bind": "n", "mode": "time" })");
    rt.advance(300);
    for(uint32_t i = 0; i < lv_obj_get_child_count(s); i++) {
        lv_obj_t* w = lv_obj_get_child(s, static_cast<int32_t>(i));
        if(lv_obj_t* r = datetime_roller(w, "day")) { lv_roller_set_selected(r, 3, LV_ANIM_OFF); lv_obj_send_event(r, LV_EVENT_VALUE_CHANGED, nullptr); }
        if(lv_obj_t* r = datetime_roller(w, "hour")) { lv_roller_set_selected(r, 7, LV_ANIM_OFF); lv_obj_send_event(r, LV_EVENT_VALUE_CHANGED, nullptr); }
    }
    CHECK(tags().find("flag")->get_number() == 0);               // a bool is not a time: left alone
    CHECK(lv_roller_get_option_count(datetime_roller(lv_obj_get_child(s, 5), "year")) <= 201);
    CHECK(lv_roller_get_option_count(datetime_roller(lv_obj_get_child(s, 6), "year")) >= 1);
    // a number tag takes what was entered; a value that is no time before that did no harm
    for(double x : { std::numeric_limits<double>::quiet_NaN(), 1e300, -1e300, std::numeric_limits<double>::infinity() }) { tags().find("n")->set_number(x, nullptr); rt.advance(300); }
    lv_obj_t* tm = lv_obj_get_child(s, 9);
    turn(tm, "hour", 1);
    turn(tm, "minute", 2);
    CHECK(tags().find("n")->get_number() == 3720);
    CHECK(datetime_roller(s, "year") == nullptr);                // (not a datetime_input)
    CHECK(datetime_roller(nullptr, "year") == nullptr);
    CHECK(datetime_roller(tm, "decade") == nullptr);
    ui().unload_all();
}

// ================================================================ security

TEST_CASE("datetime_input: a user below the level of the widget changes nothing; an entry is in the operation log")
{
    Runtime rt;
    static std::atomic<int> counter{ 0 };
    const fs::path dir = fs::temp_directory_path() / ("hml_datetime_test_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "project.json", std::ios::binary);
        f << R"json({ "start_screen": "main", "tags": [ { "name": "at", "type": "string", "init": "2026-11-21 13:05" }, { "name": "free", "type": "string", "init": "10:00" } ],
            "users": { "list": [ { "name": "op", "level": 1, "pin": "1111" }, { "name": "admin", "level": 2, "pin": "0000" } ] },
            "operation_log": { "path": "op.db" },
            "screens": [ { "name": "main", "children": [ { "type": "datetime_input", "bind": "at", "level": 2 },
                                                         { "type": "datetime_input", "bind": "free", "mode": "time" },
                                                         { "type": "panel", "level": 2, "level_deny": "login", "children": [ { "type": "datetime_input", "bind": "at" } ] } ] } ] })json";
    }
    Project p;
    REQUIRE(p.load((dir / "project.json").generic_string()));
    p.start_logs();
    REQUIRE(p.build_screens() == 1);
    REQUIRE(ui().show_screen("main", "none"));
    rt.advance(300);
    lv_obj_t* scr = lv_screen_active();
    lv_obj_t* guarded = lv_obj_get_child(scr, 0);
    lv_obj_t* open = lv_obj_get_child(scr, 1);
    lv_obj_t* inside = lv_obj_get_child(lv_obj_get_child(scr, 2), 0);

    // nobody is logged in: the rollers are shown as switched off, and turning one is undone
    CHECK(lv_obj_has_state(guarded, LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(datetime_roller(guarded, "day"), LV_STATE_DISABLED));
    turn(guarded, "day", 0);
    CHECK(str("at") == "2026-11-21 13:05");
    CHECK(sel(guarded, "day") == "21");
    // ... also when the level is on what the widget is in
    turn(inside, "hour", 0);
    CHECK(str("at") == "2026-11-21 13:05");
    CHECK(sel(inside, "hour") == "13");
    CHECK_FALSE(ui().operable(inside));
    CHECK(ui().operable(open));
    // a widget without a level is everybody's
    turn(open, "hour", 11);
    CHECK(str("free") == "11:00");

    users().login("admin", "0000");
    REQUIRE(users().level() == 2);
    ui().refresh_access();
    rt.advance(300);
    CHECK_FALSE(lv_obj_has_state(datetime_roller(guarded, "day"), LV_STATE_DISABLED));
    turn(guarded, "day", 0);
    CHECK(str("at") == "2026-11-01 13:05");
    turn(inside, "hour", 0);
    CHECK(str("at") == "2026-11-01 00:05");

    // who entered what, with the value before (entries on the same tag within a second are one row: first old, last new)
    rt.advance(1500);
    bool free_logged = false, at_logged = false;
    for(const Operation& o : operation_log().query()) {
        if(o.kind == "write" && o.target == "free" && o.old_value == "10:00" && o.new_value == "11:00") free_logged = true;
        if(o.kind == "write" && o.target == "at" && o.old_value == "2026-11-21 13:05" && o.new_value == "2026-11-01 00:05" && o.user == "admin") at_logged = true;
    }
    CHECK(free_logged);
    CHECK(at_logged);

    ui().unload_all();
    p.stop_logs();
    p.unload();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ================================================================ maintainability: the checker knows the widget

TEST_CASE("datetime_input: --check takes its keys and finds a wrong one and an unknown tag")
{
    static std::atomic<int> counter{ 0 };
    const fs::path dir = fs::temp_directory_path() / ("hml_datetime_check_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir / "screens");
    auto problems = [&](const std::string& widgets) {
        { std::ofstream f(dir / "project.json", std::ios::binary);
          f << R"({ "start_screen": "s", "tags": [ { "name": "t", "type": "string" } ], "screens": ["screens/s.json"] })"; }
        { std::ofstream f(dir / "screens" / "s.json", std::ios::binary); f << "{ \"name\": \"s\", \"children\": [ " << widgets << " ] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems(R"({ "type": "datetime_input", "bind": "t", "mode": "date", "seconds": true, "year_min": 2020, "year_max": 2030 })") == "");
    const std::string out = problems(R"({ "type": "datetime_input", "bind": "nope", "format": "x" })");
    CHECK(out.find("children[0] (datetime_input): bind: unknown tag \"nope\"") != std::string::npos);
    CHECK(out.find("children[0] (datetime_input): unknown key \"format\"") != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
