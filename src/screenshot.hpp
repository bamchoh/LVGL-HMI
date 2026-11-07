// Backend independent screenshot: render the active screen with lv_snapshot and write a 24-bit BMP.
#pragma once
#include <string>
#include "lvgl.h"

namespace hml {
bool save_screenshot_bmp(lv_display_t* disp, const std::string& path);
}
