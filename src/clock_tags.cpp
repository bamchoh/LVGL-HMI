#include "clock_tags.hpp"
#include <cstdio>
#include <string>
#include "tag.hpp"

namespace hml {

namespace {
void set_text(const char* name, const std::string& text)
{
    Tag* t = tags().find(name);
    if(!t) {
        Value v = Value::string(text);
        t = tags().create(name, TagType::String, &v, false, 0, 0);
        return;
    }
    if(t->get().format() != text) t->set_string(text, nullptr);
}
} // namespace

void update_clock_tags(std::time_t now)
{
    std::tm tm{};
#if defined(_WIN32)
    if(localtime_s(&tm, &now) != 0) return;
#else
    if(!localtime_r(&now, &tm)) return;
#endif
    char date[16], time[16];
    std::snprintf(date, sizeof date, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    std::snprintf(time, sizeof time, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    set_text("sys.date", date);
    set_text("sys.time", time);
    set_text("sys.datetime", std::string(date) + " " + time);
}

void poll_clock_tags()
{
    update_clock_tags(std::time(nullptr));
}

} // namespace hml
