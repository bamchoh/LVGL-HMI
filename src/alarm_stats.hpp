// Alarm statistics: for a span of time, how often each alarm was raised and for how long it was
// active in all ("what happened most", "what stopped us longest"). Computed from the alarm log
// (alarm.hpp); nothing is stored.
//
// No LVGL here: alarm_stats() is a pure function of a list of events, so that it can be asked
// from a worker thread and tested without a screen or a database.
//
// How the time of an alarm is counted:
//  - from "raised" to the next "cleared" of the same alarm, the part that lies in the span
//  - an alarm that is active now counts up to now
//  - a "raised" that follows a "raised" without a "cleared" between them (the log was cut, the
//    power went): the first one is taken to have ended when the log last said anything before
//    the second one - never longer than that
//  - a "raised" without any "cleared" for an alarm that is not active now: the same
//  - a "cleared" before its "raised" (the clock was set back): no time, never a negative one
//  - the time the runtime was switched off with an alarm active counts as active (the alarm is
//    cleared when the runtime starts again and finds the condition gone)
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "alarm.hpp"

namespace hml {

struct AlarmStat {
    std::string name, message, severity;    // message and severity as of the newest event
    int     count = 0;                      // raises in the span
    int64_t active_ms = 0;                  // time active in the span
    int64_t longest_ms = 0;                 // the longest single stretch (its part in the span)
    int64_t last_raised_ms = 0;             // the newest raise in the span (0 = none: it was active from before)
    bool    active_now = false;
};

enum class AlarmSort { Count, Time };

// events: oldest first, every alarm, at least up to to_ms (events before from_ms are needed to
// know what was active at from_ms). active_now: the alarms that are active at now_ms.
// The span is [from_ms, to_ms); from_ms <= 0 = from the first event; to_ms <= 0 = up to now_ms.
// Alarms with neither a raise nor active time in the span are left out.
std::vector<AlarmStat> alarm_stats(const std::vector<AlarmEvent>& events, int64_t from_ms, int64_t to_ms, int64_t now_ms,
                                   const std::set<std::string>& active_now, AlarmSort sort = AlarmSort::Count,
                                   const std::string& severity = "");

// The same from the alarm log of the running project, for the last `days` days (0 = everything).
// Thread safe (the log locks its database; the set of active alarms is passed in by the caller,
// who is on the UI thread).
std::vector<AlarmStat> alarm_stats_recent(int days, const std::set<std::string>& active_now, AlarmSort sort = AlarmSort::Count,
                                          const std::string& severity = "", int64_t now_ms = 0);

// "1:02:03" (hours do not stop at 24), "0:00:07"
std::string alarm_duration_text(int64_t ms);

// CSV of the statistics: name,message,severity,count,active_s,longest_s,active_now. Empty path =
// next to the alarm database ("alarm-summary-YYYYMMDD-HHMMSS.csv"). Returns the rows written,
// -1 on failure. Texts that a spreadsheet would run as a formula are made harmless.
int64_t alarm_stats_export(const std::vector<AlarmStat>& stats, std::string path = "", std::string* written = nullptr);
// A text cell for a CSV that people open in a spreadsheet: one that starts with = + - @ (or a
// tab / carriage return) gets a ' in front, so that it is shown and not calculated.
std::string csv_safe_text(const std::string& s);

} // namespace hml
