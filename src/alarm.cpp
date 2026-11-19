#include "alarm.hpp"
#include <algorithm>
#include <cstdlib>
#include <tuple>
#include "tag.hpp"
#include "texts.hpp"
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
    bool        need_ack = true;

    // state
    bool        active = false;
    bool        acked = true;
    int64_t     raised_ms = 0, cleared_ms = 0, acked_ms = 0;
    uint64_t    seq = 0;                    // order of the raises (several can share a millisecond)
    std::string acked_by, value;

    bool current() const { return active || !acked; }

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

    AlarmStatus status() const
    {
        AlarmStatus s;
        s.name = name; s.message = texts().resolve(message); s.severity = severity; s.value = value; s.acked_by = acked_by;
        s.active = active; s.acked = acked; s.need_ack = need_ack;
        s.raised_ms = raised_ms; s.cleared_ms = cleared_ms; s.acked_ms = acked_ms; s.seq = seq;
        return s;
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

void set_sys_tag(const std::string& name, TagType type, const Value& v)
{
    Tag* t = tags().find(name);
    if(!t) tags().create(name, type, &v, false, 0, 0);
    else t->set(v, nullptr);
}

AlarmEvent to_event(const LogRow& r)
{
    // [id, ts, name, event, severity, message, value, user]
    return { r[1].i, r[2].s, r[3].s, r[4].s, r[5].s, r.size() > 6 ? r[6].str() : "", r.size() > 7 ? r[7].str() : "" };
}

} // namespace

struct Alarms::Impl {
    LogDb db{ "alarm log", "alarm", { { "name", "TEXT NOT NULL" }, { "event", "TEXT NOT NULL" }, { "severity", "TEXT NOT NULL" },
                                      { "message", "TEXT NOT NULL" }, { "value", "TEXT" }, { "user", "TEXT" } }, "name, ts" };
    std::vector<std::unique_ptr<Alarm>> items;
    uint64_t gen = 1;
    uint64_t seq = 0;
    int      ack_level = 0;

    void record(Alarm& a, int64_t ts, const char* event, const std::string& user = "")
    {
        db.add(ts, { a.name, event, a.severity, texts().resolve(a.message), a.value, user });
    }

    void changed()
    {
        gen++;
        size_t active = 0, unacked = 0;
        const Alarm* latest = nullptr;           // newest unacknowledged, else newest active
        for(const auto& a : items) {
            if(a->active) active++;
            if(!a->acked) unacked++;
            if(!a->current()) continue;
            auto rank = [](const Alarm* x) { return std::make_tuple(!x->acked ? 1 : 0, x->raised_ms, x->seq); };
            if(!latest || rank(a.get()) > rank(latest)) latest = a.get();
        }
        set_sys_tag("sys.alarm.active", TagType::Int, Value::number(static_cast<double>(active), TagType::Int));
        set_sys_tag("sys.alarm.unacked", TagType::Int, Value::number(static_cast<double>(unacked), TagType::Int));
        set_sys_tag("sys.alarm.latest", TagType::String, Value::string(latest ? texts().resolve(latest->message) : ""));
        set_sys_tag("sys.alarm.latest_severity", TagType::String, Value::string(latest ? latest->severity : ""));
    }

    void update(Alarm& a)
    {
        bool now = a.evaluate();
        if(now == a.active) return;
        const int64_t ts = LogDb::now_ms();
        a.active = now;
        a.value = a.tag->get().format();
        if(now) {
            a.raised_ms = ts;
            a.seq = ++seq;
            a.acked = !a.need_ack;              // a new occurrence has to be acknowledged again
            a.acked_ms = 0;
            a.acked_by.clear();
        }
        else a.cleared_ms = ts;
        record(a, ts, now ? "raised" : "cleared");
        changed();
    }

    // The state at the end of the last run, from its newest events. After the newest "raised"
    // there can only be one "cleared" and one "acked", so a few rows are enough.
    void restore(Alarm& a)
    {
        a.active = false; a.acked = true;
        std::vector<LogRow> rows = db.query("name = ?1", { a.name }, 4, true);
        bool cleared = false, acked = false;
        for(const LogRow& r : rows) {
            AlarmEvent e = to_event(r);
            if(e.event == "cleared") { if(!cleared) { cleared = true; a.cleared_ms = e.ts_ms; } }
            else if(e.event == "acked") { if(!acked) { acked = true; a.acked_ms = e.ts_ms; a.acked_by = e.user; } }
            else if(e.event == "raised") {
                a.active = !cleared;
                a.acked = acked || !a.need_ack;
                a.raised_ms = e.ts_ms;
                a.value = e.value;
                return;
            }
        }
        a.cleared_ms = a.acked_ms = 0; a.acked_by.clear();      // no raise found: nothing pending
    }

    // load(): bring the restored state up to date with the tags without recording what is unchanged
    void start(Alarm& a)
    {
        const bool now = a.evaluate();
        if(now == a.active) return;
        update(a);
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
    m.ack_level = 0;
    m.gen++;
}

int Alarms::load(const json* section, const std::string& base_dir, bool read_only)
{
    Impl& m = *impl_;
    clear();
    LogDbConfig cfg = parse_log_config(section, base_dir, "logs/alarm.db", 100000);
    if(read_only) return section && section->is_object() && m.db.open(cfg, true) ? 1 : 0;
    m.changed();                                // the sys.alarm.* tags exist even without alarms
    if(!section || !section->is_object()) return 0;
    if(!cfg.enabled) return 0;
    if(!m.db.open(cfg)) return 0;
    m.ack_level = std::max(0, static_cast<int>(jnum(*section, "ack_level", 0)));

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
            a->need_ack = jbool(aj, "ack", true);

            Alarm* raw = a.get();
            Impl* mp = &m;
            a->tag->subscribe(SubGroup::Alarm, [mp, raw](Tag&, const Value&, const void*) { mp->update(*raw); });
            if(a->value_tag) a->value_tag->subscribe(SubGroup::Alarm, [mp, raw](Tag&, const Value&, const void*) { mp->update(*raw); });
            m.items.push_back(std::move(a));
        }
    }
    for(auto& a : m.items) m.restore(*a);
    for(auto& a : m.items) m.start(*a);
    m.changed();
    HML_INFO("alarms: %d defined, recording to '%s'", static_cast<int>(m.items.size()), cfg.path.c_str());
    return static_cast<int>(m.items.size());
}

bool Alarms::enabled() const { return impl_->db.is_open(); }
size_t Alarms::count() const { return impl_->items.size(); }
LogDb& Alarms::db() { return impl_->db; }
uint64_t Alarms::generation() const { return impl_->gen; }
int Alarms::ack_level() const { return impl_->ack_level; }

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

std::vector<AlarmStatus> Alarms::current() const
{
    std::vector<AlarmStatus> out;
    for(const auto& a : impl_->items) if(a->current()) out.push_back(a->status());
    std::stable_sort(out.begin(), out.end(), [](const AlarmStatus& x, const AlarmStatus& y) {
        return x.raised_ms != y.raised_ms ? x.raised_ms > y.raised_ms : x.seq > y.seq;
    });
    return out;
}

std::optional<AlarmStatus> Alarms::status(const std::string& name) const
{
    for(const auto& a : impl_->items) if(a->name == name) return a->status();
    return std::nullopt;
}

size_t Alarms::active_count() const { size_t n = 0; for(const auto& a : impl_->items) n += a->active ? 1 : 0; return n; }
size_t Alarms::unacked_count() const { size_t n = 0; for(const auto& a : impl_->items) n += a->acked ? 0 : 1; return n; }

std::vector<std::string> Alarms::severities() const
{
    std::vector<std::string> out;
    for(const auto& a : impl_->items) if(std::find(out.begin(), out.end(), a->severity) == out.end()) out.push_back(a->severity);
    return out;
}

bool Alarms::ack(const std::string& name, const std::string& user)
{
    Impl& m = *impl_;
    for(auto& a : m.items) {
        if(a->name != name) continue;
        if(a->acked) return false;
        a->acked = true;
        a->acked_ms = LogDb::now_ms();
        a->acked_by = user;
        m.record(*a, a->acked_ms, "acked", user);
        m.changed();
        return true;
    }
    return false;
}

int Alarms::ack_all(const std::string& user)
{
    std::vector<std::string> names;
    for(const auto& a : impl_->items) if(!a->acked) names.push_back(a->name);
    int n = 0;
    for(const std::string& name : names) n += ack(name, user) ? 1 : 0;
    return n;
}

std::vector<AlarmEvent> Alarms::query(int limit, bool newest_first)
{
    std::vector<AlarmEvent> out;
    for(const LogRow& r : impl_->db.query("", {}, limit, newest_first)) out.push_back(to_event(r));
    return out;
}

namespace {
// the WHERE of a history query; values go in as parameters
std::string history_where(const std::string& severity, int64_t since_ms, LogRow& binds)
{
    std::string where;
    if(!severity.empty()) { binds.emplace_back(severity); where = "severity = ?" + std::to_string(binds.size()); }
    if(since_ms > 0) { binds.emplace_back(since_ms); where += (where.empty() ? "" : " AND ") + std::string("ts >= ?") + std::to_string(binds.size()); }
    return where;
}
} // namespace

std::vector<AlarmEvent> Alarms::history(int limit, int offset, const std::string& severity, int64_t since_ms)
{
    std::vector<AlarmEvent> out;
    if(limit <= 0) return out;
    LogRow binds;
    std::string where = history_where(severity, since_ms, binds);
    for(const LogRow& r : impl_->db.query(where, binds, limit, true, std::max(0, offset))) out.push_back(to_event(r));
    return out;
}

int64_t Alarms::history_count(const std::string& severity, int64_t since_ms)
{
    LogRow binds;
    std::string where = history_where(severity, since_ms, binds);
    return impl_->db.count(where, binds);
}

int64_t Alarms::export_csv(std::string path)
{
    if(!impl_->db.is_open()) { HML_ERROR("alarm log: export failed: no alarms are defined"); return -1; }
    std::vector<std::vector<std::string>> rows;
    for(const AlarmEvent& e : query()) rows.push_back({ LogDb::format_time(e.ts_ms), e.name, e.event, e.severity, e.message, e.value, e.user });
    if(path.empty()) path = impl_->db.export_path("alarm-log");
    return LogDb::write_csv(path, { "time", "name", "event", "severity", "message", "value", "user" }, rows);
}

Alarms& alarms()
{
    static Alarms instance;
    return instance;
}

std::string alarm_text_line(const std::string& s, size_t max_bytes)
{
    std::string out;
    out.reserve(std::min(s.size(), max_bytes));
    for(unsigned char c : s) out.push_back(c < 0x20 || c == 0x7f ? ' ' : static_cast<char>(c));
    if(out.size() <= max_bytes) return out;
    size_t cut = max_bytes;
    while(cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;     // not inside a UTF-8 character
    return out.substr(0, cut) + "\xE2\x80\xA6";
}

} // namespace hml
