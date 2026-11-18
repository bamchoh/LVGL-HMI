#include "alarm.hpp"
#include <cstdlib>
#include "tag.hpp"
#include "util.hpp"

namespace hml {
namespace {

enum class Cond { True, False, Eq, Ne, Gt, Lt, Ge, Le };

struct Alarm {
    std::string name, message, severity;
    Tag*        tag = nullptr;
    Cond        cond = Cond::True;
    bool        value_is_str = false;
    double      value_num = 0;
    std::string value_str;
    Tag*        value_tag = nullptr;
    bool        active = false;

    bool evaluate() const
    {
        Value v = tag->get();
        if(cond == Cond::True) return v.truthy();
        if(cond == Cond::False) return !v.truthy();
        bool str = false;
        double a = v.as_number(), b = value_num;
        std::string sa, sb;
        if(value_tag) {
            Value o = value_tag->get();
            if(v.is_string() && o.is_string()) { str = true; sa = v.str; sb = o.str; }
            else b = o.as_number();
        }
        else if(value_is_str && v.is_string()) { str = true; sa = v.str; sb = value_str; }
        if(str) return cond == Cond::Eq ? sa == sb : cond == Cond::Ne ? sa != sb : false;
        switch(cond) {
            case Cond::Eq: return a == b;
            case Cond::Ne: return a != b;
            case Cond::Gt: return a > b;
            case Cond::Lt: return a < b;
            case Cond::Ge: return a >= b;
            case Cond::Le: return a <= b;
            default: return false;
        }
    }
};

bool parse_cond(const std::string& s, Cond& out)
{
    static const struct { const char* a; const char* b; Cond c; } tbl[] = {
        { "true", "on", Cond::True }, { "false", "off", Cond::False }, { "eq", "==", Cond::Eq }, { "ne", "!=", Cond::Ne },
        { "gt", ">", Cond::Gt }, { "lt", "<", Cond::Lt }, { "ge", ">=", Cond::Ge }, { "le", "<=", Cond::Le },
    };
    for(const auto& t : tbl) if(iequals(s, t.a) || s == t.b) { out = t.c; return true; }
    return false;
}

} // namespace

struct Alarms::Impl {
    LogDb db{ "alarm log", "alarm", { { "name", "TEXT NOT NULL" }, { "event", "TEXT NOT NULL" }, { "severity", "TEXT NOT NULL" },
                                      { "message", "TEXT NOT NULL" }, { "value", "TEXT" } }, "name, ts" };
    std::vector<std::unique_ptr<Alarm>> items;

    void update(Alarm& a)
    {
        bool now = a.evaluate();
        if(now == a.active) return;
        a.active = now;
        db.add({ a.name, now ? "raised" : "cleared", a.severity, a.message, a.tag->get().format() });
    }
};

Alarms::Alarms() : impl_(std::make_unique<Impl>()) {}
Alarms::~Alarms() = default;

void Alarms::clear()
{
    Impl& m = *impl_;
    if(!m.items.empty()) tags().unsubscribe_group(SubGroup::Alarm);
    m.items.clear();
    m.db.close();
}

int Alarms::load(const json* section, const std::string& base_dir, bool read_only)
{
    Impl& m = *impl_;
    clear();
    LogDbConfig cfg = parse_log_config(section, base_dir, "logs/alarm.db", 100000);
    if(!section || !section->is_object()) return 0;
    if(read_only) return m.db.open(cfg, true) ? 1 : 0;
    if(!cfg.enabled) return 0;
    if(!m.db.open(cfg)) return 0;

    const json* list = jget(*section, "items");
    if(list && list->is_array()) {
        for(const json& aj : *list) {
            auto a = std::make_unique<Alarm>();
            a->name = jstr(aj, "name", "");
            if(a->name.empty()) { HML_WARN("alarm without \"name\" skipped"); continue; }
            bool dup = false;
            for(const auto& o : m.items) dup = dup || o->name == a->name;
            if(dup) { HML_WARN("alarm '%s' defined twice, keeping the first", a->name.c_str()); continue; }
            if(!jbool(aj, "enabled", true)) continue;
            std::string tn = jstr(aj, "tag", "");
            a->tag = tags().find(tn);
            if(!a->tag) { HML_WARN("alarm '%s': unknown tag '%s'", a->name.c_str(), tn.c_str()); continue; }
            std::string cond = jstr(aj, "condition", "true");
            if(!parse_cond(cond, a->cond)) { HML_WARN("alarm '%s': unknown condition '%s' (true | false | eq | ne | gt | lt | ge | le)", a->name.c_str(), cond.c_str()); continue; }
            if(auto vt = jstr_opt(aj, "value_tag"); vt && !vt->empty()) {
                a->value_tag = tags().find(*vt);
                if(!a->value_tag) { HML_WARN("alarm '%s': unknown value_tag '%s'", a->name.c_str(), vt->c_str()); continue; }
            }
            else if(const json* v = jget(aj, "value")) {
                if(v->is_string()) { a->value_is_str = true; a->value_str = v->get<std::string>(); a->value_num = std::atof(a->value_str.c_str()); }
                else a->value_num = jscalar_to_number(*v);
            }
            a->message = jstr(aj, "message", a->name);
            a->severity = jstr(aj, "severity", "warning");

            Alarm* raw = a.get();
            Impl* mp = &m;
            a->tag->subscribe(SubGroup::Alarm, [mp, raw](Tag&, const Value&, const void*) { mp->update(*raw); });
            if(a->value_tag) a->value_tag->subscribe(SubGroup::Alarm, [mp, raw](Tag&, const Value&, const void*) { mp->update(*raw); });
            m.items.push_back(std::move(a));
        }
    }
    for(auto& a : m.items) m.update(*a);        // alarms that are active from the start
    HML_INFO("alarms: %d defined, recording to '%s'", static_cast<int>(m.items.size()), cfg.path.c_str());
    return static_cast<int>(m.items.size());
}

bool Alarms::enabled() const { return impl_->db.is_open(); }
size_t Alarms::count() const { return impl_->items.size(); }
LogDb& Alarms::db() { return impl_->db; }

bool Alarms::active(const std::string& name) const
{
    for(const auto& a : impl_->items) if(a->name == name) return a->active;
    return false;
}

std::vector<std::string> Alarms::active_names() const
{
    std::vector<std::string> out;
    for(const auto& a : impl_->items) if(a->active) out.push_back(a->name);
    return out;
}

std::vector<AlarmEvent> Alarms::query(int limit, bool newest_first)
{
    std::vector<AlarmEvent> out;
    for(const LogRow& r : impl_->db.query("", {}, limit, newest_first)) {
        out.push_back({ r[1].i, r[2].s, r[3].s, r[4].s, r[5].s, r[6].str() });
    }
    return out;
}

int64_t Alarms::export_csv(std::string path)
{
    if(!impl_->db.is_open()) { HML_ERROR("alarm log: export failed: no alarms are defined"); return -1; }
    std::vector<std::vector<std::string>> rows;
    for(const AlarmEvent& e : query()) rows.push_back({ LogDb::format_time(e.ts_ms), e.name, e.event, e.severity, e.message, e.value });
    if(path.empty()) path = impl_->db.export_path("alarm-log");
    return LogDb::write_csv(path, { "time", "name", "event", "severity", "message", "value" }, rows);
}

Alarms& alarms()
{
    static Alarms instance;
    return instance;
}

} // namespace hml
