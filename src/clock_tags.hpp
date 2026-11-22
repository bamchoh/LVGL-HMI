// The date and time of the device as tags, for a clock on the screens without a script:
//   sys.date      "2026-11-22"
//   sys.time      "13:05:07"
//   sys.datetime  "2026-11-22 13:05:07"
// (local time, strings). They are created when the project is loaded and follow the clock while
// the runtime runs. Other layouts: a script with os.date writes its own string tag.
#pragma once
#include <ctime>

namespace hml {

// Create the tags if needed and give them the time `now` (only the tags whose text changed are set).
void update_clock_tags(std::time_t now);
// The same with the present time: called a few times a second.
void poll_clock_tags();

} // namespace hml
