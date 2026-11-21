// Steadying a touch panel whose readings wander (a resistive panel under a finger: a few pixels
// from sample to sample, and a wild first sample as the finger lands). Without it a press
// trembles: a roller wiggles, a button under a held finger sees a drag, a slider shivers.
//
//   "display": { "touch": { "jitter": 12, "smooth": 3, "settle": 1, "release": 1, "scroll_limit": 16, "log": false } }
//
//   settle        the first N samples of a touch are left out (0 .. 5; the finger is still landing)
//   release       the last N samples of a touch are left out (0 .. 3; the finger is already lifting).
//                 Every reading is passed on N samples late for that (about 16 ms each on an ADS7846)
//   lift_ms       a lift shorter than this is not a lift (0 .. 300; 0 = off). A finger that is dragged
//                 over a resistive panel loses contact for a moment now and then: the panel says
//                 "released, pressed" in the middle of the drag, and the first half becomes a tap on
//                 whatever was under it. With this the touch goes on as one. A real release is
//                 passed on this much later
//   smooth        the average of the last N samples is used (1 .. 8; 1 = as they come)
//   jitter        the point that is reported moves only when the finger is more than N pixels away
//                 from it, and then follows at that distance (0 = off). A press stays a press; a
//                 drag lags by at most N pixels
//   scroll_limit  pixels a finger has to travel before a list scrolls (LVGL's own: 10)
//   log           one line per touch in the system log: samples, how far the readings wandered
//                 (all of them, and without the first and last two: the part a finger rests), how
//                 far the reported point moved. For choosing the numbers above
//
// All off unless set: a capacitive panel needs none of it. No LVGL in here (tested on the PC).
#pragma once
#include <cstdint>
#include <string>
#include "jsonutil.hpp"

namespace hml {

struct TouchFilterConfig {
    int  settle = 0, smooth = 1, jitter = 0, release = 0, lift_ms = 0;
    int  scroll_limit = 0;              // 0 = leave LVGL's
    bool log = false;
    bool active() const { return settle > 0 || smooth > 1 || jitter > 0 || release > 0 || lift_ms > 0 || log; }
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
        int core_dx = 0, core_dy = 0;   // ... without the first two and the last two readings
        int out_dx = 0, out_dy = 0;     // how far the reported point moved
        int lifts = 0;                  // short lifts in the middle of the touch (bridged with lift_ms, else each ended a touch)
        uint32_t longest_lift_ms = 0;
        uint32_t since_last_ms = 0;     // from the end of the touch before to the start of this one
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
    int    late_x_[4] = {}, late_y_[4] = {}, late_n_ = 0;      // readings held back ("release")
    int    tail_x_[2] = {}, tail_y_[2] = {};                   // the last two readings (for the numbers of the log)
    bool   core_any_ = false;
    bool   lifting_ = false;            // the panel says "released"; we wait whether it is one
    uint32_t lift_at_ = 0, ended_at_ = 0, started_gap_ = 0;
    int    lifts_ = 0, skip_ = 0;
    uint32_t longest_lift_ = 0;
    bool   ever_ = false;
    int    cmin_x_ = 0, cmax_x_ = 0, cmin_y_ = 0, cmax_y_ = 0;
    uint32_t t0_ = 0, t_last_ = 0;
    int    min_x_ = 0, max_x_ = 0, min_y_ = 0, max_y_ = 0, omin_x_ = 0, omax_x_ = 0, omin_y_ = 0, omax_y_ = 0;
    Stats  last_;
};

} // namespace hml
