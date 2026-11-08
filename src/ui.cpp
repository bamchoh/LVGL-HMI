#include "ui.hpp"
#include "tag.hpp"
#include "util.hpp"
#include <cmath>
#include <cstdlib>
#include <deque>
#include <vector>

namespace hml {

// ================================================================ helpers

lv_color_t parse_color(const std::string& s, lv_color_t dflt)
{
    if(s.empty()) return dflt;
    if(s[0] == '#') {
        unsigned v = static_cast<unsigned>(std::strtoul(s.c_str() + 1, nullptr, 16));
        if(s.size() == 4) {
            unsigned r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
            return lv_color_make(static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17), static_cast<uint8_t>(b * 17));
        }
        return lv_color_hex(v);
    }
    static const struct { const char* name; lv_palette_t p; } palette[] = {
        { "red", LV_PALETTE_RED }, { "pink", LV_PALETTE_PINK }, { "purple", LV_PALETTE_PURPLE },
        { "deep_purple", LV_PALETTE_DEEP_PURPLE }, { "indigo", LV_PALETTE_INDIGO }, { "blue", LV_PALETTE_BLUE },
        { "light_blue", LV_PALETTE_LIGHT_BLUE }, { "cyan", LV_PALETTE_CYAN }, { "teal", LV_PALETTE_TEAL },
        { "green", LV_PALETTE_GREEN }, { "light_green", LV_PALETTE_LIGHT_GREEN }, { "lime", LV_PALETTE_LIME },
        { "yellow", LV_PALETTE_YELLOW }, { "amber", LV_PALETTE_AMBER }, { "orange", LV_PALETTE_ORANGE },
        { "deep_orange", LV_PALETTE_DEEP_ORANGE }, { "brown", LV_PALETTE_BROWN }, { "grey", LV_PALETTE_GREY },
        { "gray", LV_PALETTE_GREY }, { "blue_grey", LV_PALETTE_BLUE_GREY },
    };
    for(const auto& p : palette) if(iequals(s, p.name)) return lv_palette_main(p.p);
    if(iequals(s, "white")) return lv_color_white();
    if(iequals(s, "black")) return lv_color_black();
    HML_WARN("unknown color '%s'", s.c_str());
    return dflt;
}

const lv_font_t* font_from_json(const json* v)
{
    int size = 0;
    if(v && v->is_number()) size = static_cast<int>(v->get<double>());
    else if(v && v->is_string()) {
        const std::string& s = v->get_ref<const std::string&>();
        size_t p = s.rfind('_');
        size = std::atoi(p == std::string::npos ? s.c_str() : s.c_str() + p + 1);
    }
    switch(size) {
        case 12: return &lv_font_montserrat_12;
        case 14: return &lv_font_montserrat_14;
        case 16: return &lv_font_montserrat_16;
        case 18: return &lv_font_montserrat_18;
        case 20: return &lv_font_montserrat_20;
        case 24: return &lv_font_montserrat_24;
        case 28: return &lv_font_montserrat_28;
        case 32: return &lv_font_montserrat_32;
        case 40: return &lv_font_montserrat_40;
        case 48: return &lv_font_montserrat_48;
        default:
            HML_WARN("font size %d not available (12,14,16,18,20,24,28,32,40,48)", size);
            return LV_FONT_DEFAULT;
    }
}

namespace {

int32_t parse_size(const json* v, int32_t dflt)
{
    if(!v) return dflt;
    if(v->is_number()) return static_cast<int32_t>(v->get<double>());
    if(v->is_string()) {
        const std::string& s = v->get_ref<const std::string&>();
        if(iequals(s, "content")) return LV_SIZE_CONTENT;
        if(iequals(s, "full")) return lv_pct(100);
        if(s.size() > 1 && s.back() == '%') return lv_pct(std::atoi(s.c_str()));
        return std::atoi(s.c_str());
    }
    return dflt;
}

lv_align_t parse_align(const std::string& s)
{
    static const struct { const char* n; lv_align_t a; } tbl[] = {
        { "center", LV_ALIGN_CENTER }, { "top_left", LV_ALIGN_TOP_LEFT }, { "top_mid", LV_ALIGN_TOP_MID },
        { "top_right", LV_ALIGN_TOP_RIGHT }, { "left_mid", LV_ALIGN_LEFT_MID }, { "right_mid", LV_ALIGN_RIGHT_MID },
        { "bottom_left", LV_ALIGN_BOTTOM_LEFT }, { "bottom_mid", LV_ALIGN_BOTTOM_MID },
        { "bottom_right", LV_ALIGN_BOTTOM_RIGHT }, { "top", LV_ALIGN_TOP_MID }, { "bottom", LV_ALIGN_BOTTOM_MID },
        { "left", LV_ALIGN_LEFT_MID }, { "right", LV_ALIGN_RIGHT_MID },
    };
    for(const auto& t : tbl) if(iequals(s, t.n)) return t.a;
    HML_WARN("unknown align '%s'", s.c_str());
    return LV_ALIGN_TOP_LEFT;
}

lv_flex_align_t parse_flex_align(const std::optional<std::string>& s, lv_flex_align_t dflt)
{
    if(!s) return dflt;
    if(iequals(*s, "start")) return LV_FLEX_ALIGN_START;
    if(iequals(*s, "end")) return LV_FLEX_ALIGN_END;
    if(iequals(*s, "center")) return LV_FLEX_ALIGN_CENTER;
    if(iequals(*s, "space_evenly")) return LV_FLEX_ALIGN_SPACE_EVENLY;
    if(iequals(*s, "space_around")) return LV_FLEX_ALIGN_SPACE_AROUND;
    if(iequals(*s, "space_between")) return LV_FLEX_ALIGN_SPACE_BETWEEN;
    return dflt;
}

lv_screen_load_anim_t parse_anim(const std::string& s)
{
    if(s.empty() || iequals(s, "none")) return LV_SCREEN_LOAD_ANIM_NONE;
    if(iequals(s, "fade")) return LV_SCREEN_LOAD_ANIM_FADE_IN;
    if(iequals(s, "slide_left") || iequals(s, "move_left")) return LV_SCREEN_LOAD_ANIM_MOVE_LEFT;
    if(iequals(s, "slide_right") || iequals(s, "move_right")) return LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
    if(iequals(s, "slide_top") || iequals(s, "move_top")) return LV_SCREEN_LOAD_ANIM_MOVE_TOP;
    if(iequals(s, "slide_bottom") || iequals(s, "move_bottom")) return LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM;
    if(iequals(s, "over_left")) return LV_SCREEN_LOAD_ANIM_OVER_LEFT;
    if(iequals(s, "over_right")) return LV_SCREEN_LOAD_ANIM_OVER_RIGHT;
    return LV_SCREEN_LOAD_ANIM_FADE_IN;
}

// Join a JSON array with '\n' (dropdown / roller options).
std::string join_options(const json* arr)
{
    std::string out;
    if(!arr || !arr->is_array()) return out;
    for(const json& e : *arr) {
        if(!out.empty()) out += '\n';
        out += jscalar_to_string(e);
    }
    return out;
}

lv_opa_t pct_to_opa(int32_t pct) { return static_cast<lv_opa_t>(pct * 255 / 100); }

lv_style_selector_t sel(lv_part_t part, lv_state_t state = LV_STATE_DEFAULT)
{
    return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

// Show text in a textarea bypassing the operator-input filters (accepted_chars / max_length),
// which lv_textarea_set_text would otherwise apply to the displayed value too ("42.3 rpm").
void ta_show(lv_obj_t* ta, const std::string& text)
{
    const char* accepted = lv_textarea_get_accepted_chars(ta);
    uint32_t max_len = lv_textarea_get_max_length(ta);
    lv_textarea_set_accepted_chars(ta, nullptr);
    lv_textarea_set_max_length(ta, 0);
    lv_textarea_set_text(ta, text.c_str());
    lv_textarea_set_accepted_chars(ta, accepted);
    lv_textarea_set_max_length(ta, max_len);
}

// ================================================================ styles

void apply_style(lv_obj_t* obj, const json& st, lv_style_selector_t s)
{
    if(!st.is_object()) return;
    for(auto it = st.begin(); it != st.end(); ++it) {
        const std::string& k = it.key();
        const json& p = it.value();
        std::string sv = p.is_string() ? p.get<std::string>() : "";
        int32_t nv = p.is_number() ? static_cast<int32_t>(p.get<double>()) : (p.is_boolean() ? p.get<bool>() : 0);
        auto color = [&] { return parse_color(sv, lv_color_black()); };

        if(iequals(k, "bg_color")) { lv_obj_set_style_bg_color(obj, color(), s); lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, s); }
        else if(iequals(k, "bg_opa")) lv_obj_set_style_bg_opa(obj, pct_to_opa(nv), s);
        else if(iequals(k, "bg_grad_color")) lv_obj_set_style_bg_grad_color(obj, color(), s);
        else if(iequals(k, "bg_grad_dir")) lv_obj_set_style_bg_grad_dir(obj, iequals(sv, "hor") ? LV_GRAD_DIR_HOR : iequals(sv, "ver") ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, s);
        else if(iequals(k, "border_color")) lv_obj_set_style_border_color(obj, color(), s);
        else if(iequals(k, "border_width")) lv_obj_set_style_border_width(obj, nv, s);
        else if(iequals(k, "border_opa")) lv_obj_set_style_border_opa(obj, pct_to_opa(nv), s);
        else if(iequals(k, "outline_width")) lv_obj_set_style_outline_width(obj, nv, s);
        else if(iequals(k, "outline_color")) lv_obj_set_style_outline_color(obj, color(), s);
        else if(iequals(k, "radius")) lv_obj_set_style_radius(obj, nv, s);
        else if(iequals(k, "shadow_width")) lv_obj_set_style_shadow_width(obj, nv, s);
        else if(iequals(k, "shadow_color")) lv_obj_set_style_shadow_color(obj, color(), s);
        else if(iequals(k, "shadow_opa")) lv_obj_set_style_shadow_opa(obj, pct_to_opa(nv), s);
        else if(iequals(k, "text_color")) lv_obj_set_style_text_color(obj, color(), s);
        else if(iequals(k, "text_opa")) lv_obj_set_style_text_opa(obj, pct_to_opa(nv), s);
        else if(iequals(k, "text_align")) lv_obj_set_style_text_align(obj, iequals(sv, "center") ? LV_TEXT_ALIGN_CENTER : iequals(sv, "right") ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, s);
        else if(iequals(k, "text_letter_space")) lv_obj_set_style_text_letter_space(obj, nv, s);
        else if(iequals(k, "text_line_space")) lv_obj_set_style_text_line_space(obj, nv, s);
        else if(iequals(k, "font")) lv_obj_set_style_text_font(obj, font_from_json(&p), s);
        else if(iequals(k, "pad")) lv_obj_set_style_pad_all(obj, nv, s);
        else if(iequals(k, "pad_left")) lv_obj_set_style_pad_left(obj, nv, s);
        else if(iequals(k, "pad_right")) lv_obj_set_style_pad_right(obj, nv, s);
        else if(iequals(k, "pad_top")) lv_obj_set_style_pad_top(obj, nv, s);
        else if(iequals(k, "pad_bottom")) lv_obj_set_style_pad_bottom(obj, nv, s);
        else if(iequals(k, "pad_row")) lv_obj_set_style_pad_row(obj, nv, s);
        else if(iequals(k, "pad_column")) lv_obj_set_style_pad_column(obj, nv, s);
        else if(iequals(k, "pad_gap")) lv_obj_set_style_pad_gap(obj, nv, s);
        else if(iequals(k, "margin")) lv_obj_set_style_margin_all(obj, nv, s);
        else if(iequals(k, "opa")) lv_obj_set_style_opa(obj, pct_to_opa(nv), s);
        else if(iequals(k, "line_color")) lv_obj_set_style_line_color(obj, color(), s);
        else if(iequals(k, "line_width")) lv_obj_set_style_line_width(obj, nv, s);
        else if(iequals(k, "line_rounded")) lv_obj_set_style_line_rounded(obj, nv != 0, s);
        else if(iequals(k, "arc_color")) lv_obj_set_style_arc_color(obj, color(), s);
        else if(iequals(k, "arc_width")) lv_obj_set_style_arc_width(obj, nv, s);
        else if(iequals(k, "arc_rounded")) lv_obj_set_style_arc_rounded(obj, nv != 0, s);
        else if(iequals(k, "image_recolor")) { lv_obj_set_style_image_recolor(obj, color(), s); lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, s); }
        else if(iequals(k, "width")) lv_obj_set_style_width(obj, parse_size(&p, 0), s);
        else if(iequals(k, "height")) lv_obj_set_style_height(obj, parse_size(&p, 0), s);
        else HML_WARN("unknown style property '%s'", k.c_str());
    }
}

void apply_styles(lv_obj_t* obj, const json& w)
{
    static const struct { const char* key; lv_style_selector_t s; } tbl[] = {
        { "style", sel(LV_PART_MAIN) },
        { "style_pressed", sel(LV_PART_MAIN, LV_STATE_PRESSED) },
        { "style_checked", sel(LV_PART_MAIN, LV_STATE_CHECKED) },
        { "style_disabled", sel(LV_PART_MAIN, LV_STATE_DISABLED) },
        { "style_focused", sel(LV_PART_MAIN, LV_STATE_FOCUSED) },
        { "style_indicator", sel(LV_PART_INDICATOR) },
        { "style_indicator_checked", sel(LV_PART_INDICATOR, LV_STATE_CHECKED) },
        { "style_knob", sel(LV_PART_KNOB) },
        { "style_items", sel(LV_PART_ITEMS) },
        { "style_selected", sel(LV_PART_SELECTED) },
        { "style_scrollbar", sel(LV_PART_SCROLLBAR) },
    };
    for(const auto& t : tbl) {
        if(const json* st = jget(w, t.key)) apply_style(obj, *st, t.s);
    }
}

// ================================================================ model

enum class Widget { Panel, Label, Button, Switch, Checkbox, Slider, Bar, Arc, Led, Dropdown, Roller, Textarea, Image, Chart, Spinner, Keyboard, Line };
enum class BindKind { Text, Value, Checked, Visible, Enabled, BgColor, TextColor, Options };
enum class Cmp { None, Eq, Ne, Gt, Lt, Ge, Le };

struct Binding {
    lv_obj_t*   obj = nullptr;        // the widget (for buttons with text: the inner label for Text)
    Widget      wk = Widget::Panel;
    BindKind    kind = BindKind::Text;
    Tag*        tag = nullptr;
    std::string format;
    bool        invert = false;
    Cmp         cmp = Cmp::None;
    double      cmp_num = 0;
    std::string cmp_str;
    bool        cmp_is_str = false;
    Tag*        cmp_tag = nullptr;    // compare against another tag's live value instead of a constant
    double      scale = 1, offset = 0;    // widget = tag * scale + offset (Value)
    std::vector<std::pair<double, lv_color_t>> color_map;
    std::optional<lv_color_t> default_color;
    lv_style_selector_t part = LV_PART_MAIN;
    bool        two_way = false;
    std::optional<double> vmin, vmax;     // numeric_display "min"/"max": clamp what the operator typed
    bool        commit_on_ready = false;  // data displays: write on READY / DEFOCUSED, revert on CANCEL

    bool condition(const Value& v) const;
    std::optional<lv_color_t> map_color(const Value& v) const;
    void apply_value(double n);
    void on_tag_changed(const Value& v, const void* source);
    void on_widget_changed(lv_obj_t* target);
};

static bool compare_numbers(Cmp cmp, double a, double b)
{
    switch(cmp) {
        case Cmp::Eq: return a == b;
        case Cmp::Ne: return a != b;
        case Cmp::Gt: return a > b;
        case Cmp::Lt: return a < b;
        case Cmp::Ge: return a >= b;
        case Cmp::Le: return a <= b;
        default: return false;
    }
}

static bool compare_strings(Cmp cmp, const std::string& a, const std::string& b)
{
    return (cmp == Cmp::Eq) ? a == b : (cmp == Cmp::Ne) ? a != b : false;
}

bool Binding::condition(const Value& v) const
{
    bool r;
    if(cmp == Cmp::None) r = v.truthy();
    else if(cmp_tag) {
        Value other = cmp_tag->get();
        if(v.is_string() && other.is_string()) r = compare_strings(cmp, v.str, other.str);
        else r = compare_numbers(cmp, v.as_number(), other.as_number());
    }
    else if(cmp_is_str) r = compare_strings(cmp, v.is_string() ? v.str : std::string(), cmp_str);
    else r = compare_numbers(cmp, v.as_number(), cmp_num);
    return invert ? !r : r;
}

std::optional<lv_color_t> Binding::map_color(const Value& v) const
{
    double n = v.as_number();
    for(const auto& m : color_map) if(m.first == n) return m.second;
    return default_color;
}

void Binding::apply_value(double n)
{
    int32_t iv = static_cast<int32_t>(std::llround(n * scale + offset));
    switch(wk) {
        case Widget::Slider:   lv_slider_set_value(obj, iv, LV_ANIM_OFF); break;
        case Widget::Bar:      lv_bar_set_value(obj, iv, LV_ANIM_ON); break;
        case Widget::Arc:      lv_arc_set_value(obj, iv); break;
        case Widget::Dropdown: lv_dropdown_set_selected(obj, static_cast<uint32_t>(iv < 0 ? 0 : iv)); break;
        case Widget::Roller:   lv_roller_set_selected(obj, static_cast<uint32_t>(iv < 0 ? 0 : iv), LV_ANIM_OFF); break;
        case Widget::Led:      lv_led_set_brightness(obj, static_cast<uint8_t>(iv < 0 ? 0 : iv > 255 ? 255 : iv)); break;
        default: {
            std::string text = Value::number(n * scale + offset).format(format);
            if(wk == Widget::Textarea) ta_show(obj, text);
            else if(wk == Widget::Checkbox) lv_checkbox_set_text(obj, text.c_str());
            else lv_label_set_text(obj, text.c_str());
            break;
        }
    }
}

void Binding::on_tag_changed(const Value& v, const void* source)
{
    if(source == this) return;   // our own write echoed back
    switch(kind) {
        case BindKind::Text: {
            std::string text = v.format(format);
            if(wk == Widget::Textarea) ta_show(obj, text);
            else if(wk == Widget::Checkbox) lv_checkbox_set_text(obj, text.c_str());
            else if(wk == Widget::Image) lv_image_set_src(obj, text.c_str());
            else lv_label_set_text(obj, text.c_str());
            break;
        }
        case BindKind::Value:
            apply_value(v.as_number());
            break;
        case BindKind::Checked: {
            bool on = condition(v);
            if(wk == Widget::Led) { if(on) lv_led_on(obj); else lv_led_off(obj); }
            else if(on) lv_obj_add_state(obj, LV_STATE_CHECKED);
            else lv_obj_remove_state(obj, LV_STATE_CHECKED);
            break;
        }
        case BindKind::Visible:
            if(condition(v)) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            break;
        case BindKind::Enabled:
            if(condition(v)) lv_obj_remove_state(obj, LV_STATE_DISABLED);
            else lv_obj_add_state(obj, LV_STATE_DISABLED);
            break;
        case BindKind::BgColor:
            if(auto c = map_color(v)) {
                lv_obj_set_style_bg_color(obj, *c, part);
                lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, part);
            }
            break;
        case BindKind::TextColor:
            if(auto c = map_color(v)) lv_obj_set_style_text_color(obj, *c, part);
            break;
        case BindKind::Options:
            if(wk == Widget::Dropdown) lv_dropdown_set_options(obj, v.str.c_str());
            else if(wk == Widget::Roller) lv_roller_set_options(obj, v.str.c_str(), LV_ROLLER_MODE_NORMAL);
            break;
    }
}

// widget -> tag (two-way bindings)
void Binding::on_widget_changed(lv_obj_t* target)
{
    if(commit_on_ready && wk == Widget::Textarea && lv_textarea_get_text(target)[0] == '\0') {
        on_tag_changed(tag->get(), nullptr);    // the field was cleared on focus and nothing was typed
        return;
    }
    if(kind == BindKind::Checked) {
        bool on = lv_obj_has_state(target, LV_STATE_CHECKED);
        if(invert) on = !on;
        if(cmp == Cmp::Eq && cmp_tag) { if(on) tag->set(cmp_tag->get(), this); }          // radio button: copy the compared tag
        else if(cmp == Cmp::Eq && !cmp_is_str) { if(on) tag->set_number(cmp_num, this); }
        else tag->set_number(on ? 1 : 0, this);
        return;
    }
    if(kind == BindKind::Text) {
        if(wk == Widget::Textarea) {
            tag->set_string(lv_textarea_get_text(target), this);
            if(commit_on_ready) ta_show(target, tag->get().format(format));   // normalised echo
        }
        return;
    }
    if(kind == BindKind::Value) {
        double wv = 0;
        switch(wk) {
            case Widget::Slider:   wv = lv_slider_get_value(target); break;
            case Widget::Arc:      wv = lv_arc_get_value(target); break;
            case Widget::Dropdown: wv = lv_dropdown_get_selected(target); break;
            case Widget::Roller:   wv = lv_roller_get_selected(target); break;
            case Widget::Textarea: wv = std::atof(lv_textarea_get_text(target)); break;
            default: return;
        }
        double tv = scale != 0 ? (wv - offset) / scale : wv;
        if(vmin && tv < *vmin) tv = *vmin;
        if(vmax && tv > *vmax) tv = *vmax;
        tag->set_number(tv, this);
        // show what was actually stored (clamped by min/max and the tag limits, formatted)
        if(commit_on_ready) apply_value(tag->get_number());
    }
}

// Widget-level binding options set by build_widget() for the current widget (data displays).
struct BindOpts {
    bool        has_natural = false;      // overrides the per-widget default of "bind": "tag"
    BindKind    natural = BindKind::Text;
    bool        natural_two_way = false;  // also used for the "text" / "value" keys of the object form
    bool        commit_on_ready = false;
    std::string format;                   // default "format" when the bind spec has none
    std::optional<double> vmin, vmax;
};

enum class ActionKind { Goto, Back, Set, Toggle, Inc, Dec, Reload, Exit };

struct Action {
    ActionKind  kind = ActionKind::Back;
    std::string screen, anim;
    Tag*        tag = nullptr;
    Value       value;
    double      by = 1;
};

struct ActionList {
    std::vector<Action> actions;
};

struct ChartCtx {
    struct Series { lv_chart_series_t* ser; Tag* tag; double scale; };
    lv_obj_t*           chart = nullptr;
    lv_timer_t*         timer = nullptr;
    std::vector<Series> series;

    void tick()
    {
        for(const Series& s : series) {
            lv_chart_set_next_value(chart, s.ser, static_cast<int32_t>(std::llround(s.tag->get_number() * s.scale)));
        }
    }
};

struct Screen {
    std::string name;
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   keyboard = nullptr;   // optional on-screen keyboard
};

// LVGL's stock keyboard closes through a key drawn as a keyboard icon. Operators look for an
// "x", so the lower-case, symbol and number layouts are replaced with copies that show
// LV_SYMBOL_CLOSE instead (the upper-case layout already has it). Pressing it sends
// LV_EVENT_CANCEL, i.e. the edit is discarded; the check mark confirms.
#define HML_KB_BTN(w) (LV_BUTTONMATRIX_CTRL_POPOVER | (w))
const char* const kKbMapLower[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, "",
};
const int kKbCtrlLower[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), HML_KB_BTN(4), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, HML_KB_BTN(3), HML_KB_BTN(3), HML_KB_BTN(3), HML_KB_BTN(3), HML_KB_BTN(3), HML_KB_BTN(3), HML_KB_BTN(3), HML_KB_BTN(3), HML_KB_BTN(3), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_BUTTONMATRIX_CTRL_CHECKED | HML_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | HML_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | HML_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | HML_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
};
const char* const kKbMapSpecial[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, "",
};
const int kKbCtrlSpecial[] = {
    HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | 2,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1),
    HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1), HML_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
};
const char* const kKbMapNumber[] = {
    "1", "2", "3", LV_SYMBOL_CLOSE, "\n",
    "4", "5", "6", LV_SYMBOL_OK, "\n",
    "7", "8", "9", LV_SYMBOL_BACKSPACE, "\n",
    "+/-", "0", ".", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "",
};
const int kKbCtrlNumber[] = {
    1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, 2,
    1, 1, 1, 1, 1,
};
#undef HML_KB_BTN

template <size_t N>
const lv_buttonmatrix_ctrl_t* ctrl_table(const int (&src)[N])
{
    static lv_buttonmatrix_ctrl_t table[N];           // LVGL keeps the pointer: must stay alive
    for(size_t i = 0; i < N; i++) table[i] = static_cast<lv_buttonmatrix_ctrl_t>(src[i]);
    return table;
}

void install_keyboard_maps(lv_obj_t* kb)
{
    static bool done = false;       // lv_keyboard_set_map() changes the layouts for every keyboard
    if(done) return;
    done = true;
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kKbMapLower, ctrl_table(kKbCtrlLower));
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, kKbMapSpecial, ctrl_table(kKbCtrlSpecial));
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_NUMBER, kKbMapNumber, ctrl_table(kKbCtrlNumber));
}

constexpr size_t kHistoryLen = 16;

} // namespace

// ================================================================ Ui::Impl

struct Ui::Impl {
    std::vector<std::unique_ptr<Screen>>     screens;
    int                                      current = -1;
    std::deque<int>                          history;
    std::vector<std::unique_ptr<Binding>>    bindings;
    std::vector<std::unique_ptr<ActionList>> action_lists;
    std::vector<std::unique_ptr<ChartCtx>>   charts;
    std::vector<std::unique_ptr<std::vector<lv_point_precise_t>>> line_points;   // must outlive lv_line objects
    std::vector<std::unique_ptr<std::string>>  owned_strings;  // textarea accepted_chars: LVGL stores the pointer only
    lv_group_t*   group = nullptr;
    lv_style_t    style_disabled{};   // visual feedback for LV_STATE_DISABLED (the theme has none for most widgets)
    lv_indev_t*   kb_indev = nullptr;
    AppCallbacks  app;
    std::string   base_dir;
    Screen*       building = nullptr;
    BindOpts      bind_opts;          // for the widget currently being built

    Screen* find_screen(const std::string& name)
    {
        for(auto& s : screens) if(s->name == name) return s.get();
        return nullptr;
    }

    // ---- events (LVGL -> C++) ----
    static void on_widget_value_changed(lv_event_t* e)
    {
        static_cast<Binding*>(lv_event_get_user_data(e))->on_widget_changed(lv_event_get_target_obj(e));
    }
    static void on_widget_cancel(lv_event_t* e)   // data display: discard the edit, show the tag again
    {
        Binding* b = static_cast<Binding*>(lv_event_get_user_data(e));
        b->on_tag_changed(b->tag->get(), nullptr);
    }
    static void on_widget_focus(lv_event_t* e)    // data display: start with an empty field
    {
        lv_textarea_set_text(lv_event_get_target_obj(e), "");
    }
    static void on_action_event(lv_event_t* e);
    static void on_chart_timer(lv_timer_t* t) { static_cast<ChartCtx*>(lv_timer_get_user_data(t))->tick(); }
    static void on_textarea_event(lv_event_t* e)
    {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        Screen* scr = static_cast<Screen*>(lv_event_get_user_data(e));
        if(!scr->keyboard) return;
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
            lv_keyboard_set_textarea(scr->keyboard, ta);
            // numeric_display marks itself with USER_1: give it the number pad
            lv_keyboard_set_mode(scr->keyboard, lv_obj_has_flag(ta, LV_OBJ_FLAG_USER_1) ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
            lv_obj_remove_flag(scr->keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        else if(code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            lv_obj_add_flag(scr->keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
    static void on_keyboard_done(lv_event_t* e)
    {
        // Only hide. Re-focusing the textarea here would send LV_EVENT_FOCUSED and re-open it.
        lv_obj_add_flag(lv_event_get_target_obj(e), LV_OBJ_FLAG_HIDDEN);
    }

    // ---- bindings ----
    void parse_cmp(Binding& b, const json& spec);
    Binding* add_binding(lv_obj_t* obj, Widget wk, BindKind kind, const json& spec, bool two_way);
    void add_binding_inverted(lv_obj_t* obj, Widget wk, BindKind kind, const json& spec);
    void parse_bindings(lv_obj_t* obj, lv_obj_t* text_obj, Widget wk, const json& w);

    // ---- actions ----
    void run_actions(const ActionList& list);
    std::optional<Action> parse_action(const json& j);
    void parse_actions(lv_obj_t* obj, const json& w);

    // ---- widgets ----
    void build_chart(lv_obj_t* chart, const json& w);
    void apply_layout(lv_obj_t* obj, const json& w);
    void apply_common(lv_obj_t* obj, const json& w);
    void build_children(lv_obj_t* parent, const json& w);
    lv_obj_t* build_widget(lv_obj_t* parent, const json& w);

    bool show_screen(const std::string& name, const std::string& anim);
};

// ---------------------------------------------------------------- bindings

void Ui::Impl::parse_cmp(Binding& b, const json& spec)
{
    static const struct { const char* k; Cmp c; } tbl[] = {
        { "eq", Cmp::Eq }, { "ne", Cmp::Ne }, { "gt", Cmp::Gt }, { "lt", Cmp::Lt }, { "ge", Cmp::Ge }, { "le", Cmp::Le },
    };
    for(const auto& t : tbl) {
        const json* v = jget(spec, t.k);
        if(!v) continue;
        b.cmp = t.c;
        if(v->is_object()) {
            // { "gt": { "tag": "temp_hi" } } - compare with another tag, re-evaluated when either changes
            std::string other = jstr(*v, "tag", "");
            b.cmp_tag = tags().find(other);
            if(!b.cmp_tag) { HML_WARN("bind: unknown compare tag '%s'", other.c_str()); b.cmp = Cmp::None; }
            return;
        }
        if(v->is_string()) {
            b.cmp_is_str = true;
            b.cmp_str = v->get<std::string>();
            // allow "eq": "3" on numeric tags
            if(b.tag->type() != TagType::String) { b.cmp_is_str = false; b.cmp_num = std::atof(b.cmp_str.c_str()); }
        }
        else b.cmp_num = jscalar_to_number(*v);
        return;
    }
}

Binding* Ui::Impl::add_binding(lv_obj_t* obj, Widget wk, BindKind kind, const json& spec, bool two_way)
{
    std::string tagname = spec.is_string() ? spec.get<std::string>() : jstr(spec, "tag", "");
    Tag* tag = tags().find(tagname);
    if(!tag) { HML_WARN("bind: unknown tag '%s'", tagname.c_str()); return nullptr; }

    auto b = std::make_unique<Binding>();
    b->obj = obj; b->wk = wk; b->kind = kind; b->tag = tag;
    b->format = bind_opts.format;
    b->vmin = bind_opts.vmin;
    b->vmax = bind_opts.vmax;
    b->commit_on_ready = bind_opts.commit_on_ready;
    if(spec.is_object()) {
        b->format = jstr(spec, "format", bind_opts.format);
        b->invert = jbool(spec, "invert", false);
        b->scale = jnum(spec, "scale", 1);
        b->offset = jnum(spec, "offset", 0);
        std::string part = jstr(spec, "part", "main");
        if(iequals(part, "indicator")) b->part = LV_PART_INDICATOR;
        else if(iequals(part, "knob")) b->part = LV_PART_KNOB;
        parse_cmp(*b, spec);
        if(const json* map = jget(spec, "map"); map && map->is_object()) {
            for(auto it = map->begin(); it != map->end(); ++it) {
                if(!it.value().is_string()) continue;
                lv_color_t c = parse_color(it.value().get<std::string>(), lv_color_black());
                if(iequals(it.key(), "default")) b->default_color = c;
                else {
                    double key = iequals(it.key(), "true") ? 1 : iequals(it.key(), "false") ? 0 : std::atof(it.key().c_str());
                    b->color_map.emplace_back(key, c);
                }
            }
        }
        two_way = jbool(spec, "two_way", two_way);
    }
    b->two_way = two_way;

    Binding* raw = b.get();
    bindings.push_back(std::move(b));
    tag->subscribe(SubGroup::UI, [raw](Tag&, const Value& v, const void* src) { raw->on_tag_changed(v, src); });
    if(raw->cmp_tag) {
        // the condition depends on a second tag: re-evaluate with the bound tag's current value
        raw->cmp_tag->subscribe(SubGroup::UI, [raw](Tag&, const Value&, const void*) { raw->on_tag_changed(raw->tag->get(), nullptr); });
    }
    if(two_way && raw->commit_on_ready) {
        lv_obj_add_event_cb(obj, &Impl::on_widget_focus, LV_EVENT_FOCUSED, raw);
        lv_obj_add_event_cb(obj, &Impl::on_widget_value_changed, LV_EVENT_READY, raw);      // keyboard OK / Enter
        lv_obj_add_event_cb(obj, &Impl::on_widget_value_changed, LV_EVENT_DEFOCUSED, raw);
        lv_obj_add_event_cb(obj, &Impl::on_widget_cancel, LV_EVENT_CANCEL, raw);
    }
    else if(two_way) lv_obj_add_event_cb(obj, &Impl::on_widget_value_changed, LV_EVENT_VALUE_CHANGED, raw);

    raw->on_tag_changed(tag->get(), nullptr);   // initial value
    return raw;
}

// bind property with inverted meaning ("hidden", "disabled")
void Ui::Impl::add_binding_inverted(lv_obj_t* obj, Widget wk, BindKind kind, const json& spec)
{
    Binding* b = add_binding(obj, wk, kind, spec, false);
    if(!b) return;
    b->invert = !b->invert;
    b->on_tag_changed(b->tag->get(), nullptr);
}

void Ui::Impl::parse_bindings(lv_obj_t* obj, lv_obj_t* text_obj, Widget wk, const json& w)
{
    const json* bind = jget(w, "bind");
    if(!bind) return;
    lv_obj_t* tobj = text_obj ? text_obj : obj;
    const bool ta_two_way = bind_opts.has_natural ? bind_opts.natural_two_way : true;   // textarea-based widgets
    if(bind->is_string()) {
        // "bind": "tag" - pick the natural property of the widget
        if(bind_opts.has_natural) { add_binding(obj, wk, bind_opts.natural, *bind, bind_opts.natural_two_way); return; }
        switch(wk) {
            case Widget::Switch: case Widget::Checkbox: case Widget::Led: add_binding(obj, wk, BindKind::Checked, *bind, wk != Widget::Led); break;
            case Widget::Button: add_binding(obj, wk, BindKind::Checked, *bind, true); break;
            case Widget::Slider: case Widget::Bar: case Widget::Arc: case Widget::Dropdown: case Widget::Roller:
                add_binding(obj, wk, BindKind::Value, *bind, wk != Widget::Bar); break;
            case Widget::Textarea: add_binding(obj, wk, BindKind::Text, *bind, true); break;
            default: add_binding(tobj, wk, BindKind::Text, *bind, false); break;
        }
        return;
    }
    if(!bind->is_object()) return;
    for(auto it = bind->begin(); it != bind->end(); ++it) {
        const std::string& k = it.key();
        const json& spec = it.value();
        if(iequals(k, "text")) add_binding(tobj, wk, BindKind::Text, spec, wk == Widget::Textarea && ta_two_way);
        else if(iequals(k, "value")) add_binding(obj, wk, BindKind::Value, spec, wk == Widget::Slider || wk == Widget::Arc || wk == Widget::Dropdown || wk == Widget::Roller || (wk == Widget::Textarea && ta_two_way));
        else if(iequals(k, "checked") || iequals(k, "on")) add_binding(obj, wk, BindKind::Checked, spec, wk == Widget::Switch || wk == Widget::Checkbox || wk == Widget::Button);
        else if(iequals(k, "visible")) add_binding(obj, wk, BindKind::Visible, spec, false);
        else if(iequals(k, "hidden")) add_binding_inverted(obj, wk, BindKind::Visible, spec);
        else if(iequals(k, "enabled")) add_binding(obj, wk, BindKind::Enabled, spec, false);
        else if(iequals(k, "disabled")) add_binding_inverted(obj, wk, BindKind::Enabled, spec);
        else if(iequals(k, "bg_color")) add_binding(obj, wk, BindKind::BgColor, spec, false);
        else if(iequals(k, "text_color")) add_binding(tobj, wk, BindKind::TextColor, spec, false);
        else if(iequals(k, "options")) add_binding(obj, wk, BindKind::Options, spec, false);
        else if(iequals(k, "src")) add_binding(obj, wk, BindKind::Text, spec, false);
        else HML_WARN("bind: unknown property '%s'", k.c_str());
    }
}

// ---------------------------------------------------------------- actions

void Ui::Impl::run_actions(const ActionList& list)
{
    for(const Action& a : list.actions) {
        switch(a.kind) {
            case ActionKind::Goto:   show_screen(a.screen, a.anim); break;
            case ActionKind::Back:   ui().back(); break;
            case ActionKind::Set:    a.tag->set(a.value, nullptr); break;
            case ActionKind::Toggle: a.tag->set_number(a.tag->get_number() != 0 ? 0 : 1, nullptr); break;
            case ActionKind::Inc:    a.tag->set_number(a.tag->get_number() + a.by, nullptr); break;
            case ActionKind::Dec:    a.tag->set_number(a.tag->get_number() - a.by, nullptr); break;
            case ActionKind::Reload: if(app.request_reload) app.request_reload(); break;
            case ActionKind::Exit:   if(app.request_exit) app.request_exit(); break;
        }
    }
}

void Ui::Impl::on_action_event(lv_event_t* e)
{
    ui().impl_->run_actions(*static_cast<ActionList*>(lv_event_get_user_data(e)));
}

std::optional<Action> Ui::Impl::parse_action(const json& j)
{
    auto kind = jstr_opt(j, "action");
    if(!kind) { HML_WARN("action without \"action\" key"); return std::nullopt; }
    Action a;
    if(iequals(*kind, "goto")) {
        a.kind = ActionKind::Goto;
        a.screen = jstr(j, "screen", "");
        a.anim = jstr(j, "anim", "fade");
        return a;
    }
    if(iequals(*kind, "back")) { a.kind = ActionKind::Back; return a; }
    if(iequals(*kind, "reload")) { a.kind = ActionKind::Reload; return a; }
    if(iequals(*kind, "exit")) { a.kind = ActionKind::Exit; return a; }

    std::string tagname = jstr(j, "tag", "");
    a.tag = tags().find(tagname);
    if(!a.tag) { HML_WARN("action '%s': unknown tag '%s'", kind->c_str(), tagname.c_str()); return std::nullopt; }
    if(iequals(*kind, "set")) {
        a.kind = ActionKind::Set;
        const json* v = jget(j, "value");
        if(v && v->is_string()) a.value = Value::string(v->get<std::string>());
        else if(v && v->is_boolean()) a.value = Value::number(v->get<bool>(), TagType::Bool);
        else a.value = Value::number(v ? jscalar_to_number(*v) : 0);
    }
    else if(iequals(*kind, "toggle")) a.kind = ActionKind::Toggle;
    else if(iequals(*kind, "inc")) { a.kind = ActionKind::Inc; a.by = jnum(j, "by", 1); }
    else if(iequals(*kind, "dec")) { a.kind = ActionKind::Dec; a.by = jnum(j, "by", 1); }
    else { HML_WARN("unknown action '%s'", kind->c_str()); return std::nullopt; }
    return a;
}

void Ui::Impl::parse_actions(lv_obj_t* obj, const json& w)
{
    static const struct { const char* key; lv_event_code_t ev; } tbl[] = {
        { "on_click", LV_EVENT_CLICKED }, { "on_press", LV_EVENT_PRESSED }, { "on_release", LV_EVENT_RELEASED },
        { "on_long_press", LV_EVENT_LONG_PRESSED }, { "on_long_press_repeat", LV_EVENT_LONG_PRESSED_REPEAT },
        { "on_change", LV_EVENT_VALUE_CHANGED }, { "on_ready", LV_EVENT_READY },
    };
    for(const auto& t : tbl) {
        const json* acts = jget(w, t.key);
        if(!acts) continue;
        auto list = std::make_unique<ActionList>();
        if(acts->is_array()) {
            for(const json& j : *acts) if(auto a = parse_action(j)) list->actions.push_back(std::move(*a));
        }
        else if(acts->is_object()) {
            if(auto a = parse_action(*acts)) list->actions.push_back(std::move(*a));
        }
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, &Impl::on_action_event, t.ev, list.get());
        action_lists.push_back(std::move(list));
    }
}

// ---------------------------------------------------------------- chart

void Ui::Impl::build_chart(lv_obj_t* chart, const json& w)
{
    std::string type = jstr(w, "chart_type", "line");
    lv_chart_set_type(chart, iequals(type, "bar") ? LV_CHART_TYPE_BAR : iequals(type, "scatter") ? LV_CHART_TYPE_SCATTER : LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, static_cast<uint32_t>(jnum(w, "points", 50)));
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, static_cast<int32_t>(jnum(w, "min", 0)), static_cast<int32_t>(jnum(w, "max", 100)));
    lv_chart_set_update_mode(chart, iequals(jstr(w, "update_mode", "shift"), "circular") ? LV_CHART_UPDATE_MODE_CIRCULAR : LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, static_cast<uint8_t>(jnum(w, "hdiv", 5)), static_cast<uint8_t>(jnum(w, "vdiv", 8)));
    if(jbool(w, "no_points", true)) {
        lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    }

    const json* series = jget(w, "series");
    if(!series || !series->is_array()) return;
    auto c = std::make_unique<ChartCtx>();
    c->chart = chart;
    for(const json& s : *series) {
        std::string tagname = jstr(s, "tag", "");
        Tag* tag = tags().find(tagname);
        if(!tag) { HML_WARN("chart: unknown tag '%s'", tagname.c_str()); continue; }
        lv_color_t col = parse_color(jstr(s, "color", ""), lv_palette_main(LV_PALETTE_BLUE));
        c->series.push_back({ lv_chart_add_series(chart, col, LV_CHART_AXIS_PRIMARY_Y), tag, jnum(s, "scale", 1) });
    }
    if(c->series.empty()) return;
    uint32_t sample_ms = static_cast<uint32_t>(jnum(w, "sample_ms", 500));
    c->timer = lv_timer_create(&Impl::on_chart_timer, sample_ms ? sample_ms : 500, c.get());
    c->tick();
    charts.push_back(std::move(c));
}

// ---------------------------------------------------------------- widgets

void Ui::Impl::apply_layout(lv_obj_t* obj, const json& w)
{
    if(auto flex = jstr_opt(w, "flex")) {
        lv_flex_flow_t flow = LV_FLEX_FLOW_ROW;
        if(iequals(*flex, "column")) flow = LV_FLEX_FLOW_COLUMN;
        else if(iequals(*flex, "row_wrap")) flow = LV_FLEX_FLOW_ROW_WRAP;
        else if(iequals(*flex, "column_wrap")) flow = LV_FLEX_FLOW_COLUMN_WRAP;
        else if(iequals(*flex, "row_reverse")) flow = LV_FLEX_FLOW_ROW_REVERSE;
        else if(iequals(*flex, "column_reverse")) flow = LV_FLEX_FLOW_COLUMN_REVERSE;
        lv_obj_set_flex_flow(obj, flow);
        lv_obj_set_flex_align(obj,
                              parse_flex_align(jstr_opt(w, "flex_main"), LV_FLEX_ALIGN_START),
                              parse_flex_align(jstr_opt(w, "flex_cross"), LV_FLEX_ALIGN_START),
                              parse_flex_align(jstr_opt(w, "flex_track"), LV_FLEX_ALIGN_START));
    }
    if(jhas(w, "flex_grow")) lv_obj_set_flex_grow(obj, static_cast<uint8_t>(jnum(w, "flex_grow", 0)));
}

void Ui::Impl::apply_common(lv_obj_t* obj, const json& w)
{
    if(const json* jw = jget(w, "w")) lv_obj_set_width(obj, parse_size(jw, LV_SIZE_CONTENT));
    if(const json* jh = jget(w, "h")) lv_obj_set_height(obj, parse_size(jh, LV_SIZE_CONTENT));
    if(auto align = jstr_opt(w, "align")) lv_obj_set_align(obj, parse_align(*align));
    if(const json* x = jget(w, "x")) lv_obj_set_x(obj, parse_size(x, 0));
    if(const json* y = jget(w, "y")) lv_obj_set_y(obj, parse_size(y, 0));

    if(jbool(w, "hidden", false)) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if(jhas(w, "clickable")) {
        if(jbool(w, "clickable", true)) lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        else lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    if(jhas(w, "scrollable") && !jbool(w, "scrollable", true)) lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    if(auto sb = jstr_opt(w, "scrollbar")) {
        lv_obj_set_scrollbar_mode(obj, iequals(*sb, "off") ? LV_SCROLLBAR_MODE_OFF : iequals(*sb, "on") ? LV_SCROLLBAR_MODE_ON : iequals(*sb, "active") ? LV_SCROLLBAR_MODE_ACTIVE : LV_SCROLLBAR_MODE_AUTO);
    }
    if(jbool(w, "checkable", false)) lv_obj_add_flag(obj, LV_OBJ_FLAG_CHECKABLE);
    if(jbool(w, "checked", false)) lv_obj_add_state(obj, LV_STATE_CHECKED);
    if(jbool(w, "disabled", false)) lv_obj_add_state(obj, LV_STATE_DISABLED);

    apply_layout(obj, w);
    apply_styles(obj, w);
}

void Ui::Impl::build_children(lv_obj_t* parent, const json& w)
{
    const json* children = jget(w, "children");
    if(!children || !children->is_array()) return;
    for(const json& c : *children) build_widget(parent, c);
}

lv_obj_t* Ui::Impl::build_widget(lv_obj_t* parent, const json& w)
{
    if(!w.is_object()) return nullptr;
    std::string type = jstr(w, "type", "panel");
    lv_obj_t* obj = nullptr;
    lv_obj_t* text_obj = nullptr;
    Widget wk = Widget::Panel;
    bind_opts = BindOpts{};

    if(iequals(type, "panel") || iequals(type, "container") || iequals(type, "obj")) {
        obj = lv_obj_create(parent);
    }
    else if(iequals(type, "label")) {
        wk = Widget::Label;
        obj = lv_label_create(parent);
        lv_label_set_text(obj, jstr(w, "text", "").c_str());
        if(auto lm = jstr_opt(w, "long_mode")) {
            lv_label_set_long_mode(obj, iequals(*lm, "dots") ? LV_LABEL_LONG_MODE_DOTS : iequals(*lm, "scroll") ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : iequals(*lm, "clip") ? LV_LABEL_LONG_MODE_CLIP : LV_LABEL_LONG_MODE_WRAP);
        }
        text_obj = obj;
    }
    else if(iequals(type, "button")) {
        wk = Widget::Button;
        obj = lv_button_create(parent);
        auto text = jstr_opt(w, "text");
        if(text || jhas(w, "bind")) {
            text_obj = lv_label_create(obj);
            lv_label_set_text(text_obj, text ? text->c_str() : "");
            lv_obj_center(text_obj);
            if(const json* ls = jget(w, "style_label")) apply_style(text_obj, *ls, LV_PART_MAIN);
        }
    }
    else if(iequals(type, "switch")) {
        wk = Widget::Switch;
        obj = lv_switch_create(parent);
    }
    else if(iequals(type, "checkbox")) {
        wk = Widget::Checkbox;
        obj = lv_checkbox_create(parent);
        lv_checkbox_set_text(obj, jstr(w, "text", "").c_str());
    }
    else if(iequals(type, "slider")) {
        wk = Widget::Slider;
        obj = lv_slider_create(parent);
        lv_slider_set_range(obj, static_cast<int32_t>(jnum(w, "min", 0)), static_cast<int32_t>(jnum(w, "max", 100)));
        lv_slider_set_value(obj, static_cast<int32_t>(jnum(w, "value", 0)), LV_ANIM_OFF);
    }
    else if(iequals(type, "bar")) {
        wk = Widget::Bar;
        obj = lv_bar_create(parent);
        lv_bar_set_range(obj, static_cast<int32_t>(jnum(w, "min", 0)), static_cast<int32_t>(jnum(w, "max", 100)));
        lv_bar_set_value(obj, static_cast<int32_t>(jnum(w, "value", 0)), LV_ANIM_OFF);
    }
    else if(iequals(type, "arc")) {
        wk = Widget::Arc;
        obj = lv_arc_create(parent);
        lv_arc_set_range(obj, static_cast<int32_t>(jnum(w, "min", 0)), static_cast<int32_t>(jnum(w, "max", 100)));
        lv_arc_set_bg_angles(obj, static_cast<lv_value_precise_t>(jnum(w, "start_angle", 135)), static_cast<lv_value_precise_t>(jnum(w, "end_angle", 45)));
        lv_arc_set_rotation(obj, static_cast<int32_t>(jnum(w, "rotation", 0)));
        lv_arc_set_value(obj, static_cast<int32_t>(jnum(w, "value", 0)));
        if(!jbool(w, "adjustable", true)) {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_style(obj, nullptr, LV_PART_KNOB);
        }
    }
    else if(iequals(type, "led")) {
        wk = Widget::Led;
        obj = lv_led_create(parent);
        lv_led_set_color(obj, parse_color(jstr(w, "color", ""), lv_palette_main(LV_PALETTE_GREEN)));
        if(jbool(w, "on", false)) lv_led_on(obj); else lv_led_off(obj);
    }
    else if(iequals(type, "dropdown")) {
        wk = Widget::Dropdown;
        obj = lv_dropdown_create(parent);
        lv_dropdown_set_options(obj, join_options(jget(w, "options")).c_str());
        lv_dropdown_set_selected(obj, static_cast<uint32_t>(jnum(w, "selected", 0)));
    }
    else if(iequals(type, "roller")) {
        wk = Widget::Roller;
        obj = lv_roller_create(parent);
        lv_roller_set_options(obj, join_options(jget(w, "options")).c_str(), jbool(w, "infinite", false) ? LV_ROLLER_MODE_INFINITE : LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(obj, static_cast<uint32_t>(jnum(w, "visible_rows", 3)));
        lv_roller_set_selected(obj, static_cast<uint32_t>(jnum(w, "selected", 0)), LV_ANIM_OFF);
    }
    else if(iequals(type, "textarea")) {
        wk = Widget::Textarea;
        obj = lv_textarea_create(parent);
        lv_textarea_set_text(obj, jstr(w, "text", "").c_str());
        if(auto ph = jstr_opt(w, "placeholder")) lv_textarea_set_placeholder_text(obj, ph->c_str());
        lv_textarea_set_one_line(obj, jbool(w, "one_line", true));
        lv_textarea_set_password_mode(obj, jbool(w, "password", false));
        if(jhas(w, "max_length")) lv_textarea_set_max_length(obj, static_cast<uint32_t>(jnum(w, "max_length", 0)));
        if(auto acc = jstr_opt(w, "accepted_chars")) {
            owned_strings.push_back(std::make_unique<std::string>(*acc));
            lv_textarea_set_accepted_chars(obj, owned_strings.back()->c_str());
        }
        if(group) lv_group_add_obj(group, obj);
        if(building) {
            for(lv_event_code_t code : { LV_EVENT_FOCUSED, LV_EVENT_CLICKED, LV_EVENT_DEFOCUSED, LV_EVENT_READY, LV_EVENT_CANCEL }) {
                lv_obj_add_event_cb(obj, &Impl::on_textarea_event, code, building);
            }
        }
        text_obj = obj;
    }
    else if(iequals(type, "numeric_display") || iequals(type, "text_display") || iequals(type, "password_display")) {
        // Data displays: a one-line text box showing a tag; "input": true turns it into an input field
        // that writes on confirmation (keyboard OK / Enter / focus lost) and reverts on cancel.
        wk = Widget::Textarea;
        const bool numeric = iequals(type, "numeric_display");
        const bool password = iequals(type, "password_display");
        const bool input = jbool(w, "input", password);       // a password field is for entering one
        obj = lv_textarea_create(parent);
        lv_textarea_set_one_line(obj, jbool(w, "one_line", true));
        lv_textarea_set_password_mode(obj, jbool(w, "password", password));
        lv_textarea_set_text(obj, jstr(w, "text", "").c_str());
        if(auto ph = jstr_opt(w, "placeholder")) lv_textarea_set_placeholder_text(obj, ph->c_str());
        if(jhas(w, "max_length")) lv_textarea_set_max_length(obj, static_cast<uint32_t>(jnum(w, "max_length", 0)));
        std::string accepted = jstr(w, "accepted_chars", numeric ? "0123456789.-+" : "");
        if(!accepted.empty()) {
            owned_strings.push_back(std::make_unique<std::string>(accepted));
            lv_textarea_set_accepted_chars(obj, owned_strings.back()->c_str());
        }
        bind_opts.has_natural = true;
        bind_opts.natural = numeric ? BindKind::Value : BindKind::Text;
        bind_opts.natural_two_way = input;
        bind_opts.commit_on_ready = input;
        if(numeric) {
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_USER_1);          // number pad, see on_textarea_event
            std::string fmt = jstr(w, "format", "");
            if(fmt.empty() && jhas(w, "decimals")) fmt = vformat("%%.%df", static_cast<int>(jnum(w, "decimals", 0)));
            if(fmt.empty()) fmt = "%s";                       // default rendering of the tag type
            std::string unit = jstr(w, "unit", "");            // literal text: "%" must become "%%"
            for(size_t pos = 0; (pos = unit.find('%', pos)) != std::string::npos; pos += 2) unit.insert(pos, "%");
            bind_opts.format = fmt + unit;
            if(jhas(w, "min")) bind_opts.vmin = jnum(w, "min", 0);
            if(jhas(w, "max")) bind_opts.vmax = jnum(w, "max", 0);
        }
        if(input) {
            if(group) lv_group_add_obj(group, obj);
            if(building) {
                for(lv_event_code_t code : { LV_EVENT_FOCUSED, LV_EVENT_CLICKED, LV_EVENT_DEFOCUSED, LV_EVENT_READY, LV_EVENT_CANCEL }) {
                    lv_obj_add_event_cb(obj, &Impl::on_textarea_event, code, building);
                }
            }
        }
        else {
            lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
            lv_textarea_set_cursor_click_pos(obj, false);
            lv_obj_set_style_opa(obj, LV_OPA_TRANSP, LV_PART_CURSOR);
        }
        text_obj = obj;
    }
    else if(iequals(type, "image")) {
        wk = Widget::Image;
        obj = lv_image_create(parent);
        if(auto src = jstr_opt(w, "src")) {
            bool absolute = (src->size() > 1 && (*src)[1] == ':') || (!src->empty() && (*src)[0] == '/');
            std::string path = absolute ? "A:" + *src : "A:" + base_dir + "/" + *src;
            lv_image_set_src(obj, path.c_str());
        }
        if(jhas(w, "zoom")) lv_image_set_scale(obj, static_cast<uint32_t>(jnum(w, "zoom", 1.0) * 256));
        if(jhas(w, "rotation")) lv_image_set_rotation(obj, static_cast<int32_t>(jnum(w, "rotation", 0) * 10));
    }
    else if(iequals(type, "chart")) {
        wk = Widget::Chart;
        obj = lv_chart_create(parent);
        build_chart(obj, w);
    }
    else if(iequals(type, "spinner")) {
        wk = Widget::Spinner;
        obj = lv_spinner_create(parent);
        lv_spinner_set_anim_params(obj, static_cast<uint32_t>(jnum(w, "time", 1000)), static_cast<uint32_t>(jnum(w, "angle", 60)));
    }
    else if(iequals(type, "keyboard")) {
        wk = Widget::Keyboard;
        obj = lv_keyboard_create(parent);
        install_keyboard_maps(obj);
        lv_group_remove_obj(obj);                            // pointer-operated: keep it out of the focus group
        lv_obj_remove_state(obj, LV_STATE_FOCUSED);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(obj, &Impl::on_keyboard_done, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(obj, &Impl::on_keyboard_done, LV_EVENT_CANCEL, nullptr);
        if(building) building->keyboard = obj;
    }
    else if(iequals(type, "line")) {
        wk = Widget::Line;
        obj = lv_line_create(parent);
        const json* pts = jget(w, "points");
        if(pts && pts->is_array() && pts->size() >= 2) {
            auto points = std::make_unique<std::vector<lv_point_precise_t>>();
            for(const json& pt : *pts) {
                if(pt.is_array() && pt.size() >= 2) {
                    points->push_back({ static_cast<lv_value_precise_t>(jscalar_to_number(pt[0])), static_cast<lv_value_precise_t>(jscalar_to_number(pt[1])) });
                }
            }
            lv_line_set_points(obj, points->data(), static_cast<uint32_t>(points->size()));
            line_points.push_back(std::move(points));
        }
    }
    else {
        HML_WARN("unknown widget type '%s'", type.c_str());
        return nullptr;
    }

    lv_obj_add_style(obj, &style_disabled, LV_STATE_DISABLED);
    apply_common(obj, w);
    parse_bindings(obj, text_obj, wk, w);
    parse_actions(obj, w);
    build_children(obj, w);
    return obj;
}

// ---------------------------------------------------------------- screens

bool Ui::Impl::show_screen(const std::string& name, const std::string& anim)
{
    Screen* scr = find_screen(name);
    if(!scr) { HML_WARN("goto: unknown screen '%s'", name.c_str()); return false; }
    int idx = 0;
    while(screens[static_cast<size_t>(idx)].get() != scr) idx++;
    if(idx == current) return true;

    lv_obj_t* prev = lv_screen_active();
    bool prev_is_ours = false;
    for(const auto& s : screens) if(s->obj == prev) prev_is_ours = true;

    if(current >= 0) {
        if(history.size() == kHistoryLen) history.pop_front();
        history.push_back(current);
    }
    current = idx;
    bool animated = !anim.empty() && !iequals(anim, "none");
    // The previous screen is deleted only when it is not one of ours (the blank screen from unload_all).
    lv_screen_load_anim(scr->obj, parse_anim(anim), animated ? 250 : 0, 0, !prev_is_ours);
    return true;
}

// ================================================================ Ui

Ui::Ui() : impl_(std::make_unique<Impl>()) {}
Ui::~Ui() = default;

void Ui::init(lv_indev_t* keyboard_indev, AppCallbacks cb)
{
    Impl& m = *impl_;
    m.app = std::move(cb);
    m.kb_indev = keyboard_indev;
    if(!m.group) {
        lv_style_init(&m.style_disabled);
        lv_style_set_opa(&m.style_disabled, LV_OPA_40);
        m.group = lv_group_create();
        lv_group_set_default(m.group);
        if(m.kb_indev) lv_indev_set_group(m.kb_indev, m.group);
    }
}

lv_obj_t* Ui::build_screen(const json& screen, const std::string& base_dir)
{
    Impl& m = *impl_;
    if(!screen.is_object()) return nullptr;
    auto name = jstr_opt(screen, "name");
    if(!name) { HML_WARN("screen without \"name\" skipped"); return nullptr; }
    if(m.find_screen(*name)) { HML_WARN("screen '%s' defined twice", name->c_str()); return nullptr; }
    m.base_dir = base_dir;

    auto scr = std::make_unique<Screen>();
    scr->name = *name;
    scr->obj = lv_obj_create(nullptr);
    m.building = scr.get();

    m.apply_layout(scr->obj, screen);
    apply_styles(scr->obj, screen);
    if(jhas(screen, "scrollable") && !jbool(screen, "scrollable", true)) lv_obj_remove_flag(scr->obj, LV_OBJ_FLAG_SCROLLABLE);
    m.build_children(scr->obj, screen);

    m.building = nullptr;
    lv_obj_t* obj = scr->obj;
    m.screens.push_back(std::move(scr));
    return obj;
}

void Ui::unload_all()
{
    Impl& m = *impl_;
    tags().unsubscribe_group(SubGroup::UI);
    for(auto& c : m.charts) if(c->timer) lv_timer_delete(c->timer);
    m.charts.clear();

    // LVGL needs an active screen at all times: load a blank one, then delete ours.
    // The blank screen is deleted automatically when the next screen is loaded (see show_screen).
    lv_obj_t* blank = lv_obj_create(nullptr);
    lv_screen_load(blank);
    for(auto& s : m.screens) if(s->obj) lv_obj_delete(s->obj);
    m.screens.clear();
    m.bindings.clear();
    m.action_lists.clear();
    m.line_points.clear();
    m.owned_strings.clear();
    m.current = -1;
    m.history.clear();
}

bool Ui::show_screen(const std::string& name, const std::string& anim)
{
    return impl_->show_screen(name, anim);
}

bool Ui::back()
{
    Impl& m = *impl_;
    if(m.history.empty()) return false;
    int idx = m.history.back();
    m.history.pop_back();
    if(idx < 0 || static_cast<size_t>(idx) >= m.screens.size()) return false;
    m.current = idx;
    lv_screen_load_anim(m.screens[static_cast<size_t>(idx)]->obj, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
    return true;
}

std::optional<std::string> Ui::current_screen() const
{
    const Impl& m = *impl_;
    if(m.current < 0) return std::nullopt;
    return m.screens[static_cast<size_t>(m.current)]->name;
}

size_t Ui::screen_count() const
{
    return impl_->screens.size();
}

Ui& ui()
{
    static Ui instance;
    return instance;
}

} // namespace hml
