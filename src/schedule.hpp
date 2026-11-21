// Schedules: actions that run by the clock ("on weekdays at 8:00 switch the line on").
//
//   "schedules": [
//     { "name": "start",  "at": "08:00", "days": ["mon", "tue", "wed", "thu", "fri"],
//       "do": { "action": "set", "tag": "run", "value": true } },
//     { "name": "lights", "at": "18:30", "do": [ { "action": "set", "tag": "light", "value": false } ],
//       "enabled": true, "catch_up": false },
//     { "name": "service", "at": "2026-12-01 09:00", "do": { "action": "script", "name": "service_due" } }
//   ]
//
//   at        "HH:MM" (every day that "days" names) or "YYYY-MM-DD HH:MM" (once). Local time.
//   days      a list of sun mon tue wed thu fri sat, or "daily" (the default), "weekdays", "weekend"
//   do        what the buttons of a screen can do: one action or a list (set, toggle, inc, script,
//             call, goto ...). Nothing else: a schedule cannot run a program or touch a file
//   enabled   false: kept, not run
//   catch_up  true: when the runtime was not running at the time (switched off, the clock was
//             set ahead), run it once when it runs again, if that is within a day. Default: a
//             time that passed while nothing ran is skipped
//
// On the device the time, the days and "enabled" of a schedule can be changed (System >
// Schedule); what it does cannot. The changes are kept in project.local.json.
//
// The clock: looked at once a second. Every scheduled time between the last look and now runs
// once, so a second that is skipped (a busy moment, a small correction of the clock) loses
// nothing. A clock set back does not run a time again that already ran. A jump ahead of more
// than two minutes is taken as "was not running" (see catch_up).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

struct ScheduleItem {
    std::string name;
    int      hour = 0, minute = 0;
    unsigned days = 0x7F;                 // bit 0 = Sunday ... bit 6 = Saturday
    bool     once = false;                // at a date
    int      year = 0, month = 0, day = 0;
    bool     enabled = true, catch_up = false;
    json     actions;                     // "do"
    int64_t  fired = 0;                   // the scheduled time that ran last (Unix seconds)
};

// Read one entry. False (and *err) when it cannot be used.
bool schedule_parse(const json& j, ScheduleItem& out, std::string* err = nullptr);
// "at" and "days" as text and back (the page of the device, the local file).
bool schedule_parse_at(const std::string& at, ScheduleItem& io);
bool schedule_parse_days(const json& days, unsigned& mask);
std::string schedule_at_text(const ScheduleItem& s);          // "08:00" / "2026-12-01 09:00"
std::string schedule_days_text(unsigned mask);                // "daily" / "weekdays" / "mon wed fri"
json schedule_days_json(unsigned mask);

// The newest scheduled time t of the item with after_s < t <= upto_s (Unix seconds, local time
// of the machine), if there is one. Whether the item is enabled is not looked at.
std::optional<int64_t> schedule_latest_due(const ScheduleItem& s, int64_t after_s, int64_t upto_s);
// The next scheduled time after after_s (within the next 8 days; a date that has passed: none).
std::optional<int64_t> schedule_next_due(const ScheduleItem& s, int64_t after_s);

class Scheduler {
public:
    Scheduler();
    ~Scheduler();

    // section: "schedules" of project.json; local: "schedules" of project.local.json (changes made
    // on the device, the times catch_up schedules last ran); store: the path of that file.
    // Returns the number of schedules. Starts the once-a-second look (needs LVGL).
    int  load(const json* section, const json* local, const std::string& store);
    void clear();
    void start();                         // the look once a second (after LVGL is up; load() calls it when it already is)
    size_t count() const;
    const std::vector<ScheduleItem>& items() const;

    // What runs the actions of a schedule (default: the user interface's actions). For tests.
    void set_runner(std::function<void(const ScheduleItem&)> run);
    // One look at the clock. Returns the names of the schedules that ran.
    std::vector<std::string> check(int64_t now_s);

    // A change made on the device: time ("HH:MM"), days, enabled. Kept in project.local.json.
    // Returns "" or why not.
    std::string change(const std::string& name, const std::string& at, unsigned days, bool enabled);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Scheduler& scheduler();

} // namespace hml
