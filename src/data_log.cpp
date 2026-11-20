#include "data_log.hpp"
#include <array>
#include <climits>
#include <map>
#include "lvgl.h"
#include "tag.hpp"
#include "util.hpp"

namespace hml {
namespace {

enum class Trigger { Interval, Change, TagEdge, Manual };

struct Group {
    std::string name;
    Trigger     trigger = Trigger::Interval;
    uint32_t    interval_ms = 1000;         // Interval: period; Change: minimum time between samples
    std::vector<std::string> tag_names;     // as configured (also for a reader that has no tags)
    std::vector<Tag*> tags;
    lv_timer_t* timer = nullptr;
    bool        dirty = false;              // Change / TagEdge: sample at the next poll
    bool        edge_was_on = false;        // TagEdge: previous state of trigger_tag
    uint32_t    last_sample = 0;            // millis()
    bool        sampled = false;
    // the minute being summarised: tag -> min, max, last (numbers and bools)
    int64_t     minute = -1;
    std::map<std::string, std::array<double, 3>> acc;
};

} // namespace

struct DataLog::Impl {
    LogDb db{ "data log", "sample", { { "grp", "TEXT NOT NULL" }, { "data", "TEXT NOT NULL" } }, "grp, ts" };
    // One row per group and minute: { "tag": [min, max, last] }. A trend over a long time reads
    // these instead of every sample (same file, table "minute").
    LogDb sum{ "data log summary", "minute", { { "grp", "TEXT NOT NULL" }, { "data", "TEXT NOT NULL" } }, "grp, ts" };

    void flush_minute(Group& g)
    {
        if(g.minute < 0 || g.acc.empty() || !sum.is_open()) { g.acc.clear(); return; }
        json o = json::object();
        for(const auto& [k, v] : g.acc) o[k] = json::array({ v[0], v[1], v[2] });
        sum.add(g.minute, { g.name, o.dump() });
        g.acc.clear();
    }
    std::vector<std::unique_ptr<Group>> groups;
    lv_timer_t* poll = nullptr;             // samples the groups marked dirty by tag changes

    Group* find(const std::string& name)
    {
        for(auto& g : groups) if(g->name == name) return g.get();
        return nullptr;
    }

    void take(Group& g)
    {
        json o = json::object();
        for(Tag* t : g.tags) {
            Value v = t->get();
            switch(t->type()) {
                case TagType::Bool:   o[t->name()] = v.num != 0; break;
                case TagType::Int:    o[t->name()] = static_cast<int64_t>(v.num); break;
                case TagType::Float:  o[t->name()] = v.num; break;
                case TagType::String: o[t->name()] = v.str; break;
            }
        }
        const int64_t now = LogDb::now_ms(), minute = now / 60000 * 60000;
        if(minute != g.minute) { flush_minute(g); g.minute = minute; }
        for(Tag* t : g.tags) {
            if(t->type() == TagType::String) continue;
            const double x = t->type() == TagType::Bool ? (t->get().num != 0 ? 1 : 0) : t->get().num;
            auto it = g.acc.find(t->name());
            if(it == g.acc.end()) g.acc[t->name()] = { x, x, x };
            else { it->second[0] = std::min(it->second[0], x); it->second[1] = std::max(it->second[1], x); it->second[2] = x; }
        }
        db.add(now, { g.name, o.dump() });
        g.dirty = false;
        g.sampled = true;
        g.last_sample = millis();
    }

    void poll_dirty()
    {
        for(auto& g : groups) {
            if(!g->dirty) continue;
            if(g->trigger == Trigger::Change && g->sampled && millis() - g->last_sample < g->interval_ms) continue;
            take(*g);
        }
    }
};

DataLog::DataLog() : impl_(std::make_unique<Impl>()) {}
DataLog::~DataLog() = default;

void DataLog::clear()
{
    Impl& m = *impl_;
    if(lv_is_initialized()) {
        for(auto& g : m.groups) if(g->timer) lv_timer_delete(g->timer);
        if(m.poll) lv_timer_delete(m.poll);
    }
    m.poll = nullptr;
    if(!m.groups.empty()) tags().unsubscribe_group(SubGroup::DataLog);
    for(auto& g : m.groups) m.flush_minute(*g);              // (the minute so far)
    m.groups.clear();
    m.db.close();
    m.sum.close();
}

int DataLog::load(const json* section, const std::string& base_dir, bool read_only)
{
    Impl& m = *impl_;
    clear();
    LogDbConfig cfg = parse_log_config(section, base_dir, "logs/data.db", 1000000);
    if(!section || !section->is_object()) return 0;
    if(!cfg.enabled && !read_only) return 0;

    const json* list = jget(*section, "groups");
    if(list && list->is_array()) {
        for(const json& gj : *list) {
            auto g = std::make_unique<Group>();
            g->name = jstr(gj, "name", "");
            if(g->name.empty()) { HML_WARN("data log: group without \"name\" skipped"); continue; }
            if(m.find(g->name)) { HML_WARN("data log: group '%s' defined twice, keeping the first", g->name.c_str()); continue; }
            if(!jbool(gj, "enabled", true)) continue;
            if(const json* tl = jget(gj, "tags"); tl && tl->is_array()) {
                for(const json& tj : *tl) if(tj.is_string()) g->tag_names.push_back(tj.get<std::string>());
            }
            std::string trig = jstr(gj, "trigger", "interval");
            if(iequals(trig, "interval")) g->trigger = Trigger::Interval;
            else if(iequals(trig, "change")) g->trigger = Trigger::Change;
            else if(iequals(trig, "tag")) g->trigger = Trigger::TagEdge;
            else if(iequals(trig, "manual")) g->trigger = Trigger::Manual;
            else { HML_WARN("data log: group '%s': unknown trigger '%s' (interval | change | tag | manual)", g->name.c_str(), trig.c_str()); continue; }
            double ms = jnum(gj, "interval_ms", g->trigger == Trigger::Interval ? 1000 : 0);
            g->interval_ms = static_cast<uint32_t>(ms < 0 ? 0 : ms);
            if(g->trigger == Trigger::Interval && g->interval_ms < 100) g->interval_ms = 100;

            if(!read_only) {
                for(const std::string& n : g->tag_names) {
                    Tag* t = tags().find(n);
                    if(!t) { HML_WARN("data log: group '%s': unknown tag '%s'", g->name.c_str(), n.c_str()); continue; }
                    g->tags.push_back(t);
                }
                if(g->tags.empty()) { HML_WARN("data log: group '%s' has no tags, skipped", g->name.c_str()); continue; }
                Group* raw = g.get();
                if(g->trigger == Trigger::Interval) {
                    g->timer = lv_timer_create([](lv_timer_t* t) {
                        data_log().impl_->take(*static_cast<Group*>(lv_timer_get_user_data(t)));
                    }, g->interval_ms, raw);
                }
                else if(g->trigger == Trigger::Change) {
                    for(Tag* t : g->tags) t->subscribe(SubGroup::DataLog, [raw](Tag&, const Value&, const void*) { raw->dirty = true; });
                }
                else if(g->trigger == Trigger::TagEdge) {
                    std::string tn = jstr(gj, "trigger_tag", "");
                    Tag* t = tags().find(tn);
                    if(!t) { HML_WARN("data log: group '%s': unknown trigger_tag '%s'", g->name.c_str(), tn.c_str()); continue; }
                    raw->edge_was_on = t->get().truthy();
                    t->subscribe(SubGroup::DataLog, [raw](Tag&, const Value& v, const void*) {
                        bool on = v.truthy();
                        if(on && !raw->edge_was_on) raw->dirty = true;     // off -> on
                        raw->edge_was_on = on;
                    });
                }
            }
            m.groups.push_back(std::move(g));
        }
    }

    if(!m.db.open(cfg, read_only)) {
        clear();
        return 0;
    }
    LogDbConfig scfg = cfg;
    if(scfg.max_rows > 0) scfg.max_rows = std::max(1000, scfg.max_rows / 10);
    m.sum.open(scfg, read_only);
    if(!read_only) {
        m.poll = lv_timer_create([](lv_timer_t*) { data_log().impl_->poll_dirty(); }, 50, nullptr);
        HML_INFO("data log: %d group(s), recording to '%s'", static_cast<int>(m.groups.size()), cfg.path.c_str());
    }
    return static_cast<int>(m.groups.size());
}

bool DataLog::enabled() const { return impl_->db.is_open(); }
LogDb& DataLog::summary_db() { return impl_->sum; }
LogDb& DataLog::db() { return impl_->db; }

std::vector<std::string> DataLog::groups() const
{
    std::vector<std::string> out;
    for(const auto& g : impl_->groups) out.push_back(g->name);
    return out;
}

bool DataLog::sample(const std::string& group)
{
    Group* g = impl_->find(group);
    if(!g || g->tags.empty() || !impl_->db.is_open()) { HML_WARN("data log: cannot sample unknown group '%s'", group.c_str()); return false; }
    impl_->take(*g);
    return true;
}

std::vector<std::vector<TrendBucket>> DataLog::trend(const std::string& group, const std::vector<std::string>& tags,
                                                       int64_t from_ms, int64_t to_ms, int buckets)
{
    std::vector<std::vector<TrendBucket>> out(tags.size(), std::vector<TrendBucket>(static_cast<size_t>(std::max(0, buckets))));
    if(buckets <= 0 || to_ms <= from_ms || tags.empty()) return out;
    // Long windows (a slice of a minute or more) read the per-minute summary where it exists and
    // the samples only for what it does not cover yet (the current minute, data from before it).
    std::vector<std::vector<int64_t>> last_ts(tags.size(), std::vector<int64_t>(static_cast<size_t>(buckets), INT64_MIN));
    const bool use_sum = (to_ms - from_ms) / buckets >= 60000 && impl_->sum.is_open();
    std::pair<int64_t, int64_t> covered{ 0, 0 };
    if(use_sum) {
        auto rows = impl_->sum.select("MIN(ts), MAX(ts)", "grp = ?1 AND ts >= ?2 AND ts < ?3", { group, from_ms, to_ms });
        if(!rows.empty() && rows[0][0].type == LogCell::Int) covered = { rows[0][0].i, rows[0][1].i + 60000 };
    }
    if(covered.second > covered.first) {
        trend_part(true, group, tags, from_ms, to_ms, buckets, covered.first, covered.second, out, last_ts);
        if(covered.first > from_ms) trend_part(false, group, tags, from_ms, to_ms, buckets, from_ms, covered.first, out, last_ts);
        if(covered.second < to_ms) trend_part(false, group, tags, from_ms, to_ms, buckets, covered.second, to_ms, out, last_ts);
    }
    else trend_part(false, group, tags, from_ms, to_ms, buckets, from_ms, to_ms, out, last_ts);
    return out;
}

// One part [part_from, part_to) of a trend, from the samples or from the summary, merged into out.
// bucket = (ts - from) * buckets / (to - from). Two queries: MIN / MAX per bucket (the inner query
// takes each value out of the JSON once: ->> gives numbers, 1 / 0 for bools, text for text, which
// typeof() leaves out), and the newest row of each bucket for "last" (SQLite takes bare columns
// from the MAX(ts) row only when MAX is the one aggregate, so that is a query of its own; it
// reads no JSON, the few rows it returns are parsed here).
void DataLog::trend_part(bool summary, const std::string& group, const std::vector<std::string>& tags, int64_t from_ms, int64_t to_ms,
                         int buckets, int64_t part_from, int64_t part_to, std::vector<std::vector<TrendBucket>>& out,
                         std::vector<std::vector<int64_t>>& last_ts)
{
    const LogRow base_binds = { group, from_ms, to_ms, static_cast<int64_t>(buckets), part_from, part_to };
    const std::string table = summary ? "minute" : "sample";
    const std::string where = " FROM " + table + " WHERE grp = ?1 AND ts >= ?5 AND ts < ?6)";
    LogDb& db = summary ? impl_->sum : impl_->db;
    auto num = [](const LogCell& c) { return c.type == LogCell::Int ? static_cast<double>(c.i) : c.type == LogCell::Real ? c.d : 0.0; };

    LogRow binds = base_binds;
    std::string inner = "(SELECT (ts - ?2) * ?4 / (?3 - ?2) AS b";
    std::string cols = "b";
    for(size_t i = 0; i < tags.size(); i++) {
        // the path as a parameter: tag names never become SQL. '$."name"' also takes dots in names.
        std::string esc;
        for(char c : tags[i]) { if(c == '"' || c == '\\') esc += '\\'; esc += c; }
        const std::string path = "$.\"" + esc + "\"";
        auto col = [&](const std::string& p, const std::string& name) {
            binds.emplace_back(p);
            inner += ", data ->> ?" + std::to_string(binds.size()) + " AS " + name;
            return "CASE WHEN typeof(" + name + ") IN ('integer', 'real') THEN " + name + " END";
        };
        const std::string n = std::to_string(i);
        if(summary) cols += ", MIN(" + col(path + "[0]", "a" + n) + "), MAX(" + col(path + "[1]", "z" + n) + ")";
        else { const std::string v = col(path, "r" + n); cols += ", MIN(" + v + "), MAX(" + v + ")"; }
    }
    for(const LogRow& r : db.select(cols, "", binds, "GROUP BY b", inner + where)) {
        if(r.size() < 1 + tags.size() * 2 || r[0].type != LogCell::Int) continue;
        const int64_t b = r[0].i;
        if(b < 0 || b >= buckets) continue;
        for(size_t i = 0; i < tags.size(); i++) {
            const LogCell& mn = r[1 + i * 2];
            const LogCell& mx = r[2 + i * 2];
            if(mn.type == LogCell::Null || mx.type == LogCell::Null) continue;
            TrendBucket& t = out[i][static_cast<size_t>(b)];
            if(!t.has) { t.has = true; t.min = num(mn); t.max = num(mx); t.last = t.max; continue; }
            t.min = std::min(t.min, num(mn));
            t.max = std::max(t.max, num(mx));
        }
    }
    // the newest row of each bucket
    for(const LogRow& r : db.select("b, MAX(ts), data", "", base_binds, "GROUP BY b", "(SELECT ts, (ts - ?2) * ?4 / (?3 - ?2) AS b, data" + where)) {
        if(r.size() < 3 || r[0].type != LogCell::Int || r[1].type != LogCell::Int || r[2].type != LogCell::Text) continue;
        const int64_t b = r[0].i, ts = r[1].i;
        if(b < 0 || b >= buckets) continue;
        const json d = json::parse(r[2].s, nullptr, false);
        if(!d.is_object()) continue;
        for(size_t i = 0; i < tags.size(); i++) {
            TrendBucket& t = out[i][static_cast<size_t>(b)];
            int64_t& lt = last_ts[i][static_cast<size_t>(b)];
            if(!t.has || ts < lt) continue;
            const json* v = jget(d, tags[i].c_str());
            if(v && summary) v = v->is_array() && v->size() >= 3 ? &(*v)[2] : nullptr;
            if(!v) continue;
            if(v->is_number()) t.last = v->get<double>();
            else if(v->is_boolean()) t.last = v->get<bool>() ? 1 : 0;
            else continue;
            lt = ts;
        }
    }
}

std::pair<int64_t, int64_t> DataLog::time_range(const std::string& group)
{
    auto rows = impl_->db.select("MIN(ts), MAX(ts)", "grp = ?1", { group });
    if(rows.empty() || rows[0].size() < 2 || rows[0][0].type != LogCell::Int) return { 0, 0 };
    return { rows[0][0].i, rows[0][1].i };
}

TrendFill DataLog::trend_fill(const std::string& group) const
{
    for(const auto& g : impl_->groups) if(g->name == group) {
        // an interval may come late or miss one turn: up to 2.5 periods is still one line
        if(g->trigger == Trigger::Interval) return { TrendFill::Line, static_cast<int64_t>(g->interval_ms) * 5 / 2 };
        if(g->trigger == Trigger::Change) return { TrendFill::Hold, 0 };
    }
    return {};
}

void trend_fill_gaps(std::vector<std::vector<TrendBucket>>& data, int64_t from_ms, int64_t to_ms, const TrendFill& fill, int64_t now_ms)
{
    if(fill.mode == TrendFill::None || to_ms <= from_ms) return;
    for(auto& series : data) {
        const int64_t n = static_cast<int64_t>(series.size());
        if(n < 2) continue;
        const double slice = static_cast<double>(to_ms - from_ms) / static_cast<double>(n);
        int64_t prev = -1;                                     // the last slice with a sample
        for(int64_t k = 0; k < n; k++) {
            if(!series[static_cast<size_t>(k)].has) continue;
            if(prev >= 0 && k - prev > 1) {
                const TrendBucket a = series[static_cast<size_t>(prev)], b = series[static_cast<size_t>(k)];
                // (the two samples are less than k - prev + 1 slices apart)
                const bool line = fill.mode == TrendFill::Line && static_cast<double>(k - prev - 1) * slice < static_cast<double>(fill.max_gap_ms);
                if(line || fill.mode == TrendFill::Hold) for(int64_t j = prev + 1; j < k; j++) {
                    const double f = static_cast<double>(j - prev) / static_cast<double>(k - prev);
                    const double v = line ? a.last + (b.last - a.last) * f : a.last;
                    series[static_cast<size_t>(j)] = { true, v, v, v };
                }
            }
            prev = k;
        }
        // on change: the newest value still stands (up to now)
        if(fill.mode == TrendFill::Hold && prev >= 0) for(int64_t j = prev + 1; j < n; j++) {
            if(static_cast<double>(from_ms) + static_cast<double>(j) * slice > static_cast<double>(now_ms)) break;
            const double v = series[static_cast<size_t>(prev)].last;
            series[static_cast<size_t>(j)] = { true, v, v, v };
        }
    }
}

std::vector<std::string> DataLog::group_tags(const std::string& group) const
{
    for(const auto& g : impl_->groups) if(g->name == group) {
        std::vector<std::string> out;
        for(const std::string& n : g->tag_names) out.push_back(n);
        return out;
    }
    return {};
}

std::vector<DataSample> DataLog::query(const std::string& group, int limit, bool newest_first)
{
    std::vector<DataSample> out;
    for(const LogRow& r : impl_->db.query("grp = ?1", { group }, limit, newest_first)) {
        DataSample s;
        s.ts_ms = r[1].i;
        s.values = json::parse(r[3].s, nullptr, false);
        if(s.values.is_object()) out.push_back(std::move(s));
    }
    return out;
}

int64_t DataLog::export_csv(const std::string& group, std::string path)
{
    Impl& m = *impl_;
    if(!m.db.is_open()) { HML_ERROR("data log: export failed: the data log is not enabled"); return -1; }
    std::vector<DataSample> samples = query(group);
    // columns: the group's tags as configured, then anything else found in old rows
    std::vector<std::string> cols;
    if(Group* g = m.find(group)) cols = g->tag_names;
    auto has = [&cols](const std::string& k) { for(const std::string& c : cols) if(c == k) return true; return false; };
    for(const DataSample& s : samples) for(auto it = s.values.begin(); it != s.values.end(); ++it) if(!has(it.key())) cols.push_back(it.key());
    if(samples.empty() && !m.find(group)) { HML_ERROR("data log: export failed: unknown group '%s'", group.c_str()); return -1; }

    std::vector<std::string> header{ "time" };
    header.insert(header.end(), cols.begin(), cols.end());
    std::vector<std::vector<std::string>> rows;
    rows.reserve(samples.size());
    for(const DataSample& s : samples) {
        std::vector<std::string> row{ LogDb::format_time(s.ts_ms) };
        for(const std::string& c : cols) {
            const json* v = jget(s.values, c.c_str());
            if(!v || v->is_null()) row.emplace_back();
            else if(v->is_boolean()) row.emplace_back(v->get<bool>() ? "1" : "0");
            else if(v->is_string()) row.push_back(v->get<std::string>());
            else if(v->is_number_integer()) row.push_back(std::to_string(v->get<int64_t>()));
            else row.push_back(vformat("%.10g", v->get<double>()));
        }
        rows.push_back(std::move(row));
    }
    if(path.empty()) path = m.db.export_path("data-" + group);
    return LogDb::write_csv(path, header, rows);
}

DataLog& data_log()
{
    static DataLog instance;
    return instance;
}

} // namespace hml
