#include "alarm_summary_view.hpp"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include "alarm.hpp"
#include "alarm_stats.hpp"
#include "background_worker.hpp"
#include "log_query.hpp"
#include "texts.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr uint32_t kPollMs = 250;

struct Shared {                       // between the widget (UI thread) and the worker
    std::mutex mu;
    bool       busy = false, ready = false;
    uint64_t   gen = 0;
    std::vector<AlarmStat> stats;
};

lv_color_t severity_color(const std::string& sev)
{
    if(iequals(sev, "critical") || iequals(sev, "error") || iequals(sev, "alarm")) return lv_palette_main(LV_PALETTE_RED);
    if(iequals(sev, "warning")) return lv_palette_darken(LV_PALETTE_AMBER, 2);
    if(iequals(sev, "info")) return lv_palette_main(LV_PALETTE_BLUE);
    return lv_palette_main(LV_PALETTE_GREY);
}

struct Ctx {
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   body = nullptr;
    lv_obj_t*   period_dd = nullptr;
    lv_obj_t*   sort_dd = nullptr;
    lv_obj_t*   total = nullptr;
    lv_timer_t* timer = nullptr;
    std::vector<int> periods{ 1, 7, 30, 0 };
    int         days = 7;
    AlarmSort   sort = AlarmSort::Count;
    int         rows = 10;
    bool        bars = true;
    std::string severity;
    uint32_t    refresh_ms = 5000;
    std::map<std::string, std::string> labels{
        { "empty", "No alarms" }, { "alarm", "Alarm" }, { "count", "Count" }, { "time", "Time" },
        { "day", "24 h" }, { "week", "7 d" }, { "month", "30 d" }, { "all", "All" }, { "by_count", "Count" }, { "by_time", "Time" } };
    uint64_t    asked = 0, alarm_gen = 0;
    uint32_t    asked_at = 0;
    bool        dirty = true;
    std::shared_ptr<Shared> sh = std::make_shared<Shared>();

    std::string period_text(int d) const
    {
        auto l = [&](const char* k) { auto it = labels.find(k); return it == labels.end() ? std::string(k) : it->second; };
        return d == 1 ? l("day") : d == 7 ? l("week") : d == 30 ? l("month") : d == 0 ? l("all") : vformat("%d d", d);
    }

    void ask()
    {
        {
            std::lock_guard<std::mutex> l(sh->mu);
            if(sh->busy) return;
            sh->busy = true;
        }
        dirty = false;
        asked_at = lv_tick_get();
        alarm_gen = alarms().generation();
        const uint64_t g = ++asked;
        // (what is active now is asked on the UI thread: the alarms belong to it)
        std::set<std::string> active;
        for(const std::string& n : alarms().active_names()) active.insert(n);
        auto shared = sh;
        const int d = days;
        const AlarmSort so = sort;
        const std::string sev = severity;
        BackgroundWorker::get().post([shared, g, d, so, sev, active] {
            std::vector<AlarmStat> st = alarm_stats_recent(d, active, so, sev);
            std::lock_guard<std::mutex> l(shared->mu);
            shared->stats = std::move(st);
            shared->gen = g;
            shared->ready = true;
            shared->busy = false;
        });
    }

    lv_obj_t* cell(lv_obj_t* row, const std::string& text, int32_t w, lv_text_align_t align, bool grow = false)
    {
        lv_obj_t* l = lv_label_create(row);
        lv_label_set_text(l, text.c_str());
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        if(grow) { lv_obj_set_flex_grow(l, 1); lv_obj_set_width(l, 10); }
        else lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, align, 0);
        return l;
    }

    lv_obj_t* line(bool header)
    {
        lv_obj_t* r = lv_obj_create(body);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, lv_pct(100), header ? 28 : 36);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(r, 8, 0);
        lv_obj_set_style_pad_column(r, 8, 0);
        lv_obj_set_scrollable(r, false);
        return r;
    }

    void draw(const std::vector<AlarmStat>& stats)
    {
        lv_obj_clean(body);
        lv_obj_update_layout(obj);
        const bool narrow = lv_obj_get_content_width(obj) < 420;
        const int32_t wc = 52, wt = 78, wb = bars ? (narrow ? 56 : 96) : 0;
        lv_obj_t* h = line(true);
        lv_obj_set_style_text_opa(h, LV_OPA_70, 0);
        cell(h, labels["alarm"], 0, LV_TEXT_ALIGN_LEFT, true);
        cell(h, labels["count"], wc, LV_TEXT_ALIGN_RIGHT);
        cell(h, labels["time"], wt, LV_TEXT_ALIGN_RIGHT);
        if(bars) cell(h, "", wb, LV_TEXT_ALIGN_LEFT);
        int64_t most = 1, all_count = 0, all_ms = 0;
        for(const AlarmStat& s : stats) { most = std::max<int64_t>(most, sort == AlarmSort::Time ? s.active_ms : s.count); all_count += s.count; all_ms += s.active_ms; }
        int n = 0;
        for(const AlarmStat& s : stats) {
            if(n++ >= rows) break;
            lv_obj_t* r = line(false);
            const lv_color_t col = severity_color(s.severity);
            lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_width(r, 5, 0);
            lv_obj_set_style_border_color(r, col, 0);
            lv_obj_set_style_bg_color(r, col, 0);
            lv_obj_set_style_bg_opa(r, static_cast<lv_opa_t>(s.active_now ? LV_OPA_30 : LV_OPA_10), 0);
            lv_obj_set_style_radius(r, 4, 0);
            // (active now: said by the mark, not by the shade alone)
            cell(r, std::string(s.active_now ? LV_SYMBOL_WARNING " " : "") + alarm_text_line(s.message.empty() ? s.name : s.message, 120), 0, LV_TEXT_ALIGN_LEFT, true);
            cell(r, std::to_string(s.count), wc, LV_TEXT_ALIGN_RIGHT);
            cell(r, alarm_duration_text(s.active_ms), wt, LV_TEXT_ALIGN_RIGHT);
            if(bars) {
                lv_obj_t* b = lv_bar_create(r);
                lv_obj_set_size(b, wb, 10);
                lv_bar_set_range(b, 0, 1000);
                const int64_t v = sort == AlarmSort::Time ? s.active_ms : s.count;
                lv_bar_set_value(b, static_cast<int32_t>(v * 1000 / most), LV_ANIM_OFF);
                lv_obj_set_style_bg_color(b, col, LV_PART_INDICATOR);
            }
        }
        if(stats.empty()) {
            lv_obj_t* l = lv_label_create(body);
            lv_label_set_text(l, (alarms().enabled() ? labels["empty"] : std::string("The project has no alarms.")).c_str());
            lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_obj_set_style_pad_all(l, 8, 0);
        }
        lv_label_set_text(total, vformat("%lld / %s", static_cast<long long>(all_count), alarm_duration_text(all_ms).c_str()).c_str());
    }

    void tick()
    {
        if(lv_obj_get_screen(obj) != lv_screen_active()) { dirty = true; return; }
        std::vector<AlarmStat> st;
        bool have = false;
        {
            std::lock_guard<std::mutex> l(sh->mu);
            if(sh->ready) { sh->ready = false; if(sh->gen == asked) { st = std::move(sh->stats); have = true; } }
        }
        if(have) draw(st);
        // counted again when an alarm changed, and now and then (the time of an active alarm grows)
        if(!dirty && (alarms().generation() != alarm_gen || (refresh_ms > 0 && lv_tick_elaps(asked_at) >= refresh_ms))) dirty = true;
        if(dirty) ask();
    }
};

} // namespace

lv_obj_t* build_alarm_summary(lv_obj_t* parent, const json& w)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, lv_pct(100), 300);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(obj, 6, 0);
    lv_obj_set_style_pad_row(obj, 6, 0);
    lv_obj_set_scrollable(obj, false);

    auto* c = new Ctx;
    c->obj = obj;
    c->days = std::clamp(static_cast<int>(jnum(w, "days", 7)), 0, 3650);
    c->sort = iequals(jstr(w, "sort", "count"), "time") ? AlarmSort::Time : AlarmSort::Count;
    c->rows = std::clamp(static_cast<int>(jnum(w, "rows", 10)), 1, 200);
    c->bars = jbool(w, "bars", true);
    c->severity = jstr(w, "severity", "");
    c->refresh_ms = static_cast<uint32_t>(std::max(0.0, jnum(w, "refresh_ms", 5000)));
    if(c->refresh_ms > 0 && c->refresh_ms < 1000) c->refresh_ms = 1000;
    if(const json* l = jget(w, "labels"); l && l->is_object())
        for(auto it = l->begin(); it != l->end(); ++it) if(it.value().is_string()) c->labels[it.key()] = log_view_text(texts().resolve(it.value().get<std::string>()), 64);
    if(std::find(c->periods.begin(), c->periods.end(), c->days) == c->periods.end()) c->periods.insert(c->periods.begin(), c->days);

    lv_obj_t* bar = lv_obj_create(obj);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_scrollable(bar, false);

    c->period_dd = lv_dropdown_create(bar);
    std::string opts;
    for(int d : c->periods) opts += (opts.empty() ? "" : "\n") + c->period_text(d);
    lv_dropdown_set_options(c->period_dd, opts.c_str());
    lv_dropdown_set_selected(c->period_dd, static_cast<uint32_t>(std::find(c->periods.begin(), c->periods.end(), c->days) - c->periods.begin()));
    lv_obj_set_size(c->period_dd, 104, 40);
    lv_obj_add_event_cb(c->period_dd, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        const uint32_t i = lv_dropdown_get_selected(cx->period_dd);
        if(i < cx->periods.size()) cx->days = cx->periods[i];
        cx->dirty = true;
        cx->tick();
    }, LV_EVENT_VALUE_CHANGED, c);

    c->sort_dd = lv_dropdown_create(bar);
    lv_dropdown_set_options(c->sort_dd, (c->labels["by_count"] + "\n" + c->labels["by_time"]).c_str());
    lv_dropdown_set_selected(c->sort_dd, c->sort == AlarmSort::Time ? 1 : 0);
    lv_obj_set_size(c->sort_dd, 104, 40);
    lv_obj_add_event_cb(c->sort_dd, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        cx->sort = lv_dropdown_get_selected(cx->sort_dd) == 1 ? AlarmSort::Time : AlarmSort::Count;
        cx->dirty = true;
        cx->tick();
    }, LV_EVENT_VALUE_CHANGED, c);

    c->total = lv_label_create(bar);
    lv_label_set_text(c->total, "");
    lv_obj_set_flex_grow(c->total, 1);
    lv_obj_set_style_text_align(c->total, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(c->total, LV_LABEL_LONG_MODE_DOTS);

    c->body = lv_obj_create(obj);
    lv_obj_remove_style_all(c->body);
    lv_obj_set_width(c->body, lv_pct(100));
    lv_obj_set_flex_grow(c->body, 1);
    lv_obj_set_flex_flow(c->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c->body, 3, 0);
    lv_obj_set_scroll_dir(c->body, LV_DIR_VER);

    c->timer = lv_timer_create([](lv_timer_t* t) { static_cast<Ctx*>(lv_timer_get_user_data(t))->tick(); }, kPollMs, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        if(cx->timer) lv_timer_delete(cx->timer);
        delete cx;
    }, LV_EVENT_DELETE, c);
    return obj;
}

void register_alarm_summary_actions()
{
    ui().register_action("alarm.export_summary", [](const std::string& arg) {
        const int days = std::clamp(std::atoi(arg.c_str()), 0, 3650);
        std::set<std::string> active;
        for(const std::string& n : alarms().active_names()) active.insert(n);
        std::string path;
        const int64_t n = alarm_stats_export(alarm_stats_recent(days, active), "", &path);
        if(n >= 0) HML_INFO("alarm summary: %lld rows exported to %s", static_cast<long long>(n), path.c_str());
        else HML_WARN("alarm summary: could not be exported");
    });
}

void alarm_summary_wait_idle() { BackgroundWorker::get().wait_idle(); }

} // namespace hml
