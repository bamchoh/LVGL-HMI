#include "system_access.hpp"
#include <algorithm>
#include <string>
#include "operation_log.hpp"
#include "screen_saver.hpp"
#include "system_settings.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {

void SystemAccess::configure(const json* section)
{
    corner_ = Corner::TopLeft;
    hold_ms_ = 3000;
    tracking_ = false;
    ignore_ = false;
    hint(0);
    if(!section || !section->is_object()) return;
    const std::string where = jstr(*section, "hold", "top_left");
    if(iequals(where, "top_left")) corner_ = Corner::TopLeft;
    else if(iequals(where, "top_right")) corner_ = Corner::TopRight;
    else if(iequals(where, "bottom_left")) corner_ = Corner::BottomLeft;
    else if(iequals(where, "bottom_right")) corner_ = Corner::BottomRight;
    else if(iequals(where, "none")) corner_ = Corner::None;
    else HML_WARN("system_access.hold must be top_left, top_right, bottom_left, bottom_right or none (got '%s'); using top_left", where.c_str());
    const double s = jnum(*section, "hold_s", 3);
    if(s < 2 || s > 10) HML_WARN("system_access.hold_s must be 2..10 (got %g)", s);
    hold_ms_ = static_cast<uint32_t>(std::clamp(s, 2.0, 10.0) * 1000.0);
}

void SystemAccess::clear()
{
    corner_ = Corner::None;
    tracking_ = false;
    ignore_ = false;
    hint(0);
}

int SystemAccess::region(int width, int height)
{
    return std::max(48, std::min(width, height) / 8);
}

void SystemAccess::start()
{
    if(timer_ || !lv_is_initialized()) return;
    timer_ = lv_timer_create([](lv_timer_t*) { system_access().poll(); }, 50, nullptr);
}

void SystemAccess::stop()
{
    if(timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    hint(0);
}

void SystemAccess::hint(int seconds_left)
{
    if(seconds_left <= 0) {
        if(hint_) lv_obj_delete(hint_);
        hint_ = nullptr;
        shown_ = 0;
        return;
    }
    if(seconds_left == shown_ && hint_) return;
    if(!hint_) {
        hint_ = lv_label_create(lv_layer_sys());
        lv_obj_set_clickable(hint_, false);
        lv_obj_set_style_bg_color(hint_, lv_color_hex(0x1565c0), 0);
        lv_obj_set_style_bg_opa(hint_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(hint_, lv_color_white(), 0);
        lv_obj_set_style_pad_hor(hint_, 10, 0);
        lv_obj_set_style_pad_ver(hint_, 6, 0);
        lv_obj_set_style_radius(hint_, 6, 0);
        const lv_align_t at = corner_ == Corner::TopRight ? LV_ALIGN_TOP_RIGHT : corner_ == Corner::BottomLeft ? LV_ALIGN_BOTTOM_LEFT
                            : corner_ == Corner::BottomRight ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_TOP_LEFT;
        lv_obj_align(hint_, at, 0, 0);
    }
    shown_ = seconds_left;
    lv_label_set_text_fmt(hint_, "System  %d", seconds_left);
    lv_obj_move_foreground(hint_);
}

void SystemAccess::poll()
{
    if(corner_ == Corner::None || !lv_is_initialized()) return;
    // the pointer that is down (a finger, a mouse, a VNC viewer)
    lv_indev_t* down = nullptr;
    for(lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i))
        if(lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED) { down = i; break; }
    if(!down) {
        tracking_ = false;
        ignore_ = false;
        was_dark_ = screen_saver().dark();           // (the touch that wakes the screen has lit it before it is seen here)
        hint(0);
        return;
    }
    if(ignore_) return;

    lv_display_t* disp = lv_indev_get_display(down);
    if(!disp) disp = lv_display_get_default();
    const int w = lv_display_get_horizontal_resolution(disp), h = lv_display_get_vertical_resolution(disp), r = region(w, h);
    lv_point_t p;
    lv_indev_get_point(down, &p);
    const bool left = corner_ == Corner::TopLeft || corner_ == Corner::BottomLeft, top = corner_ == Corner::TopLeft || corner_ == Corner::TopRight;
    const bool inside = (left ? p.x < r : p.x >= w - r) && (top ? p.y < r : p.y >= h - r) && p.x >= 0 && p.y >= 0 && p.x < w && p.y < h;

    if(!tracking_) {
        // a new touch: it counts only if it begins in the corner, on a screen of the project, with the screen lit
        const std::string cur = ui().current_screen().value_or("");
        if(!inside || was_dark_ || screen_saver().dark() || cur.rfind("__system", 0) == 0) { ignore_ = true; return; }
        tracking_ = true;
        t0_ = lv_tick_get();
        return;
    }
    if(!inside) {                                    // slid out of the corner: not this time
        tracking_ = false;
        ignore_ = true;
        hint(0);
        return;
    }
    const uint32_t held = lv_tick_elaps(t0_);
    if(held < hold_ms_) {
        if(held >= 1000) hint(static_cast<int>((hold_ms_ - held + 999) / 1000));
        return;
    }
    tracking_ = false;
    ignore_ = true;
    hint(0);
    lv_indev_wait_release(down);                     // what is under the finger gets no click
    HML_INFO("system: opened by holding the corner of the screen");
    ui().show_screen(SystemSettings::menu_screen(), "none");
}

SystemAccess& system_access()
{
    static SystemAccess s;
    return s;
}

} // namespace hml
