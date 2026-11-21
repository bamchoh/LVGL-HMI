#include "data_transfer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include <lvgl.h>

#include "driver.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {

const char* transfer_state_text(TransferState s)
{
    switch(s) {
        case TransferState::Ok:             return "ok";
        case TransferState::Disabled:       return "off";
        case TransferState::Ring:           return "ring: not run";
        case TransferState::NoTag:          return "unknown tag";
        case TransferState::SourceOffline:  return "source offline";
        case TransferState::SourceWait:     return "source: reading";
        case TransferState::DestOffline:    return "destination offline";
        case TransferState::DestWait:       return "destination: reading";
        case TransferState::ConditionFalse: return "condition not met";
        case TransferState::BadValue:       return "not a number";
        case TransferState::OutOfRange:     return "out of range";
    }
    return "?";
}

// ================================================================ parsing

namespace {

bool parse_when(const json& w, TransferCond& c, std::string* err)
{
    auto fail = [&](const char* why) { if(err) *err = why; return false; };
    if(w.is_string()) {
        c.tag = w.get<std::string>();
        if(c.tag.empty()) return fail("\"when\" names no tag");
        c.op = TransferCond::Op::Truthy;
        return true;
    }
    if(!w.is_object()) return fail("\"when\" is a tag, or { \"tag\": ..., \"eq\": ... }");
    c.tag = jstr(w, "tag", "");
    if(c.tag.empty()) return fail("\"when\" names no tag");
    static const std::pair<const char*, TransferCond::Op> ops[] = { { "eq", TransferCond::Op::Eq }, { "ne", TransferCond::Op::Ne }, { "gt", TransferCond::Op::Gt },
                                                                     { "lt", TransferCond::Op::Lt }, { "ge", TransferCond::Op::Ge }, { "le", TransferCond::Op::Le } };
    int found = 0;
    for(const auto& [key, op] : ops) {
        const json* v = jget(w, key);
        if(!v) continue;
        found++;
        c.op = op;
        if(v->is_number()) c.number = v->get<double>();
        else if(v->is_boolean()) c.number = v->get<bool>() ? 1 : 0;
        else if(v->is_string()) { c.text = v->get<std::string>(); c.is_text = true; }
        else if(v->is_object() && jget(*v, "tag") && (*v)["tag"].is_string() && !(*v)["tag"].get<std::string>().empty()) c.other_tag = (*v)["tag"].get<std::string>();
        else return fail("\"when\": the value to compare with is a number, a text or { \"tag\": ... }");
    }
    if(found > 1) return fail("\"when\" has more than one comparison");
    if(found == 0) c.op = TransferCond::Op::Truthy;
    for(auto it = w.begin(); it != w.end(); ++it) {
        const std::string& k = it.key();
        if(k != "tag" && k != "eq" && k != "ne" && k != "gt" && k != "lt" && k != "ge" && k != "le") return fail("\"when\" has a key that is no comparison (eq ne gt lt ge le)");
    }
    return true;
}

// a number out of a text: all of it, or nothing
bool text_number(const std::string& s, double& out)
{
    const char* b = s.c_str();
    while(*b == ' ' || *b == '\t') b++;
    if(!*b) return false;
    char* end = nullptr;
    const double v = std::strtod(b, &end);
    while(*end == ' ' || *end == '\t') end++;
    if(end == b || *end) return false;
    out = v;
    return true;
}

} // namespace

bool transfer_parse(const json& j, TransferItem& out, std::string* err)
{
    auto fail = [&](const std::string& why) { if(err) *err = why; return false; };
    if(!j.is_object()) return fail("a transfer is { \"name\": ..., \"from\": <tag>, \"to\": <tag> }");
    TransferItem t;
    t.name = jstr(j, "name", "");
    if(t.name.empty()) return fail("no \"name\"");
    if(t.name.size() > 64) return fail("the name is longer than 64 characters");
    const json* f = jget(j, "from");
    const json* to = jget(j, "to");
    if(!f || !f->is_string() || f->get<std::string>().empty()) return fail("\"from\" is the tag the value is taken from");
    if(!to || !to->is_string() || to->get<std::string>().empty()) return fail("\"to\" is the tag the value is written to");
    t.from = f->get<std::string>();
    t.to = to->get<std::string>();
    if(t.from == t.to) return fail("\"from\" and \"to\" are the same tag");
    if(t.to.rfind("sys.", 0) == 0) return fail("\"to\" is a tag of the runtime (sys.*): a transfer does not write those");
    const std::string trig = jstr(j, "trigger", "change");
    if(trig == "cyclic") t.cyclic = true;
    else if(trig != "change") return fail("\"trigger\" is \"change\" or \"cyclic\"");
    if(const json* e = jget(j, "every_ms")) {
        if(!e->is_number() || !(e->get<double>() >= 100) || e->get<double>() > 86400000.0) return fail("\"every_ms\" is 100 to 86400000");
        t.every_ms = static_cast<uint32_t>(e->get<double>());
    }
    for(const char* key : { "scale", "offset" })
        if(const json* v = jget(j, key)) {
            if(!v->is_number() || !std::isfinite(v->get<double>())) return fail(std::string("\"") + key + "\" is a number");
            (std::string(key) == "scale" ? t.scale : t.offset) = v->get<double>();
        }
    const std::string oor = jstr(j, "out_of_range", "skip");
    if(oor == "clamp") t.clamp = true;
    else if(oor != "skip") return fail("\"out_of_range\" is \"skip\" or \"clamp\"");
    if(const json* e = jget(j, "enabled")) {
        if(!e->is_boolean()) return fail("\"enabled\" is true or false");
        t.enabled = e->get<bool>();
    }
    if(const json* w = jget(j, "when")) {
        std::string why;
        if(!parse_when(*w, t.when, &why)) return fail(why);
    }
    out = std::move(t);
    return true;
}

std::vector<std::string> transfer_rings(const std::vector<TransferItem>& items)
{
    // tag -> the tags it is written to
    std::map<std::string, std::vector<std::string>> next;
    for(const TransferItem& t : items) next[t.from].push_back(t.to);
    std::vector<std::string> out;
    for(const TransferItem& t : items) {
        // from "to", following the transfers: is "from" reached?
        std::set<std::string> seen;
        std::vector<std::string> todo{ t.to };
        bool ring = false;
        while(!todo.empty() && !ring) {
            const std::string tag = todo.back();
            todo.pop_back();
            if(tag == t.from) { ring = true; break; }
            if(!seen.insert(tag).second) continue;
            auto it = next.find(tag);
            if(it != next.end()) for(const std::string& n : it->second) todo.push_back(n);
        }
        if(ring) out.push_back(t.name);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> transfer_tag_drivers(const json* drivers)
{
    std::vector<std::pair<std::string, std::string>> out;
    if(!drivers || !drivers->is_array()) return out;
    for(const json& d : *drivers) {
        if(!d.is_object() || !jbool(d, "enabled", true)) continue;
        const std::string type = jstr(d, "type", "");
        if(type.empty() || type == "sim" || type == "opcua_server") continue;        // (no connection to lose)
        const std::string name = jstr(d, "name", type);
        for(auto it = d.begin(); it != d.end(); ++it) {
            if(!it.value().is_array()) continue;
            for(const json& row : it.value()) {
                if(row.is_object() && row.contains("tag") && row["tag"].is_string()) out.emplace_back(row["tag"].get<std::string>(), name);
                else if(row.is_string() && (it.key() == "tags" || it.key() == "publish" || it.key() == "subscribe")) out.emplace_back(row.get<std::string>(), name);
            }
        }
    }
    return out;
}

// ================================================================ running

namespace {

// one end of a transfer: its tag, its driver, and whether what the tag holds was read after the
// last (re)connection
struct End {
    Tag*     tag = nullptr;
    Driver*  driver = nullptr;
    uint64_t ready_epoch = 0;           // the connection for which the values are known to be new
    bool     ready = false;
    uint64_t seen_epoch = 0, polls_then = 0;
    uint32_t since = 0;
    bool     waiting = false, updated = false;

    enum Now { Up, Offline, Wait };
    Now look(uint32_t now)
    {
        if(!driver) return Up;
        const int st = driver->link_state();
        if(st < 0) return Up;
        if(st == 0) { ready = false; waiting = false; return Offline; }
        const uint64_t epoch = driver->link_epoch();
        if(ready && epoch == ready_epoch) return Up;
        if(!waiting || epoch != seen_epoch) {           // a (new) connection: from here on we wait for a fresh read
            waiting = true;
            ready = false;
            seen_epoch = epoch;
            polls_then = driver->link_polls();
            since = now;
            updated = false;
        }
        // read in cycles: a whole round was finished since (it began after the connection came);
        // else: a value came from the device, or time passed
        const bool fresh = driver->link_polls_cyclically() ? driver->link_polls() > polls_then : (updated || now - since >= kTransferSettleMs);
        if(!fresh) return Wait;
        waiting = false;
        ready = true;
        ready_epoch = epoch;
        return Up;
    }
};

struct Running {
    End  src, dst;
    Tag* when_tag = nullptr;
    Tag* when_other = nullptr;
    bool ring = false;
    bool pending = true;                // something to write as soon as it can be
    bool was_blocked = true;
    uint32_t last = 0;
    bool wrote_once = false;
    TransferState problem = TransferState::Ok;      // a value that could not be written (until the next one)
};

bool cond_holds(const TransferCond& c, const Running& r)
{
    if(c.op == TransferCond::Op::None) return true;
    if(!r.when_tag) return false;
    const Value v = r.when_tag->get();
    if(c.op == TransferCond::Op::Truthy) return v.truthy();
    if(c.is_text && c.other_tag.empty()) {
        const std::string s = v.is_string() ? v.str : v.format();
        double a = 0, b = 0;
        if(!v.is_string() && text_number(c.text, b)) { a = v.as_number(); }           // "eq": "3" on a number
        else {
            const int cmp = s.compare(c.text);
            switch(c.op) {
                case TransferCond::Op::Eq: return cmp == 0;
                case TransferCond::Op::Ne: return cmp != 0;
                case TransferCond::Op::Gt: return cmp > 0;
                case TransferCond::Op::Lt: return cmp < 0;
                case TransferCond::Op::Ge: return cmp >= 0;
                case TransferCond::Op::Le: return cmp <= 0;
                default: return false;
            }
        }
        switch(c.op) {
            case TransferCond::Op::Eq: return a == b;
            case TransferCond::Op::Ne: return a != b;
            case TransferCond::Op::Gt: return a > b;
            case TransferCond::Op::Lt: return a < b;
            case TransferCond::Op::Ge: return a >= b;
            case TransferCond::Op::Le: return a <= b;
            default: return false;
        }
    }
    double b = c.number;
    if(!c.other_tag.empty()) {
        if(!r.when_other) return false;
        b = r.when_other->get().as_number();
    }
    const double a = v.as_number();
    if(std::isnan(a) || std::isnan(b)) return false;
    switch(c.op) {
        case TransferCond::Op::Eq: return a == b;
        case TransferCond::Op::Ne: return a != b;
        case TransferCond::Op::Gt: return a > b;
        case TransferCond::Op::Lt: return a < b;
        case TransferCond::Op::Ge: return a >= b;
        case TransferCond::Op::Le: return a <= b;
        default: return false;
    }
}

} // namespace

struct Transfers::Impl {
    std::vector<TransferItem> items;
    std::vector<Running> run;
    lv_timer_t* timer = nullptr;
    std::string summary;                // the last line written to the log
    uint32_t summary_at = 0;

    // what to write, or why not
    TransferState value_for(const TransferItem& t, const Running& r, Value& out) const
    {
        const Value v = r.src.tag->get();
        const TagType dt = r.dst.tag->type();
        if(dt == TagType::String) {
            if(v.is_string()) { out = v; return TransferState::Ok; }
            const double n = v.num * t.scale + t.offset;
            if(!std::isfinite(n)) return TransferState::BadValue;
            out = Value::string(Value::number(n, t.scale == 1 && t.offset == 0 ? v.type : TagType::Float).format());
            return TransferState::Ok;
        }
        double n = 0;
        if(v.is_string()) { if(!text_number(v.str, n)) return TransferState::BadValue; }
        else n = v.num;
        if(!std::isfinite(n)) return TransferState::BadValue;
        n = n * t.scale + t.offset;
        if(!std::isfinite(n)) return TransferState::BadValue;
        if(dt == TagType::Bool) { out = Value::number(n != 0 ? 1 : 0, TagType::Bool); return TransferState::Ok; }
        if(dt == TagType::Int) n = std::round(n);
        // the limits of the destination tag; an integer has those of what a double holds exactly
        double lo = dt == TagType::Int ? -9007199254740992.0 : -1e308, hi = -lo, tmin = 0, tmax = 0;
        if(r.dst.tag->limits(tmin, tmax)) { lo = std::max(lo, tmin); hi = std::min(hi, tmax); }
        if(n < lo || n > hi) {
            if(!t.clamp) return TransferState::OutOfRange;
            n = std::clamp(n, lo, hi);
        }
        out = Value::number(n, dt);
        return TransferState::Ok;
    }
};

Transfers::Transfers() : impl_(std::make_unique<Impl>()) {}
Transfers::~Transfers() = default;

void Transfers::clear()
{
    Impl& m = *impl_;
    tags().unsubscribe_group(SubGroup::Transfer);
    if(m.timer && lv_is_initialized()) lv_timer_delete(m.timer);
    m.timer = nullptr;
    m.items.clear();
    m.run.clear();
    m.summary.clear();
}

int Transfers::load(const json* section, const json* drivers_cfg, DriverRegistry& registry)
{
    clear();
    Impl& m = *impl_;
    if(!section) return 0;
    if(!section->is_array()) { HML_WARN("transfers: a list is needed"); return 0; }
    std::set<std::string> names;
    for(const json& j : *section) {
        if(m.items.size() >= kTransferMax) { HML_WARN("transfers: more than %zu, the rest is left out", kTransferMax); break; }
        TransferItem t;
        std::string err;
        if(!transfer_parse(j, t, &err)) { HML_WARN("transfers: an entry is left out: %s", err.c_str()); continue; }
        if(!names.insert(t.name).second) { HML_WARN("transfers: '%s' is defined twice: the second is left out", t.name.c_str()); continue; }
        m.items.push_back(std::move(t));
    }
    // the drivers the tags belong to
    std::map<std::string, Driver*> by_name;
    for(Driver* d : registry.all()) by_name[d->name()] = d;
    std::map<std::string, std::string> owner;
    for(const auto& [tag, drv] : transfer_tag_drivers(drivers_cfg)) owner.emplace(tag, drv);
    const std::vector<std::string> rings = transfer_rings(m.items);
    m.run.resize(m.items.size());
    for(size_t i = 0; i < m.items.size(); i++) {
        TransferItem& t = m.items[i];
        Running& r = m.run[i];
        r.src.tag = tags().find(t.from);
        r.dst.tag = tags().find(t.to);
        if(!r.src.tag) HML_WARN("transfer '%s': unknown tag '%s'", t.name.c_str(), t.from.c_str());
        if(!r.dst.tag) HML_WARN("transfer '%s': unknown tag '%s'", t.name.c_str(), t.to.c_str());
        auto drv = [&](const std::string& tag, std::string& name_out) -> Driver* {
            auto o = owner.find(tag);
            if(o == owner.end()) return nullptr;
            name_out = o->second;
            auto d = by_name.find(o->second);
            return d == by_name.end() ? nullptr : d->second;
        };
        r.src.driver = drv(t.from, t.from_driver);
        r.dst.driver = drv(t.to, t.to_driver);
        r.ring = std::find(rings.begin(), rings.end(), t.name) != rings.end();
        if(r.ring) HML_WARN("transfer '%s' (%s -> %s) is part of a ring of transfers: it is not run", t.name.c_str(), t.from.c_str(), t.to.c_str());
        if(t.when.op != TransferCond::Op::None) {
            r.when_tag = tags().find(t.when.tag);
            if(!r.when_tag) HML_WARN("transfer '%s': unknown tag '%s' in \"when\" (it never runs)", t.name.c_str(), t.when.tag.c_str());
            if(!t.when.other_tag.empty()) {
                r.when_other = tags().find(t.when.other_tag);
                if(!r.when_other) HML_WARN("transfer '%s': unknown tag '%s' in \"when\" (it never runs)", t.name.c_str(), t.when.other_tag.c_str());
            }
        }
        if(r.src.tag) r.src.tag->subscribe(SubGroup::Transfer, [this, i](Tag&, const Value&, const void* source) {
            if(i >= impl_->run.size()) return;
            Running& rr = impl_->run[i];
            rr.pending = true;
            rr.problem = TransferState::Ok;
            if(source == rr.src.driver) rr.src.updated = true;
        });
        if(r.dst.tag) r.dst.tag->subscribe(SubGroup::Transfer, [this, i](Tag&, const Value&, const void* source) {
            if(i >= impl_->run.size()) return;
            Running& rr = impl_->run[i];
            if(source == rr.dst.driver) rr.dst.updated = true;
        });
    }
    if(!m.items.empty()) HML_INFO("transfers: %zu defined", m.items.size());
    start();
    return static_cast<int>(m.items.size());
}

void Transfers::start()
{
    Impl& m = *impl_;
    if(m.timer || m.items.empty() || !lv_is_initialized()) return;
    m.timer = lv_timer_create([](lv_timer_t* t) { static_cast<Transfers*>(lv_timer_get_user_data(t))->poll(millis()); }, 100, this);
}

size_t Transfers::count() const { return impl_->items.size(); }
const std::vector<TransferItem>& Transfers::items() const { return impl_->items; }

int Transfers::poll(uint32_t now)
{
    Impl& m = *impl_;
    int written = 0;
    for(size_t i = 0; i < m.items.size(); i++) {
        TransferItem& t = m.items[i];
        Running& r = m.run[i];
        auto blocked = [&](TransferState s) { t.state = s; r.was_blocked = true; };
        if(!t.enabled) { blocked(TransferState::Disabled); continue; }
        if(r.ring) { blocked(TransferState::Ring); continue; }
        if(!r.src.tag || !r.dst.tag) { blocked(TransferState::NoTag); continue; }
        // both ends are looked at every time: each keeps track of its own connection
        const End::Now s = r.src.look(now), d = r.dst.look(now);
        if(s == End::Offline) { blocked(TransferState::SourceOffline); continue; }
        if(s == End::Wait) { blocked(TransferState::SourceWait); continue; }
        if(d == End::Offline) { blocked(TransferState::DestOffline); continue; }
        if(d == End::Wait) { blocked(TransferState::DestWait); continue; }
        if(!cond_holds(t.when, r)) { blocked(TransferState::ConditionFalse); continue; }
        // it may run. What was held back goes out now, with the value the source has now.
        if(r.was_blocked) { r.pending = true; r.was_blocked = false; }
        const bool due = r.pending || (t.cyclic && (!r.wrote_once || now - r.last >= t.every_ms));
        if(!due) { t.state = r.problem; continue; }
        r.pending = false;
        r.last = now;
        r.wrote_once = true;
        Value v;
        const TransferState vs = m.value_for(t, r, v);
        r.problem = vs;
        t.state = vs;
        if(vs != TransferState::Ok) continue;
        if(r.dst.tag->set(v, this)) { t.written++; written++; }
    }
    // the log: one line when the picture changes, not one per transfer and second
    if(!m.items.empty() && (m.summary_at == 0 || now - m.summary_at >= 1000)) {
        m.summary_at = now ? now : 1;
        std::map<std::string, int> why;
        int waiting = 0;
        for(const TransferItem& t : m.items) {
            if(t.state == TransferState::Ok || t.state == TransferState::Disabled || t.state == TransferState::ConditionFalse) continue;
            waiting++;
            std::string w = transfer_state_text(t.state);
            if(t.state == TransferState::SourceOffline || t.state == TransferState::SourceWait) w += " (" + t.from_driver + ")";
            if(t.state == TransferState::DestOffline || t.state == TransferState::DestWait) w += " (" + t.to_driver + ")";
            why[w]++;
        }
        std::string line = waiting == 0 ? "all run" : std::to_string(waiting) + " of " + std::to_string(m.items.size()) + " do not run:";
        for(const auto& [w, n] : why) line += " " + std::to_string(n) + " " + w + ";";
        if(line != m.summary) {
            if(!(m.summary.empty() && waiting == 0)) HML_INFO("transfers: %s", line.c_str());
            m.summary = line;
        }
    }
    return written;
}

Transfers& transfers()
{
    static Transfers instance;
    return instance;
}

// ================================================================ System > Transfers

namespace {

constexpr int kRows = 5;

class TransfersPage final : public SettingsPage {
public:
    std::string id() const override { return "transfers"; }
    std::string title() const override { return "Transfers"; }
    bool has_apply() const override { return false; }

    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        auto info = [&](const std::string& key, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Info; f.value = value; out.push_back(f);
        };
        auto flag = [&](const std::string& key, bool on) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Bool; f.value = on ? "1" : "0"; out.push_back(f);
        };
        const auto& list = transfers().items();
        const int n = static_cast<int>(list.size());
        if(first_ >= n) first_ = std::max(0, (n - 1) / kRows * kRows);
        int bad = 0;
        for(const TransferItem& t : list) if(t.state != TransferState::Ok && t.state != TransferState::Disabled && t.state != TransferState::ConditionFalse) bad++;
        info("range", n == 0 ? "no transfers" : vformat("%d - %d / %d    %s", first_ + 1, std::min(first_ + kRows, n), n, bad ? vformat("%d do not run", bad).c_str() : "all run"));
        flag("more", n > kRows);
        shown_.clear();
        for(int i = 0; i < kRows; i++) {
            const TransferItem* t = first_ + i < n ? &list[static_cast<size_t>(first_ + i)] : nullptr;
            const std::string k = std::to_string(i + 1);
            flag("r" + k + "_used", t != nullptr);
            info("n" + k, t ? t->name : "");
            info("p" + k, t ? t->from + " > " + t->to : "");
            std::string st;
            if(t) {
                st = transfer_state_text(t->state);
                if((t->state == TransferState::SourceOffline || t->state == TransferState::SourceWait) && !t->from_driver.empty()) st += " (" + t->from_driver + ")";
                if((t->state == TransferState::DestOffline || t->state == TransferState::DestWait) && !t->to_driver.empty()) st += " (" + t->to_driver + ")";
                if(t->state == TransferState::Ok) st += vformat("  %llu", static_cast<unsigned long long>(t->written));
            }
            info("s" + k, st);
            flag("b" + k, t && t->state != TransferState::Ok && t->state != TransferState::Disabled && t->state != TransferState::ConditionFalse);
            shown_ += st + "|";
        }
        return out;
    }
    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    void command(const std::string& cmd, const std::string&) override
    {
        const int n = static_cast<int>(transfers().count());
        if(cmd == "next") { if(first_ + kRows < n) first_ += kRows; }
        else if(cmd == "prev") first_ = std::max(0, first_ - kRows);
        else HML_WARN("system: transfers: unknown command '%s'", cmd.c_str());
    }

    // the states move while the page is looked at
    bool poll() override
    {
        auto cur = ui().current_screen();
        if(!cur || *cur != SystemSettings::page_screen(id())) return false;
        return ++ticks_ % 4 == 0;
    }

    json custom_rows(bool compact, int font) const override
    {
        const char* muted = "#9fb3c8";
        auto tag = [&](const std::string& key) { return SystemSettings::tag_name(id(), key); };
        auto call = [&](const std::string& cmd) { return json{ { "action", "call" }, { "fn", "system.do" }, { "arg", id() + ":" + cmd } }; };
        auto label = [&](const std::string& key, const char* color, int f) {
            return json{ { "type", "label" }, { "long_mode", "dots" }, { "bind", { { "text", tag(key) } } }, { "style", { { "font", f }, { "text_color", color } } } };
        };
        auto button = [&](const char* text, const std::string& cmd) {
            return json{ { "type", "button" }, { "text", text }, { "w", compact ? 40 : 56 }, { "h", compact ? 34 : 44 }, { "style", { { "bg_color", "#2c3e50" } } },
                         { "style_label", { { "font", font } } }, { "bind", { { "visible", tag("more") } } }, { "on_click", call(cmd) } };
        };
        json rows = json::array();
        json range = label("range", muted, font); range["flex_grow"] = 1;
        rows.push_back(json{ { "type", "panel" }, { "w", "100%" }, { "h", compact ? 36 : 46 }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                             { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 6 } } },
                             { "children", json::array({ range, button("<", "prev"), button(">", "next") }) } });
        for(int i = 1; i <= kRows; i++) {
            const std::string k = std::to_string(i);
            json n = label("n" + k, "#ffffff", font + 2); n["w"] = compact ? 96 : 160;
            json p = label("p" + k, "#90caf9", font); p["flex_grow"] = 1;
            json s = label("s" + k, "#a5d6a7", font); s["w"] = compact ? 150 : 230; s["style"]["text_align"] = "right";
            // (what does not run is written in the colour of a warning)
            json sb = label("s" + k, "#ffb74d", font); sb["w"] = compact ? 150 : 230; sb["style"]["text_align"] = "right";
            s["bind"]["hidden"] = tag("b" + k);
            sb["bind"]["visible"] = tag("b" + k);
            rows.push_back(json{ { "type", "panel" }, { "w", "100%" }, { "h", compact ? 34 : 44 }, { "flex", "row" }, { "flex_cross", "center" }, { "flex_track", "center" }, { "scrollable", false },
                                 { "style", { { "bg_color", "#2c3e50" }, { "radius", 6 }, { "border_width", 0 }, { "pad_left", 8 }, { "pad_right", 8 }, { "pad_top", 0 }, { "pad_bottom", 0 }, { "pad_column", 6 } } },
                                 { "bind", { { "visible", tag("r" + k + "_used") } } },
                                 { "children", json::array({ n, p, s, sb }) } });
        }
        return rows;
    }

private:
    mutable int first_ = 0;
    int ticks_ = 0;
    std::string shown_;
};

} // namespace

std::unique_ptr<SettingsPage> make_transfers_page() { return std::make_unique<TransfersPage>(); }

} // namespace hml
