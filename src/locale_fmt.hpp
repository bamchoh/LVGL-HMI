// How dates and numbers are WRITTEN ON THE SCREEN ("locale" of project.json):
//
//   "locale": { "date": "dmy", "decimal": "," }
//
//   date     "ymd" 2026-11-28 (default) | "dmy" 28.11.2026 | "mdy" 11/28/2026
//   decimal  "." (default) | ","
//
// Only what an operator reads and types: the clock (sys.date, sys.datetime), the times in the
// alarm list, the log view, the trend, the schedule page and in mails; the numbers of labels,
// numeric displays, tables and the trend, and what is typed into a number field.
// What is stored or passed on keeps one form everywhere: the databases, CSV files, file names,
// the values of tags, Lua, project.json - so that a file from one device reads on another.
#pragma once
#include <string>
#include "jsonutil.hpp"

namespace hml {

struct DisplayLocale {
    enum class Date { YMD, DMY, MDY };
    Date date = Date::YMD;
    char decimal = '.';
};

void set_display_locale(const json* section);          // nullptr = the defaults
const DisplayLocale& display_locale();

// A time as the code writes it ("2026-11-28 15:50:00", "2026-11-28", or without the year
// "11-28 15:50:00") in the order of the locale: "28.11.2026 15:50:00", "28.11. 15:50:00",
// "11/28/2026 ...", "11/28 ...". Anything else comes back as it is.
std::string local_time_text(const std::string& iso);
// A number that was formatted for a screen ("12.5 °C", "-0.25") with the decimal sign of the
// locale. Only a point between two digits is changed.
std::string local_number_text(const std::string& text);
// What an operator typed into a number field, as the code reads numbers ("12,5" -> "12.5").
std::string plain_number_text(const std::string& typed);

} // namespace hml
