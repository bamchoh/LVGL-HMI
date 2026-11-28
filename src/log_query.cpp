#include "locale_fmt.hpp"
#include "log_query.hpp"
#include <algorithm>
#include "data_log.hpp"
#include "logdb.hpp"
#include "operation_log.hpp"
#include "system_log.hpp"
#include "util.hpp"

namespace hml {
namespace {

// "MM-DD HH:MM:SS" (the year is rarely in doubt on a device, the width is)
std::string short_time(int64_t ms)
{
    if(ms <= 0) return "";
    const std::string t = LogDb::format_time(ms);          // YYYY-MM-DD HH:MM:SS.mmm
    return t.size() >= 19 ? local_time_text(t.substr(5, 14)) : t;
}

int64_t since(const LogViewQuery& q, int64_t now_ms)
{
    if(q.days <= 0) return 0;
    return (now_ms > 0 ? now_ms : LogDb::now_ms()) - static_cast<int64_t>(q.days) * 86400000LL;
}

std::string json_cell(const json& v)
{
    if(v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if(v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if(v.is_number()) return vformat("%.6g", v.get<double>());
    if(v.is_string()) return v.get<std::string>();
    return "";
}

} // namespace

std::string log_view_text(const std::string& s, size_t max_chars)
{
    std::string out;
    size_t chars = 0;
    for(size_t i = 0; i < s.size();) {
        if(chars >= max_chars) { out += "..."; break; }
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 0x06 ? 2 : (c >> 4) == 0x0E ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool good = n > 0 && i + n <= s.size();
        for(size_t k = 1; good && k < n; k++) good = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
        if(good && n == 2 && c < 0xC2) good = false;                                       // (overlong)
        if(good && n == 3 && c == 0xE0 && static_cast<unsigned char>(s[i + 1]) < 0xA0) good = false;
        if(good && n == 4 && (c > 0xF4 || (c == 0xF0 && static_cast<unsigned char>(s[i + 1]) < 0x90))) good = false;
        if(!good) { out += '?'; i++; }
        else if(n == 1) { out += (c < 0x20 || c == 0x7F) ? ' ' : static_cast<char>(c); i++; }
        else { out.append(s, i, n); i += n; }
        chars++;
    }
    return out;
}

std::vector<std::string> log_view_known_columns(const std::string& source, const std::string& group)
{
    if(source == "system") return { "time", "level", "source", "message", "count" };
    if(source == "operation") return { "time", "user", "screen", "kind", "target", "old", "new" };
    if(source == "data") {
        std::vector<std::string> c{ "time" };
        for(const std::string& t : data_log().group_tags(group)) c.push_back(t);
        return c;
    }
    return {};
}

std::vector<std::string> log_view_columns(const LogViewQuery& q)
{
    if(!q.columns.empty()) return q.columns;
    if(q.source == "system") return { "time", "level", "source", "message" };
    if(q.source == "operation") return { "time", "user", "kind", "target", "old", "new" };
    return log_view_known_columns(q.source, q.group);
}

LogViewPage log_view_fetch(const LogViewQuery& q, int64_t now_ms)
{
    LogViewPage page;
    const std::vector<std::string> cols = log_view_columns(q);
    const int limit = std::clamp(q.limit, 1, 200), offset = std::max(0, q.offset);
    const int64_t from = since(q, now_ms);

    if(q.source == "system") {
        if(!system_log().is_open()) { page.problem = "The system log is not recorded (\"system_log\" in project.json)."; return page; }
        SystemLogQuery sq;
        sq.min_level = q.min_level >= 2 ? LogLevel::Error : q.min_level == 1 ? LogLevel::Warn : LogLevel::Info;
        sq.text = q.text;
        sq.since_ms = from;
        sq.newest_first = true;
        page.total = system_log().count(sq);
        sq.limit = limit;
        sq.offset = offset;
        for(const SystemLogEntry& e : system_log().query(sq)) {
            LogViewRow r;
            r.level = e.level == LogLevel::Error ? 2 : e.level == LogLevel::Warn ? 1 : 0;
            for(const std::string& c : cols) {
                if(c == "time") r.cells.push_back(short_time(e.last_ts_ms > 0 ? e.last_ts_ms : e.ts_ms));
                else if(c == "level") r.cells.push_back(r.level == 2 ? "ERROR" : r.level == 1 ? "WARN" : "INFO");
                else if(c == "source") r.cells.push_back(log_view_text(e.source, 24));
                else if(c == "message") r.cells.push_back(log_view_text(e.message));
                else if(c == "count") r.cells.push_back(e.count > 1 ? "x" + std::to_string(e.count) : "");
                else r.cells.emplace_back();
            }
            page.rows.push_back(std::move(r));
        }
        return page;
    }

    if(q.source == "operation") {
        if(!operation_log().enabled()) { page.problem = "The operation log is not recorded (\"operation_log\" in project.json)."; return page; }
        // (every value goes through a bind: what the operator types never becomes SQL)
        const std::string where = "(?1 = 0 OR ts >= ?1) AND (?2 = '' OR instr(user, ?2) > 0 OR instr(screen, ?2) > 0 OR instr(kind, ?2) > 0"
                                  " OR instr(target, ?2) > 0 OR instr(old_value, ?2) > 0 OR instr(new_value, ?2) > 0)";
        const LogRow binds{ from, q.text };
        LogDb& db = operation_log().db();
        page.total = db.count(where, binds);
        for(const LogRow& row : db.query(where, binds, limit, true, offset)) {       // id, ts, user, screen, kind, target, old, new
            if(row.size() < 8) continue;
            LogViewRow r;
            for(const std::string& c : cols) {
                if(c == "time") r.cells.push_back(short_time(row[1].i));
                else if(c == "user") r.cells.push_back(log_view_text(row[2].str(), 32));
                else if(c == "screen") r.cells.push_back(log_view_text(row[3].str(), 48));
                else if(c == "kind") r.cells.push_back(log_view_text(row[4].str(), 24));
                else if(c == "target") r.cells.push_back(log_view_text(row[5].str()));
                else if(c == "old") r.cells.push_back(log_view_text(row[6].str(), 64));
                else if(c == "new") r.cells.push_back(log_view_text(row[7].str(), 64));
                else r.cells.emplace_back();
            }
            page.rows.push_back(std::move(r));
        }
        return page;
    }

    if(q.source == "data") {
        if(!data_log().enabled()) { page.problem = "The data log is not recorded (\"data_log\" in project.json)."; return page; }
        const std::vector<std::string> groups = data_log().groups();
        if(std::find(groups.begin(), groups.end(), q.group) == groups.end()) {
            page.problem = q.group.empty() ? "No group of the data log is named (\"group\")." : "The data log has no group '" + log_view_text(q.group, 40) + "'.";
            return page;
        }
        const std::string where = "grp = ?1 AND (?2 = 0 OR ts >= ?2)";
        const LogRow binds{ q.group, from };
        LogDb& db = data_log().db();
        page.total = db.count(where, binds);
        for(const LogRow& row : db.query(where, binds, limit, true, offset)) {       // id, ts, grp, data
            if(row.size() < 4) continue;
            const json values = json::parse(row[3].str(), nullptr, false);
            LogViewRow r;
            for(const std::string& c : cols) {
                if(c == "time") r.cells.push_back(short_time(row[1].i));
                else if(values.is_object() && values.contains(c)) r.cells.push_back(log_view_text(json_cell(values[c]), 64));
                else r.cells.emplace_back();
            }
            page.rows.push_back(std::move(r));
        }
        return page;
    }

    page.problem = "\"source\" is system, operation or data.";
    return page;
}

} // namespace hml
