#include "alarm_stats.hpp"
#include <algorithm>
#include <map>
#include "util.hpp"

namespace hml {

std::vector<AlarmStat> alarm_stats(const std::vector<AlarmEvent>& events, int64_t from_ms, int64_t to_ms, int64_t now_ms,
                                   const std::set<std::string>& active_now, AlarmSort sort, const std::string& severity)
{
    // (up to now means now included: an alarm raised in this very millisecond is counted)
    const bool to_now = to_ms <= 0 || to_ms > now_ms;
    if(to_now) to_ms = now_ms;
    if(from_ms < 0) from_ms = 0;
    struct Run { AlarmStat st; bool open = false; int64_t since = 0; };
    std::map<std::string, Run> runs;
    // a stretch [a, b) of an alarm: its part inside the span
    auto add = [&](Run& r, int64_t a, int64_t b) {
        a = std::max(a, from_ms);
        b = std::min(b, to_ms);
        if(b <= a) return;                                   // outside the span, or the clock went backwards
        r.st.active_ms += b - a;
        r.st.longest_ms = std::max(r.st.longest_ms, b - a);
    };
    int64_t last_seen = 0;                                   // when the log last said anything (the runtime was alive then)
    // (every event is looked at, also those after the span: an alarm that was active at the end of
    // the span and cleared later was active to the end of the span)
    for(const AlarmEvent& e : events) {
        if(e.event == "raised" || e.event == "cleared") {
            Run& r = runs[e.name];
            r.st.name = e.name;
            r.st.message = e.message;
            r.st.severity = e.severity;
            if(e.event == "raised") {
                if(r.open) add(r, r.since, std::max(r.since, std::min(last_seen, e.ts_ms)));      // (its "cleared" is missing)
                r.open = true;
                r.since = e.ts_ms;
                if(e.ts_ms >= from_ms && (e.ts_ms < to_ms || (to_now && e.ts_ms == to_ms))) { r.st.count++; r.st.last_raised_ms = e.ts_ms; }
            }
            else if(r.open) {
                add(r, r.since, e.ts_ms);
                r.open = false;
            }
        }
        last_seen = std::max(last_seen, e.ts_ms);
    }
    std::vector<AlarmStat> out;
    for(auto& [name, r] : runs) {
        r.st.active_now = active_now.count(name) > 0;
        if(r.open) add(r, r.since, r.st.active_now ? to_ms : std::max(r.since, last_seen));
        if(r.st.count == 0 && r.st.active_ms == 0) continue;
        if(!severity.empty() && !iequals(r.st.severity, severity)) continue;
        out.push_back(std::move(r.st));
    }
    std::sort(out.begin(), out.end(), [sort](const AlarmStat& a, const AlarmStat& b) {
        if(sort == AlarmSort::Time) { if(a.active_ms != b.active_ms) return a.active_ms > b.active_ms; if(a.count != b.count) return a.count > b.count; }
        else { if(a.count != b.count) return a.count > b.count; if(a.active_ms != b.active_ms) return a.active_ms > b.active_ms; }
        return a.name < b.name;
    });
    return out;
}

std::vector<AlarmStat> alarm_stats_recent(int days, const std::set<std::string>& active_now, AlarmSort sort, const std::string& severity, int64_t now_ms)
{
    if(!alarms().enabled()) return {};
    if(now_ms <= 0) now_ms = LogDb::now_ms();
    const int64_t from = days > 0 ? now_ms - static_cast<int64_t>(days) * 86400000LL : 0;
    // (every event up to now: what was raised before the span may still be active in it)
    std::vector<AlarmEvent> events;
    for(const LogRow& r : alarms().db().query("ts <= ?1 AND event != 'acked'", { now_ms }, 0, false)) {       // id, ts, name, event, severity, message, value, user
        if(r.size() < 6) continue;
        AlarmEvent e;
        e.ts_ms = r[1].i;
        e.name = r[2].str(); e.event = r[3].str(); e.severity = r[4].str(); e.message = r[5].str();
        events.push_back(std::move(e));
    }
    // (ids follow the order of writing; a clock set back must not reorder what happened)
    return alarm_stats(events, from, 0, now_ms, active_now, sort, severity);
}

std::string alarm_duration_text(int64_t ms)
{
    if(ms < 0) ms = 0;
    const int64_t s = (ms + 500) / 1000;
    return vformat("%lld:%02d:%02d", static_cast<long long>(s / 3600), static_cast<int>(s / 60 % 60), static_cast<int>(s % 60));
}

std::string csv_safe_text(const std::string& s)
{
    if(s.empty()) return s;
    const char c = s[0];
    return (c == '=' || c == '+' || c == '-' || c == '@' || c == '\t' || c == '\r') ? "'" + s : s;
}

int64_t alarm_stats_export(const std::vector<AlarmStat>& stats, std::string path, std::string* written)
{
    if(path.empty()) {
        if(!alarms().enabled()) return -1;
        path = alarms().db().export_path("alarm-summary");
    }
    std::vector<std::vector<std::string>> rows;
    for(const AlarmStat& s : stats) {
        rows.push_back({ csv_safe_text(s.name), csv_safe_text(alarm_text_line(s.message)), csv_safe_text(s.severity), std::to_string(s.count),
                         std::to_string((s.active_ms + 500) / 1000), std::to_string((s.longest_ms + 500) / 1000), s.active_now ? "1" : "0" });
    }
    const int64_t n = LogDb::write_csv(path, { "name", "message", "severity", "count", "active_s", "longest_s", "active_now" }, rows);
    if(n >= 0 && written) *written = path;
    return n;
}

} // namespace hml
