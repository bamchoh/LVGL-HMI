#include "splash.hpp"
#include <algorithm>
#include <string>
#include "util.hpp"

namespace hml {

lv_obj_t* build_splash(lv_display_t* disp, const ProjectInfo& info)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, info.splash_bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    const int32_t w = lv_display_get_horizontal_resolution(disp), h = lv_display_get_vertical_resolution(disp);

    bool shown = false;
    if(!info.splash.empty()) {
        std::string src = "A:" + info.splash;
        lv_image_header_t hdr{};
        if(lv_image_decoder_get_info(src.c_str(), &hdr) == LV_RESULT_OK && hdr.w > 0 && hdr.h > 0) {
            lv_obj_t* img = lv_image_create(scr);
            lv_image_set_src(img, src.c_str());
            // shrink a large picture to fit; a small logo keeps its size (and stays sharp)
            double z = std::min({ 1.0, double(w) / hdr.w, double(h) / hdr.h });
            if(z < 1.0) lv_image_set_scale(img, static_cast<uint32_t>(z * LV_SCALE_NONE));
            lv_obj_center(img);
            shown = true;
        }
        else HML_WARN("splash: cannot read %s, showing the title instead", info.splash.c_str());
    }
    if(!shown) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, info.title.c_str());
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(lbl, w - 32);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lbl, w >= 640 ? &lv_font_montserrat_40 : &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl, lv_color_luminance(info.splash_bg) > 128 ? lv_color_black() : lv_color_white(), 0);
        lv_obj_center(lbl);
    }
    lv_screen_load(scr);
    return scr;
}

} // namespace hml
