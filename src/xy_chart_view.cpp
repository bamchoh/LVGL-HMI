#include "locale_fmt.hpp"
#include "xy_chart_view.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr int32_t kSteps = 10000;         // the chart counts 0 .. kSteps on both axes, whatever the ranges
constexpr size_t  kMaxSeries = 4;

struct Series {
    lv_chart_series_t* ser = nullptr;
    Tag* x = nullptr;
    Tag* y = nullptr;
    std::deque<std::pair<double, double>> pts;    // oldest first, in the units of the tags
    std::deque<std::pair<int32_t, int32_t>> pending;   // taken, not yet given to the chart
    double last_x = 0, last_y = 0;
    bool   has_last = false;
};

struct Ctx {
    lv_obj_t*   obj = nullptr;            // the widget (a container)
    lv_obj_t*   chart = nullptr;
    lv_obj_t*   lx0 = nullptr, *lx1 = nullptr, *ly0 = nullptr, *ly1 = nullptr;
    lv_timer_t* timer = nullptr;
    double      x_min = 0, x_max = 100, y_min = 0, y_max = 100;
    size_t      points = 200;
    bool        on_change = false;
    Tag*        clear_tag = nullptr;
    bool        clear_was = false;
    uint32_t    refresh_ms = 250, drawn_at = 0;
    uint32_t    draws = 0;                // times points were handed to the chart
    std::vector<Series> series;

    // a value on the chart's own scale; outside the range it keeps its side (bounded, so that nothing overflows)
    static int32_t to_steps(double v, double lo, double hi)
    {
        const double s = (v - lo) / (hi - lo) * kSteps;
        return static_cast<int32_t>(std::llround(std::clamp(s, -3.0 * kSteps, 4.0 * kSteps)));
    }

    void add(Series& s, double x, double y)
    {
        s.pts.emplace_back(x, y);
        while(s.pts.size() > points) s.pts.pop_front();
        s.pending.emplace_back(to_steps(x, x_min, x_max), to_steps(y, y_min, y_max));
        while(s.pending.size() > points) s.pending.pop_front();
    }

    // The chart draws all of its lines again for every point it is given (measured on a Raspberry
    // Pi 4: 300 points at 20 points a second took a third of a core). So the points are handed
    // over a few times a second, all that came since, and only while the screen is shown.
    void draw(bool force = false)
    {
        if(!force && lv_tick_elaps(drawn_at) < refresh_ms) return;
        if(lv_obj_get_screen(obj) != lv_screen_active()) return;
        drawn_at = lv_tick_get();
        bool any = false;
        for(Series& s : series) {
            for(const auto& p : s.pending) lv_chart_set_next_value2(chart, s.ser, p.first, p.second);
            any = any || !s.pending.empty();
            s.pending.clear();
        }
        if(any) draws++;
    }

    void clear()
    {
        for(Series& s : series) {
            s.pts.clear();
            s.pending.clear();
            s.has_last = false;
            lv_chart_set_all_values(chart, s.ser, LV_CHART_POINT_NONE);
            // (next_value2 continues at the start again)
            lv_chart_set_x_start_point(chart, s.ser, 0);
        }
        lv_chart_refresh(chart);
    }

    void tick()
    {
        if(clear_tag) {
            const bool now = clear_tag->get().truthy();
            if(now && !clear_was) clear();
            clear_was = now;
        }
        for(Series& s : series) {
            if(!s.x || !s.y) continue;
            const double x = s.x->get_number(), y = s.y->get_number();
            if(!std::isfinite(x) || !std::isfinite(y)) continue;          // what is no number is no point
            if(on_change && s.has_last && x == s.last_x && y == s.last_y) continue;
            s.last_x = x; s.last_y = y; s.has_last = true;
            add(s, x, y);
        }
        draw();
    }

    void fit()
    {
        lv_obj_update_layout(obj);
        const int32_t w = lv_obj_get_content_width(obj), h = lv_obj_get_content_height(obj);
        lv_obj_update_layout(ly1);
        const int32_t left = std::max(lv_obj_get_width(ly0), lv_obj_get_width(ly1)) + 6, bottom = lv_obj_get_height(lx0) + 4;
        lv_obj_set_pos(chart, left, 0);
        lv_obj_set_size(chart, std::max<int32_t>(w - left, 20), std::max<int32_t>(h - bottom, 20));
        lv_obj_align(ly1, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_align(ly0, LV_ALIGN_BOTTOM_LEFT, 0, -bottom);
        lv_obj_align(lx0, LV_ALIGN_BOTTOM_LEFT, left, 0);
        lv_obj_align(lx1, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
};

std::unordered_map<lv_obj_t*, Ctx*>& registry() { static std::unordered_map<lv_obj_t*, Ctx*> r; return r; }

void range(const json& w, const char* lo_key, const char* hi_key, double& lo, double& hi)
{
    lo = jnum(w, lo_key, 0);
    hi = jnum(w, hi_key, 100);
    if(!std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(hi - lo) || !(hi - lo > 0)) {
        HML_WARN("xy_chart: \"%s\" (%g) has to be above \"%s\" (%g): using 0 .. 100", hi_key, hi, lo_key, lo);
        lo = 0; hi = 100;
    }
}

Tag* tag_of(const json& o, const char* key)
{
    const std::string name = jstr(o, key, "");
    if(name.empty()) return nullptr;
    Tag* t = tags().find(name);
    if(!t) HML_WARN("xy_chart: unknown tag '%s'", name.c_str());
    return t;
}

lv_obj_t* end_label(lv_obj_t* parent, const std::string& text)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
    return l;
}

} // namespace

lv_obj_t* build_xy_chart(lv_obj_t* parent, const json& w)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 300, 240);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);

    auto* c = new Ctx;
    c->obj = obj;
    range(w, "x_min", "x_max", c->x_min, c->x_max);
    range(w, "y_min", "y_max", c->y_min, c->y_max);
    c->points = static_cast<size_t>(std::clamp(jnum(w, "points", 200), 2.0, 2000.0));
    c->refresh_ms = static_cast<uint32_t>(std::clamp(jnum(w, "refresh_ms", 250), 50.0, 10000.0));
    const double sample = jnum(w, "sample_ms", 100);
    c->on_change = !(sample > 0);
    const uint32_t period = c->on_change ? 50 : static_cast<uint32_t>(std::clamp(sample, 20.0, 3600000.0));
    const bool dots = iequals(jstr(w, "mode", "line"), "dots");
    const int decimals = std::clamp(static_cast<int>(jnum(w, "decimals", 0)), 0, 3);

    c->chart = lv_chart_create(obj);
    lv_chart_set_type(c->chart, LV_CHART_TYPE_SCATTER);
    lv_chart_set_point_count(c->chart, static_cast<uint32_t>(c->points));
    lv_chart_set_axis_range(c->chart, LV_CHART_AXIS_PRIMARY_X, 0, kSteps);
    lv_chart_set_axis_range(c->chart, LV_CHART_AXIS_PRIMARY_Y, 0, kSteps);
    lv_chart_set_div_line_count(c->chart, static_cast<uint32_t>(std::clamp(jnum(w, "hdiv", 5), 0.0, 20.0)), static_cast<uint32_t>(std::clamp(jnum(w, "vdiv", 5), 0.0, 20.0)));
    lv_obj_set_style_pad_all(c->chart, 4, 0);
    lv_obj_set_clickable(c->chart, false);
    if(dots) {
        lv_obj_set_style_line_width(c->chart, 0, LV_PART_ITEMS);
        lv_obj_set_style_width(c->chart, 5, LV_PART_INDICATOR);
        lv_obj_set_style_height(c->chart, 5, LV_PART_INDICATOR);
    }
    else {
        lv_obj_set_style_line_width(c->chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_width(c->chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(c->chart, 0, LV_PART_INDICATOR);
    }

    static const lv_palette_t kColors[kMaxSeries] = { LV_PALETTE_BLUE, LV_PALETTE_RED, LV_PALETTE_GREEN, LV_PALETTE_ORANGE };
    auto add_series = [&](const json& o) {
        if(c->series.size() >= kMaxSeries) return;
        Series s;
        s.x = tag_of(o, "x_tag");
        s.y = tag_of(o, "y_tag");
        const lv_color_t col = parse_color(jstr(o, "color", ""), lv_palette_main(kColors[c->series.size()]));
        s.ser = lv_chart_add_series(c->chart, col, LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_values(c->chart, s.ser, LV_CHART_POINT_NONE);
        c->series.push_back(std::move(s));
    };
    add_series(w);
    if(const json* more = jget(w, "series"); more && more->is_array())
        for(const json& o : *more) if(o.is_object()) add_series(o);
    if(auto ct = jstr_opt(w, "clear_tag"); ct && !ct->empty()) {
        c->clear_tag = tags().find(*ct);
        if(!c->clear_tag) HML_WARN("xy_chart: unknown clear_tag '%s'", ct->c_str());
        else c->clear_was = c->clear_tag->get().truthy();
    }

    c->lx0 = end_label(obj, local_number_text(vformat("%.*f", decimals, c->x_min)));
    c->lx1 = end_label(obj, local_number_text(vformat("%.*f", decimals, c->x_max)));
    c->ly0 = end_label(obj, local_number_text(vformat("%.*f", decimals, c->y_min)));
    c->ly1 = end_label(obj, local_number_text(vformat("%.*f", decimals, c->y_max)));

    registry()[obj] = c;
    c->timer = lv_timer_create([](lv_timer_t* t) { static_cast<Ctx*>(lv_timer_get_user_data(t))->tick(); }, period, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) { static_cast<Ctx*>(lv_event_get_user_data(e))->fit(); }, LV_EVENT_SIZE_CHANGED, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        if(cx->timer) lv_timer_delete(cx->timer);
        registry().erase(cx->obj);
        delete cx;
    }, LV_EVENT_DELETE, c);
    c->fit();
    return obj;
}

std::vector<std::pair<double, double>> xy_chart_points(lv_obj_t* chart, int series)
{
    auto it = registry().find(chart);
    if(it == registry().end() || series < 0 || static_cast<size_t>(series) >= it->second->series.size()) return {};
    const auto& p = it->second->series[static_cast<size_t>(series)].pts;
    return { p.begin(), p.end() };
}

uint32_t xy_chart_draws(lv_obj_t* chart)
{
    auto it = registry().find(chart);
    return it == registry().end() ? 0 : it->second->draws;
}

int xy_chart_series(lv_obj_t* chart)
{
    auto it = registry().find(chart);
    return it == registry().end() ? 0 : static_cast<int>(it->second->series.size());
}

} // namespace hml
