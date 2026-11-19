// Alarm widgets on the screens (see alarm.hpp for the alarms themselves).
//
//   { "type": "alarm_list", "w": "100%", "h": 300,
//     "mode": "current",          // "current" (active / not acknowledged) | "history" (events, newest first)
//     "rows": 50,                 // current: most rows shown; history: rows per page (default 20)
//     "row_height": 40,           // >= 40 px is easy to touch
//     "columns": ["time", "severity", "message", "state"],   // any of them, in this order
//     "ack_on_tap": true,         // current: a tap on a row acknowledges that alarm
//     "severity": "",             // only this severity ("" = all; history: also chosen on the widget)
//     "days": 0,                  // history: only the last N days (0 = all)
//     "labels": { "unacked": "UNACK", "active": "ACTIVE", "cleared": "CLEARED", "empty": "No alarms", ... } }
//
//   { "type": "alarm_banner", "w": "100%", "h": 36, "blink": true, "ack_on_tap": false,
//     "labels": { "idle": "No alarms" } }
//     The newest unacknowledged alarm (else the newest active one) with the number of others;
//     it blinks while something is not acknowledged. Its own on_click (e.g. goto the alarm
//     screen) works as on any widget.
//
// Acknowledging needs the "ack_level" of the alarms section; a user below it gets the login
// screen. Every acknowledge is recorded in the alarm log and in the operation log.
// Actions: { "action": "call", "fn": "alarm.ack", "arg": "<name>" } ("" = the newest one not
// acknowledged), { "action": "call", "fn": "alarm.ack_all" }.
#pragma once
#include <string>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_alarm_list(lv_obj_t* parent, const json& w);
lv_obj_t* build_alarm_banner(lv_obj_t* parent, const json& w);

// Acknowledge as the operator: checks the level (opens the login screen if it is too low) and
// records it. name "" = every alarm that is not acknowledged. Returns how many were acknowledged.
int ack_alarms_as_operator(const std::string& name, bool all = false);

// Register the alarm.ack / alarm.ack_all actions.
void register_alarm_actions();

} // namespace hml
