// Project file: tags, drivers, display settings and the list of screens.
#pragma once
#include <string>
#include "touch_filter.hpp"
#include "lvgl.h"
#include "jsonutil.hpp"
#include "system_log.hpp"

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
    // boot splash (hml_runtime --splash): image path resolved against the project dir, empty =
    // show the title; background colour (default black)
    std::string splash;
    lv_color_t  splash_bg{};
    bool touch_swap_axes = false;
    bool touch_has_calib = false;
    int  touch_calib[4] = { 0, 0, 0, 0 };   // min_x, min_y, max_x, max_y
    TouchFilterConfig touch_filter;         // steadying a panel whose readings wander (touch_filter.hpp)
    bool dark = true;
    lv_color_t  primary{}, secondary{};
    std::string start_screen;
    SystemLogConfig system_log;              // "system_log": system log database (opened by main, not by load())
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
    // Data log, alarms and operation log ("data_log" / "alarms" / "operation_log"): open the
    // databases and start recording; also registers their call actions (data_log.export ...).
    // With read_only nothing is recorded (export from a second process). Needs the tags, and
    // LVGL unless read_only. stop_logs() must run before the tags are cleared.
    // log_dir: where the databases go instead of the project folder (hml_runtime --check: a
    // temporary folder, so that alarms and trends work and the project is not written to).
    void start_logs(bool read_only = false, const std::string& log_dir = "");
    void stop_logs();

    // Driver configuration (overlay applied) by its overlay key, see driver_keys(). nullptr if unknown.
    const json* driver_config(const std::string& key) const;
    // The same driver as project.json has it (before project.local.json is applied).
    const json* driver_base_config(const std::string& key) const;
    const json* drivers_config() const { return jget(root_, "drivers"); }
    const json* section(const char* name) const { return jget(root_, name); }   // any top-level object of project.json
    std::string local_overlay_path() const { return info_.dir + "/project.local.json"; }

    // Build (or rebuild) every screen from its JSON file and show the start screen
    // (or the previously shown one on a reload). Returns the number of screens built.
    int  build_screens();
    void rebuild_screens();         // after a language switch (texts.hpp)

    void unload();

private:
    std::string resolve_path(const std::string& rel) const;
    void apply_local_overlay();        // merge project.local.json into root_ (drivers only, for now)

    ProjectInfo info_;
    json        root_;
    json        base_drivers_;         // "drivers" of project.json before the overlay
    json        local_users_;          // "users" of project.local.json (users edited on the device)
    json        local_screen_saver_;   // "screen_saver" of project.local.json (time changed on the device)
};

Project& project();

} // namespace hml
