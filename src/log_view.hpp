// A log on a screen: the system log, the operation log or a group of the data log as a table,
// newest first, with pages (log_query.hpp does the asking).
//
//   { "type": "log_view", "w": "100%", "h": 300,
//     "source": "system",          // "system" | "operation" | "data"
//     "group": "process",          // data: the group of the data log
//     "columns": ["time", "level", "message"],   // any of the source's, in this order (default: the usual ones)
//                                  //   system: time level source message count
//                                  //   operation: time user screen kind target old new
//                                  //   data: time and the tags of the group
//     "rows": 10,                  // rows of a page
//     "days": 0,                   // only the last N days (0 = everything)
//     "min_level": "info",         // system: "info" | "warn" | "error" (also chosen on the widget)
//     "filter": true,              // system: the dropdown for the level is shown
//     "filter_tag": "search",      // a string tag: only rows that contain its text (system: the message,
//                                  // operation: any column). Put a text_display with "input" on the screen for it
//     "refresh_ms": 2000,          // the first page follows the log (0 = read when the screen is shown only)
//     "widths": { "time": 150 },   // pixels of a column (the others share the rest)
//     "labels": { "empty": "No entries", "time": "Time", "message": "Message", ... } }
//
// The log is read by a worker thread, never by the UI thread: a large log does not stop the
// screen. The widget only reads; nothing in a log can be changed or removed from a screen. Who
// may see it is decided by "level" of the widget or of its screen, as for any widget.
#pragma once
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_log_view(lv_obj_t* parent, const json& w);
void log_view_wait_idle();          // (tests) until the worker has nothing to do

} // namespace hml
