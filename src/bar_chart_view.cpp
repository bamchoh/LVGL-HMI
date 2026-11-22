#include "bar_chart_view.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <string>
#include <unordered_map>
#include "data_log.hpp"
#include "logdb.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr int kMaxBars = 48;
int64_t s_now_for_tests = 0;

int64_t now_ms() { return s_now_for_tests ? s_now_for_tests : LogDb::now_ms(); }

std::tm local(int64_t ms)
{
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// local midnight of the day of ms
int64_t midnight(int64_t ms)
{
    std::tm tm = local(ms);
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&tm)) * 1000;
}

struct Bar {
    lv_obj_t* value = nullptr;    // the number above
    lv_obj_t* area = nullptr;     // where the bar grows
    lv_obj_t* bar = nullptr;
    lv_obj_t* mark = nullptr;     // the target, a piece of a line across the chart
    lv_obj_t* time = nullptr;     // the time below
};

struct Ctx {
    lv_obj_t*   obj = nullptr;
    lv_timer_t* timer = nullptr;
    std::string group, tag, label_fmt;
    bool        increase = true, from_day = false, show_values = true;
    int64_t     bucket_ms = 3600000;
    int         bars = 12, decimals = 0;
    double      max = 0, target = std::numeric_limits<double>::quiet_NaN();
    Tag*        target_tag = nullptr;
    std::vector<Bar>    ui;
    std::vector<double> values;

    // the time slices: [from + i * bucket, from + (i + 1) * bucket)
    int64_t from_ms(int64_t now) const
    {
        const int64_t m = midnight(now);
        if(from_day) return m;
        const int64_t end = m + ((now - m) / bucket_ms + 1) * bucket_ms;      // the slice of now is the last
        return end - static_cast<int64_t>(bars) * bucket_ms;
    }

    void refresh()
    {
        const int64_t now = now_ms(), from = from_ms(now), to = from + static_cast<int64_t>(bars) * bucket_ms;
        values.assign(static_cast<size_t>(bars), std::numeric_limits<double>::quiet_NaN());
        // one slice more in front: where the counter stood before the first bar
        auto data = data_log().trend(group, { tag }, from - bucket_ms, to, bars + 1);
        if(!data.empty() && data[0].size() == static_cast<size_t>(bars) + 1) {
            const auto& b = data[0];
            bool has_prev = b[0].has;
            double prev = b[0].last;
            for(int i = 0; i < bars; i++) {
                const TrendBucket& s = b[static_cast<size_t>(i) + 1];
                const int64_t start = from + static_cast<int64_t>(i) * bucket_ms;
                if(start > now) break;                                       // the future: empty
                if(increase) {
                    if(!s.has) { values[static_cast<size_t>(i)] = has_prev ? 0 : std::numeric_limits<double>::quiet_NaN(); continue; }
                    const double base = has_prev ? prev : s.min;
                    // set back (a new day): what it counted since then, at least
                    values[static_cast<size_t>(i)] = s.last >= base ? s.last - base : std::max(0.0, s.last);
                    prev = s.last;
                    has_prev = true;
                }
                else if(s.has) values[static_cast<size_t>(i)] = s.last;
            }
        }
        draw(from, now);
    }

    void draw(int64_t from, int64_t now)
    {
        double top = max;
        double tgt = target_tag ? target_tag->get_number() : target;
        if(top <= 0) {
            top = 0;
            for(double v : values) if(!std::isnan(v)) top = std::max(top, v);
            if(!std::isnan(tgt)) top = std::max(top, tgt);
            top = top > 0 ? top * 1.1 : 1;
        }
        for(int i = 0; i < bars; i++) {
            Bar& u = ui[static_cast<size_t>(i)];
            const double v = values[static_cast<size_t>(i)];
            const int32_t pct = std::isnan(v) || v <= 0 ? 0 : static_cast<int32_t>(std::lround(std::min(1.0, v / top) * 100));
            lv_obj_set_height(u.bar, lv_pct(pct));
            lv_obj_set_hidden(u.bar, pct == 0);
            lv_label_set_text(u.value, std::isnan(v) || !show_values ? "" : vformat("%.*f", decimals, v).c_str());
            if(!std::isnan(tgt) && tgt > 0) {
                const int32_t tp = static_cast<int32_t>(std::lround(std::min(1.0, tgt / top) * 100));
                lv_obj_set_y(u.mark, lv_pct(100 - tp));
                lv_obj_set_hidden(u.mark, false);
            }
            else lv_obj_set_hidden(u.mark, true);
            const int64_t start = from + static_cast<int64_t>(i) * bucket_ms;
            char buf[32] = "";
            std::tm tm = local(start);
            std::strftime(buf, sizeof buf, label_fmt.c_str(), &tm);
            lv_label_set_text(u.time, buf);
            // the slice of now is not complete: lighter
            lv_obj_set_style_bg_opa(u.bar, static_cast<lv_opa_t>(start + bucket_ms > now ? LV_OPA_60 : LV_OPA_COVER), 0);
        }
    }
};

std::unordered_map<lv_obj_t*, Ctx*>& registry() { static std::unordered_map<lv_obj_t*, Ctx*> r; return r; }

lv_obj_t* plain(lv_obj_t* parent)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

} // namespace

lv_obj_t* build_bar_chart(lv_obj_t* parent, const json& w)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(obj, 6, 0);
    auto* c = new Ctx();
    c->obj = obj;
    c->group = jstr(w, "group", "");
    c->tag = jstr(w, "tag", "");
    const std::string mode = jstr(w, "mode", "increase");
    c->increase = !iequals(mode, "value");
    if(!iequals(mode, "increase") && !iequals(mode, "value")) HML_WARN("bar_chart: unknown mode '%s' (increase, value)", mode.c_str());
    const double bs = std::clamp(jnum(w, "bucket_s", 3600), 60.0, 86400.0);
    c->bucket_ms = static_cast<int64_t>(bs) * 1000;
    c->bars = std::clamp(static_cast<int>(jnum(w, "bars", 12)), 1, kMaxBars);
    c->from_day = iequals(jstr(w, "start", "now"), "day");
    c->max = jnum(w, "max", 0);
    c->decimals = std::clamp(static_cast<int>(jnum(w, "decimals", 0)), 0, 3);
    c->show_values = jbool(w, "show_values", true);
    c->label_fmt = jstr(w, "label", bs >= 86400 ? "%m/%d" : bs >= 3600 ? "%H" : "%H:%M");
    if(const json* t = jget(w, "target")) {
        if(t->is_number()) c->target = t->get<double>();
        else if(t->is_object() || t->is_string()) {
            const std::string n = t->is_string() ? t->get<std::string>() : jstr(*t, "tag", "");
            c->target_tag = tags().find(n);
            if(!c->target_tag) HML_WARN("bar_chart: unknown target tag '%s'", n.c_str());
        }
    }
    if(c->group.empty() || c->tag.empty()) HML_WARN("bar_chart: \"group\" and \"tag\" are needed");
    const lv_color_t color = parse_color(jstr(w, "color", ""), lv_theme_get_color_primary(obj));
    const lv_color_t tcolor = parse_color(jstr(w, "target_color", ""), lv_palette_main(LV_PALETTE_AMBER));

    for(int i = 0; i < c->bars; i++) {
        Bar u;
        lv_obj_t* col = plain(obj);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, lv_pct(100));
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 2, 0);
        u.value = lv_label_create(col);
        lv_label_set_text(u.value, "");
        lv_obj_set_style_text_align(u.value, LV_TEXT_ALIGN_CENTER, 0);
        u.area = plain(col);
        lv_obj_set_width(u.area, lv_pct(100));
        lv_obj_set_flex_grow(u.area, 1);
        u.bar = plain(u.area);
        lv_obj_set_width(u.bar, lv_pct(70));
        lv_obj_align(u.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(u.bar, color, 0);
        lv_obj_set_style_bg_opa(u.bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(u.bar, 2, 0);
        u.mark = plain(u.area);
        lv_obj_set_size(u.mark, lv_pct(100), 2);
        lv_obj_set_style_bg_color(u.mark, tcolor, 0);
        lv_obj_set_style_bg_opa(u.mark, LV_OPA_COVER, 0);
        lv_obj_set_hidden(u.mark, true);
        u.time = lv_label_create(col);
        lv_label_set_text(u.time, "");
        lv_obj_set_style_text_opa(u.time, LV_OPA_70, 0);
        c->ui.push_back(u);
    }

    registry()[obj] = c;
    const uint32_t period = static_cast<uint32_t>(std::clamp(jnum(w, "refresh_ms", 10000), 1000.0, 3600000.0));
    c->timer = lv_timer_create([](lv_timer_t* t) {
        auto* cx = static_cast<Ctx*>(lv_timer_get_user_data(t));
        if(lv_obj_get_screen(cx->obj) == lv_screen_active()) cx->refresh();      // (only while it is seen)
    }, period, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        if(cx->timer) lv_timer_delete(cx->timer);
        registry().erase(cx->obj);
        delete cx;
    }, LV_EVENT_DELETE, c);
    // shown again: the data at once, not after a period
    lv_obj_add_event_cb(lv_obj_get_screen(obj), [](lv_event_t* e) {
        auto it = registry().find(static_cast<lv_obj_t*>(lv_event_get_user_data(e)));
        if(it != registry().end()) it->second->refresh();
    }, LV_EVENT_SCREEN_LOADED, obj);
    c->refresh();
    return obj;
}

void bar_chart_refresh(lv_obj_t* obj)
{
    auto it = registry().find(obj);
    if(it != registry().end()) it->second->refresh();
}

std::vector<double> bar_chart_values(lv_obj_t* obj)
{
    auto it = registry().find(obj);
    return it == registry().end() ? std::vector<double>{} : it->second->values;
}

void bar_chart_set_now_for_tests(int64_t ms) { s_now_for_tests = ms; }

} // namespace hml
