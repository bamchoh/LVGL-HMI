// Screen saver (画面消灯): the screen goes dark after a while without a touch.
//
//   "screen_saver": { "after_s": 300, "wake_on_alarm": true, "logout": false }
//
// after_s 0 (the default) = never. When it goes dark, a black layer is put over everything
// (screens, popups, dialogs) and the backlight is switched off where the system allows it
// (Linux: /sys/class/backlight/*, else the framebuffer is blanked; neither on the PC).
//
// - The touch that wakes the screen is not passed on: no button under it is pressed.
// - A new alarm (raised, or not acknowledged) wakes it (wake_on_alarm, default true).
// - "logout": true logs the user out when it goes dark.
// - Tag sys.screen.off: 1 while dark; writing 1 / 0 switches it now (a "screen off" button).
// - The time can be changed on the device (System > Display): kept in project.local.json
//   ("screen_saver": { "after_s": N }) like the driver settings.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "jsonutil.hpp"
#include "lvgl.h"

namespace hml {

class ScreenSaver {
public:
    // section: project.json "screen_saver"; local: the same from project.local.json (or null)
    void configure(const json* section, const json* local, const std::string& store = "");
    void clear();

    int  after_s() const { return after_s_; }
    int  project_after_s() const { return base_after_s_; }
    // Change the time on the device; written to `store` (project.local.json). "" = fine.
    std::string set_after_s(int s);

    bool dark() const { return dark_; }
    void blank();
    void wake();

    // Call regularly (main loop): goes dark after after_s without input; wakes on alarms.
    void poll();
    // (tests) the time since the last input, in ms
    void set_clock(std::function<uint32_t()> inactive_ms) { inactive_fn_ = std::move(inactive_ms); }

private:
    void backlight(bool on);
    void update_tag();

    int  after_s_ = 0, base_after_s_ = 0;
    bool wake_on_alarm_ = true, logout_ = false;
    bool dark_ = false, updating_ = false;
    std::string store_;
    lv_obj_t* layer_ = nullptr;
    size_t seen_active_ = 0, seen_unacked_ = 0;
    std::function<uint32_t()> inactive_fn_;
    bool backlight_warned_ = false;
};

ScreenSaver& screen_saver();

class SettingsPage;
std::unique_ptr<SettingsPage> make_display_page();          // System > Display

} // namespace hml
