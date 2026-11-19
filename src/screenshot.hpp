// Backend independent screenshot: render the active screen (and what is on the layers above it)
// with lv_snapshot and write it as PNG (file name ending in .png) or as a 24-bit BMP.
#pragma once
#include <string>
#include "lvgl.h"

namespace hml {
bool save_screenshot(lv_display_t* disp, const std::string& path);
inline bool save_screenshot_bmp(lv_display_t* disp, const std::string& path) { return save_screenshot(disp, path); }
}
