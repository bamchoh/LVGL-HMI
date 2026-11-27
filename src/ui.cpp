#include "ui.hpp"
#include "src/misc/lv_text_private.h"
#include "texts.hpp"
#include "font_cmap.hpp"
#include <map>
#include "trend_view.hpp"
#include "bar_chart_view.hpp"
#include "recipe.hpp"
#include "alarm_view.hpp"
#include "alarm_summary_view.hpp"
#include "log_view.hpp"
#include "meter_view.hpp"
#include "datetime_view.hpp"
#include "table_view.hpp"
#include "xy_chart_view.hpp"
#include "operation_log.hpp"
#include "script.hpp"
#include "tag.hpp"
#include "users.hpp"
#include "util.hpp"
#include "value_map.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
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

namespace {
std::string s_font_file;                         // the project's font ("" = the built-in Montserrat)
// One per size: the TinyTTF font, and a copy of it that LVGL uses. The copy asks the font only
// for characters it has (FontCmap): see font_cmap.hpp for why.
struct TtfFont {
    lv_font_t* ttf = nullptr;
    lv_font_t  use{};
    int        size = 0;
};
std::map<int, std::unique_ptr<TtfFont>> s_ttf;   // by size
FontCmap s_cmap;
bool s_font_failed = false;

bool ttf_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, uint32_t letter, uint32_t next)
{
    if(!s_cmap.has(letter)) return false;            // -> LVGL tries the fallback (Montserrat)
    const auto* t = static_cast<const TtfFont*>(font->user_data);
    if(!t->ttf->get_glyph_dsc(font, dsc, letter, s_cmap.has(next) ? next : 0)) return false;
    // the advance from the font's own metrics (TinyTTF's can be another glyph's: font_cmap.hpp)
    const int adv = s_cmap.advance_px(letter, t->size);
    if(adv >= 0) dsc->adv_w = static_cast<uint16_t>(adv);
    return true;
}

const lv_font_t* montserrat(int size)
{
    static const struct { int size; const lv_font_t* f; } tbl[] = {
        { 12, &lv_font_montserrat_12 }, { 14, &lv_font_montserrat_14 }, { 16, &lv_font_montserrat_16 }, { 18, &lv_font_montserrat_18 },
        { 20, &lv_font_montserrat_20 }, { 24, &lv_font_montserrat_24 }, { 28, &lv_font_montserrat_28 }, { 32, &lv_font_montserrat_32 },
        { 40, &lv_font_montserrat_40 }, { 48, &lv_font_montserrat_48 },
    };
    const lv_font_t* best = tbl[0].f;
    int diff = 1000;
    for(const auto& t : tbl) if(std::abs(t.size - size) < diff) { diff = std::abs(t.size - size); best = t.f; }
    return best;
}
} // namespace

void set_font_file(const std::string& path)
{
    if(path == s_font_file) return;
    // The fonts made so far are kept, not destroyed: a theme or a style made before may still
    // point at one (a font file changes only when a project is loaded again, so this is little).
    static std::vector<std::unique_ptr<TtfFont>> retired;
    for(auto& [size, f] : s_ttf) retired.push_back(std::move(f));
    s_ttf.clear();
    s_font_file = path;
    s_font_failed = false;
    if(!path.empty()) {
        std::string err;
        if(!s_cmap.load(path, &err)) { HML_ERROR("font '%s': %s: using the built-in font", path.c_str(), err.c_str()); s_font_failed = true; }
    }
}

bool has_font_file() { return !s_font_file.empty(); }

bool font_file_has(const std::string& text)
{
    if(s_font_file.empty() || s_font_failed) return false;
    const char* p = text.c_str();
    uint32_t i = 0;
    while(uint32_t c = lv_text_encoded_next(p, &i)) if(!s_cmap.has(c)) return false;
    return true;
}

const lv_font_t* font_of_size(int size)
{
#if LV_USE_TINY_TTF
    if(!s_font_file.empty() && !s_font_failed && size >= 6 && size <= 200) {
        auto it = s_ttf.find(size);
        if(it != s_ttf.end()) return &it->second->use;
        // (no kerning: it matters little at HMI sizes, not at all for Japanese, and looking pairs up
        // in a CFF font is slow. LVGL 9.4 also used a freed entry when the kerning cache evicted
        // one - found with AddressSanitizer, fixed in 9.6)
        lv_font_t* f = lv_tiny_ttf_create_file_ex(("A:" + s_font_file).c_str(), size, LV_FONT_KERNING_NONE, LV_TINY_TTF_CACHE_GLYPH_CNT);
        if(f) {
            auto t = std::make_unique<TtfFont>();
            t->ttf = f;
            t->size = size;
            t->use = *f;                                 // same metrics and drawing, own glyph lookup
            t->use.get_glyph_dsc = &ttf_glyph_dsc;
            t->use.user_data = t.get();
            t->use.fallback = montserrat(size);          // LV_SYMBOL_* and anything the file does not have
            lv_font_t* use = &t->use;
            s_ttf[size] = std::move(t);
            return use;
        }
        HML_ERROR("font: cannot read '%s' (TTF / OTF): using the built-in font", s_font_file.c_str());
        s_font_failed = true;
    }
#endif
    return nullptr;
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
    if(const lv_font_t* f = font_of_size(size)) return f;     // the project's font: any size
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
        out += texts().resolve(jscalar_to_string(e));
    }
    return out;
}

lv_opa_t pct_to_opa(int32_t pct) { return static_cast<lv_opa_t>(pct * 255 / 100); }

lv_style_selector_t sel(lv_part_t part, lv_state_t state = LV_STATE_DEFAULT)
{
    return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

// A pointer (touch / mouse) focuses an object the moment it is pressed, before LVGL knows whether
// the gesture is a tap or a scroll. For pointers we therefore act on CLICKED (released without
// scrolling) instead of FOCUSED; keypads / encoders have no CLICKED, so FOCUSED stays for them.
bool focus_comes_from_pointer()
{
    lv_indev_t* indev = lv_indev_active();
    return indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER;
}

// ---- marks on a text field ----
// LVGL 9.6 has a setter per object flag and none for the LV_OBJ_FLAG_USER_x ones (the functions
// that take a flag are deprecated), so the runtime keeps its own bits in the user data of the
// text area, which nothing else uses.
enum : uintptr_t {
    MARK_NUMBER_PAD  = 1,   // numeric_display: its keyboard is the number pad
    MARK_TAP_PENDING = 2,   // edit session, see Ui::Impl::on_widget_focus and friends
    MARK_EDITING     = 4,
    MARK_REPLACE     = 8,
};
bool has_mark(lv_obj_t* obj, uintptr_t m) { return (reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj)) & m) != 0; }
void set_mark(lv_obj_t* obj, uintptr_t m, bool on)
{
    uintptr_t v = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj));
    lv_obj_set_user_data(obj, reinterpret_cast<void*>(on ? (v | m) : (v & ~m)));
}

// ---- data display edit session (see Ui::Impl::on_widget_focus and friends) ----
// Marks on the textarea: tap pending, editing, replace (whole text selected, the next insert replaces).
void select_all(lv_obj_t* ta)
{
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    uint32_t len = lv_textarea_get_cursor_pos(ta);             // characters, not bytes
    lv_obj_t* label = lv_textarea_get_label(ta);
    lv_label_set_text_selection_start(label, 0);
    lv_label_set_text_selection_end(label, len);
    set_mark(ta, MARK_EDITING | MARK_REPLACE, true);
}

void clear_selection(lv_obj_t* ta)
{
    lv_obj_t* label = lv_textarea_get_label(ta);
    lv_label_set_text_selection_start(label, LV_DRAW_LABEL_NO_TXT_SEL);
    lv_label_set_text_selection_end(label, LV_DRAW_LABEL_NO_TXT_SEL);
    set_mark(ta, MARK_REPLACE, false);
}

void end_edit_session(lv_obj_t* ta)
{
    clear_selection(ta);
    set_mark(ta, MARK_TAP_PENDING | MARK_EDITING | MARK_REPLACE, false);
}

// LVGL keeps the pressed object until the finger is lifted (PRESS_LOCK), so CLICKED also arrives
// when the finger slid before release: off the object, or along it on a screen that does not
// scroll. Only a release on the object without sliding more than the scroll limit is a tap.
struct PressTrack { lv_obj_t* obj = nullptr; lv_point_t start{}; int32_t moved = 0; };
PressTrack g_press;
constexpr int32_t kTapSlop = 10;   // px, same as LVGL's default scroll limit

void track_press(lv_event_t* e)   // PRESSED / PRESSING on input fields
{
    lv_indev_t* indev = lv_indev_active();
    if(!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_obj_t* obj = lv_event_get_target_obj(e);
    if(lv_event_get_code(e) == LV_EVENT_PRESSED || g_press.obj != obj) { g_press = { obj, p, 0 }; return; }
    int32_t d = std::max(std::abs(p.x - g_press.start.x), std::abs(p.y - g_press.start.y));
    g_press.moved = std::max(g_press.moved, d);
}

bool is_tap(lv_obj_t* obj)
{
    lv_indev_t* indev = lv_indev_active();
    if(!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return true;   // keypad / programmatic
    if(g_press.obj == obj && g_press.moved > kTapSlop) return false;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return p.x >= a.x1 && p.x <= a.x2 && p.y >= a.y1 && p.y <= a.y2;
}

// Show text in a textarea bypassing the operator-input filters (accepted_chars / max_length),
// which lv_textarea_set_text would otherwise apply to the displayed value too ("42.3 rpm").
void ta_show(lv_obj_t* ta, const std::string& text)
{
    // a copy: LVGL (9.6) owns its list and frees it when another one is set
    const char* list = lv_textarea_get_accepted_chars(ta);
    const bool filtered = list != nullptr;
    const std::string accepted = filtered ? list : "";
    uint32_t max_len = lv_textarea_get_max_length(ta);
    lv_textarea_set_accepted_chars(ta, nullptr);
    lv_textarea_set_max_length(ta, 0);
    lv_textarea_set_text(ta, text.c_str());
    if(filtered) lv_textarea_set_accepted_chars(ta, accepted.c_str());
    lv_textarea_set_max_length(ta, max_len);
}

// ================================================================ styles

// ---- image files -------------------------------------------------------------------------
// The folder the screen being built belongs to (image paths in styles are relative to it).
std::string s_image_base_dir;
// LVGL keeps the pointer of an image path only: the strings live here (one per file).
const char* keep_path(const std::string& path)
{
    static std::set<std::string> pool;
    return pool.insert(path).first->c_str();
}
// An image of the project as LVGL wants it ("A:<folder>/<file>"), or "" (and a warning): the
// file must be inside the project folder (no "..", no absolute path), readable as an image and
// not larger than kMaxImagePixels (a decoded image takes 4 bytes per pixel of memory).
constexpr uint32_t kMaxImagePixels = 4u * 1024 * 1024;
std::string image_path(const std::string& base_dir, const std::string& src)
{
    if(src.empty()) return "";
    std::string norm = src;
    for(char& c : norm) if(c == '\\') c = '/';
    const bool outside = norm[0] == '/' || (norm.size() > 1 && norm[1] == ':') || norm == ".." || norm.rfind("../", 0) == 0
                      || norm.find("/../") != std::string::npos || (norm.size() >= 3 && norm.compare(norm.size() - 3, 3, "/..") == 0);
    if(outside) { HML_WARN("image '%s': only files inside the project folder can be shown", src.c_str()); return ""; }
    const std::string path = "A:" + base_dir + "/" + norm;
    if(norm.size() > 4 && iequals(norm.substr(norm.size() - 4), ".gif")) {
        // (GIFs are read by the gif widget, not by an image decoder: look at the file itself)
        std::string err;
        auto data = read_file(base_dir + "/" + norm, 4u << 20, &err);
        if(!data || data->size() < 10 || data->compare(0, 4, "GIF8") != 0) { HML_WARN("image '%s': no such file, not a GIF, or larger than 4 MB", src.c_str()); return ""; }
        const uint32_t w = static_cast<uint8_t>((*data)[6]) | static_cast<uint8_t>((*data)[7]) << 8, hgt = static_cast<uint8_t>((*data)[8]) | static_cast<uint8_t>((*data)[9]) << 8;
        if(static_cast<uint64_t>(w) * hgt > kMaxImagePixels / 4) { HML_WARN("image '%s': %ux%u is too large for an animation", src.c_str(), w, hgt); return ""; }
        return path;
    }
    lv_image_header_t h;
    if(lv_image_decoder_get_info(path.c_str(), &h) != LV_RESULT_OK) { HML_WARN("image '%s': no such file, or not a PNG / JPEG / BMP / GIF", src.c_str()); return ""; }
    if(static_cast<uint64_t>(h.w) * h.h > kMaxImagePixels) {
        HML_WARN("image '%s': %ux%u is too large (at most %u pixels in all)", src.c_str(), static_cast<unsigned>(h.w), static_cast<unsigned>(h.h), static_cast<unsigned>(kMaxImagePixels));
        return "";
    }
    return path;
}
// JPEG and BMP are decoded piece by piece while they are drawn: they can only be shown in their
// own size (no fit / zoom / rotation; stretching one draws garbage).
bool own_size_only(const std::string& src)
{
    const size_t dot = src.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : src.substr(dot);
    return iequals(ext, ".jpg") || iequals(ext, ".jpeg") || iequals(ext, ".bmp");
}
bool is_gif(const std::string& src)
{
    return src.size() > 4 && iequals(src.substr(src.size() - 4), ".gif");
}

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
        else if(iequals(k, "bg_image")) {
            const std::string path = image_path(s_image_base_dir, sv);
            if(!path.empty()) lv_obj_set_style_bg_image_src(obj, keep_path(path), s);
        }
        else if(iequals(k, "bg_image_opa")) lv_obj_set_style_bg_image_opa(obj, pct_to_opa(nv), s);
        else if(iequals(k, "bg_image_tiled")) lv_obj_set_style_bg_image_tiled(obj, p.is_boolean() ? p.get<bool>() : nv != 0, s);
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

enum class Widget { Panel, Label, Button, Switch, Checkbox, Slider, Bar, Arc, Led, Dropdown, Roller, Textarea, Image, Chart, Spinner, Keyboard, Line, Meter, DateTime };
enum class BindKind { Text, Value, Checked, Visible, Enabled, BgColor, TextColor, BorderColor, ImageRecolor, Options, Anim };
enum class Cmp { None, Eq, Ne, Gt, Lt, Ge, Le };

struct Screen;

// ---- "animate": a property of a widget that moves by itself -------------------------------
// { "prop": "opa", "from": 100, "to": 30, "time": 600, "playback": true, "when": { "tag": "alarm", "eq": 1 } }
// It runs while its screen is shown and its "when" holds (always, without one); when it stops
// the property goes back to what it was (rest).
struct AnimSpec {
    enum class Prop { Opa, X, Y, W, H, Rotation, Zoom, BgColor, TextColor, Frames };
    lv_obj_t*   obj = nullptr;
    Screen*     screen = nullptr;
    bool        image = false;            // an image widget: rotation / zoom are its own (cheaper than a transform)
    Prop        prop = Prop::Opa;
    int32_t     from = 0, to = 0, rest = 0;
    lv_color_t  c_from{}, c_to{}, c_rest{};
    std::vector<std::string> files;       // Frames: the pictures in order ("A:..." paths)
    std::string rest_file;
    uint32_t    time = 1000, delay = 0;
    uint32_t    repeat = 0;               // 0 = for ever
    bool        playback = false;
    lv_anim_path_cb_t path = lv_anim_path_linear;
    bool        cond = true;              // what "when" says now
    bool        running = false;

    void apply(int32_t v) const
    {
        switch(prop) {
            case Prop::Opa: lv_obj_set_style_opa(obj, static_cast<lv_opa_t>(v), LV_PART_MAIN); break;
            case Prop::X: lv_obj_set_x(obj, v); break;
            case Prop::Y: lv_obj_set_y(obj, v); break;
            case Prop::W: lv_obj_set_width(obj, v); break;
            case Prop::H: lv_obj_set_height(obj, v); break;
            case Prop::Rotation:
                if(image) lv_image_set_rotation(obj, v);
                else lv_obj_set_style_transform_rotation(obj, v, LV_PART_MAIN);
                break;
            case Prop::Zoom:
                if(image) lv_image_set_scale(obj, static_cast<uint32_t>(v));
                else { lv_obj_set_style_transform_scale_x(obj, v, LV_PART_MAIN); lv_obj_set_style_transform_scale_y(obj, v, LV_PART_MAIN); }
                break;
            case Prop::BgColor: lv_obj_set_style_bg_color(obj, lv_color_mix(c_to, c_from, static_cast<uint8_t>(v)), LV_PART_MAIN); break;
            case Prop::TextColor: lv_obj_set_style_text_color(obj, lv_color_mix(c_to, c_from, static_cast<uint8_t>(v)), LV_PART_MAIN); break;
            case Prop::Frames: {
                if(files.empty()) break;
                const size_t i = std::min<size_t>(static_cast<size_t>(v < 0 ? 0 : v), files.size() - 1);
                if(lv_image_get_src(obj) != files[i].c_str()) lv_image_set_src(obj, files[i].c_str());
                break;
            }
        }
    }
    void to_rest() const
    {
        switch(prop) {
            case Prop::BgColor: lv_obj_set_style_bg_color(obj, c_rest, LV_PART_MAIN); break;
            case Prop::TextColor: lv_obj_set_style_text_color(obj, c_rest, LV_PART_MAIN); break;
            case Prop::Frames: if(!rest_file.empty()) lv_image_set_src(obj, rest_file.c_str()); break;
            default: apply(rest);
        }
    }
    static void exec(void* var, int32_t v) { static_cast<AnimSpec*>(var)->apply(v); }
    void stop()
    {
        if(!running) return;
        running = false;
        lv_anim_delete(this, &AnimSpec::exec);
        to_rest();
    }
    void start()
    {
        if(running) return;
        running = true;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, this);
        lv_anim_set_exec_cb(&a, &AnimSpec::exec);
        lv_anim_set_values(&a, from, to);
        lv_anim_set_duration(&a, time);
        lv_anim_set_delay(&a, delay);
        if(playback) lv_anim_set_reverse_duration(&a, time);
        lv_anim_set_repeat_count(&a, repeat ? repeat : LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, path);
        lv_anim_start(&a);
    }
    void update();   // start or stop, by "when" and by whether the screen is shown (defined after Screen)
};

struct Binding {
    lv_obj_t*   obj = nullptr;        // the widget (for buttons with text: the inner label for Text)
    Screen*     screen = nullptr;     // the screen the widget is on
    bool        stale = false;        // the tag changed while that screen was not shown
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
    ValueMap<lv_color_t> color_map;      // bind.bg_color / text_color "map": value -> color
    ValueMap<std::string> text_map;      // bind.text "map": value -> text ("@ID" resolved)
    std::vector<std::pair<double, std::string>> src_map;   // bind.src "map": value -> image file
    std::string default_src, base_dir;
    bool        has_src_map = false;
    AnimSpec*   anim = nullptr;       // BindKind::Anim: the animation whose "when" this is
    lv_style_selector_t part = LV_PART_MAIN;
    bool        two_way = false;
    std::optional<double> vmin, vmax;     // numeric_display "min"/"max": clamp what the operator typed
    bool        commit_on_ready = false;  // data displays: write on READY / DEFOCUSED, revert on CANCEL

    bool condition(const Value& v) const;
    std::optional<lv_color_t> map_color(const Value& v) const;
    void apply_value(double n);
    void on_tag_changed(const Value& v, const void* source);
    // A tag notification. Widgets of a screen that is not shown are not touched: with many
    // screens most notifications would go to widgets nobody sees. They are brought up to date
    // when their screen is shown (Impl::refresh_stale).
    void tag_event(const Value& v, const void* source);
    void on_widget_changed(lv_obj_t* target);
    void write_from_widget(lv_obj_t* target);
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
    if(const lv_color_t* c = color_map.find(v)) return *c;
    return std::nullopt;
}

void Binding::apply_value(double n)
{
    int32_t iv = static_cast<int32_t>(std::llround(n * scale + offset));
    switch(wk) {
        case Widget::Slider:   lv_slider_set_value(obj, iv, LV_ANIM_OFF); break;
        case Widget::Bar:      lv_bar_set_value(obj, iv, LV_ANIM_ON); break;
        case Widget::Arc:      lv_arc_set_value(obj, iv); break;
        case Widget::Meter:    meter_set_value(obj, n * scale + offset); break;
        case Widget::Dropdown: lv_dropdown_set_selected(obj, static_cast<uint32_t>(iv < 0 ? 0 : iv)); break;
        case Widget::Roller:   lv_roller_set_selected(obj, static_cast<uint32_t>(iv < 0 ? 0 : iv), LV_ANIM_OFF); break;
        case Widget::Led:      lv_led_set_brightness(obj, static_cast<uint8_t>(iv < 0 ? 0 : iv > 255 ? 255 : iv)); break;
        default: {
            std::string text = Value::number(n * scale + offset).format(format);
            if(wk == Widget::Textarea) ta_show(obj, text);
            else if(wk == Widget::Checkbox) lv_checkbox_set_text(obj, text.c_str());
            else if(lv_obj_check_type(obj, &lv_label_class)) lv_label_set_text(obj, text.c_str());   // (nothing else has a text)
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
            if(!text_map.empty() && wk != Widget::Image && wk != Widget::Textarea) {
                if(const std::string* t = text_map.find(v)) text = *t;
            }
            if(wk == Widget::Textarea) ta_show(obj, text);
            else if(wk == Widget::Checkbox) lv_checkbox_set_text(obj, text.c_str());
            else if(wk == Widget::Image) {
                // the file: by the map (value -> file), else the tag's text; inside the project folder.
                // (a text that starts with "A:" is taken as it is, as before)
                std::string file = text;
                if(has_src_map) {
                    file = default_src;
                    const double n = v.is_string() ? std::atof(v.str.c_str()) : v.num;
                    for(const auto& [key, f] : src_map) if(key == n) { file = f; break; }
                }
                if(file.rfind("A:", 0) == 0) lv_image_set_src(obj, keep_path(file));
                else {
                    const std::string path = image_path(base_dir, file);
                    if(path.empty()) lv_obj_set_hidden(obj, true);          // nothing to show for this value
                    else { lv_image_set_src(obj, keep_path(path)); lv_obj_set_hidden(obj, false); }
                }
            }
            else if(lv_obj_check_type(obj, &lv_label_class)) lv_label_set_text(obj, text.c_str());   // (nothing else has a text)
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
            if(condition(v)) lv_obj_set_hidden(obj, false);
            else lv_obj_set_hidden(obj, true);
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
        case BindKind::BorderColor:
            // (the width comes from the style: a widget without a border shows nothing)
            if(auto c = map_color(v)) { lv_obj_set_style_border_color(obj, *c, part); lv_obj_set_style_border_opa(obj, LV_OPA_COVER, part); }
            break;
        case BindKind::ImageRecolor:
            if(auto c = map_color(v)) { lv_obj_set_style_image_recolor(obj, *c, 0); lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, 0); }
            break;
        case BindKind::Anim:
            if(anim) { anim->cond = condition(v); anim->update(); }
            break;
        case BindKind::Options:
            // new options reset the selection: keep the selected index (the value binding only
            // speaks up when its own tag changes)
            if(wk == Widget::Dropdown) {
                uint32_t sel = lv_dropdown_get_selected(obj);
                lv_dropdown_set_options(obj, v.str.c_str());
                lv_dropdown_set_selected(obj, sel < lv_dropdown_get_option_count(obj) ? sel : 0);
            }
            else if(wk == Widget::Roller) {
                uint32_t sel = lv_roller_get_selected(obj);
                lv_roller_set_options(obj, v.str.c_str(), LV_ROLLER_MODE_NORMAL);
                lv_roller_set_selected(obj, sel < lv_roller_get_option_count(obj) ? sel : 0, LV_ANIM_OFF);
            }
            break;
    }
}

// widget -> tag (two-way bindings)
void Binding::on_widget_changed(lv_obj_t* target)
{
    if(!operation_log().enabled()) { write_from_widget(target); return; }
    const Value before = tag->get();
    write_from_widget(target);
    const Value after = tag->get();
    if(before.num == after.num && before.str == after.str) return;
    // never store what was typed into a password field
    const bool secret = wk == Widget::Textarea && lv_textarea_get_password_mode(target);
    operation_log().record("write", tag->name(), secret ? "***" : before.format(), secret ? "***" : after.format());
}

void Binding::write_from_widget(lv_obj_t* target)
{
    if(commit_on_ready) end_edit_session(target);   // edit session over
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
    AnimSpec*   anim = nullptr;           // add_binding(BindKind::Anim): whose "when" it is
};

enum class ActionKind { Goto, Back, Set, Toggle, Inc, Dec, Reload, Exit, Restart, Call, Script, Popup, Close };

struct Action {
    ActionKind  kind = ActionKind::Back;
    std::string screen, anim;
    std::string fn, arg;              // Call; Script: fn = script name, arg = inline code
    Tag*        tag = nullptr;
    Value       value;
    double      by = 1;
    RestartTarget restart = RestartTarget::Default;
    std::vector<std::pair<Tag*, Value>> sets;    // Popup: tags written before it opens
};

struct ActionList {
    std::vector<Action> actions;
};

// "level": N on a widget: only users of that level may operate it. What the others get:
//   "level_deny": "disable" (default) | "hide" | "login" (looks normal, a touch opens the login screen)
struct AccessRule {
    enum class Deny { Disable, Hide, Login };
    lv_obj_t* obj = nullptr;
    int       level = 0;
    Deny      deny = Deny::Disable;
    bool      base_disabled = false, base_hidden = false;    // as the screen file says, without the rule
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
    bool        shown = false;        // the screen on the display right now
    json        def;                  // its description, until the widgets are built (obj == nullptr)
    json        source;               // its description (for rebuild_all)
    std::string base_dir;
    std::vector<Binding*> bindings;   // of the widgets on this screen
    int         level = 0;            // "level": minimum user level to see the screen
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   keyboard = nullptr;   // optional on-screen keyboard
    lv_obj_t*   kb_echo = nullptr;    // shows the edited text next to the keyboard when it hides the field
    // "popup": true - shown over the current screen (action "popup"), never as a screen itself.
    // obj is then a box on `backdrop`, a full-size object on the top layer that takes every touch.
    bool        popup = false;
    lv_obj_t*   backdrop = nullptr;
};

void AnimSpec::update()
{
    const bool want = cond && (!screen || screen->shown);
    if(want) start();
    else stop();
}

// While no screen is shown at all (screens built for a test, or between unload and the first
// show) every widget is kept up to date.
static bool s_some_screen_shown = false;

void Binding::tag_event(const Value& v, const void* source)
{
    if(screen && !screen->shown && s_some_screen_shown) { if(source != this) stale = true; return; }
    on_tag_changed(v, source);
}

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
    std::vector<int>                         popups;      // open popups, the top one last

    void bring_up_to_date(Screen& scr)
    {
        for(Binding* b : scr.bindings) {
            if(!b->stale) continue;
            b->stale = false;
            b->on_tag_changed(b->tag->get(), nullptr);
        }
    }
    bool open_popup(const std::string& name);
    bool close_popup(const std::string& name);      // "" = the top one
    void close_all_popups() { while(!popups.empty()) close_popup(""); }
    std::vector<std::unique_ptr<Binding>>    bindings;
    std::vector<std::unique_ptr<ActionList>> action_lists;
    std::vector<std::unique_ptr<AccessRule>> access_rules;
    std::vector<std::unique_ptr<ChartCtx>>   charts;
    std::vector<std::unique_ptr<AnimSpec>>   anims;       // "animate" of the widgets
    void update_anims() { for(auto& a : anims) a->update(); }
    void parse_animate(lv_obj_t* obj, Widget wk, const json& w);
    std::vector<std::unique_ptr<std::vector<lv_point_precise_t>>> line_points;   // must outlive lv_line objects
    std::vector<std::unique_ptr<std::string>>  owned_strings;  // textarea accepted_chars: LVGL stores the pointer only
    lv_group_t*   group = nullptr;
    lv_style_t    style_disabled{};   // visual feedback for LV_STATE_DISABLED (the theme has none for most widgets)
    lv_indev_t*   kb_indev = nullptr;
    AppCallbacks  app;
    std::string   base_dir;
    Screen*       building = nullptr;
    BindOpts      bind_opts;          // for the widget currently being built
    std::map<std::string, std::function<void(const std::string&)>> named_actions;

    Screen* find_screen(const std::string& name) const
    {
        for(auto& s : screens) if(s->name == name) return s.get();
        return nullptr;
    }

    // ---- events (LVGL -> C++) ----
    static void on_widget_value_changed(lv_event_t* e)
    {
        Binding* b = static_cast<Binding*>(lv_event_get_user_data(e));
        if(ui().impl_->denied(lv_event_get_target_obj(e))) { b->on_tag_changed(b->tag->get(), nullptr); return; }   // show the tag again
        b->on_widget_changed(lv_event_get_target_obj(e));
    }
    // ---- access ("level") ----
    const AccessRule* rule_of(lv_obj_t* obj) const
    {
        for(const auto& r : access_rules) if(r->obj == obj) return r.get();
        return nullptr;
    }
    bool denied(lv_obj_t* obj) const
    {
        const AccessRule* r = rule_of(obj);
        return r && !users().allowed(r->level);
    }
    // Before the widget itself sees the press: a user who may not operate it gets no click, no
    // drag, no edit; with "level_deny": "login" the login screen opens.
    static void on_access_press(lv_event_t* e)
    {
        const AccessRule* r = static_cast<const AccessRule*>(lv_event_get_user_data(e));
        if(users().allowed(r->level)) return;
        if(lv_indev_t* indev = lv_indev_active()) lv_indev_wait_release(indev);
        lv_event_stop_processing(e);
        if(r->deny == AccessRule::Deny::Login) users().request_login(r->level);
    }
    void parse_access(lv_obj_t* obj, const json& w);
    void ensure_built(Screen& scr);
    std::vector<json> template_layers(const Screen& scr, const json& def);   // build the widgets of a screen registered with add_screen()
    // The screen at `idx` becomes the one on the display: bring its widgets up to date with the
    // tags that changed while it was not shown.
    // sys.screen: the name of the screen shown (not the popups). Made before any screen is built,
    // so that bindings find it (a template's menu marks the screen it is on).
    static Tag* screen_tag()
    {
        Tag* t = tags().find("sys.screen");
        if(!t) { Value v = Value::string(""); t = tags().create("sys.screen", TagType::String, &v, false, 0, 0); }
        return t;
    }
    void set_shown(int idx)
    {
        for(auto& sc : screens) if(!sc->popup) sc->shown = false;     // (open popups stay shown)
        if(idx < 0 || static_cast<size_t>(idx) >= screens.size()) { s_some_screen_shown = false; return; }
        Screen& scr = *screens[static_cast<size_t>(idx)];
        scr.shown = true;
        s_some_screen_shown = true;
        if(!scr.popup) {
            if(Tag* t = screen_tag(); t && t->get().format() != scr.name) {
                t->set_string(scr.name, nullptr);
                // the new screen shows its own name at once (not one frame later, at the dispatch)
                for(Binding* b : scr.bindings) if(b->tag == t) b->on_tag_changed(t->get(), nullptr);
            }
        }
        bring_up_to_date(scr);
        update_anims();                                               // (those of the screen that was left stop)
    }
    void apply_access(const AccessRule& r);
    static void on_widget_cancel(lv_event_t* e)   // data display: discard the edit, show the tag again
    {
        Binding* b = static_cast<Binding*>(lv_event_get_user_data(e));
        end_edit_session(lv_event_get_target_obj(e));
        b->on_tag_changed(b->tag->get(), nullptr);
    }
    // Data display edit session, tracked with marks on the textarea (has_mark / set_mark):
    //   MARK_TAP_PENDING = "tap pending": pressed (or focused) by a pointer, waiting for the CLICKED that is not a scroll
    //   MARK_EDITING = "editing": the session started (the text was selected once); later taps only move the cursor
    //   MARK_REPLACE = "replace pending": the whole text is selected, the next insert replaces it
    // READY / CANCEL / DEFOCUSED end the session.
    static void on_widget_focus(lv_event_t* e)
    {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        if(has_mark(ta, MARK_EDITING)) return;
        if(focus_comes_from_pointer()) { set_mark(ta, MARK_TAP_PENDING, true); return; }
        select_all(ta);                                           // keypad / encoder: start editing right away
    }
    // LVGL 9.6 sends FOCUSED only when the focus moves: a press on the field that already has the
    // focus (the usual case after an edit was confirmed) must start the wait for the tap too.
    static void on_widget_press(lv_event_t* e)
    {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        if(has_mark(ta, MARK_EDITING)) return;
        if(focus_comes_from_pointer()) set_mark(ta, MARK_TAP_PENDING, true);
    }
    static void on_widget_click(lv_event_t* e)
    {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        if(!has_mark(ta, MARK_TAP_PENDING)) return;
        if(!is_tap(ta)) return;                                   // slid or dragged off: keep waiting for a real tap
        set_mark(ta, MARK_TAP_PENDING, false);
        select_all(ta);
    }
    static void on_widget_insert(lv_event_t* e)   // first key after select-all replaces the old text
    {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        if(!has_mark(ta, MARK_REPLACE)) return;
        const char* txt = static_cast<const char*>(lv_event_get_param(e));
        clear_selection(ta);
        lv_textarea_set_text(ta, "");                             // the pending insert then lands in the empty field
        // backspace / delete on a selection: deleting the selection is all it does
        if(txt && (txt[0] == LV_KEY_DEL || txt[0] == LV_KEY_BACKSPACE) && txt[1] == 0) lv_textarea_set_insert_replace(ta, "");
    }
    static void on_widget_edited(lv_event_t* e)   // any other change (e.g. backspace) just drops the selection
    {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        if(has_mark(ta, MARK_REPLACE)) clear_selection(ta);
    }
    static void on_widget_blur(lv_event_t* e) { end_edit_session(lv_event_get_target_obj(e)); }
    static void on_action_event(lv_event_t* e);
    static void on_chart_timer(lv_timer_t* t) { static_cast<ChartCtx*>(lv_timer_get_user_data(t))->tick(); }
    static void on_textarea_event(lv_event_t* e)
    {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        Screen* scr = static_cast<Screen*>(lv_event_get_user_data(e));
        if(!scr->keyboard) return;
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_VALUE_CHANGED) { update_echo(scr, ta); return; }
        if(code == LV_EVENT_FOCUSED && focus_comes_from_pointer()) return;   // wait for CLICKED: it may be a scroll
        if(code == LV_EVENT_CLICKED && !is_tap(ta)) return;                  // finger slid or left the field: not a tap
        if(code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
            lv_keyboard_set_textarea(scr->keyboard, ta);
            // numeric_display marks itself: give it the number pad
            lv_keyboard_set_mode(scr->keyboard, has_mark(ta, MARK_NUMBER_PAD) ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
            lv_obj_set_hidden(scr->keyboard, false);
            place_keyboard(scr, ta);
        }
        else if(code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            lv_obj_set_hidden(scr->keyboard, true);
            if(scr->kb_echo) lv_obj_set_hidden(scr->kb_echo, true);
        }
    }
    // A keyboard aligned to the bottom (or top) edge moves to the opposite edge when it would hide
    // the field being edited, so the operator can see what is typed. When the field is hidden either
    // way (middle of a small screen) an "echo" bar next to the keyboard shows the text instead.
    static void place_keyboard(Screen* scr, lv_obj_t* ta)
    {
        lv_obj_t* kb = scr->keyboard;
        lv_align_t align = lv_obj_get_style_align(kb, LV_PART_MAIN);
        bool edge = align == LV_ALIGN_BOTTOM_MID || align == LV_ALIGN_TOP_MID;
        lv_obj_update_layout(scr->obj);
        int32_t kb_h = lv_obj_get_height(kb);
        int32_t scr_h = lv_obj_get_height(scr->obj);
        lv_area_t a;
        lv_obj_get_coords(ta, &a);
        bool covered_at_bottom = a.y2 > scr_h - kb_h;
        bool covered_at_top = a.y1 < kb_h;
        bool covered;
        if(edge) {
            if(covered_at_bottom && !covered_at_top) align = LV_ALIGN_TOP_MID;
            else if(covered_at_top && !covered_at_bottom) align = LV_ALIGN_BOTTOM_MID;
            else align = LV_ALIGN_BOTTOM_MID;
            lv_obj_set_align(kb, align);
            covered = align == LV_ALIGN_BOTTOM_MID ? covered_at_bottom : covered_at_top;
        }
        else {
            lv_obj_update_layout(scr->obj);
            lv_area_t k;
            lv_obj_get_coords(kb, &k);
            covered = a.y2 >= k.y1 && a.y1 <= k.y2;
        }

        if(!covered) { if(scr->kb_echo) lv_obj_set_hidden(scr->kb_echo, true); return; }
        if(!scr->kb_echo) {
            lv_obj_t* echo = lv_label_create(scr->obj);
            lv_obj_set_width(echo, lv_pct(100));
            lv_label_set_long_mode(echo, LV_LABEL_LONG_MODE_CLIP);
            lv_obj_set_style_bg_color(echo, lv_color_hex(0x1f2a38), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(echo, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(echo, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_pad_all(echo, 8, LV_PART_MAIN);
            lv_obj_set_style_text_align(echo, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_set_clickable(echo, false);
            scr->kb_echo = echo;
        }
        lv_obj_set_style_text_font(scr->kb_echo, lv_obj_get_style_text_font(ta, LV_PART_MAIN), LV_PART_MAIN);
        lv_obj_set_hidden(scr->kb_echo, false);
        lv_obj_move_foreground(scr->kb_echo);
        update_echo(scr, ta);
        lv_obj_update_layout(scr->obj);
        lv_obj_align_to(scr->kb_echo, kb, edge && align == LV_ALIGN_TOP_MID ? LV_ALIGN_OUT_BOTTOM_MID : LV_ALIGN_OUT_TOP_MID, 0, 0);
    }
    static void update_echo(Screen* scr, lv_obj_t* ta)
    {
        if(!scr->kb_echo || lv_obj_is_hidden(scr->kb_echo)) return;
        if(!scr->keyboard || lv_keyboard_get_textarea(scr->keyboard) != ta) return;
        std::string text = lv_textarea_get_text(ta);
        if(lv_textarea_get_password_mode(ta)) text = std::string(text.size(), '*');
        lv_label_set_text(scr->kb_echo, (text + "|").c_str());
    }
    static void on_keyboard_done(lv_event_t* e)
    {
        // Only hide. Re-focusing the textarea here would send LV_EVENT_FOCUSED and re-open it.
        lv_obj_set_hidden(lv_event_get_target_obj(e), true);
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

    bool show_screen(const std::string& name, const std::string& anim, bool replace = false);
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

// "animate": one object or a list of them.
//   prop      opa (0-100) | x | y | w | h (px) | rotation (degrees) | zoom (1 = as it is) | bg_color | text_color
//             | frames ("files": pictures shown one after the other; image widgets)
//   from, to  the two ends ("from" default: what the widget has now)
//   time      ms for one way (default 1000);  delay ms;  playback: true = there and back
//   repeat    how many times (default: for ever)
//   path      linear (default) | ease_in | ease_out | ease_in_out | step (jumps at the end of "time")
//   when      a tag ("alarm") or a condition ({ "tag": "state", "eq": 2 }): it runs only while that holds
//   preset    "blink" | "pulse" | "spin": the keys above filled in (they can still be given)
// Something that flashes faster than 3 times a second can trigger seizures: opa and colour
// animations are slowed down to that.
void Ui::Impl::parse_animate(lv_obj_t* obj, Widget wk, const json& w)
{
    const json* list = jget(w, "animate");
    if(!list) return;
    for(json j : list->is_array() ? *list : json::array({ *list })) {
        if(j.is_string()) j = json{ { "preset", j } };
        if(!j.is_object()) { HML_WARN("animate: an animation is an object { \"prop\": ... }"); continue; }
        const std::string preset = jstr(j, "preset", "");
        auto dflt = [&](const char* k, const json& v) { if(!j.contains(k)) j[k] = v; };
        if(iequals(preset, "blink")) { dflt("prop", "opa"); dflt("to", 0); dflt("time", 500); dflt("playback", true); dflt("path", "step"); }
        else if(iequals(preset, "pulse")) { dflt("prop", "opa"); dflt("to", 35); dflt("time", 700); dflt("playback", true); dflt("path", "ease_in_out"); }
        else if(iequals(preset, "spin")) { dflt("prop", "rotation"); dflt("from", 0); dflt("to", 360); dflt("time", 2000); }
        else if(!preset.empty()) { HML_WARN("animate: unknown preset '%s' (blink, pulse, spin)", preset.c_str()); continue; }

        auto a = std::make_unique<AnimSpec>();
        a->obj = obj;
        a->screen = building;
        a->image = wk == Widget::Image;
        const std::string prop = jstr(j, "prop", "");
        using P = AnimSpec::Prop;
        static const std::pair<const char*, P> props[] = { { "opa", P::Opa }, { "x", P::X }, { "y", P::Y }, { "w", P::W }, { "h", P::H }, { "rotation", P::Rotation },
            { "zoom", P::Zoom }, { "bg_color", P::BgColor }, { "text_color", P::TextColor }, { "frames", P::Frames } };
        bool known = false;
        for(const auto& [n, p] : props) if(iequals(prop, n)) { a->prop = p; known = true; }
        if(!known) { HML_WARN("animate: unknown prop '%s' (opa, x, y, w, h, rotation, zoom, bg_color, text_color, frames)", prop.c_str()); continue; }

        a->time = static_cast<uint32_t>(std::clamp(jnum(j, "time", 1000), 20.0, 3600000.0));
        a->delay = static_cast<uint32_t>(std::clamp(jnum(j, "delay", 0), 0.0, 3600000.0));
        a->playback = jbool(j, "playback", false);
        a->repeat = static_cast<uint32_t>(std::clamp(jnum(j, "repeat", 0), 0.0, 65000.0));
        const std::string path = jstr(j, "path", "linear");
        a->path = iequals(path, "ease_in") ? lv_anim_path_ease_in : iequals(path, "ease_out") ? lv_anim_path_ease_out
                : iequals(path, "ease_in_out") ? lv_anim_path_ease_in_out : iequals(path, "step") ? lv_anim_path_step : lv_anim_path_linear;
        if(!iequals(path, "linear") && a->path == lv_anim_path_linear) HML_WARN("animate: unknown path '%s' (linear, ease_in, ease_out, ease_in_out, step)", path.c_str());

        const bool has_from = j.contains("from");
        if(!j.contains("to") && a->prop != P::Frames) { HML_WARN("animate: '%s' needs \"to\"", prop.c_str()); continue; }
        switch(a->prop) {
            case P::Opa:
                a->rest = lv_obj_get_style_opa(obj, LV_PART_MAIN);
                a->from = has_from ? pct_to_opa(static_cast<int32_t>(jnum(j, "from", 100))) : a->rest;
                a->to = pct_to_opa(static_cast<int32_t>(jnum(j, "to", 0)));
                break;
            case P::X: case P::Y: case P::W: case P::H:
                a->rest = a->prop == P::X ? lv_obj_get_style_x(obj, LV_PART_MAIN) : a->prop == P::Y ? lv_obj_get_style_y(obj, LV_PART_MAIN)
                        : a->prop == P::W ? lv_obj_get_style_width(obj, LV_PART_MAIN) : lv_obj_get_style_height(obj, LV_PART_MAIN);
                a->from = has_from ? static_cast<int32_t>(jnum(j, "from", 0)) : a->rest;
                a->to = static_cast<int32_t>(jnum(j, "to", 0));
                break;
            case P::Rotation:
                a->rest = a->image ? lv_image_get_rotation(obj) : 0;
                a->from = has_from ? static_cast<int32_t>(jnum(j, "from", 0) * 10) : a->rest;
                a->to = static_cast<int32_t>(jnum(j, "to", 0) * 10);
                if(!a->image) { lv_obj_set_style_transform_pivot_x(obj, lv_pct(50), LV_PART_MAIN); lv_obj_set_style_transform_pivot_y(obj, lv_pct(50), LV_PART_MAIN); }
                break;
            case P::Zoom:
                a->rest = a->image ? static_cast<int32_t>(lv_image_get_scale(obj)) : 256;
                a->from = has_from ? static_cast<int32_t>(jnum(j, "from", 1) * 256) : a->rest;
                a->to = static_cast<int32_t>(jnum(j, "to", 1) * 256);
                if(!a->image) { lv_obj_set_style_transform_pivot_x(obj, lv_pct(50), LV_PART_MAIN); lv_obj_set_style_transform_pivot_y(obj, lv_pct(50), LV_PART_MAIN); }
                break;
            case P::BgColor: case P::TextColor:
                a->c_rest = a->prop == P::BgColor ? lv_obj_get_style_bg_color(obj, LV_PART_MAIN) : lv_obj_get_style_text_color(obj, LV_PART_MAIN);
                a->c_from = has_from ? parse_color(jstr(j, "from", ""), a->c_rest) : a->c_rest;
                a->c_to = parse_color(jstr(j, "to", ""), a->c_rest);
                a->from = 0; a->to = 255;
                break;
            case P::Frames: {
                if(!a->image) { HML_WARN("animate: \"frames\" is for image widgets"); break; }
                if(const json* fl = jget(j, "files"); fl && fl->is_array())
                    for(const json& f : *fl) if(f.is_string()) { const std::string p = image_path(base_dir, f.get<std::string>()); if(!p.empty()) a->files.push_back(keep_path(p)); }
                if(const void* src = lv_image_get_src(obj)) a->rest_file = static_cast<const char*>(src);
                a->from = 0; a->to = static_cast<int32_t>(a->files.size());
                a->playback = false;
                a->path = lv_anim_path_linear;
                break;
            }
        }
        if(a->prop == P::Frames && a->files.size() < 2) { HML_WARN("animate: \"frames\" needs \"files\" with two pictures or more"); continue; }
        // at most three flashes a second
        if(a->prop == P::Opa || a->prop == P::BgColor || a->prop == P::TextColor) {
            const uint32_t min_time = a->playback ? 167 : 334;
            if(a->time < min_time) { HML_WARN("animate: %u ms flashes faster than 3 times a second; %u ms is used", static_cast<unsigned>(a->time), static_cast<unsigned>(min_time)); a->time = min_time; }
        }

        AnimSpec* raw = a.get();
        anims.push_back(std::move(a));
        if(const json* when = jget(j, "when")) {
            raw->cond = false;
            BindOpts saved = bind_opts;
            bind_opts = BindOpts{};
            bind_opts.anim = raw;
            if(!add_binding(obj, wk, BindKind::Anim, *when, false)) HML_WARN("animate: it never runs (its \"when\" names no tag)");
            bind_opts = saved;
        }
        raw->update();
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
    b->anim = bind_opts.anim;
    if(spec.is_object()) {
        b->format = jstr(spec, "format", bind_opts.format);
        b->invert = jbool(spec, "invert", false);
        b->scale = jnum(spec, "scale", 1);
        b->offset = jnum(spec, "offset", 0);
        std::string part = jstr(spec, "part", "main");
        if(iequals(part, "indicator")) b->part = LV_PART_INDICATOR;
        else if(iequals(part, "knob")) b->part = LV_PART_KNOB;
        parse_cmp(*b, spec);
        const json* map = jget(spec, "map");
        if(map && !map->is_object()) map = nullptr;
        if(map && wk == Widget::Image && kind == BindKind::Text) {          // (bind.src: value -> file)
            b->has_src_map = true;
            for(auto it = map->begin(); it != map->end(); ++it) {
                if(!it.value().is_string()) continue;
                if(iequals(it.key(), "default")) b->default_src = it.value().get<std::string>();
                else b->src_map.emplace_back(iequals(it.key(), "true") ? 1 : iequals(it.key(), "false") ? 0 : std::atof(it.key().c_str()), it.value().get<std::string>());
            }
        }
        else if(map && kind == BindKind::Text) {
            // value -> text ("@ID" in the language of the screens: they are rebuilt on a switch)
            for(auto it = map->begin(); it != map->end(); ++it) {
                std::string t;
                if(it.value().is_string()) t = texts().resolve(it.value().get<std::string>());
                else if(it.value().is_number() || it.value().is_boolean()) t = it.value().dump();
                else continue;
                b->text_map.add(iequals(it.key(), "default") ? "default" : it.key(), std::move(t));
            }
        }
        else if(map) {
            for(auto it = map->begin(); it != map->end(); ++it) {
                if(!it.value().is_string()) continue;
                lv_color_t c = parse_color(it.value().get<std::string>(), lv_color_black());
                b->color_map.add(iequals(it.key(), "default") ? "default" : it.key(), c);
            }
        }
        two_way = jbool(spec, "two_way", two_way);
    }
    b->two_way = two_way;
    b->base_dir = base_dir;

    Binding* raw = b.get();
    raw->screen = building;
    if(building) building->bindings.push_back(raw);
    bindings.push_back(std::move(b));
    tag->subscribe(SubGroup::UI, [raw](Tag&, const Value& v, const void* src) { raw->tag_event(v, src); });
    if(raw->cmp_tag) {
        // the condition depends on a second tag: re-evaluate with the bound tag's current value
        raw->cmp_tag->subscribe(SubGroup::UI, [raw](Tag&, const Value&, const void*) { raw->tag_event(raw->tag->get(), nullptr); });
    }
    if(two_way && raw->commit_on_ready) {
        lv_obj_add_event_cb(obj, &Impl::on_widget_focus, LV_EVENT_FOCUSED, raw);
        lv_obj_add_event_cb(obj, &Impl::on_widget_press, LV_EVENT_PRESSED, raw);
        lv_obj_add_event_cb(obj, &Impl::on_widget_click, LV_EVENT_CLICKED, raw);
        lv_obj_add_event_cb(obj, &Impl::on_widget_insert, LV_EVENT_INSERT, raw);
        lv_obj_add_event_cb(obj, &Impl::on_widget_edited, LV_EVENT_VALUE_CHANGED, raw);
        lv_obj_add_event_cb(obj, &Impl::on_widget_blur, LV_EVENT_DEFOCUSED, raw);
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
            case Widget::Slider: case Widget::Bar: case Widget::Arc: case Widget::Dropdown: case Widget::Roller: case Widget::Meter:
                add_binding(obj, wk, BindKind::Value, *bind, wk != Widget::Bar && wk != Widget::Meter); break;
            case Widget::Textarea: add_binding(obj, wk, BindKind::Text, *bind, true); break;
            case Widget::DateTime: break;                    // (the widget reads and writes its tag itself)
            case Widget::Panel: case Widget::Spinner: add_binding(obj, wk, BindKind::Visible, *bind, false); break;
            default: add_binding(tobj, wk, BindKind::Text, *bind, false); break;
        }
        return;
    }
    if(!bind->is_object()) return;
    for(auto it = bind->begin(); it != bind->end(); ++it) {
        const std::string& k = it.key();
        const json& spec = it.value();
        if(iequals(k, "text")) add_binding(tobj, wk, BindKind::Text, spec, wk == Widget::Textarea && ta_two_way);
        else if(iequals(k, "value") && wk == Widget::DateTime) continue;
        else if(iequals(k, "value")) add_binding(obj, wk, BindKind::Value, spec, wk == Widget::Slider || wk == Widget::Arc || wk == Widget::Dropdown || wk == Widget::Roller || (wk == Widget::Textarea && ta_two_way));
        else if(iequals(k, "checked") || iequals(k, "on")) add_binding(obj, wk, BindKind::Checked, spec, wk == Widget::Switch || wk == Widget::Checkbox || wk == Widget::Button);
        else if(iequals(k, "visible")) add_binding(obj, wk, BindKind::Visible, spec, false);
        else if(iequals(k, "hidden")) add_binding_inverted(obj, wk, BindKind::Visible, spec);
        else if(iequals(k, "enabled")) add_binding(obj, wk, BindKind::Enabled, spec, false);
        else if(iequals(k, "disabled")) add_binding_inverted(obj, wk, BindKind::Enabled, spec);
        else if(iequals(k, "bg_color")) add_binding(obj, wk, BindKind::BgColor, spec, false);
        else if(iequals(k, "text_color")) add_binding(tobj, wk, BindKind::TextColor, spec, false);
        else if(iequals(k, "border_color")) add_binding(obj, wk, BindKind::BorderColor, spec, false);
        else if(iequals(k, "image_recolor")) {
            if(wk == Widget::Image) add_binding(obj, wk, BindKind::ImageRecolor, spec, false);
            else HML_WARN("bind: image_recolor is for image widgets");
        }
        else if(iequals(k, "options")) add_binding(obj, wk, BindKind::Options, spec, false);
        else if(iequals(k, "src")) add_binding(obj, wk, BindKind::Text, spec, false);
        else HML_WARN("bind: unknown property '%s'", k.c_str());
    }
}

// ---------------------------------------------------------------- actions

void Ui::Impl::run_actions(const ActionList& list)
{
    OperationLog& oplog = operation_log();
    for(const Action& a : list.actions) {
        switch(a.kind) {
            case ActionKind::Goto:   oplog.record("screen", a.screen); show_screen(a.screen, a.anim); break;
            case ActionKind::Back:   oplog.record("screen", "(back)"); ui().back(); break;
            case ActionKind::Popup:
                for(const auto& [t, v] : a.sets) t->set(v, nullptr);
                if(open_popup(a.screen)) oplog.record("screen", "popup " + a.screen);
                break;
            case ActionKind::Close:  close_popup(a.screen); break;
            case ActionKind::Set:
            case ActionKind::Toggle:
            case ActionKind::Inc:
            case ActionKind::Dec: {
                const Value before = a.tag->get();
                if(a.kind == ActionKind::Set) a.tag->set(a.value, nullptr);
                else if(a.kind == ActionKind::Toggle) a.tag->set_number(a.tag->get_number() != 0 ? 0 : 1, nullptr);
                else a.tag->set_number(a.tag->get_number() + (a.kind == ActionKind::Inc ? a.by : -a.by), nullptr);
                if(oplog.enabled()) {
                    const Value after = a.tag->get();
                    if(before.num != after.num || before.str != after.str) oplog.record("write", a.tag->name(), before.format(), after.format());
                }
                break;
            }
            case ActionKind::Reload: oplog.record("system", "reload"); if(app.request_reload) app.request_reload(); break;
            case ActionKind::Exit:   oplog.record("system", "exit"); if(app.request_exit) app.request_exit(); break;
            case ActionKind::Restart: oplog.record("system", "restart"); if(app.request_restart) app.request_restart(a.restart); break;
            case ActionKind::Call: {
                // not the login screen's keys (they would spell the PIN); users.cpp records logins itself
                if(a.fn.rfind("user.", 0) != 0) oplog.record("call", a.fn, "", a.arg);
                auto it = named_actions.find(a.fn);
                if(it == named_actions.end()) HML_WARN("call: unknown action '%s'", a.fn.c_str());
                else it->second(a.arg);
                break;
            }
            case ActionKind::Script:
                oplog.record("script", a.fn.empty() ? "(inline)" : a.fn);
                if(!a.fn.empty()) scripts().run(a.fn);
                else scripts().exec(a.arg, "action");
                break;
        }
    }
}

void Ui::Impl::on_action_event(lv_event_t* e)
{
    if(ui().impl_->denied(lv_event_get_current_target_obj(e))) return;
    ui().impl_->run_actions(*static_cast<ActionList*>(lv_event_get_user_data(e)));
}

void Ui::Impl::parse_access(lv_obj_t* obj, const json& w)
{
    int level = static_cast<int>(jnum(w, "level", 0));
    if(level <= 0) return;
    auto r = std::make_unique<AccessRule>();
    r->obj = obj;
    r->level = level;
    std::string deny = jstr(w, "level_deny", "disable");
    if(iequals(deny, "hide")) r->deny = AccessRule::Deny::Hide;
    else if(iequals(deny, "login")) r->deny = AccessRule::Deny::Login;
    else if(!iequals(deny, "disable")) HML_WARN("level_deny: unknown value '%s' (disable | hide | login)", deny.c_str());
    r->base_disabled = lv_obj_has_state(obj, LV_STATE_DISABLED);
    r->base_hidden = lv_obj_is_hidden(obj);
    lv_obj_add_event_cb(obj, &Impl::on_access_press, static_cast<lv_event_code_t>(LV_EVENT_PRESSED | LV_EVENT_PREPROCESS), r.get());
    apply_access(*r);
    access_rules.push_back(std::move(r));
}

void Ui::Impl::apply_access(const AccessRule& r)
{
    const bool ok = users().allowed(r.level);
    if(r.deny == AccessRule::Deny::Disable) {
        if(!ok) lv_obj_add_state(r.obj, LV_STATE_DISABLED);
        else if(!r.base_disabled) lv_obj_remove_state(r.obj, LV_STATE_DISABLED);
    }
    else if(r.deny == AccessRule::Deny::Hide) {
        if(!ok) lv_obj_set_hidden(r.obj, true);
        else if(!r.base_hidden) lv_obj_set_hidden(r.obj, false);
    }
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
    if(iequals(*kind, "popup")) {
        a.kind = ActionKind::Popup;
        a.screen = jstr(j, "screen", "");
        if(a.screen.empty()) { HML_WARN("popup action without \"screen\""); return std::nullopt; }
        // "set": { "tag": value, ... } written before the popup opens (which machine it is about)
        if(const json* s = jget(j, "set"); s && s->is_object()) {
            for(auto it = s->begin(); it != s->end(); ++it) {
                Tag* t = tags().find(it.key());
                if(!t) { HML_WARN("popup '%s': unknown tag '%s' in \"set\"", a.screen.c_str(), it.key().c_str()); continue; }
                const json& v = it.value();
                a.sets.emplace_back(t, v.is_string() ? Value::string(v.get<std::string>()) : v.is_boolean() ? Value::number(v.get<bool>() ? 1 : 0, TagType::Bool)
                                                                                                : Value::number(jscalar_to_number(v)));
            }
        }
        return a;
    }
    if(iequals(*kind, "language")) {                     // switch the language ("next": the next one of the project)
        a.kind = ActionKind::Call;
        a.fn = "lang.set";
        a.arg = jstr(j, "lang", "next");
        return a;
    }
    if(iequals(*kind, "close")) { a.kind = ActionKind::Close; a.screen = jstr(j, "screen", ""); return a; }
    if(iequals(*kind, "back")) { a.kind = ActionKind::Back; return a; }
    if(iequals(*kind, "reload")) { a.kind = ActionKind::Reload; return a; }
    if(iequals(*kind, "exit")) { a.kind = ActionKind::Exit; return a; }
    if(iequals(*kind, "login") || iequals(*kind, "logout")) {   // user management (users.hpp)
        a.kind = ActionKind::Call;
        a.fn = iequals(*kind, "login") ? "user.login" : "user.logout";
        return a;
    }
    if(iequals(*kind, "system")) {                       // open the built-in system settings menu
        a.kind = ActionKind::Goto;
        a.screen = "__system";
        a.anim = jstr(j, "anim", "slide_left");
        return a;
    }
    if(iequals(*kind, "call")) {
        a.kind = ActionKind::Call;
        a.fn = jstr(j, "fn", "");
        a.arg = jstr(j, "arg", "");
        if(a.fn.empty()) { HML_WARN("call action without \"fn\""); return std::nullopt; }
        return a;
    }
    if(iequals(*kind, "script")) {                       // { "name": "<project script>" } or { "code": "<Lua>" }
        a.kind = ActionKind::Script;
        a.fn = jstr(j, "name", "");
        if(const json* code = jget(j, "code")) {
            if(code->is_string()) a.arg = code->get<std::string>();
            else if(code->is_array()) for(const json& line : *code) if(line.is_string()) a.arg += line.get<std::string>() + "\n";
        }
        if(a.fn.empty() && a.arg.empty()) { HML_WARN("script action without \"name\" or \"code\""); return std::nullopt; }
        if(!a.fn.empty()) { if(!scripts().has(a.fn)) HML_WARN("script action: unknown script '%s'", a.fn.c_str()); }
        else scripts().check(a.arg, "action");           // report syntax errors now, not at the first click
        return a;
    }
    if(iequals(*kind, "restart")) {
        a.kind = ActionKind::Restart;
        std::string what = jstr(j, "what", "");
        if(iequals(what, "app")) a.restart = RestartTarget::App;
        else if(iequals(what, "system") || iequals(what, "reboot")) a.restart = RestartTarget::System;
        else if(!what.empty()) HML_WARN("restart: unknown target '%s' (app | system)", what.c_str());
        return a;
    }

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
        lv_obj_set_clickable(obj, true);
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

    if(jbool(w, "hidden", false)) lv_obj_set_hidden(obj, true);
    if(jhas(w, "clickable")) {
        if(jbool(w, "clickable", true)) lv_obj_set_clickable(obj, true);
        else lv_obj_set_clickable(obj, false);
    }
    if(jhas(w, "scrollable") && !jbool(w, "scrollable", true)) lv_obj_set_scrollable(obj, false);
    if(auto sb = jstr_opt(w, "scrollbar")) {
        lv_obj_set_scrollbar_mode(obj, iequals(*sb, "off") ? LV_SCROLLBAR_MODE_OFF : iequals(*sb, "on") ? LV_SCROLLBAR_MODE_ON : iequals(*sb, "active") ? LV_SCROLLBAR_MODE_ACTIVE : LV_SCROLLBAR_MODE_AUTO);
    }
    if(jbool(w, "checkable", false)) lv_obj_set_checkable(obj, true);
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
        lv_label_set_text(obj, texts().resolve(jstr(w, "text", "")).c_str());
        if(auto lm = jstr_opt(w, "long_mode")) {
            lv_label_set_long_mode(obj, iequals(*lm, "dots") ? LV_LABEL_LONG_MODE_DOTS : (iequals(*lm, "scroll") || iequals(*lm, "circular")) ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : iequals(*lm, "clip") ? LV_LABEL_LONG_MODE_CLIP : LV_LABEL_LONG_MODE_WRAP);
        }
        text_obj = obj;
    }
    else if(iequals(type, "button")) {
        wk = Widget::Button;
        obj = lv_button_create(parent);
        auto text = jstr_opt(w, "text");
        if(text || jhas(w, "bind")) {
            text_obj = lv_label_create(obj);
            lv_label_set_text(text_obj, text ? texts().resolve(*text).c_str() : "");
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
        lv_checkbox_set_text(obj, texts().resolve(jstr(w, "text", "")).c_str());
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
            lv_obj_set_clickable(obj, false);
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
        if(auto ph = jstr_opt(w, "placeholder")) lv_textarea_set_placeholder_text(obj, texts().resolve(*ph).c_str());
        lv_textarea_set_one_line(obj, jbool(w, "one_line", true));
        if(jbool(w, "one_line", true)) lv_obj_set_scroll_dir(obj, LV_DIR_HOR);   // see the data displays below
        lv_textarea_set_password_mode(obj, jbool(w, "password", false));
        if(jhas(w, "max_length")) lv_textarea_set_max_length(obj, static_cast<uint32_t>(jnum(w, "max_length", 0)));
        if(auto acc = jstr_opt(w, "accepted_chars")) {
            owned_strings.push_back(std::make_unique<std::string>(*acc));
            lv_textarea_set_accepted_chars(obj, owned_strings.back()->c_str());
        }
        if(group) lv_group_add_obj(group, obj);
        lv_obj_add_event_cb(obj, track_press, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(obj, track_press, LV_EVENT_PRESSING, nullptr);
        if(building) {
            for(lv_event_code_t code : { LV_EVENT_FOCUSED, LV_EVENT_CLICKED, LV_EVENT_DEFOCUSED, LV_EVENT_READY, LV_EVENT_CANCEL, LV_EVENT_VALUE_CHANGED }) {
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
        // one line of a value: no scrollbar (a stub of it showed at the edge of taller boxes);
        // "scrollbar" in the JSON still sets it
        if(jbool(w, "one_line", true)) lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
        // ... and it scrolls sideways only. A line taller than the box (a big font, a font file with
        // tall lines) would make the field scrollable by a few pixels up and down, and LVGL 9.6 then
        // gives it the drag instead of the list around it.
        if(jbool(w, "one_line", true)) lv_obj_set_scroll_dir(obj, LV_DIR_HOR);
        lv_textarea_set_password_mode(obj, jbool(w, "password", password));
        lv_textarea_set_text(obj, jstr(w, "text", "").c_str());
        if(auto ph = jstr_opt(w, "placeholder")) lv_textarea_set_placeholder_text(obj, texts().resolve(*ph).c_str());
        if(jhas(w, "max_length")) lv_textarea_set_max_length(obj, static_cast<uint32_t>(jnum(w, "max_length", 0)));
        std::string accepted = jstr(w, "accepted_chars", numeric ? "0123456789.-+" : "");
        if(!accepted.empty()) {
            owned_strings.push_back(std::make_unique<std::string>(accepted));
            lv_textarea_set_accepted_chars(obj, owned_strings.back()->c_str());
            // a text field that takes nothing but what the number pad has gets the number pad
            if(accepted.find_first_not_of("0123456789.-+") == std::string::npos) set_mark(obj, MARK_NUMBER_PAD, true);
        }
        bind_opts.has_natural = true;
        bind_opts.natural = numeric ? BindKind::Value : BindKind::Text;
        bind_opts.natural_two_way = input;
        bind_opts.commit_on_ready = input;
        if(numeric) {
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            set_mark(obj, MARK_NUMBER_PAD, true);            // number pad, see on_textarea_event
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
            lv_obj_add_event_cb(obj, track_press, LV_EVENT_PRESSED, nullptr);
            lv_obj_add_event_cb(obj, track_press, LV_EVENT_PRESSING, nullptr);
            if(building) {
                for(lv_event_code_t code : { LV_EVENT_FOCUSED, LV_EVENT_CLICKED, LV_EVENT_DEFOCUSED, LV_EVENT_READY, LV_EVENT_CANCEL, LV_EVENT_VALUE_CHANGED }) {
                    lv_obj_add_event_cb(obj, &Impl::on_textarea_event, code, building);
                }
            }
        }
        else {
            lv_obj_set_clickable(obj, false);
            lv_obj_set_click_focusable(obj, false);
            lv_textarea_set_cursor_click_pos(obj, false);
            lv_obj_set_style_opa(obj, LV_OPA_TRANSP, LV_PART_CURSOR);
        }
        text_obj = obj;
    }
    else if(iequals(type, "image")) {
        wk = Widget::Image;
        const std::string src = jstr(w, "src", "");
        const std::string path = image_path(base_dir, src);
        if(is_gif(src) && !path.empty()) {
            // an animated GIF plays by itself (no zoom / fit / bind for it)
            wk = Widget::Panel;
            obj = lv_gif_create(parent);
            lv_gif_set_src(obj, keep_path(path));
        }
        else {
            obj = lv_image_create(parent);
            if(!path.empty()) lv_image_set_src(obj, keep_path(path));
            // "fit": how the picture fills the widget's w / h (without it: its own size)
            const std::string fit = jstr(w, "fit", "");
            // (a picture that follows a tag: any of its files may be a JPEG / BMP)
            bool plain = own_size_only(src);
            if(const json* b = jget(w, "bind"); b && b->is_object())
                if(const json* sb = jget(*b, "src"); sb && sb->is_object())
                    if(const json* map = jget(*sb, "map"); map && map->is_object())
                        for(auto it = map->begin(); it != map->end(); ++it) if(it.value().is_string() && own_size_only(it.value().get<std::string>())) plain = true;
            if(plain) {
                if(!fit.empty() || jhas(w, "zoom") || jhas(w, "rotation"))
                    HML_WARN("image '%s': fit, zoom and rotation are for PNG only (a JPEG / BMP is shown in its own size)", src.empty() ? "(by tag)" : src.c_str());
            }
            else if(jhas(w, "zoom") || jhas(w, "rotation")) {
                if(jhas(w, "zoom")) lv_image_set_scale(obj, static_cast<uint32_t>(jnum(w, "zoom", 1.0) * 256));
                if(jhas(w, "rotation")) lv_image_set_rotation(obj, static_cast<int32_t>(jnum(w, "rotation", 0) * 10));
            }
            if(plain) {}
            else if(iequals(fit, "contain")) lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_CONTAIN);
            else if(iequals(fit, "stretch")) lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_STRETCH);
            else if(iequals(fit, "tile")) lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_TILE);
            else if(iequals(fit, "center")) lv_image_set_inner_align(obj, LV_IMAGE_ALIGN_CENTER);
            else if(!fit.empty()) HML_WARN("image: unknown fit '%s' (contain, stretch, tile, center)", fit.c_str());
        }
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
        lv_obj_set_hidden(obj, true);
        lv_obj_add_event_cb(obj, &Impl::on_keyboard_done, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(obj, &Impl::on_keyboard_done, LV_EVENT_CANCEL, nullptr);
        if(building) building->keyboard = obj;
    }
    else if(iequals(type, "trend")) {
        obj = build_trend(parent, w);
    }
    else if(iequals(type, "bar_chart")) {
        obj = build_bar_chart(parent, w);
    }
    else if(iequals(type, "recipe_table")) {
        obj = build_recipe_table(parent, w);
    }
    else if(iequals(type, "alarm_list")) {
        obj = build_alarm_list(parent, w);
    }
    else if(iequals(type, "log_view")) {
        obj = build_log_view(parent, w);
    }
    else if(iequals(type, "xy_chart")) {
        obj = build_xy_chart(parent, w);
    }
    else if(iequals(type, "table")) {
        obj = build_table_view(parent, w);
    }
    else if(iequals(type, "datetime_input")) {
        wk = Widget::DateTime;
        obj = build_datetime_input(parent, w);
    }
    else if(iequals(type, "meter")) {
        wk = Widget::Meter;
        obj = build_meter(parent, w);
    }
    else if(iequals(type, "alarm_summary")) {
        obj = build_alarm_summary(parent, w);
    }
    else if(iequals(type, "alarm_banner")) {
        obj = build_alarm_banner(parent, w);
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
    parse_animate(obj, wk, w);
    parse_actions(obj, w);
    parse_access(obj, w);
    build_children(obj, w);
    return obj;
}

// ---------------------------------------------------------------- screens

bool Ui::Impl::open_popup(const std::string& name)
{
    Screen* scr = find_screen(name);
    if(!scr) { HML_WARN("popup: unknown screen '%s'", name.c_str()); return false; }
    if(!scr->popup) { HML_WARN("popup: screen '%s' is not a popup (\"popup\": true)", name.c_str()); return false; }
    if(!users().allowed(scr->level)) { users().request_login(scr->level); return false; }   // (the popup is not opened)
    ensure_built(*scr);
    int idx = 0;
    while(screens[static_cast<size_t>(idx)].get() != scr) idx++;
    popups.erase(std::remove(popups.begin(), popups.end(), idx), popups.end());     // open already: to the top
    popups.push_back(idx);
    lv_obj_set_hidden(scr->backdrop, false);
    lv_obj_move_foreground(scr->backdrop);
    const bool was = scr->shown;
    scr->shown = true;
    bring_up_to_date(*scr);
    update_anims();
    if(!was) scripts().on_screen(name, true);
    return true;
}

bool Ui::Impl::close_popup(const std::string& name)
{
    if(popups.empty()) return false;
    int idx = popups.back();
    if(!name.empty()) {
        Screen* scr = find_screen(name);
        auto it = std::find_if(popups.begin(), popups.end(), [&](int i) { return screens[static_cast<size_t>(i)].get() == scr; });
        if(it == popups.end()) return false;
        idx = *it;
    }
    popups.erase(std::remove(popups.begin(), popups.end(), idx), popups.end());
    Screen& scr = *screens[static_cast<size_t>(idx)];
    lv_obj_set_hidden(scr.backdrop, true);
    scr.shown = false;
    update_anims();
    scripts().on_screen(scr.name, false);
    return true;
}

bool Ui::Impl::show_screen(const std::string& name, const std::string& anim, bool replace)
{
    Screen* scr = find_screen(name);
    if(!scr) { HML_WARN("goto: unknown screen '%s'", name.c_str()); return false; }
    if(scr->popup) return open_popup(name);                  // (a goto to a popup opens it)
    int idx = 0;
    while(screens[static_cast<size_t>(idx)].get() != scr) idx++;
    if(idx == current) return true;
    if(!users().allowed(scr->level)) {                       // log in first, then continue to it
        users().request_login(scr->level, name, anim);
        return false;
    }
    ensure_built(*scr);
    close_all_popups();                                      // a popup belongs to the screen it was opened on

    lv_obj_t* prev = lv_screen_active();
    bool prev_is_ours = false;
    for(const auto& s : screens) if(s->obj == prev) prev_is_ours = true;

    std::string left;
    if(current >= 0) {
        if(!replace) {
            if(history.size() == kHistoryLen) history.pop_front();
            history.push_back(current);
        }
        left = screens[static_cast<size_t>(current)]->name;
    }
    current = idx;
    set_shown(idx);                                          // before it is drawn
    bool animated = !anim.empty() && !iequals(anim, "none");
    // The previous screen is deleted only when it is not one of ours (the blank screen from unload_all).
    lv_screen_load_anim(scr->obj, parse_anim(anim), animated ? 250 : 0, 0, !prev_is_ours);
    // screen_hide / screen_show scripts; they may navigate again, so nothing of ours is touched after this
    std::string shown = name;
    if(!left.empty()) scripts().on_screen(left, false);
    scripts().on_screen(shown, true);
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

void Ui::register_action(const std::string& name, std::function<void(const std::string& arg)> fn)
{
    impl_->named_actions[name] = std::move(fn);
}

int Ui::run_actions(const json& actions)
{
    ActionList list;
    if(actions.is_array()) { for(const json& j : actions) if(j.is_object()) if(auto a = impl_->parse_action(j)) list.actions.push_back(std::move(*a)); }
    else if(actions.is_object()) { if(auto a = impl_->parse_action(actions)) list.actions.push_back(std::move(*a)); }
    impl_->run_actions(list);
    return static_cast<int>(list.actions.size());
}

bool Ui::call_action(const std::string& name, const std::string& arg)
{
    auto it = impl_->named_actions.find(name);
    if(it == impl_->named_actions.end()) return false;
    it->second(arg);
    return true;
}

bool Ui::add_screen(const json& screen, const std::string& base_dir)
{
    Impl& m = *impl_;
    if(!screen.is_object()) return false;
    Impl::screen_tag();
    auto name = jstr_opt(screen, "name");
    if(!name) { HML_WARN("screen without \"name\" skipped"); return false; }
    if(m.find_screen(*name)) { HML_WARN("screen '%s' defined twice", name->c_str()); return false; }
    auto scr = std::make_unique<Screen>();
    scr->name = *name;
    scr->level = std::max(0, static_cast<int>(jnum(screen, "level", 0)));
    scr->popup = jbool(screen, "popup", false);
    scr->def = screen;
    scr->source = screen;
    scr->base_dir = base_dir;
    m.screens.push_back(std::move(scr));
    return true;
}

// "${name}" in the texts of a template -> what the screen that uses it gives ("template_args"),
// "${screen}" -> the name of that screen. What nobody gives is empty.
json template_fill(const json& j, const std::map<std::string, std::string>& args)
{
    if(j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        if(s.find("${") == std::string::npos) return j;
        std::string out;
        for(size_t i = 0; i < s.size();) {
            const size_t a = s.find("${", i);
            if(a == std::string::npos) { out += s.substr(i); break; }
            const size_t b = s.find('}', a + 2);
            if(b == std::string::npos) { out += s.substr(i); break; }
            out += s.substr(i, a - i);
            auto it = args.find(s.substr(a + 2, b - a - 2));
            if(it != args.end()) out += it->second;
            i = b + 1;
        }
        return out;
    }
    if(j.is_array()) { json out = json::array(); for(const json& x : j) out.push_back(template_fill(x, args)); return out; }
    if(j.is_object()) { json out = json::object(); for(auto it = j.begin(); it != j.end(); ++it) out[it.key()] = template_fill(it.value(), args); return out; }
    return j;
}

// The screens a screen is built on, the outermost first: its "template", the template of that ...
// A template that is not there, is a popup, or leads back to a screen already taken ends the chain.
std::vector<json> Ui::Impl::template_layers(const Screen& scr, const json& def)
{
    std::vector<json> out;
    std::vector<std::string> seen{ scr.name };
    std::string next = jstr(def, "template", "");
    while(!next.empty()) {
        Screen* t = find_screen(next);
        if(!t) { HML_WARN("screen '%s': unknown template '%s'", scr.name.c_str(), next.c_str()); break; }
        if(std::find(seen.begin(), seen.end(), next) != seen.end()) { HML_WARN("screen '%s': the templates form a ring (at '%s')", scr.name.c_str(), next.c_str()); break; }
        if(t->popup) { HML_WARN("screen '%s': the template '%s' is a popup", scr.name.c_str(), next.c_str()); break; }
        if(seen.size() > 8) { HML_WARN("screen '%s': more than 8 templates on top of each other", scr.name.c_str()); break; }
        seen.push_back(next);
        out.insert(out.begin(), t->source);
        next = jstr(t->source, "template", "");
    }
    return out;
}

void Ui::Impl::ensure_built(Screen& scr)
{
    if(scr.obj) return;
    Screen* outer = building;                                // (a screen is not built from within another, but be safe)
    const std::string outer_dir = base_dir;
    const json def = std::move(scr.def);
    scr.def = json();
    base_dir = scr.base_dir;
    s_image_base_dir = base_dir;
    if(scr.popup) {
        // the backdrop: dims the screen (unless "dim": false) and takes every touch (modal)
        scr.backdrop = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(scr.backdrop);
        lv_obj_set_size(scr.backdrop, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(scr.backdrop, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr.backdrop, jbool(def, "dim", true) ? LV_OPA_50 : LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(scr.backdrop, true);
        lv_obj_set_hidden(scr.backdrop, true);
        scr.obj = lv_obj_create(scr.backdrop);
        lv_obj_set_size(scr.obj, lv_pct(60), lv_pct(60));
        lv_obj_center(scr.obj);
        json box = def;
        box.erase("children");
        box.erase("hidden");
        apply_common(scr.obj, box);                          // w / h / x / y / align / style
    }
    else scr.obj = lv_obj_create(nullptr);
    building = &scr;

    // "template": the look and the widgets of another screen come first (a header, a navigation
    // that is the same everywhere), what this screen has itself goes on top
    if(jhas(def, "template")) {
        std::map<std::string, std::string> args{ { "screen", scr.name } };
        if(const json* a = jget(def, "template_args"); a && a->is_object())
            for(auto it = a->begin(); it != a->end(); ++it) {
                if(it.value().is_string()) args[it.key()] = it.value().get<std::string>();
                else if(it.value().is_number() || it.value().is_boolean()) args[it.key()] = it.value().dump();
            }
        for(const json& layer : template_layers(scr, def)) {
            if(!scr.popup) {
                apply_layout(scr.obj, layer);
                apply_styles(scr.obj, layer);
                if(jhas(layer, "scrollable") && !jbool(layer, "scrollable", true)) lv_obj_set_scrollable(scr.obj, false);
            }
            json filled = json::object();
            if(const json* ch = jget(layer, "children")) filled["children"] = template_fill(*ch, args);
            build_children(scr.obj, filled);
        }
    }
    if(!scr.popup) {
        apply_layout(scr.obj, def);
        apply_styles(scr.obj, def);
    }
    if(jhas(def, "scrollable") && !jbool(def, "scrollable", true)) lv_obj_set_scrollable(scr.obj, false);
    build_children(scr.obj, def);

    building = outer;
    base_dir = outer_dir;
    s_image_base_dir = base_dir;
}

lv_obj_t* Ui::build_screen(const json& screen, const std::string& base_dir)
{
    if(!add_screen(screen, base_dir)) return nullptr;
    Screen& scr = *impl_->screens.back();
    impl_->ensure_built(scr);
    return scr.obj;
}

size_t Ui::build_pending(size_t max)
{
    size_t left = 0;
    for(auto& s : impl_->screens) {
        if(s->obj) continue;
        if(max > 0) { impl_->ensure_built(*s); max--; }
        else left++;
    }
    return left;
}

bool Ui::replace_source(const json& screen)
{
    auto name = jstr_opt(screen, "name");
    if(!name) return false;
    for(auto& s : impl_->screens) if(s->name == *name) { s->source = screen; return true; }
    return false;
}

void Ui::rebuild_all()
{
    std::vector<std::pair<json, std::string>> defs;
    for(auto& s : impl_->screens) defs.emplace_back(s->source, s->base_dir);
    unload_all();
    for(auto& [def, dir] : defs) add_screen(def, dir);
}

void Ui::request_restart(RestartTarget what)
{
    if(impl_->app.request_restart) impl_->app.request_restart(what);
}

void Ui::unload_all()
{
    Impl& m = *impl_;
    tags().unsubscribe_group(SubGroup::UI);
    for(auto& c : m.charts) if(c->timer) lv_timer_delete(c->timer);
    m.charts.clear();
    for(auto& a : m.anims) lv_anim_delete(a.get(), nullptr);   // before their widgets go
    m.anims.clear();

    // LVGL needs an active screen at all times: load a blank one, then delete ours.
    // The blank screen is deleted automatically when the next screen is loaded (see show_screen).
    lv_obj_t* blank = lv_obj_create(nullptr);
    lv_screen_load(blank);
    for(auto& s : m.screens) {
        if(s->backdrop) lv_obj_delete(s->backdrop);          // (a popup's box goes with it)
        else if(s->obj) lv_obj_delete(s->obj);
    }
    m.screens.clear();
    m.popups.clear();
    lv_image_cache_drop(nullptr);                            // (the files may have changed: read them again)
    s_some_screen_shown = false;
    m.bindings.clear();
    m.action_lists.clear();
    m.access_rules.clear();
    m.line_points.clear();
    m.owned_strings.clear();
    m.current = -1;
    m.history.clear();
}

bool Ui::show_screen(const std::string& name, const std::string& anim, bool replace)
{
    return impl_->show_screen(name, anim, replace);
}

void Ui::clear_history() { impl_->history.clear(); }

int Ui::screen_level(const std::string& name) const
{
    const Screen* s = impl_->find_screen(name);
    return s ? s->level : 0;
}

bool Ui::operable(lv_obj_t* obj, bool ask_login)
{
    for(lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        const AccessRule* r = impl_->rule_of(o);
        if(!r || users().allowed(r->level)) continue;
        if(ask_login && r->deny == AccessRule::Deny::Login) users().request_login(r->level);
        return false;
    }
    return true;
}

void Ui::refresh_access()
{
    for(const auto& r : impl_->access_rules) impl_->apply_access(*r);
}

bool Ui::back()
{
    Impl& m = *impl_;
    // screens the current user may not see (any more) are skipped
    while(!m.history.empty()) {
        int top = m.history.back();
        if(top >= 0 && static_cast<size_t>(top) < m.screens.size() && users().allowed(m.screens[static_cast<size_t>(top)]->level)) break;
        m.history.pop_back();
    }
    if(!m.popups.empty()) { m.close_popup(""); return true; }   // "back" in a popup closes it
    if(m.history.empty()) return false;
    int idx = m.history.back();
    m.history.pop_back();
    if(idx < 0 || static_cast<size_t>(idx) >= m.screens.size()) return false;
    std::string left = m.current >= 0 ? m.screens[static_cast<size_t>(m.current)]->name : std::string();
    std::string shown = m.screens[static_cast<size_t>(idx)]->name;
    m.current = idx;
    m.ensure_built(*m.screens[static_cast<size_t>(idx)]);
    m.set_shown(idx);
    lv_screen_load_anim(m.screens[static_cast<size_t>(idx)]->obj, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
    if(!left.empty()) scripts().on_screen(left, false);
    scripts().on_screen(shown, true);
    return true;
}

bool Ui::open_popup(const std::string& name) { return impl_->open_popup(name); }
bool Ui::close_popup(const std::string& name) { return impl_->close_popup(name); }
std::vector<std::string> Ui::open_popups() const
{
    std::vector<std::string> out;
    for(int i : impl_->popups) out.push_back(impl_->screens[static_cast<size_t>(i)]->name);
    return out;
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

std::vector<std::string> Ui::history() const
{
    std::vector<std::string> out;
    for(int idx : impl_->history) {
        if(idx >= 0 && static_cast<size_t>(idx) < impl_->screens.size()) out.push_back(impl_->screens[static_cast<size_t>(idx)]->name);
    }
    return out;
}

void Ui::set_history(const std::vector<std::string>& names)
{
    Impl& m = *impl_;
    m.history.clear();
    for(const std::string& n : names) {
        for(size_t i = 0; i < m.screens.size(); i++) {
            if(m.screens[i]->name == n) {
                if(m.history.size() == kHistoryLen) m.history.pop_front();
                m.history.push_back(static_cast<int>(i));
                break;
            }
        }
    }
}

Ui& ui()
{
    static Ui instance;
    return instance;
}

bool ui_number_pad(lv_obj_t* textarea) { return has_mark(textarea, MARK_NUMBER_PAD); }

} // namespace hml
