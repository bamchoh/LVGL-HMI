// "bar_chart": a value of the data log in time slices, one bar each (production per hour, the
// energy of each shift ...).
//
//   { "type": "bar_chart", "w": 600, "h": 260,
//     "group": "production",        // a data log group that records the tag
//     "tag": "production_count",
//     "mode": "increase",           // increase: what the tag grew by in each slice (a counter);
//                                   // value: its last value in each slice
//     "bucket_s": 3600,             // one bar = this many seconds (60 .. 86400), on the clock (whole hours)
//     "bars": 12,                   // how many (1 .. 48), the newest on the right
//     "start": "now",               // now: the last bars up to now; day: from midnight on (24 bars of an hour = today)
//     "max": 0,                     // the top of the scale (0 / omitted: the largest bar)
//     "target": 500,                // a line at this value (or { "tag": "hourly_target" })
//     "color": "#26c6da", "target_color": "#f5b942",
//     "decimals": 0, "show_values": true,
//     "label": "%H",                // the time under each bar (strftime); default by bucket_s
//     "refresh_ms": 10000 }
//
// A counter that is set back (a new day, a new shift) is counted from 0 again: the slice then
// counts what it reached after the reset. Bars of the future are empty.
#pragma once
#include <vector>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_bar_chart(lv_obj_t* parent, const json& w);
// (tests) read the data log again now; the values of the bars (NaN: no data / the future)
void bar_chart_refresh(lv_obj_t* obj);
std::vector<double> bar_chart_values(lv_obj_t* obj);
// (tests) the time the chart takes as "now" (0 = the clock)
void bar_chart_set_now_for_tests(int64_t ms);

} // namespace hml
