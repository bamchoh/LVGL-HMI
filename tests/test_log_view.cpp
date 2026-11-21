// The logs on a screen: log_query (one page of a log as text) and the "log_view" widget.
// Grouped by ISO/IEC 25010 characteristic (issue #40).
#include <doctest.h>
#include "check.hpp"
#include "data_log.hpp"
#include "log_query.hpp"
#include "log_view.hpp"
#include "lvgl_fixture.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "system_log.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

// a folder with the three logs of the program open in it
struct Logs {
    fs::path dir;
    explicit Logs(bool system = true, bool operation = true)
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_logview_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
        if(system) {
            SystemLogConfig c;
            c.enabled = true;
            c.path = (dir / "system.db").generic_string();
            c.max_rows = 0;
            REQUIRE(system_log().open(c));
        }
        if(operation) {
            const json cfg = json::parse(R"({ "path": "operation.db", "max_rows": 0, "retention_days": 0 })");
            REQUIRE(operation_log().load(&cfg, dir.generic_string()));
        }
    }
    ~Logs()
    {
        log_view_wait_idle();
        system_log().close();
        operation_log().clear();
        data_log().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void op(int64_t ts, const char* user, const char* kind, const std::string& target, const char* old_v = "", const char* new_v = "", const char* screen = "main")
    {
        operation_log().db().add(ts, { std::string(user), std::string(screen), std::string(kind), target, std::string(old_v), std::string(new_v) });
    }
};

LogViewQuery sys_q(const char* text = "row ", int limit = 10, int offset = 0, int min_level = 0)
{
    LogViewQuery q;
    q.source = "system"; q.text = text; q.limit = limit; q.offset = offset; q.min_level = min_level;
    return q;
}
LogViewQuery op_q(const char* text = "", int limit = 10, int offset = 0)
{
    LogViewQuery q;
    q.source = "operation"; q.text = text; q.limit = limit; q.offset = offset;
    return q;
}

// the widget's parts (log_view.cpp builds them in this order)
lv_obj_t* bar_of(lv_obj_t* view) { return lv_obj_get_child(view, 0); }
lv_obj_t* table_of(lv_obj_t* view) { return lv_obj_get_child(view, 1); }
lv_obj_t* info_of(lv_obj_t* view) { return lv_obj_get_child(view, 2); }
lv_obj_t* from_end(lv_obj_t* bar, int back) { return lv_obj_get_child(bar, static_cast<int32_t>(lv_obj_get_child_count(bar)) - back); }
std::string page_text(lv_obj_t* view) { return lv_label_get_text(from_end(bar_of(view), 3)); }
std::string cell(lv_obj_t* view, uint32_t row, uint32_t col) { const char* t = lv_table_get_cell_value(table_of(view), row, col); return t ? t : ""; }
uint32_t rows_of(lv_obj_t* view) { return lv_table_get_row_count(table_of(view)); }

// let the timer ask, the worker answer and the timer draw
void settle(Runtime& rt, int rounds = 4)
{
    for(int i = 0; i < rounds; i++) { rt.advance(250); log_view_wait_idle(); }
    rt.advance(250);
}

lv_obj_t* show_view(const char* screen, const std::string& widget_json)
{
    lv_obj_t* s = ui().build_screen(json::parse(std::string("{ \"name\": \"") + screen + "\", \"children\": [ " + widget_json + " ] }"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen(screen, "none"));
    return lv_obj_get_child(s, 0);
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("log_view: the system log by pages, newest first, by level and by text")
{
    Runtime rt;
    Logs logs;
    for(int i = 1; i <= 25; i++) system_log().add(i % 5 == 0 ? LogLevel::Error : i % 2 == 0 ? LogLevel::Warn : LogLevel::Info, i % 3 == 0 ? "driver" : "script", "row " + std::to_string(i));

    LogViewPage p = log_view_fetch(sys_q());
    CHECK(p.problem == "");
    CHECK(p.total == 25);
    REQUIRE(p.rows.size() == 10);
    REQUIRE(p.rows[0].cells.size() == 4);                       // time level source message
    CHECK(p.rows[0].cells[3] == "row 25");
    CHECK(p.rows[0].cells[1] == "ERROR");
    CHECK(p.rows[0].level == 2);
    CHECK(p.rows[0].cells[0].size() == 14);                     // MM-DD HH:MM:SS
    CHECK(p.rows[1].cells[3] == "row 24");
    CHECK(p.rows[1].cells[1] == "WARN");
    CHECK(p.rows[1].cells[2] == "driver");
    CHECK(p.rows[9].cells[3] == "row 16");
    p = log_view_fetch(sys_q("row ", 10, 20));
    CHECK(p.total == 25);
    REQUIRE(p.rows.size() == 5);
    CHECK(p.rows[4].cells[3] == "row 1");
    CHECK(log_view_fetch(sys_q("row ", 10, 30)).rows.empty());

    CHECK(log_view_fetch(sys_q("row ", 50, 0, 1)).total == 15);  // warnings and errors (10 + 5)
    CHECK(log_view_fetch(sys_q("row ", 50, 0, 2)).total == 5);
    p = log_view_fetch(sys_q("row 7"));
    REQUIRE(p.total == 1);
    CHECK(p.rows[0].cells[3] == "row 7");
    CHECK(log_view_fetch(sys_q("nothing like this")).total == 0);

    // the columns asked for, in their order; a repeated message is one row with its count
    system_log().add(LogLevel::Warn, "driver", "row again");
    system_log().add(LogLevel::Warn, "driver", "row again");
    system_log().add(LogLevel::Warn, "driver", "row again");
    LogViewQuery q = sys_q("row again");
    q.columns = { "count", "message", "level" };
    p = log_view_fetch(q);
    REQUIRE(p.total == 1);
    CHECK(p.rows[0].cells == std::vector<std::string>{ "x3", "row again", "WARN" });

    CHECK(log_view_columns(sys_q()) == std::vector<std::string>{ "time", "level", "source", "message" });
    CHECK(log_view_known_columns("system", "").size() == 5);
    CHECK(log_view_known_columns("operation", "").size() == 7);
    CHECK(log_view_known_columns("nope", "").empty());
}

TEST_CASE("log_view: the operation log by pages, by text in any column, and the last N days")
{
    Runtime rt;
    Logs logs(false);
    const int64_t now = LogDb::now_ms(), day = 86400000LL;
    logs.op(now - 10 * day, "op", "write", "old_setpoint", "1", "2");
    logs.op(now - 3 * day, "maint", "write", "setpoint", "40", "45");
    logs.op(now - 2 * day, "maint", "screen", "trend");
    logs.op(now - 3600000, "op", "write", "run", "false", "true", "pump");
    logs.op(now - 60000, "admin", "system", "backup", "", "/media/usb/hml-backup");

    LogViewPage p = log_view_fetch(op_q());
    CHECK(p.total == 5);
    REQUIRE(p.rows.size() == 5);
    CHECK(p.rows[0].cells == std::vector<std::string>{ p.rows[0].cells[0], "admin", "system", "backup", "", "/media/usb/hml-backup" });
    CHECK(p.rows[4].cells[3] == "old_setpoint");
    CHECK(log_view_fetch(op_q("maint")).total == 2);              // the user
    CHECK(log_view_fetch(op_q("setpoint")).total == 2);           // the target
    CHECK(log_view_fetch(op_q("pump")).total == 1);               // the screen (not shown, still found)
    CHECK(log_view_fetch(op_q("hml-backup")).total == 1);         // the new value
    CHECK(log_view_fetch(op_q("MAINT")).total == 0);              // (as written)
    LogViewQuery q = op_q();
    q.days = 1;
    CHECK(log_view_fetch(q, now).total == 2);
    q.days = 5;
    CHECK(log_view_fetch(q, now).total == 4);
    q.days = 0;
    q.limit = 2; q.offset = 2;
    p = log_view_fetch(q);
    CHECK(p.total == 5);
    REQUIRE(p.rows.size() == 2);
    CHECK(p.rows[0].cells[3] == "trend");
    q.columns = { "screen", "target" };
    q.limit = 1; q.offset = 1;
    CHECK(log_view_fetch(q).rows[0].cells == std::vector<std::string>{ "pump", "run" });
}

TEST_CASE("log_view: a group of the data log, one column per tag")
{
    Runtime rt;
    Logs logs(false, false);
    rt.tag("temp", TagType::Float);
    rt.tag("run", TagType::Bool);
    rt.tag("name", std::string(""));
    const json cfg = json::parse(R"~({ "path": "data.db", "max_rows": 0, "groups": [ { "name": "g", "trigger": "manual", "tags": ["temp", "run", "name"] },
                                                                                      { "name": "other", "trigger": "manual", "tags": ["temp"] } ] })~");
    REQUIRE(data_log().load(&cfg, logs.dir.generic_string()) == 2);
    for(int i = 1; i <= 12; i++) {
        tags().find("temp")->set_number(20 + i * 0.5, nullptr);
        tags().find("run")->set_number(i % 2, nullptr);
        tags().find("name")->set_string("lot" + std::to_string(i), nullptr);
        REQUIRE(data_log().sample("g"));
    }
    REQUIRE(data_log().sample("other"));

    LogViewQuery q;
    q.source = "data"; q.group = "g"; q.limit = 5;
    CHECK(log_view_columns(q) == std::vector<std::string>{ "time", "temp", "run", "name" });
    LogViewPage p = log_view_fetch(q);
    CHECK(p.problem == "");
    CHECK(p.total == 12);                                         // (the other group does not count)
    REQUIRE(p.rows.size() == 5);
    CHECK(p.rows[0].cells == std::vector<std::string>{ p.rows[0].cells[0], "26", "false", "lot12" });
    CHECK(p.rows[1].cells[1] == "25.5");
    CHECK(p.rows[1].cells[2] == "true");
    q.columns = { "name", "nope", "time" };
    q.offset = 10;
    p = log_view_fetch(q);
    REQUIRE(p.rows.size() == 2);
    CHECK(p.rows[1].cells[0] == "lot1");
    CHECK(p.rows[1].cells[1] == "");                              // a tag the group does not record

    q.group = "missing";
    CHECK(log_view_fetch(q).problem.find("no group 'missing'") != std::string::npos);
    q.group = "";
    CHECK(log_view_fetch(q).problem.find("No group") != std::string::npos);
}

// ================================================================ reliability

TEST_CASE("log_view: a log that is not recorded, and a source that does not exist, say so")
{
    Runtime rt;
    system_log().close();
    operation_log().clear();
    data_log().clear();
    LogViewQuery q;
    for(const char* src : { "system", "operation", "data" }) {
        q.source = src;
        const LogViewPage p = log_view_fetch(q);
        CHECK(p.rows.empty());
        CHECK(p.total == 0);
        CHECK(p.problem.find("not recorded") != std::string::npos);
    }
    q.source = "alarm";
    CHECK(log_view_fetch(q).problem.find("system, operation or data") != std::string::npos);
    q.source = "";
    CHECK(log_view_fetch(q).problem != "");

    // the widget shows the reason instead of rows, and nothing breaks
    lv_obj_t* v = show_view("none", R"({ "type": "log_view", "w": 600, "h": 300, "source": "operation" })");
    settle(rt);
    CHECK(rows_of(v) == 1);                                        // the titles only
    CHECK(std::string(lv_label_get_text(info_of(v))).find("not recorded") != std::string::npos);
    CHECK_FALSE(lv_obj_has_flag(info_of(v), LV_OBJ_FLAG_HIDDEN));
    CHECK(page_text(v) == "0 - 0 / 0");
    ui().unload_all();
}

// ================================================================ security

TEST_CASE("log_view: what is typed into the filter is never SQL")
{
    Runtime rt;
    Logs logs;
    const int64_t now = LogDb::now_ms();
    for(int i = 0; i < 5; i++) { system_log().add(LogLevel::Info, "script", "row " + std::to_string(i)); logs.op(now - i, "op", "write", "t" + std::to_string(i)); }
    system_log().add(LogLevel::Info, "script", "row 100% done_");
    for(const char* evil : { "'; DROP TABLE log; --", "' OR '1'='1", "\" OR 1=1 --", "%", "_", "?1", "\\", "'); DELETE FROM operation; --", "row' --" }) {
        CAPTURE(evil);
        // the text itself is looked for, nothing else: "%" and "_" are in one of our rows (and are no wildcards), the rest nowhere
        const int64_t found = log_view_fetch(sys_q(evil)).total;
        if(std::string(evil) == "%" || std::string(evil) == "_") { CHECK(found >= 1); CHECK(found < log_view_fetch(sys_q("")).total); }
        else CHECK(found == 0);
        CHECK(log_view_fetch(op_q(evil)).total == 0);
    }
    // everything is still there
    CHECK(log_view_fetch(sys_q("row ")).total == 6);
    CHECK(log_view_fetch(op_q()).total == 5);
    // a source or a group from a project file is no SQL either
    LogViewQuery q;
    q.source = "data"; q.group = "g'; DROP TABLE sample; --";
    CHECK(log_view_fetch(q).problem != "");
    q.source = "system; DROP TABLE log";
    CHECK(log_view_fetch(q).problem != "");
    CHECK(log_view_fetch(sys_q("row ")).total == 6);
}

TEST_CASE("log_view: control characters, endless texts and broken UTF-8 in a log come out as plain cells")
{
    CHECK(log_view_text("plain") == "plain");
    CHECK(log_view_text("a\nb\tc\rd\x01" "e\x7f" "f") == "a b c d e f");
    CHECK(log_view_text("\xe6\xb8\xa9\xe5\xba\xa6 25\xc2\xb0" "C") == "\xe6\xb8\xa9\xe5\xba\xa6 25\xc2\xb0" "C");      // 温度 25°C
    CHECK(log_view_text("\xff\xfe" "x") == "??x");
    CHECK(log_view_text("a\xe6\xb8") == "a??");                    // cut in the middle of a character
    CHECK(log_view_text("\xc0\xaf") == "??");                      // an overlong '/'
    CHECK(log_view_text("\xf5\x80\x80\x80") == "????");            // beyond Unicode
    CHECK(log_view_text(std::string(5000, 'x')).size() == 203);
    CHECK(log_view_text(std::string(5000, 'x'), 10) == "xxxxxxxxxx...");
    CHECK(log_view_text("\xe6\xb8\xa9\xe6\xb8\xa9\xe6\xb8\xa9", 2) == "\xe6\xb8\xa9\xe6\xb8\xa9...");                    // counted in characters
    CHECK(log_view_text("") == "");
    CHECK(log_view_text(std::string("a\0b", 3)) == "a b");

    Runtime rt;
    Logs logs;
    const std::string nasty = std::string("row \x1b[31mred\n%s%n\xff ") + std::string(3000, 'y');
    system_log().add(LogLevel::Error, "script", nasty);
    logs.op(LogDb::now_ms(), "o\np", "write", nasty, "\xff", std::string(1000, 'z').c_str());
    LogViewPage p = log_view_fetch(sys_q("row "));
    REQUIRE(p.rows.size() == 1);
    const std::string& m = p.rows[0].cells[3];
    CHECK(m.find_first_of("\n\r\x1b") == std::string::npos);
    CHECK(m.find("%s%n") != std::string::npos);                    // (shown as text, formatted by nobody)
    CHECK(m.size() <= 203);
    p = log_view_fetch(op_q());
    REQUIRE(p.rows.size() == 1);
    CHECK(p.rows[0].cells[1] == "o p");
    CHECK(p.rows[0].cells[4] == "?");
    CHECK(p.rows[0].cells[5].size() == 67);

    // ... and the widget draws them
    lv_obj_t* v = show_view("nasty", R"({ "type": "log_view", "w": 700, "h": 300, "source": "system", "refresh_ms": 0 })");
    settle(rt);
    REQUIRE(rows_of(v) >= 2);
    CHECK(cell(v, 1, 1) == "ERROR");
    ui().unload_all();
}

TEST_CASE("log_view: a screen or a widget with a level keeps the log from the others")
{
    Runtime rt;
    Logs logs(false, false);
    {
        std::ofstream f(logs.dir / "project.json", std::ios::binary);
        f << R"json({ "start_screen": "main", "tags": [ { "name": "x", "type": "int" } ],
            "users": { "list": [ { "name": "op", "level": 1, "pin": "1111" }, { "name": "admin", "level": 2, "pin": "0000" } ] },
            "operation_log": { "path": "op.db" },
            "screens": [ { "name": "main", "children": [ { "type": "log_view", "source": "operation", "level": 2, "level_deny": "hide", "h": 200 } ] },
                         { "name": "audit", "level": 2, "children": [ { "type": "log_view", "source": "operation" } ] } ] })json";
    }
    Project p;
    REQUIRE(p.load((logs.dir / "project.json").generic_string()));
    REQUIRE(p.build_screens() == 2);
    REQUIRE(ui().show_screen("main", "none"));
    lv_obj_t* v = lv_obj_get_child(lv_screen_active(), 0);
    CHECK(lv_obj_has_flag(v, LV_OBJ_FLAG_HIDDEN));                 // nobody is logged in: the log on "main" is not shown
    CHECK_FALSE(ui().show_screen("audit", "none"));                // ... and the screen with the log asks for a login
    CHECK(ui().current_screen().value_or("") == "__login");
    ui().unload_all();
    p.stop_logs();
    p.unload();
}

// ================================================================ the widget (functional suitability, usability)

TEST_CASE("log_view widget: pages, the level, the filter tag; it follows the log while it is shown")
{
    Runtime rt;
    Logs logs;
    rt.tag("search", std::string(""));
    for(int i = 1; i <= 25; i++) system_log().add(i % 5 == 0 ? LogLevel::Error : i % 2 == 0 ? LogLevel::Warn : LogLevel::Info, "script", "row " + std::to_string(i));
    lv_obj_t* v = show_view("logs", R"({ "type": "log_view", "w": 760, "h": 400, "source": "system", "rows": 10, "filter_tag": "search", "refresh_ms": 500,
                                         "columns": ["time", "level", "message"], "labels": { "message": "Text", "empty": "Nothing" } })");
    tags().find("search")->set_string("row ", nullptr);
    settle(rt);
    lv_obj_t* bar = bar_of(v);
    lv_obj_t* prev = from_end(bar, 2);
    lv_obj_t* next = from_end(bar, 1);
    lv_obj_t* level = lv_obj_get_child(bar, 0);

    REQUIRE(rows_of(v) == 11);                                     // titles + 10
    CHECK(lv_table_get_column_count(table_of(v)) == 3);
    CHECK(cell(v, 0, 0) == "Time");
    CHECK(cell(v, 0, 2) == "Text");
    CHECK(cell(v, 1, 2) == "row 25");
    CHECK(cell(v, 1, 1) == "ERROR");
    CHECK(cell(v, 10, 2) == "row 16");
    CHECK(page_text(v) == "1 - 10 / 25");
    CHECK(lv_obj_has_state(prev, LV_STATE_DISABLED));
    CHECK_FALSE(lv_obj_has_state(next, LV_STATE_DISABLED));
    CHECK(lv_obj_has_flag(info_of(v), LV_OBJ_FLAG_HIDDEN));
    // the buttons are big enough for a finger
    lv_obj_update_layout(v);
    CHECK(lv_obj_get_height(next) >= 40);
    CHECK(lv_obj_get_width(next) >= 48);
    CHECK(lv_obj_get_height(level) >= 40);

    lv_obj_send_event(next, LV_EVENT_CLICKED, nullptr);
    settle(rt);
    CHECK(page_text(v) == "11 - 20 / 25");
    CHECK(cell(v, 1, 2) == "row 15");
    lv_obj_send_event(next, LV_EVENT_CLICKED, nullptr);
    settle(rt);
    CHECK(page_text(v) == "21 - 25 / 25");
    CHECK(rows_of(v) == 6);
    CHECK(lv_obj_has_state(next, LV_STATE_DISABLED));
    lv_obj_send_event(next, LV_EVENT_CLICKED, nullptr);            // no further page
    settle(rt);
    CHECK(page_text(v) == "21 - 25 / 25");

    // the level: back to the first page
    lv_dropdown_set_selected(level, 2);
    lv_obj_send_event(level, LV_EVENT_VALUE_CHANGED, nullptr);
    settle(rt);
    CHECK(page_text(v) == "1 - 5 / 5");
    CHECK(cell(v, 5, 2) == "row 5");
    lv_dropdown_set_selected(level, 0);
    lv_obj_send_event(level, LV_EVENT_VALUE_CHANGED, nullptr);
    settle(rt);
    lv_obj_send_event(next, LV_EVENT_CLICKED, nullptr);
    settle(rt);
    CHECK(page_text(v) == "11 - 20 / 25");

    // the filter tag (what an input field on the screen writes): first page of what matches; nothing matches
    tags().find("search")->set_string("row 2", nullptr);
    settle(rt);
    CHECK(page_text(v) == "1 - 7 / 7");                            // 2, 20 .. 25
    tags().find("search")->set_string("no such text", nullptr);
    settle(rt);
    CHECK(page_text(v) == "0 - 0 / 0");
    CHECK(rows_of(v) == 1);
    CHECK_FALSE(lv_obj_has_flag(info_of(v), LV_OBJ_FLAG_HIDDEN));
    CHECK(std::string(lv_label_get_text(info_of(v))) == "Nothing");
    tags().find("search")->set_string("row ", nullptr);
    settle(rt);
    CHECK(page_text(v) == "1 - 10 / 25");

    // a new row appears by itself on the first page
    system_log().add(LogLevel::Error, "script", "row 26");
    settle(rt, 6);
    CHECK(page_text(v) == "1 - 10 / 26");
    CHECK(cell(v, 1, 2) == "row 26");

    // while another screen is shown nothing is read; back on the screen it is up to date
    REQUIRE(ui().build_screen(json::parse(R"({ "name": "other", "children": [] })"), ""));
    REQUIRE(ui().show_screen("other", "none"));
    settle(rt);
    system_log().add(LogLevel::Error, "script", "row 27");
    settle(rt, 6);
    CHECK(cell(v, 1, 2) == "row 26");
    REQUIRE(ui().show_screen("logs", "none"));
    settle(rt);
    CHECK(cell(v, 1, 2) == "row 27");

    // deleting the widget while a question is on its way does no harm
    system_log().add(LogLevel::Info, "script", "row 28");
    rt.advance(600);
    ui().unload_all();
    log_view_wait_idle();
    rt.advance(300);
}

TEST_CASE("log_view widget: unknown columns are left out, a narrow widget keeps the room for the message")
{
    Runtime rt;
    Logs logs;
    system_log().add(LogLevel::Warn, "driver", "row narrow");
    logs.op(LogDb::now_ms(), "op", "write", "setpoint", "1", "2");
    lv_obj_t* s = ui().build_screen(json::parse(R"({ "name": "two", "children": [
        { "type": "log_view", "x": 0, "y": 0, "w": 300, "h": 110, "source": "system", "filter": false },
        { "type": "log_view", "x": 0, "y": 120, "w": 300, "h": 110, "source": "operation", "columns": "target, nope, new", "rows": 500 },
        { "type": "log_view", "x": 0, "y": 0, "w": 472, "h": 100, "source": "operation" } ] })"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen("two", "none"));
    settle(rt);
    lv_obj_t* sys = lv_obj_get_child(s, 0);
    lv_obj_t* op = lv_obj_get_child(s, 1);
    CHECK(lv_obj_get_child_count(bar_of(sys)) == 3);               // no dropdown for the level
    CHECK(lv_table_get_column_count(table_of(sys)) == 3);          // time level message (no source: the widget is narrow)
    CHECK(cell(sys, 0, 2) == "Message");
    CHECK(lv_table_get_column_width(table_of(sys), 2) >= 80);
    CHECK(lv_table_get_column_count(table_of(op)) == 2);
    CHECK(cell(op, 0, 0) == "Target");
    CHECK(cell(op, 1, 0) == "setpoint");
    CHECK(cell(op, 1, 1) == "2");
    // the operation log on a 480 px display: who, what and the new value, and all of it fits (found on the device)
    lv_obj_t* narrow = lv_obj_get_child(s, 2);
    REQUIRE(lv_table_get_column_count(table_of(narrow)) == 4);
    CHECK(cell(narrow, 0, 1) == "User");
    CHECK(cell(narrow, 0, 3) == "New");
    CHECK(cell(narrow, 1, 2) == "setpoint");
    int32_t sum = 0;
    for(uint32_t i = 0; i < 4; i++) sum += lv_table_get_column_width(table_of(narrow), i);
    CHECK(sum <= lv_obj_get_content_width(table_of(narrow)));
    CHECK(lv_table_get_column_width(table_of(narrow), 2) >= 100);
    ui().unload_all();
}

// ================================================================ performance efficiency

TEST_CASE("log_view: a page of a log of 100 000 rows comes quickly, also a late page and a filter")
{
    Runtime rt;
    Logs logs(false);
    const int64_t now = LogDb::now_ms();
    const int n = 100000;
    for(int i = 0; i < n; i++) {
        logs.op(now - (n - i) * 1000LL, i % 3 ? "op" : "maint", "write", "tag" + std::to_string(i % 500), "1", "2");
        if(i % 5000 == 4999) operation_log().db().flush();        // (the queue of the writer is not endless)
    }
    operation_log().db().flush();

    auto timed = [](const LogViewQuery& q, LogViewPage& p) { const uint32_t t0 = millis(); p = log_view_fetch(q); return millis() - t0; };
    LogViewPage p;
    const uint32_t first = timed(op_q("", 20, 0), p);
    CHECK(p.total == n);
    CHECK(p.rows.size() == 20);
    const uint32_t late = timed(op_q("", 20, 90000), p);
    CHECK(p.rows.size() == 20);
    const uint32_t filtered = timed(op_q("tag499", 20, 0), p);
    CHECK(p.total == n / 500);
    LogViewQuery q = op_q("", 20, 0);
    q.days = 1;
    const uint32_t recent = timed(q, p);
    CHECK(p.total > 80000);
    MESSAGE("100 000 rows: first page " << first << " ms, page 4500 " << late << " ms, filter " << filtered << " ms, last day " << recent << " ms (worker thread)");
    // (loose: a debug build on a busy CI machine; the UI thread does not wait for any of them)
    CHECK(first < 2000);
    CHECK(late < 2000);
    CHECK(filtered < 3000);
    CHECK(recent < 2000);

    // the UI thread stays free while the widget asks: a frame is never held up by the log
    lv_obj_t* v = show_view("big", R"({ "type": "log_view", "w": 760, "h": 400, "source": "operation", "rows": 20, "refresh_ms": 0 })");
    uint32_t worst = 0;
    for(int i = 0; i < 20; i++) { const uint32_t t0 = millis(); rt.advance(100); worst = std::max(worst, millis() - t0); }
    log_view_wait_idle();
    rt.advance(300);
    CHECK(page_text(v) == "1 - 20 / 100000");
    MESSAGE("the longest step of the UI thread while the page was read: " << worst << " ms");
    CHECK(worst < 500);
    ui().unload_all();
}

// ================================================================ maintainability: the checker knows the widget

TEST_CASE("log_view: --check finds a wrong source, group, column and filter tag")
{
    Logs logs(false, false);
    auto problems = [&](const std::string& extra, const std::string& widgets) {
        fs::create_directories(logs.dir / "screens");
        { std::ofstream f(logs.dir / "project.json", std::ios::binary);
          f << "{ \"start_screen\": \"s\", \"tags\": [ { \"name\": \"q\", \"type\": \"string\" }, { \"name\": \"t\", \"type\": \"float\" } ], " << extra
            << " \"screens\": [\"screens/s.json\"] }"; }
        { std::ofstream f(logs.dir / "screens" / "s.json", std::ios::binary); f << "{ \"name\": \"s\", \"children\": [ " << widgets << " ] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((logs.dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    const std::string with_logs = R"("system_log": {}, "operation_log": { "path": "op.db" }, "data_log": { "groups": [ { "name": "g", "tags": ["t"] } ] },)";
    CHECK(problems(with_logs, R"({ "type": "log_view" }, { "type": "log_view", "source": "operation", "columns": ["time", "user", "new"], "filter_tag": "q", "rows": 5,
                                   "days": 7, "refresh_ms": 0, "labels": { "empty": "-" }, "widths": { "time": 100 } },
                                 { "type": "log_view", "source": "data", "group": "g", "columns": ["time", "t"] },
                                 { "type": "log_view", "source": "system", "min_level": "warn", "filter": false, "columns": ["time", "message", "count"] })") == "");
    std::string out = problems(with_logs, R"({ "type": "log_view", "source": "alarm" }, { "type": "log_view", "source": "data" },
                                             { "type": "log_view", "source": "data", "group": "nope" }, { "type": "log_view", "source": "data", "group": "g", "columns": ["x"] },
                                             { "type": "log_view", "columns": ["time", "user"] }, { "type": "log_view", "source": "operation", "columns": ["level"] },
                                             { "type": "log_view", "filter_tag": "missing" }, { "type": "log_view", "colums": ["time"] })");
    CHECK(out.find("children[0] (log_view): source: \"alarm\" is not system, operation or data") != std::string::npos);
    CHECK(out.find("children[1] (log_view): no \"group\"") != std::string::npos);
    CHECK(out.find("children[2] (log_view): unknown data log group \"nope\"") != std::string::npos);
    CHECK(out.find("children[3] (log_view): data log group \"g\" does not record \"x\"") != std::string::npos);
    CHECK(out.find("children[4] (log_view): the system log has no column \"user\"") != std::string::npos);
    CHECK(out.find("children[5] (log_view): the operation log has no column \"level\"") != std::string::npos);
    CHECK(out.find("children[6] (log_view): filter_tag") != std::string::npos);
    CHECK(out.find("children[7] (log_view): unknown key \"colums\"") != std::string::npos);
    // a log that the project does not record cannot be shown
    out = problems("", R"({ "type": "log_view" }, { "type": "log_view", "source": "operation" })");
    CHECK(out.find("children[0] (log_view): the project has no \"system_log\"") != std::string::npos);
    CHECK(out.find("children[1] (log_view): the project has no \"operation_log\"") != std::string::npos);
    out = problems(R"("system_log": { "enabled": false },)", R"({ "type": "log_view" })");
    CHECK(out.find("the project has no \"system_log\"") != std::string::npos);
}
