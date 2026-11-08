// Project file: tags, drivers, display settings and the list of screens.
#pragma once
#include <string>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

struct ProjectInfo {
    std::string path, dir, title;
    int    width = 800, height = 480;
    double zoom = 1.0;
    int    rotation = 0;            // 0, 90, 180, 270 (degrees)
    int    dpi = 130;               // theme scale (padding, radius, default sizes); 130 = LVGL default
    // Linux backends (fbdev / drm): device paths, empty = default
    std::string fbdev;              // "/dev/fb0"
    std::string drm;                // "/dev/dri/card0"
    std::string input;              // "/dev/input/eventN"; empty = auto-discover all
    std::string console;            // VT to put into graphics mode: "/dev/tty1" (default), "none" = leave alone
    bool touch_swap_axes = false;
    bool touch_has_calib = false;
    int  touch_calib[4] = { 0, 0, 0, 0 };   // min_x, min_y, max_x, max_y
    bool dark = true;
    lv_color_t  primary{}, secondary{};
    std::string start_screen;
};

class Project {
public:
    // Parse project.json and create the tags. LVGL does not need to be initialised yet.
    bool load(const std::string& path);
    const ProjectInfo& info() const { return info_; }

    // After LVGL and the display exist: apply the theme, start the drivers.
    void apply_theme(lv_display_t* disp);
    int  start_drivers();
    // Stop every driver, re-read project.json + project.local.json and start them again
    // (system settings "Apply"). Returns the number of drivers running.
    int  restart_drivers();

    // Driver configuration (overlay applied) by its overlay key, see driver_keys(). nullptr if unknown.
    const json* driver_config(const std::string& key) const;
    const json* drivers_config() const { return jget(root_, "drivers"); }
    std::string local_overlay_path() const { return info_.dir + "/project.local.json"; }

    // Build (or rebuild) every screen from its JSON file and show the start screen
    // (or the previously shown one on a reload). Returns the number of screens built.
    int  build_screens();

    void unload();

private:
    std::string resolve_path(const std::string& rel) const;
    void apply_local_overlay();        // merge project.local.json into root_ (drivers only, for now)

    ProjectInfo info_;
    json        root_;
};

Project& project();

} // namespace hml
