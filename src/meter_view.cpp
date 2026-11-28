#include "locale_fmt.hpp"
#include "meter_view.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>
#include "log_query.hpp"
#include "texts.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

struct Ctx {
    lv_obj_t* obj = nullptr;
    lv_obj_t* scale = nullptr;
    lv_obj_t* needle = nullptr;
    lv_obj_t* hub = nullptr;
    lv_obj_t* label = nullptr;
    double    min = 0, max = 100, value = 0;
    int       decimals = 0;
    int       angle = 270;
    bool      has_value = false;
    std::string unit;
    std::vector<std::string> tick_text;
    std::vector<const char*> tick_ptr;                    // what the scale holds on to
    std::vector<std::unique_ptr<lv_style_t>> styles;      // of the zones (the scale keeps pointers)

    // The scale itself always counts 0 .. kSteps (it knows whole numbers only, and a range like
    // 0 .. 0.001 or -1e300 .. 1e300 must not become 0 .. 0 or leave the integers).
    static constexpr int32_t kSteps = 10000;
    int32_t to_scale(double v) const
    {
        if(!(v > min)) return 0;                             // (also what is no number)
        if(v >= max) return kSteps;
        return static_cast<int32_t>(std::llround((v - min) / (max - min) * kSteps));
    }
    double from_scale(int32_t n) const { return min + (max - min) * n / kSteps; }

    void show()
    {
        const int32_t size = lv_obj_get_width(scale);
        // the needle: from the middle to just short of the ticks
        lv_scale_set_line_needle_value(scale, needle, std::max<int32_t>(size / 2 - 14, 4), to_scale(has_value && std::isfinite(value) ? value : min));
        if(label) {
            const std::string t = !has_value || !std::isfinite(value) ? "---" : local_number_text(vformat("%.*f", decimals, value)) + (unit.empty() ? "" : " " + unit);
            lv_label_set_text(label, t.c_str());
        }
    }

    // a square as large as the widget allows, in its middle
    void fit()
    {
        lv_obj_update_layout(obj);
        const int32_t side = std::max<int32_t>(std::min(lv_obj_get_content_width(obj), lv_obj_get_content_height(obj)), 40);
        lv_obj_set_size(scale, side, side);
        lv_obj_center(scale);
        // a small meter has no room for the numbers of the scale: the ticks and the value stay
        lv_scale_set_label_show(scale, side >= 140);
        // The number sits in the opening of the scale, below its first and last number (seen on the
        // 480x320 device: beside them a "0.77 MPa" ran into "0.00" and "1.50"). A scale that goes
        // all the way round has no opening: there it sits under the hub.
        if(label) lv_obj_align(label, LV_ALIGN_CENTER, 0, angle > 300 ? side / 5 : angle <= 200 ? side / 4 : side * 2 / 5);
        show();
    }
};

std::unordered_map<lv_obj_t*, Ctx*>& registry() { static std::unordered_map<lv_obj_t*, Ctx*> r; return r; }
Ctx* ctx_of(lv_obj_t* obj) { auto it = registry().find(obj); return it == registry().end() ? nullptr : it->second; }

} // namespace

lv_obj_t* build_meter(lv_obj_t* parent, const json& w)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 200, 200);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);

    auto* c = new Ctx;
    c->obj = obj;
    c->min = jnum(w, "min", 0);
    c->max = jnum(w, "max", 100);
    if(!std::isfinite(c->min) || !std::isfinite(c->max) || !std::isfinite(c->max - c->min) || !(c->max - c->min > 0)) {
        HML_WARN("meter: \"max\" (%g) has to be above \"min\" (%g): using 0 .. 100", c->max, c->min);
        c->min = 0; c->max = 100;
    }
    c->decimals = std::clamp(static_cast<int>(jnum(w, "decimals", 0)), 0, 3);
    c->angle = std::clamp(static_cast<int>(jnum(w, "angle", 270)), 90, 360);
    c->unit = log_view_text(texts().resolve(jstr(w, "unit", "")), 16);
    const int ticks = std::clamp(static_cast<int>(jnum(w, "ticks", 21)), 2, 101);
    const int major = std::clamp(static_cast<int>(jnum(w, "major_every", 5)), 1, 100);

    c->scale = lv_scale_create(obj);
    lv_scale_set_mode(c->scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(c->scale, 0, Ctx::kSteps);
    lv_scale_set_angle_range(c->scale, static_cast<uint32_t>(c->angle));
    lv_scale_set_rotation(c->scale, 90 + (360 - c->angle) / 2);            // the gap at the bottom
    lv_scale_set_total_tick_count(c->scale, static_cast<uint32_t>(ticks));
    lv_scale_set_major_tick_every(c->scale, static_cast<uint32_t>(major));
    lv_scale_set_label_show(c->scale, true);
    lv_obj_set_style_length(c->scale, 6, LV_PART_ITEMS);
    lv_obj_set_style_length(c->scale, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(c->scale, 3, LV_PART_MAIN);
    lv_obj_set_clickable(c->scale, false);
    // the numbers at the long ticks, with the decimals asked for
    const int majors = (ticks - 1) / major + 1;
    for(int i = 0; i < majors; i++) {
        const double v = c->min + (c->max - c->min) * (i * major) / (ticks - 1);
        c->tick_text.push_back(local_number_text(vformat("%.*f", c->decimals, v)));
    }
    if(c->angle == 360 && c->tick_text.size() > 1) c->tick_text.back().clear();   // (a full circle: the end is where the start is)
    for(const std::string& s : c->tick_text) c->tick_ptr.push_back(s.c_str());
    c->tick_ptr.push_back(nullptr);
    lv_scale_set_text_src(c->scale, c->tick_ptr.data());

    if(const json* zones = jget(w, "zones"); zones && zones->is_array()) {
        for(const json& z : *zones) {
            if(!z.is_object() || c->styles.size() >= 3 * 8) continue;
            double from = jnum(z, "from", c->min), to = jnum(z, "to", c->max);
            if(!std::isfinite(from) || !std::isfinite(to)) continue;
            from = std::clamp(from, c->min, c->max);
            to = std::clamp(to, c->min, c->max);
            if(to <= from || c->to_scale(to) <= c->to_scale(from)) continue;
            const lv_color_t col = parse_color(jstr(z, "color", ""), lv_palette_main(LV_PALETTE_RED));
            lv_scale_section_t* sec = lv_scale_add_section(c->scale);
            lv_scale_set_section_range(c->scale, sec, c->to_scale(from), c->to_scale(to));
            auto style = [&](bool arc) {
                c->styles.push_back(std::make_unique<lv_style_t>());
                lv_style_t* s = c->styles.back().get();
                lv_style_init(s);
                lv_style_set_line_color(s, col);
                lv_style_set_text_color(s, col);
                if(arc) { lv_style_set_arc_color(s, col); lv_style_set_arc_width(s, 6); }
                return s;
            };
            lv_scale_set_section_style_main(c->scale, sec, style(true));
            lv_scale_set_section_style_indicator(c->scale, sec, style(false));
            lv_scale_set_section_style_items(c->scale, sec, style(false));
        }
    }

    const lv_color_t needle = parse_color(jstr(w, "needle_color", ""), lv_palette_main(LV_PALETTE_RED));
    c->needle = lv_line_create(c->scale);
    lv_obj_set_style_line_width(c->needle, 4, 0);
    lv_obj_set_style_line_rounded(c->needle, true, 0);
    lv_obj_set_style_line_color(c->needle, needle, 0);
    c->hub = lv_obj_create(c->scale);
    lv_obj_remove_style_all(c->hub);
    lv_obj_set_size(c->hub, 14, 14);
    lv_obj_set_style_radius(c->hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c->hub, needle, 0);
    lv_obj_set_style_bg_opa(c->hub, LV_OPA_COVER, 0);
    lv_obj_center(c->hub);
    if(jbool(w, "show_value", true)) {
        c->label = lv_label_create(obj);
        lv_label_set_text(c->label, "---");
    }
    c->value = jnum(w, "value", c->min);
    c->has_value = w.contains("value");

    registry()[obj] = c;
    lv_obj_add_event_cb(obj, [](lv_event_t* e) { static_cast<Ctx*>(lv_event_get_user_data(e))->fit(); }, LV_EVENT_SIZE_CHANGED, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        registry().erase(cx->obj);
        for(auto& s : cx->styles) lv_style_reset(s.get());
        delete cx;
    }, LV_EVENT_DELETE, c);
    c->fit();
    return obj;
}

void meter_set_value(lv_obj_t* meter, double value)
{
    Ctx* c = ctx_of(meter);
    if(!c) return;
    c->value = value;
    c->has_value = true;
    c->show();
}

double meter_needle_value(lv_obj_t* meter)
{
    Ctx* c = ctx_of(meter);
    return c ? c->from_scale(c->to_scale(c->has_value && std::isfinite(c->value) ? c->value : c->min)) : 0;
}

std::string meter_value_text(lv_obj_t* meter)
{
    Ctx* c = ctx_of(meter);
    return c && c->label ? lv_label_get_text(c->label) : "";
}

} // namespace hml
