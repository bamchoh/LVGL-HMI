#include "schedule.hpp"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include "lvgl.h"
#include "operation_log.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

const char* const kDayNames[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
constexpr int64_t kJumpS = 120;           // a step of the clock larger than this is "was not running"
constexpr int64_t kCatchUpS = 86400;      // a missed time is caught up for a day at most

bool digits(const std::string& s, size_t pos, size_t n, int& out)
{
    if(pos + n > s.size()) return false;
    int v = 0;
    for(size_t i = 0; i < n; i++) { const char c = s[pos + i]; if(c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
    out = v;
    return true;
}

int month_days(int year, int month)
{
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if(month < 1 || month > 12) return 0;
    return month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : d[month - 1];
}

// local date and time -> Unix seconds; the weekday of the result (0 = Sunday)
int64_t local_time(int year, int month, int day, int hour, int minute, int* wday = nullptr)
{
    std::tm tm{};
    tm.tm_year = year - 1900; tm.tm_mon = month - 1; tm.tm_mday = day; tm.tm_hour = hour; tm.tm_min = minute;
    tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    if(wday) *wday = tm.tm_wday;
    return static_cast<int64_t>(t);
}

bool local_date(int64_t t, std::tm& out)
{
    const std::time_t tt = static_cast<std::time_t>(t);
#ifdef _WIN32
    return localtime_s(&out, &tt) == 0;
#else
    return localtime_r(&tt, &out) != nullptr;
#endif
}

} // namespace

bool schedule_parse_at(const std::string& text, ScheduleItem& io)
{
    size_t a = text.find_first_not_of(" \t"), b = text.find_last_not_of(" \t");
    if(a == std::string::npos) return false;
    const std::string s = text.substr(a, b - a + 1);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if(s.size() == 5 && s[2] == ':' && digits(s, 0, 2, h) && digits(s, 3, 2, mi)) {
        if(h > 23 || mi > 59) return false;
        io.once = false;
        io.hour = h; io.minute = mi;
        return true;
    }
    if(s.size() == 16 && s[4] == '-' && s[7] == '-' && (s[10] == ' ' || s[10] == 'T') && s[13] == ':' &&
       digits(s, 0, 4, y) && digits(s, 5, 2, mo) && digits(s, 8, 2, d) && digits(s, 11, 2, h) && digits(s, 14, 2, mi)) {
        if(y < 1970 || y > 9999 || d < 1 || d > month_days(y, mo) || h > 23 || mi > 59) return false;
        io.once = true;
        io.year = y; io.month = mo; io.day = d; io.hour = h; io.minute = mi;
        return true;
    }
    return false;
}

bool schedule_parse_days(const json& days, unsigned& mask)
{
    auto one = [&](const std::string& n, unsigned& m) {
        if(iequals(n, "daily") || iequals(n, "all")) { m |= 0x7F; return true; }
        if(iequals(n, "weekdays")) { m |= 0x3E; return true; }
        if(iequals(n, "weekend")) { m |= 0x41; return true; }
        for(int i = 0; i < 7; i++) if(iequals(n, kDayNames[i])) { m |= 1u << i; return true; }
        return false;
    };
    unsigned m = 0;
    if(days.is_string()) {
        std::string word;
        for(char c : days.get<std::string>() + " ") {
            if(c == ' ' || c == ',') { if(!word.empty() && !one(word, m)) return false; word.clear(); }
            else word += c;
        }
    }
    else if(days.is_array()) { for(const json& d : days) if(!d.is_string() || !one(d.get<std::string>(), m)) return false; }
    else return false;
    if(m == 0) return false;
    mask = m;
    return true;
}

std::string schedule_at_text(const ScheduleItem& s)
{
    return s.once ? vformat("%04d-%02d-%02d %02d:%02d", s.year, s.month, s.day, s.hour, s.minute) : vformat("%02d:%02d", s.hour, s.minute);
}

std::string schedule_days_text(unsigned mask)
{
    mask &= 0x7F;
    if(mask == 0x7F) return "daily";
    if(mask == 0x3E) return "weekdays";
    if(mask == 0x41) return "weekend";
    std::string out;
    for(int i = 1; i <= 7; i++) if(mask & (1u << (i % 7))) out += (out.empty() ? "" : " ") + std::string(kDayNames[i % 7]);     // (monday first)
    return out;
}

json schedule_days_json(unsigned mask)
{
    mask &= 0x7F;
    if(mask == 0x7F) return "daily";
    json a = json::array();
    for(int i = 1; i <= 7; i++) if(mask & (1u << (i % 7))) a.push_back(kDayNames[i % 7]);
    return a;
}

bool schedule_parse(const json& j, ScheduleItem& out, std::string* err)
{
    auto fail = [&](const std::string& why) { if(err) *err = why; return false; };
    if(!j.is_object()) return fail("a schedule is { \"name\", \"at\", \"do\" }");
    ScheduleItem s;
    s.name = jstr(j, "name", "");
    if(s.name.empty()) return fail("a schedule needs a \"name\"");
    if(s.name.size() > 64) return fail("the name is too long");
    if(!schedule_parse_at(jstr(j, "at", ""), s)) return fail("\"at\" is \"HH:MM\" or \"YYYY-MM-DD HH:MM\"");
    if(const json* d = jget(j, "days")) { if(!schedule_parse_days(*d, s.days)) return fail("\"days\" is a list of sun mon tue wed thu fri sat, or daily / weekdays / weekend"); }
    const json* act = jget(j, "do");
    if(!act || !(act->is_object() || act->is_array())) return fail("\"do\" is an action or a list of actions");
    s.actions = *act;
    s.enabled = jbool(j, "enabled", true);
    s.catch_up = jbool(j, "catch_up", false);
    out = std::move(s);
    return true;
}

std::optional<int64_t> schedule_latest_due(const ScheduleItem& s, int64_t after_s, int64_t upto_s)
{
    if(upto_s <= after_s) return std::nullopt;
    if(s.once) {
        const int64_t t = local_time(s.year, s.month, s.day, s.hour, s.minute);
        return t > after_s && t <= upto_s ? std::optional<int64_t>(t) : std::nullopt;
    }
    std::tm day{};
    if(!local_date(upto_s, day)) return std::nullopt;
    // from the day of upto_s back, day by day (a week holds every day a schedule can name)
    for(int back = 0; back <= 7; back++) {
        int wday = 0;
        const int64_t t = local_time(day.tm_year + 1900, day.tm_mon + 1, day.tm_mday - back, s.hour, s.minute, &wday);
        if(t <= after_s) break;
        if(t <= upto_s && (s.days & (1u << wday))) return t;
    }
    return std::nullopt;
}

std::optional<int64_t> schedule_next_due(const ScheduleItem& s, int64_t after_s)
{
    if(s.once) {
        const int64_t t = local_time(s.year, s.month, s.day, s.hour, s.minute);
        return t > after_s ? std::optional<int64_t>(t) : std::nullopt;
    }
    std::tm day{};
    if(!local_date(after_s, day)) return std::nullopt;
    for(int ahead = 0; ahead <= 8; ahead++) {
        int wday = 0;
        const int64_t t = local_time(day.tm_year + 1900, day.tm_mon + 1, day.tm_mday + ahead, s.hour, s.minute, &wday);
        if(t > after_s && (s.days & (1u << wday))) return t;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- the scheduler

struct Scheduler::Impl {
    std::vector<ScheduleItem> items;
    std::vector<ScheduleItem> base;                    // as the project says (what differs is kept in the local file)
    std::map<std::string, int64_t> last_run;           // catch_up schedules: the scheduled time that ran last
    std::string store;
    std::function<void(const ScheduleItem&)> run;
    lv_timer_t* timer = nullptr;
    int64_t last = 0;                                  // the clock at the last look (0 = none yet)

    json read_store() const
    {
        json overlay = json::object();
        if(store.empty()) return overlay;
        std::ifstream f(std::filesystem::u8path(store), std::ios::binary);
        if(!f) return overlay;
        std::stringstream ss;
        ss << f.rdbuf();
        overlay = json::parse(ss.str(), nullptr, false, true);
        return overlay.is_object() ? overlay : json::object();
    }

    // only what differs from the project, and the times catch_up schedules last ran
    std::string write_store()
    {
        if(store.empty()) return "";
        json overlay = read_store();
        json mine = json::object();
        for(size_t i = 0; i < items.size() && i < base.size(); i++) {
            const ScheduleItem& s = items[i];
            const ScheduleItem& b = base[i];
            json o = json::object();
            if(schedule_at_text(s) != schedule_at_text(b)) o["at"] = schedule_at_text(s);
            if(s.days != b.days) o["days"] = schedule_days_json(s.days);
            if(s.enabled != b.enabled) o["enabled"] = s.enabled;
            auto lr = last_run.find(s.name);
            if(s.catch_up && lr != last_run.end() && lr->second > 0) o["last"] = lr->second;
            if(!o.empty()) mine[s.name] = std::move(o);
        }
        if(mine.empty()) overlay.erase("schedules"); else overlay["schedules"] = std::move(mine);
        if(overlay.empty()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::u8path(store), ec);
            return "";
        }
        std::string err;
        return write_file_atomic(store, overlay.dump(2) + "\n", &err) ? "" : err;
    }

    void fire(ScheduleItem& s, int64_t due)
    {
        s.fired = due;
        HML_INFO("schedule '%s' (%s %s) runs", s.name.c_str(), schedule_at_text(s).c_str(), s.once ? "once" : schedule_days_text(s.days).c_str());
        operation_log().record("schedule", s.name, "", schedule_at_text(s));
        if(run) run(s);
        if(s.catch_up) { last_run[s.name] = due; write_store(); }
    }
};

static void run_with_ui(const ScheduleItem& s) { ui().run_actions(s.actions); }

Scheduler::Scheduler() : impl_(std::make_unique<Impl>()) { impl_->run = run_with_ui; }
Scheduler::~Scheduler() = default;

void Scheduler::clear()
{
    Impl& m = *impl_;
    if(m.timer) { lv_timer_delete(m.timer); m.timer = nullptr; }
    m.items.clear();
    m.base.clear();
    m.last_run.clear();
    m.store.clear();
    m.last = 0;
    m.run = run_with_ui;                               // (a runner set for a test does not outlive it)
}

int Scheduler::load(const json* section, const json* local, const std::string& store)
{
    clear();
    Impl& m = *impl_;
    m.store = store;
    if(!section) return 0;
    if(!section->is_array()) { HML_WARN("schedules: a list is needed"); return 0; }
    for(const json& j : *section) {
        ScheduleItem s;
        std::string err;
        if(!schedule_parse(j, s, &err)) { HML_WARN("schedules: %s%s%s: %s", j.is_object() ? "'" : "", j.is_object() ? jstr(j, "name", "").c_str() : "", j.is_object() ? "'" : "", err.c_str()); continue; }
        if(std::any_of(m.items.begin(), m.items.end(), [&](const ScheduleItem& o) { return o.name == s.name; })) { HML_WARN("schedules: '%s' is defined twice, the first one is used", s.name.c_str()); continue; }
        if(m.items.size() >= 200) { HML_WARN("schedules: more than 200, the rest is left out"); break; }
        m.items.push_back(s);
    }
    m.base = m.items;
    // what was changed on the device
    if(local && local->is_object()) {
        for(ScheduleItem& s : m.items) {
            const json* o = jget(*local, s.name.c_str());
            if(!o || !o->is_object()) continue;
            if(auto at = jstr_opt(*o, "at")) { ScheduleItem t = s; if(schedule_parse_at(*at, t)) s = t; }
            if(const json* d = jget(*o, "days")) schedule_parse_days(*d, s.days);
            if(jhas(*o, "enabled")) s.enabled = jbool(*o, "enabled", s.enabled);
            if(jhas(*o, "last")) m.last_run[s.name] = static_cast<int64_t>(jnum(*o, "last", 0));
        }
    }
    if(!m.items.empty()) HML_INFO("schedules: %zu defined", m.items.size());
    start();
    return static_cast<int>(m.items.size());
}

// The once-a-second look. A project is loaded before LVGL is up when the runtime starts: main()
// calls this again then.
void Scheduler::start()
{
    Impl& m = *impl_;
    if(m.timer || m.items.empty() || !lv_is_initialized()) return;
    m.timer = lv_timer_create([](lv_timer_t* t) { static_cast<Scheduler*>(lv_timer_get_user_data(t))->check(static_cast<int64_t>(std::time(nullptr))); }, 1000, this);
}

size_t Scheduler::count() const { return impl_->items.size(); }
const std::vector<ScheduleItem>& Scheduler::items() const { return impl_->items; }
void Scheduler::set_runner(std::function<void(const ScheduleItem&)> run) { impl_->run = std::move(run); }

std::vector<std::string> Scheduler::check(int64_t now)
{
    Impl& m = *impl_;
    std::vector<std::string> ran;
    if(now <= 0) return ran;
    const bool first = m.last == 0;
    if(!first && now < m.last) {                      // the clock was set back: nothing runs twice (see "fired")
        for(ScheduleItem& s : m.items) if(s.fired > now + 86400) s.fired = 0;       // (set back by more than a day: start over)
        m.last = now;
        return ran;
    }
    if(first || now - m.last > kJumpS) {
        // we were not running at some scheduled times (just started, or the clock jumped ahead):
        // only the schedules that ask for it run, once, for the newest time that was missed
        bool noted = false;
        for(ScheduleItem& s : m.items) {
            if(!s.enabled || !s.catch_up) continue;
            auto lr = m.last_run.find(s.name);
            const int64_t since = std::max<int64_t>({ lr != m.last_run.end() ? lr->second : 0, s.fired, now - kCatchUpS });
            // (never ran here before and no record of it: nothing is known to be missed)
            // ... but from now on there is: a time that passes while the device is off is caught up
            if(first && lr == m.last_run.end()) { m.last_run[s.name] = now; noted = true; continue; }
            if(auto t = schedule_latest_due(s, since, now)) { m.fire(s, *t); ran.push_back(s.name); }
        }
        if(noted) m.write_store();
        m.last = now;
        return ran;
    }
    for(ScheduleItem& s : m.items) {
        if(!s.enabled) continue;
        const auto t = schedule_latest_due(s, m.last, now);
        if(t && *t > s.fired) { m.fire(s, *t); ran.push_back(s.name); }
    }
    m.last = now;
    return ran;
}

std::string Scheduler::change(const std::string& name, const std::string& at, unsigned days, bool enabled)
{
    Impl& m = *impl_;
    for(ScheduleItem& s : m.items) {
        if(s.name != name) continue;
        ScheduleItem t = s;
        if(!schedule_parse_at(at, t)) return "The time is HH:MM.";
        if(t.once != s.once) return s.once ? "This schedule runs once: its time is a date and a time." : "This schedule runs by the day: its time is HH:MM.";
        if((days & 0x7F) == 0) return "Pick at least one day.";
        t.days = days & 0x7F;
        t.enabled = enabled;
        t.fired = 0;                                   // (a new time may be today's)
        const std::string before = schedule_at_text(s) + " " + schedule_days_text(s.days) + (s.enabled ? "" : " (off)");
        s = t;
        const std::string after = schedule_at_text(s) + " " + schedule_days_text(s.days) + (s.enabled ? "" : " (off)");
        if(after != before) operation_log().record("schedule", name, before, after);
        const std::string err = m.write_store();
        if(!err.empty()) return "Changed, but it could not be saved: " + err;
        return "";
    }
    return "There is no schedule '" + name + "'.";
}

Scheduler& scheduler()
{
    static Scheduler s;
    return s;
}

} // namespace hml
