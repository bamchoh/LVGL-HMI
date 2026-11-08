// Headless LVGL for the unit tests.
//
// LVGL is initialised once per process with a small in-memory display (no SDL / framebuffer).
// The tick is driven manually so that timers (sim driver, charts, screen animations) run
// deterministically: advance(ms) moves the clock and pumps lv_timer_handler + tags().dispatch().
#pragma once
#include <cstdint>
#include <string>
#include "lvgl.h"
#include "driver.hpp"
#include "tag.hpp"

namespace hmltest {

struct Lvgl {
    static Lvgl& get();                 // lv_init + display on first use

    lv_display_t* disp = nullptr;
    uint32_t      tick = 0;             // current LVGL time in ms
    int           flushes = 0;          // flush callbacks seen (rendering happened)

    // Move the LVGL clock forward in `step` ms increments, running timers and tag dispatch after each.
    void advance(uint32_t ms, uint32_t step = 10);
    void render();                      // lv_refr_now
};

// Per-test environment: a clean tag store, a freshly initialised Ui and a driver registry with the
// built-in drivers. Everything is torn down in the destructor (drivers stopped, screens deleted,
// tags removed) so tests do not leak state into each other.
struct Runtime {
    Runtime();
    ~Runtime();

    hml::Tag* tag(const std::string& name, hml::TagType type, double init = 0);
    hml::Tag* tag(const std::string& name, hml::TagType type, double init, double min, double max);
    hml::Tag* tag(const std::string& name, const std::string& init);

    void advance(uint32_t ms, uint32_t step = 10) { Lvgl::get().advance(ms, step); }
    void pump() { hml::tags().dispatch(false); }

    hml::DriverRegistry drivers;        // builtins registered
    int reload_requests = 0;            // AppCallbacks hits (actions "reload" / "exit")
    int exit_requests = 0;
};

} // namespace hmltest
