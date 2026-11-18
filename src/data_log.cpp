#include "data_log.hpp"
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
};

} // namespace

struct DataLog::Impl {
    LogDb db{ "data log", "sample", { { "grp", "TEXT NOT NULL" }, { "data", "TEXT NOT NULL" } }, "grp, ts" };
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
        db.add({ g.name, o.dump() });
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
    m.groups.clear();
    m.db.close();
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
    if(!read_only) {
        m.poll = lv_timer_create([](lv_timer_t*) { data_log().impl_->poll_dirty(); }, 50, nullptr);
        HML_INFO("data log: %d group(s), recording to '%s'", static_cast<int>(m.groups.size()), cfg.path.c_str());
    }
    return static_cast<int>(m.groups.size());
}

bool DataLog::enabled() const { return impl_->db.is_open(); }
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
