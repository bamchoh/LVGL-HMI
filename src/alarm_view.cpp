#include "alarm_view.hpp"
#include <algorithm>
#include <map>
#include <vector>
#include "alarm.hpp"
#include "texts.hpp"
#include "operation_log.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr uint32_t kPollMs = 250;

lv_color_t severity_color(const std::string& sev)
{
    if(iequals(sev, "critical") || iequals(sev, "error") || iequals(sev, "alarm")) return lv_palette_main(LV_PALETTE_RED);
    if(iequals(sev, "warning")) return lv_palette_darken(LV_PALETTE_AMBER, 2);
    if(iequals(sev, "info")) return lv_palette_main(LV_PALETTE_BLUE);
    return lv_palette_main(LV_PALETTE_GREY);
}

const char* severity_symbol(const std::string& sev)
{
    if(iequals(sev, "critical") || iequals(sev, "error") || iequals(sev, "alarm")) return LV_SYMBOL_WARNING;
    if(iequals(sev, "info")) return LV_SYMBOL_BELL;
    return LV_SYMBOL_WARNING;
}

// "time" column: HH:MM:SS for today's list, MM-DD HH:MM:SS for the history
std::string short_time(int64_t ms, bool with_date)
{
    if(ms <= 0) return "";
    std::string t = LogDb::format_time(ms);               // YYYY-MM-DD HH:MM:SS.mmm
    return t.size() >= 19 ? (with_date ? t.substr(5, 14) : t.substr(11, 8)) : t;
}

bool on_shown_screen(lv_obj_t* obj) { return lv_obj_get_screen(obj) == lv_screen_active(); }

struct Labels {
    std::map<std::string, std::string> m{
        { "unacked", "UNACK" }, { "active", "ACTIVE" }, { "cleared", "CLEARED" }, { "acked", "ACK" },
        { "empty", "No alarms" }, { "idle", "No alarms" }, { "all", "All" }, { "more", "+%d more" },
        { "raised", "raised" }, { "cleared_event", "cleared" }, { "acked_event", "acked" },
    };
    void load(const json& w)
    {
        const json* l = jget(w, "labels");
        if(!l || !l->is_object()) return;
        for(auto it = l->begin(); it != l->end(); ++it) if(it.value().is_string()) m[it.key()] = alarm_text_line(texts().resolve(it.value().get<std::string>()), 64);
    }
    const std::string& operator[](const std::string& k) const { static const std::string none; auto it = m.find(k); return it == m.end() ? none : it->second; }
};

lv_obj_t* make_label(lv_obj_t* parent, const std::string& text, lv_color_t color, bool grow = false)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_color(l, color, 0);
    if(grow) {
        lv_obj_set_flex_grow(l, 1);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(l, 10);                           // (grows; dots instead of wrapping)
    }
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

// ================================================================ alarm_list

struct ListCtx {
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   body = nullptr;
    lv_obj_t*   page_label = nullptr;
    lv_obj_t*   prev = nullptr;
    lv_obj_t*   next = nullptr;
    lv_obj_t*   sev_dd = nullptr;
    lv_timer_t* timer = nullptr;
    bool        history = false;
    int         rows = 50;
    int32_t     row_h = 40;
    bool        ack_on_tap = true;
    std::string severity;
    int         days = 0;
    std::vector<std::string> columns{ "time", "message", "state" };
    std::vector<std::string> severities;                 // of the history's dropdown ("" = all)
    Labels      labels;
    uint64_t    seen = 0;                                // generation drawn
    bool        dirty = true;
    int         page = 0;
    std::vector<std::string> row_names;                  // alarm of each row (current mode)

    bool has(const char* col) const { return std::find(columns.begin(), columns.end(), col) != columns.end(); }

    void refresh(bool force = false)
    {
        const uint64_t g = alarms().generation();
        if(g != seen) dirty = true;
        if(!dirty && !force) return;
        if(!force && !on_shown_screen(obj)) return;      // drawn when the screen is shown
        seen = g;
        dirty = false;
        lv_obj_clean(body);
        row_names.clear();
        if(history) draw_history(); else draw_current();
    }

    lv_obj_t* row(lv_color_t bg, lv_opa_t opa, lv_color_t edge)
    {
        lv_obj_t* r = lv_obj_create(body);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, lv_pct(100), row_h);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(r, 8, 0);
        lv_obj_set_style_pad_column(r, 10, 0);
        lv_obj_set_style_bg_color(r, bg, 0);
        lv_obj_set_style_bg_opa(r, opa, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(r, 6, 0);
        lv_obj_set_style_border_color(r, edge, 0);
        lv_obj_set_style_radius(r, 4, 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);
        return r;
    }

    void empty_row(const std::string& text)
    {
        lv_obj_t* l = make_label(body, text, lv_palette_main(LV_PALETTE_GREY));
        lv_obj_set_style_pad_all(l, 8, 0);
    }

    void draw_current()
    {
        std::vector<AlarmStatus> list = alarms().current();
        if(!severity.empty()) list.erase(std::remove_if(list.begin(), list.end(), [&](const AlarmStatus& a) { return !iequals(a.severity, severity); }), list.end());
        if(list.empty()) { empty_row(labels["empty"]); return; }
        const size_t shown = std::min(list.size(), static_cast<size_t>(std::max(1, rows)));
        const lv_color_t white = lv_color_white(), dark = lv_color_hex(0x263238);
        for(size_t i = 0; i < shown; i++) {
            const AlarmStatus& a = list[i];
            const lv_color_t sc = severity_color(a.severity);
            // unacknowledged: filled; acknowledged but active: tinted; cleared, not acknowledged: grey with the colour at the edge
            const bool unacked = !a.acked, cleared = !a.active;
            lv_obj_t* r = cleared ? row(lv_palette_lighten(LV_PALETTE_GREY, 2), LV_OPA_COVER, sc)
                        : unacked ? row(sc, LV_OPA_COVER, sc) : row(sc, LV_OPA_40, sc);
            const lv_color_t fg = cleared ? dark : white;
            if(has("time")) make_label(r, short_time(a.raised_ms, false), fg);
            if(has("severity")) make_label(r, std::string(severity_symbol(a.severity)) + " " + alarm_text_line(a.severity, 24), fg);
            if(has("message")) make_label(r, alarm_text_line(a.message), fg, true);
            if(has("state")) {
                // the state also in words and with a symbol: not only by colour
                std::string st = unacked ? std::string(LV_SYMBOL_BELL " ") + labels[cleared ? "cleared" : "unacked"]
                               : std::string(LV_SYMBOL_OK " ") + labels["active"];
                if(unacked && !cleared) st = std::string(LV_SYMBOL_BELL " ") + labels["unacked"];
                make_label(r, st, fg);
            }
            row_names.push_back(a.name);
            if(ack_on_tap && unacked) lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(r, &ListCtx::on_row_click, LV_EVENT_CLICKED, this);
        }
        if(list.size() > shown) {
            // (the label comes from the project: put the number in by hand, never use it as a printf format)
            std::string more = labels["more"];
            size_t at = more.find("%d");
            std::string n = std::to_string(list.size() - shown);
            empty_row(at == std::string::npos ? more + " " + n : more.replace(at, 2, n));
        }
    }

    int64_t since() const { return days > 0 ? LogDb::now_ms() - static_cast<int64_t>(days) * 86400000LL : 0; }

    void draw_history()
    {
        const int per = std::max(1, rows);
        const int64_t total = alarms().history_count(severity, since());
        const int pages = static_cast<int>(std::max<int64_t>(1, (total + per - 1) / per));
        page = std::clamp(page, 0, pages - 1);
        lv_label_set_text(page_label, vformat("%d / %d", page + 1, pages).c_str());
        if(page == 0) lv_obj_add_state(prev, LV_STATE_DISABLED); else lv_obj_remove_state(prev, LV_STATE_DISABLED);
        if(page >= pages - 1) lv_obj_add_state(next, LV_STATE_DISABLED); else lv_obj_remove_state(next, LV_STATE_DISABLED);
        std::vector<AlarmEvent> ev = alarms().history(per, page * per, severity, since());
        if(ev.empty()) { empty_row(labels["empty"]); return; }
        const lv_color_t fg = lv_color_hex(0x263238);
        for(const AlarmEvent& e : ev) {
            lv_obj_t* r = row(lv_palette_lighten(LV_PALETTE_GREY, 3), LV_OPA_COVER, severity_color(e.severity));
            if(has("time")) make_label(r, short_time(e.ts_ms, true), fg);
            if(has("severity")) make_label(r, std::string(severity_symbol(e.severity)) + " " + alarm_text_line(e.severity, 24), fg);
            if(has("message")) make_label(r, alarm_text_line(e.message), fg, true);
            if(has("state")) {
                std::string what = e.event == "raised" ? labels["raised"] : e.event == "cleared" ? labels["cleared_event"] : labels["acked_event"];
                if(e.event == "acked" && !e.user.empty()) what += " (" + alarm_text_line(e.user, 32) + ")";
                make_label(r, what, fg);
            }
        }
    }

    static void on_row_click(lv_event_t* e)
    {
        ListCtx* c = static_cast<ListCtx*>(lv_event_get_user_data(e));
        lv_obj_t* r = lv_event_get_current_target_obj(e);
        if(!c->ack_on_tap || c->history) return;
        const int32_t idx = lv_obj_get_index(r);
        if(idx < 0 || static_cast<size_t>(idx) >= c->row_names.size()) return;
        std::string name = c->row_names[static_cast<size_t>(idx)];   // (the list may be redrawn by the acknowledge)
        ack_alarms_as_operator(name);
    }
    static void on_timer(lv_timer_t* t) { static_cast<ListCtx*>(lv_timer_get_user_data(t))->refresh(); }
    static void on_delete(lv_event_t* e)
    {
        ListCtx* c = static_cast<ListCtx*>(lv_event_get_user_data(e));
        if(c->timer) lv_timer_delete(c->timer);
        delete c;
    }
    static void on_page(lv_event_t* e)
    {
        ListCtx* c = static_cast<ListCtx*>(lv_event_get_user_data(e));
        c->page += lv_event_get_current_target_obj(e) == c->next ? 1 : -1;
        if(c->page < 0) c->page = 0;
        c->refresh(true);
    }
    static void on_severity(lv_event_t* e)
    {
        ListCtx* c = static_cast<ListCtx*>(lv_event_get_user_data(e));
        uint32_t i = lv_dropdown_get_selected(c->sev_dd);
        c->severity = i < c->severities.size() ? c->severities[i] : "";
        c->page = 0;
        c->refresh(true);
    }
};

lv_obj_t* footer_button(lv_obj_t* parent, const char* text, ListCtx* c)
{
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, 56, 40);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, &ListCtx::on_page, LV_EVENT_CLICKED, c);
    return b;
}

// ================================================================ alarm_banner

struct BannerCtx {
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   label = nullptr;
    lv_timer_t* timer = nullptr;
    bool        blink = true;
    bool        ack_on_tap = false;
    Labels      labels;
    uint64_t    seen = 0;
    bool        captured = false, on = false, lit = true;
    lv_color_t  idle_bg{}, idle_fg{};
    lv_opa_t    idle_opa = LV_OPA_COVER;
    std::string shown;                                   // the alarm shown ("" = none)
    uint32_t    ticks = 0;

    void refresh()
    {
        if(!captured) {                                  // the widget's own look (after its style was applied)
            idle_bg = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
            idle_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
            idle_fg = lv_obj_get_style_text_color(label, LV_PART_MAIN);
            captured = true;
        }
        const uint64_t g = alarms().generation();
        if(g != seen) {
            seen = g;
            std::vector<AlarmStatus> list = alarms().current();
            const AlarmStatus* top = nullptr;
            for(const AlarmStatus& a : list) if(!a.acked) { top = &a; break; }       // newest unacknowledged
            if(!top) for(const AlarmStatus& a : list) if(a.active) { top = &a; break; }
            on = top != nullptr;
            shown = top ? top->name : "";
            if(top) {
                std::string text = std::string(severity_symbol(top->severity)) + " " + short_time(top->raised_ms, false) + "  " + alarm_text_line(top->message);
                if(list.size() > 1) text += vformat("  (+%d)", static_cast<int>(list.size() - 1));
                lv_label_set_text(label, text.c_str());
                lv_obj_set_style_bg_color(obj, severity_color(top->severity), 0);
                lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(label, lv_color_white(), 0);
                blinking = !top->acked;
            }
            else {
                lv_label_set_text(label, labels["idle"].c_str());
                lv_obj_set_style_bg_color(obj, idle_bg, 0);
                lv_obj_set_style_bg_opa(obj, idle_opa, 0);
                lv_obj_set_style_text_color(label, idle_fg, 0);
                blinking = false;
            }
            lit = true;
        }
        // blink while not acknowledged: the colour goes half way every 500 ms
        if(blink && blinking && ++ticks % 2 == 0) {
            lit = !lit;
            lv_obj_set_style_bg_opa(obj, static_cast<lv_opa_t>(lit ? LV_OPA_COVER : LV_OPA_50), 0);
        }
        else if(!blinking && on && !lit) { lit = true; lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0); }
    }
    bool blinking = false;

    static void on_timer(lv_timer_t* t) { static_cast<BannerCtx*>(lv_timer_get_user_data(t))->refresh(); }
    static void on_click(lv_event_t* e)
    {
        BannerCtx* c = static_cast<BannerCtx*>(lv_event_get_user_data(e));
        if(c->ack_on_tap && !c->shown.empty()) ack_alarms_as_operator(c->shown);
    }
    static void on_delete(lv_event_t* e)
    {
        BannerCtx* c = static_cast<BannerCtx*>(lv_event_get_user_data(e));
        if(c->timer) lv_timer_delete(c->timer);
        delete c;
    }
};

} // namespace

lv_obj_t* build_alarm_list(lv_obj_t* parent, const json& w)
{
    auto* c = new ListCtx();
    c->history = iequals(jstr(w, "mode", "current"), "history");
    c->rows = std::clamp(static_cast<int>(jnum(w, "rows", c->history ? 20 : 50)), 1, 500);
    c->row_h = std::clamp(static_cast<int32_t>(jnum(w, "row_height", 40)), 16, 200);
    c->ack_on_tap = jbool(w, "ack_on_tap", true);
    c->severity = jstr(w, "severity", "");
    c->days = std::max(0, static_cast<int>(jnum(w, "days", 0)));
    c->labels.load(w);
    if(const json* cols = jget(w, "columns"); cols && cols->is_array()) {
        c->columns.clear();
        for(const json& col : *cols) if(col.is_string()) c->columns.push_back(col.get<std::string>());
    }

    lv_obj_t* obj = lv_obj_create(parent);
    c->obj = obj;
    lv_obj_set_size(obj, lv_pct(100), 240);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(obj, 4, 0);
    lv_obj_set_style_pad_row(obj, 4, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    c->body = lv_obj_create(obj);
    lv_obj_remove_style_all(c->body);
    lv_obj_set_width(c->body, lv_pct(100));
    lv_obj_set_flex_grow(c->body, 1);
    lv_obj_set_flex_flow(c->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c->body, 3, 0);
    lv_obj_add_flag(c->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c->body, LV_DIR_VER);

    if(c->history) {
        lv_obj_t* foot = lv_obj_create(obj);
        lv_obj_remove_style_all(foot);
        lv_obj_set_size(foot, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(foot, 8, 0);
        c->prev = footer_button(foot, LV_SYMBOL_LEFT, c);
        c->page_label = lv_label_create(foot);
        c->next = footer_button(foot, LV_SYMBOL_RIGHT, c);
        // the severity filter, unless the widget is fixed to one
        if(c->severity.empty()) {
            c->severities.push_back("");
            std::string opts = c->labels["all"];
            for(const std::string& s : alarms().severities()) { c->severities.push_back(s); opts += "\n" + alarm_text_line(s, 24); }
            c->sev_dd = lv_dropdown_create(foot);
            lv_dropdown_set_options(c->sev_dd, opts.c_str());
            lv_obj_set_width(c->sev_dd, 150);
            lv_obj_add_event_cb(c->sev_dd, &ListCtx::on_severity, LV_EVENT_VALUE_CHANGED, c);
        }
    }

    c->timer = lv_timer_create(&ListCtx::on_timer, kPollMs, c);
    lv_obj_add_event_cb(obj, &ListCtx::on_delete, LV_EVENT_DELETE, c);
    c->refresh(true);
    return obj;
}

lv_obj_t* build_alarm_banner(lv_obj_t* parent, const json& w)
{
    auto* c = new BannerCtx();
    c->blink = jbool(w, "blink", true);
    c->ack_on_tap = jbool(w, "ack_on_tap", false);
    c->labels.load(w);
    lv_obj_t* obj = lv_obj_create(parent);
    c->obj = obj;
    lv_obj_set_size(obj, lv_pct(100), 36);
    lv_obj_set_style_pad_hor(obj, 10, 0);
    lv_obj_set_style_pad_ver(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    c->label = lv_label_create(obj);
    lv_label_set_long_mode(c->label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(c->label, lv_pct(100));
    lv_obj_align(c->label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(c->label, c->labels["idle"].c_str());
    c->timer = lv_timer_create(&BannerCtx::on_timer, kPollMs, c);
    lv_obj_add_event_cb(obj, &BannerCtx::on_click, LV_EVENT_CLICKED, c);
    lv_obj_add_event_cb(obj, &BannerCtx::on_delete, LV_EVENT_DELETE, c);
    return obj;
}

int ack_alarms_as_operator(const std::string& name, bool all)
{
    const int level = alarms().ack_level();
    if(!users().allowed(level)) {
        if(lv_indev_t* indev = lv_indev_active()) lv_indev_wait_release(indev);
        users().request_login(level);
        return 0;
    }
    const std::string user = users().name();
    std::string target = name;
    if(!all && target.empty()) {                         // the newest one that is not acknowledged
        for(const AlarmStatus& a : alarms().current()) if(!a.acked) { target = a.name; break; }
        if(target.empty()) return 0;
    }
    const int n = all ? alarms().ack_all(user) : (alarms().ack(target, user) ? 1 : 0);
    if(n > 0) operation_log().record("alarm", all ? "(all)" : target, "", "acked");
    return n;
}

void register_alarm_actions()
{
    ui().register_action("alarm.ack", [](const std::string& arg) { ack_alarms_as_operator(arg); });
    ui().register_action("alarm.ack_all", [](const std::string&) { ack_alarms_as_operator("", true); });
}

} // namespace hml
