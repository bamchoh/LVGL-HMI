// Opening the system settings without a button of the project: a finger held in a corner of
// the screen. A project that has no { "action": "system" } anywhere would otherwise leave the
// device without a way to its own settings (System > Deploy, Keys, Storage, Driver ...).
//
// project.json:
//   "system_access": {
//     "hold": "top_left",      // top_left (default) | top_right | bottom_left | bottom_right | none
//     "hold_s": 3              // how long, 2..10 seconds (default 3)
//   }
//
//  - After one second a small note in the corner counts down: the operator sees that this is
//    not an ordinary touch, and can let go.
//  - When the menu opens, the rest of that touch is dropped: a button under the finger gets no
//    click. (What a widget does while it is held - "repeat", long press - has happened by then:
//    put such a widget somewhere else, or choose another corner.)
//  - It is the same door as { "action": "system" }: with "users" the login for
//    users.system_level comes first.
//  - A touch that wakes the dark screen does not count, nor one on the system screens themselves.
#pragma once
#include <cstdint>
#include "jsonutil.hpp"
#include "lvgl.h"

namespace hml {

class SystemAccess {
public:
    enum class Corner { None, TopLeft, TopRight, BottomLeft, BottomRight };

    // section: "system_access" of project.json (nullptr = the defaults: top left, 3 s)
    void configure(const json* section);
    void clear();                                    // off (no project)
    Corner   corner() const { return corner_; }
    uint32_t hold_ms() const { return hold_ms_; }
    // The side of the square in the corner, for a display of this size.
    static int region(int width, int height);

    // Look at the pointers now (UI thread). start() makes a timer that does it 20 times a second.
    void poll();
    void start();
    void stop();

private:
    void hint(int seconds_left);                     // 0 = take it away

    Corner      corner_ = Corner::None;
    uint32_t    hold_ms_ = 3000;
    lv_timer_t* timer_ = nullptr;
    lv_obj_t*   hint_ = nullptr;
    bool        tracking_ = false;                   // a touch that began in the corner is being timed
    bool        ignore_ = false;                     // this touch does not count (any more)
    bool        was_dark_ = false;                   // the screen was dark when nothing touched it last
    uint32_t    t0_ = 0;
    int         shown_ = 0;
};

SystemAccess& system_access();

} // namespace hml
