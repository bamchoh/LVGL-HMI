// sys.date / sys.time / sys.datetime (clock_tags.hpp). Grouped by ISO/IEC 25010 characteristic.
#include <doctest.h>
#include "clock_tags.hpp"
#include "tag.hpp"

#include <ctime>
#include <string>

using namespace hml;

namespace {
// a local time as time_t (what the device clock would say)
std::time_t local(int y, int mo, int d, int h, int mi, int s)
{
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
    return std::mktime(&tm);
}
std::string text(const char* name)
{
    Tag* t = tags().find(name);
    return t ? t->get().format() : "<none>";
}
} // namespace

TEST_SUITE("functional suitability") {
TEST_CASE("clock tags: the date and time of the device as strings")
{
    tags().clear();
    update_clock_tags(local(2026, 3, 7, 9, 4, 5));
    CHECK(text("sys.date") == "2026-03-07");
    CHECK(text("sys.time") == "09:04:05");
    CHECK(text("sys.datetime") == "2026-03-07 09:04:05");
    REQUIRE(tags().find("sys.time"));
    CHECK(tags().find("sys.time")->type() == TagType::String);

    // they follow the clock
    update_clock_tags(local(2026, 12, 31, 23, 59, 59));
    CHECK(text("sys.datetime") == "2026-12-31 23:59:59");
    update_clock_tags(local(2027, 1, 1, 0, 0, 0));
    CHECK(text("sys.date") == "2027-01-01");
    CHECK(text("sys.time") == "00:00:00");
    tags().clear();
}
}

TEST_SUITE("performance efficiency") {
TEST_CASE("clock tags: a tag is only written when its text changes")
{
    tags().clear();
    const std::time_t t0 = local(2026, 5, 1, 12, 0, 0);
    update_clock_tags(t0);
    int date_writes = 0, time_writes = 0;
    tags().find("sys.date")->subscribe(SubGroup::Screen, [&](Tag&, const Value&, const void*) { date_writes++; });
    tags().find("sys.time")->subscribe(SubGroup::Screen, [&](Tag&, const Value&, const void*) { time_writes++; });
    update_clock_tags(t0);                         // the same second (the poll runs 4 times a second)
    update_clock_tags(t0 + 1);
    tags().dispatch(false);
    CHECK(date_writes == 0);                       // the date did not change: nothing to redraw
    CHECK(time_writes == 1);
    CHECK(text("sys.time") == "12:00:01");
    CHECK(text("sys.date") == "2026-05-01");
    tags().unsubscribe_group(SubGroup::Screen);
    tags().clear();
}
}
