// Alarm statistics on a screen: which alarm was raised how often and was active for how long,
// in a span of time (alarm_stats.hpp does the counting).
//
//   { "type": "alarm_summary", "w": "100%", "h": 300,
//     "days": 7,                  // the span: the last N days (0 = everything). Also chosen on the widget: 1 / 7 / 30 / all
//     "sort": "count",            // "count" (most often first) | "time" (longest first). Also chosen on the widget
//     "rows": 10,                 // the first N alarms are shown
//     "severity": "",             // only this severity ("" = all)
//     "bars": true,               // a bar beside the numbers (the share of the largest one)
//     "refresh_ms": 5000,         // counted again this often while the screen is shown
//     "labels": { "empty": "No alarms", "alarm": "Alarm", "count": "Count", "time": "Time",
//                 "day": "24 h", "week": "7 d", "month": "30 d", "all": "All", "by_count": "Count", "by_time": "Time" } }
//
// A row: the message of the alarm (with a mark while it is active now), how often it was raised,
// the time it was active as h:mm:ss, a bar. Read only. The counting runs on a worker thread.
//
// Action: { "action": "call", "fn": "alarm.export_summary", "arg": "7" } writes the statistics
// of the last 7 days ("" or "0" = everything) as CSV next to the alarm database.
#pragma once
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_alarm_summary(lv_obj_t* parent, const json& w);
void register_alarm_summary_actions();
void alarm_summary_wait_idle();          // (tests)

} // namespace hml
