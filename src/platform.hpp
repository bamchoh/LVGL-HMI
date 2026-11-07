// Display / input backend abstraction.
//
// Exactly one backend is compiled in, selected by CMake (-DHML_BACKEND=...):
//   sdl    - SDL2 window (Windows / Linux desktop / macOS), F5 = reload, Esc = quit
//   fbdev  - Linux framebuffer /dev/fbN + evdev input (Raspberry Pi console)
//   drm    - Linux DRM/KMS /dev/dri/cardN + evdev input (Raspberry Pi, needs libdrm)
#pragma once
#include <functional>
#include <string>
#include "lvgl.h"
#include "project.hpp"

namespace hml::platform {

struct Callbacks {
    std::function<void()> reload;
    std::function<void()> quit;
};

// Create the display and the input devices. *kb_out receives the keyboard indev when
// the backend has one at init time (may stay nullptr). Returns nullptr on failure.
lv_display_t* init(const ProjectInfo& info, Callbacks cb, lv_indev_t** kb_out);

// Called once per main-loop iteration (signal polling etc.).
void poll();

// Save the current frame as a BMP file (test automation).
void screenshot(lv_display_t* disp, const std::string& path);

void deinit();

const char* name();
const char* hint();   // human readable: how to reload / quit on this backend

} // namespace hml::platform
