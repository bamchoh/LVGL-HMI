// Steadying a touch panel whose readings wander (a resistive panel under a finger: a few pixels
// from sample to sample, and a wild first sample as the finger lands). Without it a press
// trembles: a roller wiggles, a button under a held finger sees a drag, a slider shivers.
//
//   "display": { "touch": { "jitter": 8, "smooth": 3, "settle": 1, "scroll_limit": 16, "log": false } }
//
//   settle        the first N samples of a touch are left out (0 .. 5; the finger is still landing)
//   smooth        the average of the last N samples is used (1 .. 8; 1 = as they come)
//   jitter        the point that is reported moves only when the finger is more than N pixels away
//                 from it, and then follows at that distance (0 = off). A press stays a press; a
//                 drag lags by at most N pixels
//   scroll_limit  pixels a finger has to travel before a list scrolls (LVGL's own: 10)
//   log           one line per touch in the system log: samples, how far the readings wandered,
//                 how far the reported point moved. For choosing the numbers above
//
// All off unless set: a capacitive panel needs none of it. No LVGL in here (tested on the PC).
#pragma once
#include <cstdint>
#include <string>
#include "jsonutil.hpp"

namespace hml {

struct TouchFilterConfig {
    int  settle = 0, smooth = 1, jitter = 0;
    int  scroll_limit = 0;              // 0 = leave LVGL's
    bool log = false;
    bool active() const { return settle > 0 || smooth > 1 || jitter > 0 || log; }
};
TouchFilterConfig parse_touch_filter(const json* touch_section);

class TouchFilter {
public:
    explicit TouchFilter(const TouchFilterConfig& cfg = {}) : cfg_(cfg) {}

    struct Point { int x = 0, y = 0; bool pressed = false; };
    // One reading of the panel in, what the user interface is told out.
    Point feed(int x, int y, bool pressed, uint32_t now_ms = 0);

    // The touch that just ended (valid after a reading with pressed = false that followed a touch).
    struct Stats {
        int samples = 0;
        uint32_t ms = 0;
        int raw_dx = 0, raw_dy = 0;     // how far the readings wandered (max - min), pixels
        int out_dx = 0, out_dy = 0;     // how far the reported point moved
    };
    const Stats& last() const { return last_; }
    bool finished() const { return finished_; }      // true for the one reading that ended a touch
    std::string describe() const;                    // the line for the log

private:
    TouchFilterConfig cfg_;
    bool   down_ = false, reported_ = false, finished_ = false;
    int    count_ = 0;
    int    ring_x_[8] = {}, ring_y_[8] = {};
    int    ring_n_ = 0, ring_at_ = 0;
    int    ax_ = 0, ay_ = 0;            // the point that is reported
    uint32_t t0_ = 0, t_last_ = 0;
    int    min_x_ = 0, max_x_ = 0, min_y_ = 0, max_y_ = 0, omin_x_ = 0, omax_x_ = 0, omin_y_ = 0, omax_y_ = 0;
    Stats  last_;
};

} // namespace hml
