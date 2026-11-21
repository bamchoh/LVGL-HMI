// Entering a date and a time with rollers (year, month, day, hour, minute, second): no keyboard,
// and only dates that exist can be chosen.
//
//   { "type": "datetime_input", "bind": "start_at",
//     "mode": "datetime",         // "datetime" | "date" | "time"
//     "seconds": false,           // a roller for the seconds too
//     "year_min": 2020, "year_max": 2040 }
//
// The tag:
//   string  "2026-11-21 13:05" ("2026-11-21 13:05:09" with seconds), "2026-11-21", "13:05"
//   int / float   seconds: Unix time in the local time of the device (datetime, date - a date is
//                 its midnight); seconds since midnight (time)
// The operator's choice is written when a roller comes to rest, goes the way a write from any
// widget goes (limits of the tag, the driver, the operation log) and obeys "level".
// The day roller follows the month: 28, 29, 30 or 31 days.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

struct DateTime {
    int year = 2000, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    bool operator==(const DateTime& o) const { return year == o.year && month == o.month && day == o.day && hour == o.hour && minute == o.minute && second == o.second; }
};
enum class DateTimeMode { DateAndTime, Date, Time };

int  days_in_month(int year, int month);                    // 28 .. 31 (0 for a month that is none)
bool datetime_valid(const DateTime& t);                     // a date and a time that exist (years 1970 .. 9999)
// "2026-11-21 13:05[:09]" (also with 'T' or '/' as written by other systems), "2026-11-21",
// "13:05[:09]". nullopt for anything else and for dates that do not exist.
std::optional<DateTime> datetime_parse(const std::string& text, DateTimeMode mode);
std::string datetime_format(const DateTime& t, DateTimeMode mode, bool seconds);
// Local time <-> Unix seconds. Time mode: seconds since midnight.
int64_t  datetime_to_number(const DateTime& t, DateTimeMode mode);
std::optional<DateTime> datetime_from_number(double n, DateTimeMode mode);

lv_obj_t* build_datetime_input(lv_obj_t* parent, const json& w);
// (tests) the rollers from the left: year month day hour minute second, as the mode has them
lv_obj_t* datetime_roller(lv_obj_t* widget, const char* part);

} // namespace hml
