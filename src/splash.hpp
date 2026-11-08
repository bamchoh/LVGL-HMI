// Boot splash: one frame drawn by `hml_runtime --splash` early during boot, before the HMI starts.
#pragma once
#include "lvgl.h"
#include "project.hpp"

namespace hml {

// Build and load the splash screen on `disp`: the display.splash image centred (scaled down to
// fit, never enlarged) on display.splash_bg, or the project title when no image is set or it
// cannot be read. Returns the screen object.
lv_obj_t* build_splash(lv_display_t* disp, const ProjectInfo& info);

} // namespace hml
