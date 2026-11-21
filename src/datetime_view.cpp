#include "datetime_view.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <unordered_map>
#include "operation_log.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {

// ---------------------------------------------------------------- dates (no LVGL)

int days_in_month(int year, int month)
{
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if(month < 1 || month > 12) return 0;
    if(month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
    return d[month - 1];
}

bool datetime_valid(const DateTime& t)
{
    return t.year >= 1970 && t.year <= 9999 && t.month >= 1 && t.month <= 12 && t.day >= 1 && t.day <= days_in_month(t.year, t.month) &&
           t.hour >= 0 && t.hour <= 23 && t.minute >= 0 && t.minute <= 59 && t.second >= 0 && t.second <= 59;
}

namespace {

// digits only, of exactly these lengths: no signs, no spaces, nothing sscanf would let through
bool number(const std::string& s, size_t& i, size_t min_digits, size_t max_digits, int& out)
{
    size_t n = 0;
    long v = 0;
    while(i < s.size() && s[i] >= '0' && s[i] <= '9' && n < max_digits) { v = v * 10 + (s[i] - '0'); i++; n++; }
    if(n < min_digits) return false;
    if(i < s.size() && s[i] >= '0' && s[i] <= '9') return false;       // more digits than a field has
    out = static_cast<int>(v);
    return true;
}
bool date_part(const std::string& s, size_t& i, DateTime& t)
{
    if(!number(s, i, 4, 4, t.year) || i >= s.size() || (s[i] != '-' && s[i] != '/')) return false;
    const char sep = s[i++];
    if(!number(s, i, 1, 2, t.month) || i >= s.size() || s[i] != sep) return false;
    i++;
    return number(s, i, 1, 2, t.day);
}
bool time_part(const std::string& s, size_t& i, DateTime& t)
{
    if(!number(s, i, 1, 2, t.hour) || i >= s.size() || s[i] != ':') return false;
    i++;
    if(!number(s, i, 1, 2, t.minute)) return false;
    t.second = 0;
    if(i < s.size() && s[i] == ':') { i++; if(!number(s, i, 1, 2, t.second)) return false; }
    return true;
}

} // namespace

std::optional<DateTime> datetime_parse(const std::string& text, DateTimeMode mode)
{
    size_t a = text.find_first_not_of(" \t"), b = text.find_last_not_of(" \t");
    if(a == std::string::npos) return std::nullopt;
    const std::string s = text.substr(a, b - a + 1);
    if(s.size() > 32) return std::nullopt;
    DateTime t;
    size_t i = 0;
    if(mode == DateTimeMode::Time) {
        t = { 2000, 1, 1, 0, 0, 0 };
        if(!time_part(s, i, t)) return std::nullopt;
    }
    else {
        if(!date_part(s, i, t)) return std::nullopt;
        if(i < s.size()) {                                   // a time may follow (a date tag may hold one: it is left out)
            if(s[i] != ' ' && s[i] != 'T') return std::nullopt;
            i++;
            if(!time_part(s, i, t)) return std::nullopt;
        }
        if(mode == DateTimeMode::Date) t.hour = t.minute = t.second = 0;
    }
    if(i != s.size() || !datetime_valid(t)) return std::nullopt;
    return t;
}

std::string datetime_format(const DateTime& t, DateTimeMode mode, bool seconds)
{
    const std::string date = vformat("%04d-%02d-%02d", t.year, t.month, t.day);
    const std::string time = seconds ? vformat("%02d:%02d:%02d", t.hour, t.minute, t.second) : vformat("%02d:%02d", t.hour, t.minute);
    return mode == DateTimeMode::Date ? date : mode == DateTimeMode::Time ? time : date + " " + time;
}

int64_t datetime_to_number(const DateTime& t, DateTimeMode mode)
{
    if(mode == DateTimeMode::Time) return t.hour * 3600 + t.minute * 60 + t.second;
    std::tm tm{};
    tm.tm_year = t.year - 1900; tm.tm_mon = t.month - 1; tm.tm_mday = t.day;
    if(mode == DateTimeMode::DateAndTime) { tm.tm_hour = t.hour; tm.tm_min = t.minute; tm.tm_sec = t.second; }
    tm.tm_isdst = -1;                                        // (the C library decides about summer time)
    return static_cast<int64_t>(std::mktime(&tm));
}

std::optional<DateTime> datetime_from_number(double n, DateTimeMode mode)
{
    if(!std::isfinite(n) || n < 0) return std::nullopt;
    if(mode == DateTimeMode::Time) {
        if(n >= 86400) return std::nullopt;
        const int s = static_cast<int>(n);
        return DateTime{ 2000, 1, 1, s / 3600, s / 60 % 60, s % 60 };
    }
    if(n > 253402300799.0) return std::nullopt;              // the last second of the year 9999
    const std::time_t tt = static_cast<std::time_t>(n);
    std::tm tm{};
#ifdef _WIN32
    if(localtime_s(&tm, &tt) != 0) return std::nullopt;
#else
    if(!localtime_r(&tt, &tm)) return std::nullopt;
#endif
    DateTime t{ tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec };
    if(mode == DateTimeMode::Date) t.hour = t.minute = t.second = 0;
    return datetime_valid(t) ? std::optional<DateTime>(t) : std::nullopt;
}

// ---------------------------------------------------------------- the widget

namespace {

constexpr uint32_t kPollMs = 250;

struct Ctx {
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   year = nullptr, *month = nullptr, *day = nullptr, *hour = nullptr, *minute = nullptr, *second = nullptr;
    lv_timer_t* timer = nullptr;
    Tag*        tag = nullptr;
    DateTimeMode mode = DateTimeMode::DateAndTime;
    bool        seconds = false;
    int         year_min = 2020, year_max = 2040;
    int         day_count = 0;          // days the day roller offers now
    DateTime    shown;                  // what the rollers say
    std::string seen;                   // the tag as it was when the rollers were last set from it
    bool        have_seen = false;
    bool        setting = false;        // we move the rollers ourselves: not an entry of the operator

    static std::string options(int from, int to, int width)
    {
        std::string s;
        for(int v = from; v <= to; v++) s += (s.empty() ? "" : "\n") + vformat("%0*d", width, v);
        return s;
    }
    static void select(lv_obj_t* r, int index) { if(r) lv_roller_set_selected(r, static_cast<uint32_t>(std::max(0, index)), LV_ANIM_OFF); }
    static int  selected(lv_obj_t* r) { return r ? static_cast<int>(lv_roller_get_selected(r)) : 0; }

    void set_days(int year_v, int month_v)
    {
        const int n = days_in_month(year_v, month_v);
        if(!day || n == day_count) return;
        day_count = n;
        lv_roller_set_options(day, options(1, n, 2).c_str(), LV_ROLLER_MODE_NORMAL);
    }

    // put a date and time on the rollers (the years on offer grow when the value lies outside them)
    void put(DateTime t)
    {
        setting = true;
        if(year && (t.year < year_min || t.year > year_max)) {
            year_min = std::min(year_min, t.year);
            year_max = std::max(year_max, t.year);
            lv_roller_set_options(year, options(year_min, year_max, 4).c_str(), LV_ROLLER_MODE_NORMAL);
        }
        set_days(t.year, t.month);
        select(year, t.year - year_min);
        select(month, t.month - 1);
        select(day, t.day - 1);
        select(hour, t.hour);
        select(minute, t.minute);
        select(second, t.second);
        if(!second) t.second = 0;
        shown = t;
        setting = false;
    }

    DateTime read() const
    {
        DateTime t = shown;
        if(year) t.year = year_min + selected(year);
        if(month) t.month = selected(month) + 1;
        if(day) t.day = selected(day) + 1;
        if(hour) t.hour = selected(hour);
        if(minute) t.minute = selected(minute);
        t.second = second ? selected(second) : 0;
        return t;
    }

    std::optional<DateTime> from_tag() const
    {
        if(!tag) return std::nullopt;
        if(tag->type() == TagType::String) return datetime_parse(tag->get().format(), mode);
        return datetime_from_number(tag->get_number(), mode);
    }

    // the tag -> the rollers, when the tag changed (a script, a PLC, another screen)
    void follow()
    {
        if(!tag) return;
        const std::string now = tag->get().format();
        if(have_seen && now == seen) return;
        seen = now;
        have_seen = true;
        if(auto t = from_tag()) put(*t);
    }

    // the operator turned a roller
    void entered()
    {
        if(setting) return;
        DateTime t = read();
        // Only what the operator turned is an entry. The rest is what the tag says now: it may have
        // changed since the rollers were set (another widget on the same tag, a script, the PLC).
        if(tag && have_seen && tag->get().format() != seen) {
            if(auto cur = from_tag()) {
                DateTime base = *cur;
                if(t.year != shown.year) base.year = t.year;
                if(t.month != shown.month) base.month = t.month;
                if(t.day != shown.day) base.day = t.day;
                if(t.hour != shown.hour) base.hour = t.hour;
                if(t.minute != shown.minute) base.minute = t.minute;
                if(second && t.second != shown.second) base.second = t.second;
                t = base;
            }
        }
        if(day && t.day > days_in_month(t.year, t.month)) t.day = days_in_month(t.year, t.month);     // 31 -> 30, 29 -> 28
        if(!ui().operable(obj)) { put(shown); return; }                                               // not this user: back to what it was
        put(t);
        if(!tag) return;
        const std::string before = tag->get().format();
        if(tag->type() == TagType::String) tag->set_string(datetime_format(t, mode, seconds), nullptr);
        else tag->set_number(static_cast<double>(datetime_to_number(t, mode)), nullptr);
        seen = tag->get().format();
        have_seen = true;
        if(seen != before) operation_log().record("write", tag->name(), before, seen);
    }

    void tick()
    {
        follow();
        // a widget that may not be operated looks like it (the rule of "level" is on the widget itself)
        const bool off = lv_obj_has_state(obj, LV_STATE_DISABLED);
        for(lv_obj_t* r : { year, month, day, hour, minute, second }) if(r) lv_obj_set_state(r, LV_STATE_DISABLED, off);
    }
};

std::unordered_map<lv_obj_t*, Ctx*>& registry() { static std::unordered_map<lv_obj_t*, Ctx*> r; return r; }

lv_obj_t* roller(Ctx* c, const std::string& opts, int32_t width)
{
    lv_obj_t* r = lv_roller_create(c->obj);
    lv_roller_set_options(r, opts.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, width);
    lv_obj_add_event_cb(r, [](lv_event_t* e) { static_cast<Ctx*>(lv_event_get_user_data(e))->entered(); }, LV_EVENT_VALUE_CHANGED, c);
    // before the roller itself sees the press: a user who may not operate the widget turns nothing
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        if(ui().operable(cx->obj, true)) return;
        if(lv_indev_t* indev = lv_indev_active()) lv_indev_wait_release(indev);
        lv_event_stop_processing(e);
    }, static_cast<lv_event_code_t>(LV_EVENT_PRESSED | LV_EVENT_PREPROCESS), c);
    return r;
}

lv_obj_t* gap(Ctx* c, const char* text)
{
    lv_obj_t* l = lv_label_create(c->obj);
    lv_label_set_text(l, text);
    return l;
}

} // namespace

lv_obj_t* build_datetime_input(lv_obj_t* parent, const json& w)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 4, 0);
    lv_obj_set_scrollable(obj, false);

    auto* c = new Ctx;
    c->obj = obj;
    const std::string mode = jstr(w, "mode", "datetime");
    c->mode = iequals(mode, "date") ? DateTimeMode::Date : iequals(mode, "time") ? DateTimeMode::Time : DateTimeMode::DateAndTime;
    if(!iequals(mode, "date") && !iequals(mode, "time") && !iequals(mode, "datetime")) HML_WARN("datetime_input: unknown mode '%s' (datetime | date | time)", mode.c_str());
    c->seconds = jbool(w, "seconds", false) && c->mode != DateTimeMode::Date;
    c->year_min = std::clamp(static_cast<int>(jnum(w, "year_min", 2020)), 1970, 9999);
    c->year_max = std::clamp(static_cast<int>(jnum(w, "year_max", 2040)), c->year_min, std::min(9999, c->year_min + 200));
    // the tag: "bind": "tag" or { "value": "tag" } / { "value": { "tag": "tag" } }
    std::string tagname;
    if(const json* b = jget(w, "bind")) {
        if(b->is_string()) tagname = b->get<std::string>();
        else if(b->is_object()) if(const json* v = jget(*b, "value")) tagname = v->is_string() ? v->get<std::string>() : v->is_object() ? jstr(*v, "tag", "") : "";
    }
    if(!tagname.empty()) {
        c->tag = tags().find(tagname);
        if(!c->tag) HML_WARN("datetime_input: unknown tag '%s'", tagname.c_str());
        else if(c->tag->type() == TagType::Bool) { HML_WARN("datetime_input: tag '%s' is a bool: a string or a number is needed", tagname.c_str()); c->tag = nullptr; }
    }

    if(c->mode != DateTimeMode::Time) {
        c->year = roller(c, Ctx::options(c->year_min, c->year_max, 4), 84);
        gap(c, "-");
        c->month = roller(c, Ctx::options(1, 12, 2), 60);
        gap(c, "-");
        c->day = roller(c, Ctx::options(1, 31, 2), 60);
        c->day_count = 31;
    }
    if(c->mode == DateTimeMode::DateAndTime) gap(c, " ");
    if(c->mode != DateTimeMode::Date) {
        c->hour = roller(c, Ctx::options(0, 23, 2), 60);
        gap(c, ":");
        c->minute = roller(c, Ctx::options(0, 59, 2), 60);
        if(c->seconds) { gap(c, ":"); c->second = roller(c, Ctx::options(0, 59, 2), 60); }
    }

    // until the tag says something: now (shown, written only when the operator turns a roller)
    DateTime now = datetime_from_number(static_cast<double>(std::time(nullptr)), DateTimeMode::DateAndTime).value_or(DateTime{ c->year_min, 1, 1, 0, 0, 0 });
    c->put(now);
    c->follow();

    registry()[obj] = c;
    c->timer = lv_timer_create([](lv_timer_t* t) { static_cast<Ctx*>(lv_timer_get_user_data(t))->tick(); }, kPollMs, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        if(cx->timer) lv_timer_delete(cx->timer);
        registry().erase(cx->obj);
        delete cx;
    }, LV_EVENT_DELETE, c);
    return obj;
}

lv_obj_t* datetime_roller(lv_obj_t* widget, const char* part)
{
    auto it = registry().find(widget);
    if(it == registry().end()) return nullptr;
    const Ctx* c = it->second;
    const std::string p = part ? part : "";
    return p == "year" ? c->year : p == "month" ? c->month : p == "day" ? c->day : p == "hour" ? c->hour : p == "minute" ? c->minute : p == "second" ? c->second : nullptr;
}

} // namespace hml
