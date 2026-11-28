// Alarm acknowledge, the state after a restart, the history, and the alarm widgets
// (alarm.hpp, alarm_view.hpp). Grouped by the ISO/IEC 25010 characteristic they check.
#include <doctest.h>
#include "alarm.hpp"
#include "alarm_view.hpp"
#include "lvgl_fixture.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "texts.hpp"
#include "users.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <random>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_alarm_view_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        project().stop_logs();
        users().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string base() const { return dir.generic_string(); }
};

json J(const char* text) { return json::parse(text); }
double tagnum(const char* name) { Tag* t = tags().find(name); return t ? t->get_number() : -999; }
std::string tagstr(const char* name) { Tag* t = tags().find(name); return t ? t->get().format() : std::string("<none>"); }

std::vector<std::string> events_of(const std::string& name)
{
    std::vector<std::string> out;
    for(const AlarmEvent& e : alarms().query()) if(e.name == name) out.push_back(e.event + (e.user.empty() ? "" : ":" + e.user));
    return out;
}

const char* kAlarms = R"~({ "items": [
    { "name": "hot",   "tag": "temp", "condition": "gt", "value": 50, "message": "Too hot", "severity": "critical" },
    { "name": "door",  "tag": "door", "message": "Door open", "severity": "warning" },
    { "name": "note",  "tag": "note", "message": "Note", "severity": "info", "ack": false } ] })~";

lv_obj_t* show(const char* name, const json& widgets)
{
    json scr = { { "name", name }, { "children", widgets } };
    lv_obj_t* s = ui().build_screen(scr, "/base");
    REQUIRE(s != nullptr);
    REQUIRE(ui().show_screen(name, "none"));
    return s;
}

// the rows of an alarm_list (its first child holds them)
lv_obj_t* body_of(lv_obj_t* list) { return lv_obj_get_child(list, 0); }
std::string text_of_row(lv_obj_t* row)
{
    std::string s;
    for(uint32_t i = 0; i < lv_obj_get_child_count(row); i++) {
        lv_obj_t* c = lv_obj_get_child(row, static_cast<int32_t>(i));
        if(lv_obj_check_type(c, &lv_label_class)) s += std::string(i ? "|" : "") + lv_label_get_text(c);
    }
    if(lv_obj_check_type(row, &lv_label_class)) s = lv_label_get_text(row);
    return s;
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("alarm acknowledge: every order of raise, clear and acknowledge")
{
    Runtime rt;
    TempDir t;
    Tag* temp = rt.tag("temp", TagType::Float, 20);
    Tag* door = rt.tag("door", TagType::Bool, 0);
    Tag* note = rt.tag("note", TagType::Bool, 0);
    json cfg = J(kAlarms);
    REQUIRE(alarms().load(&cfg, t.base()) == 3);
    CHECK(alarms().current().empty());
    CHECK(tagnum("sys.alarm.active") == 0);
    CHECK(tagstr("sys.alarm.latest") == "");

    // raise -> acknowledge -> clear: gone
    temp->set_number(60, nullptr); rt.pump();
    REQUIRE(alarms().current().size() == 1);
    CHECK_FALSE(alarms().current()[0].acked);
    CHECK(tagnum("sys.alarm.active") == 1);
    CHECK(tagnum("sys.alarm.unacked") == 1);
    CHECK(tagstr("sys.alarm.latest") == "Too hot");
    CHECK(tagstr("sys.alarm.latest_severity") == "critical");
    CHECK(alarms().ack("hot", "alice"));
    CHECK_FALSE(alarms().ack("hot", "alice"));               // nothing left to acknowledge
    CHECK(tagnum("sys.alarm.unacked") == 0);
    CHECK(tagstr("sys.alarm.latest") == "Too hot");          // still active
    temp->set_number(20, nullptr); rt.pump();
    CHECK(alarms().current().empty());
    CHECK(events_of("hot") == std::vector<std::string>{ "raised", "acked:alice", "cleared" });

    // raise -> clear -> (still listed) -> acknowledge: gone
    door->set_number(1, nullptr); rt.pump();
    door->set_number(0, nullptr); rt.pump();
    REQUIRE(alarms().current().size() == 1);
    CHECK_FALSE(alarms().current()[0].active);
    CHECK(tagnum("sys.alarm.active") == 0);
    CHECK(tagnum("sys.alarm.unacked") == 1);
    CHECK(tagstr("sys.alarm.latest") == "Door open");        // not acknowledged yet
    // raised again before it was acknowledged: a new occurrence, still to acknowledge
    door->set_number(1, nullptr); rt.pump();
    CHECK(alarms().current()[0].active);
    CHECK_FALSE(alarms().current()[0].acked);
    CHECK(alarms().ack("door", ""));
    CHECK(alarms().current()[0].active);                     // acknowledged but active: listed
    door->set_number(0, nullptr); rt.pump();
    CHECK(alarms().current().empty());

    // "ack": false - never to acknowledge, leaves the list when it clears
    note->set_number(1, nullptr); rt.pump();
    REQUIRE(alarms().current().size() == 1);
    CHECK(alarms().current()[0].acked);
    CHECK(tagnum("sys.alarm.unacked") == 0);
    CHECK_FALSE(alarms().ack("note", "x"));
    note->set_number(0, nullptr); rt.pump();
    CHECK(alarms().current().empty());

    // acknowledge all; unknown names
    temp->set_number(99, nullptr); door->set_number(1, nullptr); rt.pump();
    CHECK(alarms().unacked_count() == 2);
    CHECK(alarms().current()[0].name == "door");             // newest first
    CHECK(alarms().ack_all("bob") == 2);
    CHECK(alarms().ack_all("bob") == 0);
    CHECK_FALSE(alarms().ack("nope", "bob"));
    CHECK(alarms().status("hot")->acked_by == "bob");
    CHECK_FALSE(alarms().status("nope"));
    CHECK(alarms().severities() == std::vector<std::string>{ "critical", "warning", "info" });
}

TEST_CASE("alarm history: newest first, paging, severity and time filters")
{
    Runtime rt;
    TempDir t;
    Tag* temp = rt.tag("temp", TagType::Float, 20);
    Tag* door = rt.tag("door", TagType::Bool, 0);
    rt.tag("note", TagType::Bool, 0);
    json cfg = J(kAlarms);
    REQUIRE(alarms().load(&cfg, t.base()) == 3);
    for(int i = 0; i < 5; i++) {
        temp->set_number(60, nullptr); rt.pump();
        temp->set_number(20, nullptr); rt.pump();
    }
    door->set_number(1, nullptr); rt.pump();
    CHECK(alarms().history_count() == 11);
    CHECK(alarms().history_count("critical") == 10);
    CHECK(alarms().history_count("warning") == 1);
    auto page1 = alarms().history(4, 0);
    REQUIRE(page1.size() == 4);
    CHECK(page1[0].name == "door");                          // newest first
    CHECK(page1[1].event == "cleared");
    auto last = alarms().history(4, 8);
    CHECK(last.size() == 3);
    CHECK(alarms().history(4, 100).empty());
    CHECK(alarms().history(0, 0).empty());
    CHECK(alarms().history_count("", LogDb::now_ms() + 60000) == 0);
    CHECK(alarms().history_count("", 1) == 11);
}

// ================================================================ reliability

TEST_CASE("alarm state survives a restart without recording what did not change")
{
    Runtime rt;
    TempDir t;
    Tag* temp = rt.tag("temp", TagType::Float, 60);          // active from the start
    Tag* door = rt.tag("door", TagType::Bool, 1);
    rt.tag("note", TagType::Bool, 0);
    json cfg = J(kAlarms);
    REQUIRE(alarms().load(&cfg, t.base()) == 3);
    CHECK(alarms().ack("hot", "alice"));
    door->set_number(0, nullptr); rt.pump();                 // cleared, not acknowledged
    const size_t rows = alarms().query().size();
    const int64_t raised = alarms().status("hot")->raised_ms;

    alarms().clear();                                        // "power off"
    REQUIRE(alarms().load(&cfg, t.base()) == 3);
    CHECK(alarms().query().size() == rows);                  // nothing recorded again
    auto hot = alarms().status("hot");
    CHECK(hot->active);
    CHECK(hot->acked);
    CHECK(hot->acked_by == "alice");
    CHECK(hot->raised_ms == raised);
    auto d = alarms().status("door");
    CHECK_FALSE(d->active);
    CHECK_FALSE(d->acked);                                   // still to acknowledge
    CHECK(alarms().current().size() == 2);
    CHECK(tagnum("sys.alarm.unacked") == 1);

    // what happened while it was off: "hot" went away, "door" opened again
    alarms().clear();
    temp->set_number(20, nullptr);
    door->set_number(1, nullptr);
    rt.pump();
    REQUIRE(alarms().load(&cfg, t.base()) == 3);
    CHECK(events_of("hot") == std::vector<std::string>{ "raised", "acked:alice", "cleared" });
    CHECK(events_of("door") == std::vector<std::string>{ "raised", "cleared", "raised" });
    CHECK(alarms().current().size() == 1);
}

TEST_CASE("log database: a table from an older version gets the new column")
{
    Runtime rt;
    TempDir t;
    LogDbConfig cfg;
    cfg.enabled = true;
    cfg.path = t.base() + "/old.db";
    {
        LogDb old("test", "alarm", { { "name", "TEXT NOT NULL" }, { "event", "TEXT NOT NULL" }, { "severity", "TEXT NOT NULL" },
                                    { "message", "TEXT NOT NULL" }, { "value", "TEXT" } });
        REQUIRE(old.open(cfg));
        old.add({ "hot", "raised", "critical", "Too hot", "60" });
        old.close();
    }
    LogDb now("test", "alarm", { { "name", "TEXT NOT NULL" }, { "event", "TEXT NOT NULL" }, { "severity", "TEXT NOT NULL" },
                                { "message", "TEXT NOT NULL" }, { "value", "TEXT" }, { "user", "TEXT" }, { "site", "TEXT NOT NULL" } });
    REQUIRE(now.open(cfg));
    now.add({ "hot", "acked", "critical", "Too hot", "60", "alice", "A" });
    auto rows = now.query();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0][7].type == LogCell::Null);                 // the old row: no user
    CHECK(rows[0][8].s == "");                               // NOT NULL column: the default
    CHECK(rows[1][7].s == "alice");
    CHECK(now.count("name = ?1", { "hot" }) == 2);
    CHECK(now.query("", {}, 1, true, 1)[0][3].s == "raised");   // offset
    now.close();
}

// ================================================================ security

TEST_CASE("alarm security: filters are parameters, texts cannot break the list")
{
    Runtime rt;
    TempDir t;
    Tag* temp = rt.tag("temp", TagType::Float, 60);
    rt.tag("door", TagType::Bool, 0);
    rt.tag("note", TagType::Bool, 0);
    json cfg = J(kAlarms);
    REQUIRE(alarms().load(&cfg, t.base()) == 3);
    (void)temp;
    // a severity that is SQL: matches nothing, changes nothing
    CHECK(alarms().history_count("x' OR '1'='1") == 0);
    CHECK(alarms().history(10, 0, "critical'; DROP TABLE alarm; --").empty());
    CHECK(alarms().history_count() == 1);

    // one line, bounded, never cut inside a UTF-8 character
    CHECK(alarm_text_line("a\nb\tc\x1b[31m") == "a b c [31m");
    std::string jp;
    for(int i = 0; i < 100; i++) jp += "\xE6\xB8\xA9";       // 300 bytes of 3-byte characters
    std::string cut = alarm_text_line(jp, 200);
    CHECK(cut.size() == 198 + 3);
    CHECK(cut.substr(198) == "\xE2\x80\xA6");
    CHECK(alarm_text_line("", 10) == "");
    // (labels with printf directives: see "alarm widgets: empty list, more than fit ...")
}

TEST_CASE("alarm acknowledge on the screen needs the level, and is recorded")
{
    Runtime rt;
    TempDir t;
    Tag* temp = rt.tag("temp", TagType::Float, 20);
    rt.tag("door", TagType::Bool, 1);
    rt.tag("note", TagType::Bool, 0);
    json ucfg = J(R"~({ "levels": ["Operator", "Maintenance"], "list": [ { "name": "op", "level": 1, "pin": "1111" }, { "name": "boss", "level": 2, "pin": "2222" } ] })~");
    users().load(&ucfg, nullptr, "");
    json ocfg = J(R"~({ })~");
    REQUIRE(operation_log().load(&ocfg, t.base()));
    json cfg = J(R"~({ "ack_level": 2, "items": [
        { "name": "hot",  "tag": "temp", "condition": "gt", "value": 50, "message": "Too hot", "severity": "critical" },
        { "name": "door", "tag": "door", "message": "Door open %s%n%d", "severity": "warning" } ] })~");
    REQUIRE(alarms().load(&cfg, t.base()) == 2);
    register_alarm_actions();

    lv_obj_t* s = show("alarms", J(R"~([ { "type": "alarm_list", "w": 400, "h": 300, "columns": ["severity", "message", "state"], "labels": { "unacked": "未確認" } },
                                        { "type": "alarm_banner", "w": 400 } ])~"));
    lv_obj_t* list = lv_obj_get_child(s, 0);
    lv_obj_t* banner = lv_obj_get_child(s, 1);
    rt.advance(300);
    REQUIRE(lv_obj_get_child_count(body_of(list)) == 1);
    lv_obj_t* row = lv_obj_get_child(body_of(list), 0);
    CHECK(text_of_row(row) == LV_SYMBOL_WARNING " warning|Door open %s%n%d|" LV_SYMBOL_BELL " 未確認");   // shown as it is
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(banner, 0))).find("Door open %s%n%d") != std::string::npos);

    // nobody logged in: a tap asks for a login and acknowledges nothing
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    CHECK(alarms().unacked_count() == 1);
    CHECK(users().required() == 2);
    ui().call_action("alarm.ack_all", "");
    CHECK(alarms().unacked_count() == 1);
    // too low a level: still not
    users().finish_login(false);
    REQUIRE(users().login("op", "1111") == Users::LoginResult::Ok);
    ui().show_screen("alarms", "none");
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    CHECK(alarms().unacked_count() == 1);
    users().finish_login(false);
    users().logout();
    REQUIRE(users().login("boss", "2222") == Users::LoginResult::Ok);
    ui().show_screen("alarms", "none");
    rt.advance(300);
    row = lv_obj_get_child(body_of(list), 0);
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    CHECK(alarms().unacked_count() == 0);
    CHECK(alarms().status("door")->acked_by == "boss");
    rt.advance(300);
    CHECK(text_of_row(lv_obj_get_child(body_of(list), 0)).find(LV_SYMBOL_OK) != std::string::npos);

    temp->set_number(70, nullptr); rt.pump();
    rt.advance(300);
    CHECK(lv_obj_get_child_count(body_of(list)) == 2);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(banner, 0))).find("Too hot  (+1)") != std::string::npos);
    ui().call_action("alarm.ack", "");                      // the newest not acknowledged
    CHECK(alarms().unacked_count() == 0);

    bool recorded = false;
    for(const Operation& o : operation_log().query()) recorded = recorded || (o.kind == "alarm" && o.target == "door" && o.new_value == "acked" && o.user == "boss");
    CHECK(recorded);
}

TEST_CASE("alarm widgets: empty list, more than fit, history pages, deleting the screen")
{
    Runtime rt;
    TempDir t;
    json items = json::array();
    for(int i = 0; i < 30; i++) {
        rt.tag("b" + std::to_string(i), TagType::Bool, 1);
        items.push_back({ { "name", "a" + std::to_string(i) }, { "tag", "b" + std::to_string(i) }, { "severity", i % 2 ? "warning" : "info" } });
    }
    json cfg = { { "items", items } };
    REQUIRE(alarms().load(&cfg, t.base()) == 30);
    lv_obj_t* s = show("lists", J(R"~([ { "type": "alarm_list", "rows": 10, "labels": { "more": "%d more %s" } },
                                       { "type": "alarm_list", "mode": "history", "rows": 7 },
                                       { "type": "alarm_list", "severity": "nothing" } ])~"));
    rt.advance(300);
    lv_obj_t* cur = body_of(lv_obj_get_child(s, 0));
    CHECK(lv_obj_get_child_count(cur) == 11);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(cur, 10))) == "20 more %s");
    // (child 2 of the screen is the list of the history's severity dropdown: LVGL puts it on the screen)
    REQUIRE(lv_obj_get_child_count(s) == 4);
    lv_obj_t* empty = body_of(lv_obj_get_child(s, 3));
    REQUIRE(lv_obj_get_child_count(empty) == 1);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(empty, 0))) == "No alarms");

    lv_obj_t* hist = lv_obj_get_child(s, 1);
    lv_obj_t* foot = lv_obj_get_child(hist, 1);
    CHECK(lv_obj_get_child_count(body_of(hist)) == 7);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(foot, 1))) == "1 / 5");
    CHECK(lv_obj_has_state(lv_obj_get_child(foot, 0), LV_STATE_DISABLED));
    lv_obj_send_event(lv_obj_get_child(foot, 2), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(foot, 2), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(foot, 2), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(foot, 2), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(foot, 2), LV_EVENT_CLICKED, nullptr);   // past the end: stays on the last page
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(foot, 1))) == "5 / 5");
    CHECK(lv_obj_get_child_count(body_of(hist)) == 2);
    lv_obj_t* dd = lv_obj_get_child(foot, 3);
    lv_dropdown_set_selected(dd, 1);                         // the first severity used: info
    lv_obj_send_event(dd, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(foot, 1))) == "1 / 3");

    ui().unload_all();                                       // the widgets' timers go with them
    rt.advance(600);
    alarms().ack_all("");
    rt.advance(600);
}

TEST_CASE("alarm list: the equipment column, colours per severity, the words of the severities (#130)")
{
    Runtime rt;
    TempDir t;
    rt.tag("temp", TagType::Float, 0);
    rt.tag("door", TagType::Bool, 0);
    rt.tag("note", TagType::Bool, 0);
    texts().load(json::parse(R"~({ "texts": { "oven": { "en": "Oven 1" } } })~"));
    json cfg = J(R"~({ "items": [
        { "name": "hot",  "tag": "temp", "condition": "gt", "value": 50, "message": "Too hot", "severity": "critical", "equipment": "@oven" },
        { "name": "door", "tag": "door", "message": "Door open", "severity": "warning", "equipment": "Line A" },
        { "name": "note", "tag": "note", "message": "Note", "severity": "info" } ] })~");
    REQUIRE(alarms().load(&cfg, t.base()) == 3);
    CHECK(alarms().equipment_of("hot") == "Oven 1");          // "@ID" of texts
    CHECK(alarms().equipment_of("note").empty());
    CHECK(alarms().equipment_of("nope").empty());
    lv_obj_t* s = show("eq", J(R"~([
        { "type": "alarm_list", "columns": ["equipment", "severity", "message"],
          "labels": { "severity.critical": "Stoerung", "severity.warning": "Warnung" },
          "colors": { "critical": "#112233", "text": "#fefefe", "cleared": "#445566", "history_text": "#010203" } },
        { "type": "alarm_list", "mode": "history", "columns": ["equipment", "severity", "message", "time"],
          "labels": { "severity.warning": "Warnung" }, "colors": { "history": "#778899", "warning": "#aabbcc" } } ])~"));
    tags().find("temp")->set_number(60, nullptr);
    tags().find("door")->set_number(1, nullptr);
    tags().find("note")->set_number(1, nullptr);
    rt.advance(300);
    lv_obj_t* cur = body_of(lv_obj_get_child(s, 0));
    REQUIRE(lv_obj_get_child_count(cur) == 3);
    // in the order of "columns"; the words of labels; an alarm without equipment has an empty cell
    std::vector<std::string> rows;
    for(uint32_t i = 0; i < 3; i++) rows.push_back(text_of_row(lv_obj_get_child(cur, static_cast<int32_t>(i))));
    auto has_row = [&](const std::string& want) { return std::find(rows.begin(), rows.end(), want) != rows.end(); };
    CHECK(has_row("Oven 1|" LV_SYMBOL_WARNING " Stoerung|Too hot"));
    CHECK(has_row("Line A|" LV_SYMBOL_WARNING " Warnung|Door open"));
    CHECK(has_row("|" LV_SYMBOL_BELL " info|Note"));          // no label for info: the word as the alarms write it
    // the columns line up: every equipment cell (and every severity cell) is as wide as the widest
    for(int32_t col = 0; col < 2; col++) {
        const int32_t w0 = lv_obj_get_style_width(lv_obj_get_child(lv_obj_get_child(cur, 0), col), LV_PART_MAIN);
        CHECK(w0 > 0);
        for(int32_t i = 1; i < 3; i++) CHECK(lv_obj_get_style_width(lv_obj_get_child(lv_obj_get_child(cur, i), col), LV_PART_MAIN) == w0);
    }
    // the colour of the critical row (not acknowledged: filled) and its text
    lv_obj_t* hot = nullptr;
    for(uint32_t i = 0; i < 3; i++) if(rows[i].rfind("Oven 1", 0) == 0) hot = lv_obj_get_child(cur, static_cast<int32_t>(i));
    REQUIRE(hot);
    CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(hot, LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0x112233)));
    CHECK(lv_color_to_u32(lv_obj_get_style_text_color(lv_obj_get_child(hot, 0), LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0xfefefe)));
    // cleared and not acknowledged: the "cleared" colour, the "history_text" text
    tags().find("temp")->set_number(20, nullptr);
    rt.advance(300);
    lv_obj_t* cleared = nullptr;
    for(uint32_t i = 0; i < lv_obj_get_child_count(cur); i++)
        if(text_of_row(lv_obj_get_child(cur, static_cast<int32_t>(i))).rfind("Oven 1", 0) == 0) cleared = lv_obj_get_child(cur, static_cast<int32_t>(i));
    REQUIRE(cleared);
    CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(cleared, LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0x445566)));
    CHECK(lv_color_to_u32(lv_obj_get_style_text_color(lv_obj_get_child(cleared, 0), LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0x010203)));

    // the history: equipment as the alarm is set now, the colours of its rows, the words in the filter
    lv_obj_t* hist = lv_obj_get_child(s, 1);
    lv_obj_t* hb = body_of(hist);
    REQUIRE(lv_obj_get_child_count(hb) >= 4);
    bool door_seen = false;
    for(uint32_t i = 0; i < lv_obj_get_child_count(hb); i++) {
        lv_obj_t* r = lv_obj_get_child(hb, static_cast<int32_t>(i));
        CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(r, LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0x778899)));
        const std::string row = text_of_row(r);
        if(row.rfind("Line A|" LV_SYMBOL_WARNING " Warnung|Door open|", 0) == 0) {
            door_seen = true;
            CHECK(lv_color_to_u32(lv_obj_get_style_border_color(r, LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0xaabbcc)));
        }
    }
    CHECK(door_seen);
    std::string o = lv_dropdown_get_options(lv_obj_get_child(lv_obj_get_child(hist, 1), 3));
    CHECK(o.find("Warnung") != std::string::npos);
    CHECK(o.find("critical") != std::string::npos);          // no label for it in this widget

    ui().unload_all();
    texts().clear();
    rt.advance(600);
    alarms().ack_all("");
    rt.advance(600);
}

// ================================================================ performance efficiency

TEST_CASE("alarm list: 1000 alarms, 200 active - redrawing stays quick")
{
    Runtime rt;
    TempDir t;
    json items = json::array();
    std::vector<Tag*> tg;
    for(int i = 0; i < 1000; i++) {
        tg.push_back(rt.tag("t" + std::to_string(i), TagType::Bool, i < 200 ? 1 : 0));
        items.push_back({ { "name", "al" + std::to_string(i) }, { "tag", "t" + std::to_string(i) }, { "message", "Alarm number " + std::to_string(i) } });
    }
    json cfg = { { "items", items } };
    auto t0 = std::chrono::steady_clock::now();
    REQUIRE(alarms().load(&cfg, t.base()) == 1000);
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(alarms().current().size() == 200);
    lv_obj_t* s = show("big", J(R"~([ { "type": "alarm_list" }, { "type": "alarm_banner" } ])~"));
    rt.advance(300);
    CHECK(lv_obj_get_child_count(body_of(lv_obj_get_child(s, 0))) == 51);   // 50 rows + "150 more"

    t0 = std::chrono::steady_clock::now();
    for(int i = 200; i < 260; i++) { tg[static_cast<size_t>(i)]->set_number(1, nullptr); rt.pump(); rt.advance(250, 250); }
    auto per_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() / 60.0;
    MESSAGE("load of 1000 alarms: " << load_ms << " ms, one change + redraw: " << per_ms << " ms");
    CHECK(load_ms < 5000);
    CHECK(per_ms < 100);                                      // (a debug build on a PC; far below on release)
    CHECK(alarms().active_count() == 260);
}

// ================================================================ robustness (fuzz-like)

TEST_CASE("alarm widgets: random messages and severities do not break anything")
{
    Runtime rt;
    TempDir t;
    std::mt19937 rng(12345);
    json items = json::array();
    for(int i = 0; i < 40; i++) {
        std::string msg, sev;
        const int n = static_cast<int>(rng() % 400);
        for(int k = 0; k < n; k++) msg.push_back(static_cast<char>(rng() % 256));     // any bytes, also broken UTF-8
        for(int k = 0; k < 8; k++) sev.push_back(static_cast<char>(rng() % 256));
        rt.tag("f" + std::to_string(i), TagType::Bool, 1);
        json a = json::object();
        a["name"] = "f" + std::to_string(i);
        a["tag"] = "f" + std::to_string(i);
        a["message"] = msg;
        a["severity"] = sev;
        items.push_back(a);
    }
    json cfg = { { "items", items } };
    REQUIRE(alarms().load(&cfg, t.base()) == 40);
    lv_obj_t* s = show("fuzz", J(R"~([ { "type": "alarm_list", "columns": ["time", "severity", "message", "state"] },
                                      { "type": "alarm_list", "mode": "history" }, { "type": "alarm_banner" } ])~"));
    rt.advance(300);
    hmltest::Lvgl::get().render();
    CHECK(lv_obj_get_child_count(body_of(lv_obj_get_child(s, 0))) == 40);
    CHECK(alarms().ack_all("") == 40);
    rt.advance(300);
    hmltest::Lvgl::get().render();
}
