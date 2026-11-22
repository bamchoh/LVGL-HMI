// Simulation driver: generates values into tags so screens can be developed and shown without a PLC.
// It is the "sim" driver of "drivers", and also what the project's "simulation" section runs
// (project.cpp: the same items, kept apart from the device drivers).
//
// {
//   "type": "sim",
//   "items": [
//     { "tag": "temp",  "mode": "sine",     "min": 15, "max": 35, "period_ms": 10000 },
//     { "tag": "level", "mode": "ramp",     "min": 0,  "max": 100, "period_ms": 5000 },
//     { "tag": "blink", "mode": "square",   "period_ms": 1000 },
//     { "tag": "noise", "mode": "random",   "min": 0,  "max": 10, "period_ms": 500 },
//     { "tag": "count", "mode": "counter",  "min": 0,  "max": 9,  "period_ms": 1000 },
//     { "tag": "txt",   "mode": "cycle",    "values": ["Idle","Run","Alarm"], "period_ms": 2000 },
//     // follows another tag (or a number) at "rate" units a second; while "when" is false it goes to "idle"
//     { "tag": "rpm",   "mode": "follow",   "target": "rpm_sp", "rate": 40, "when": "mixer_run", "idle": 0, "noise": 1.5 },
//     // counts only while the line runs
//     { "tag": "made",  "mode": "counter",  "min": 0, "max": 999999, "period_ms": 2000, "when": "line_run" }
//   ]
// }
// A counter goes on from the value the tag has when the simulation starts (its "init"), when that
// is inside min..max. Waveforms start at their phase 0: sine in the middle, ramp / triangle at min.
// A follow item writes no noise while "when" is false, and its noisy values stay inside min..max.
// "when": a tag (true) or { "tag": ..., "eq" | "ne" | "gt" | "lt" | "ge" | "le": <number, true / false, text> }.
// While it is false an item writes nothing (a follow item goes to its idle value). "noise": a random
// amount up to +- noise is added to what a number item writes.
#include "driver.hpp"
#include "tag.hpp"
#include "util.hpp"
#include "lvgl.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace hml {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct SimCond {
    enum class Op { Always, Truthy, Eq, Ne, Gt, Lt, Ge, Le } op = Op::Always;
    Tag*        tag = nullptr;
    double      num = 0;
    std::string text;
    bool        is_text = false;

    // false: the item is skipped (a warning tells why)
    bool parse(const json& w, std::string& why)
    {
        std::string name = w.is_string() ? w.get<std::string>() : jstr(w, "tag", "");
        tag = tags().find(name);
        if(!tag) { why = "\"when\": unknown tag '" + name + "'"; return false; }
        op = Op::Truthy;
        if(!w.is_object()) return true;
        static const std::pair<const char*, Op> ops[] = { { "eq", Op::Eq }, { "ne", Op::Ne }, { "gt", Op::Gt }, { "lt", Op::Lt }, { "ge", Op::Ge }, { "le", Op::Le } };
        for(const auto& [key, o] : ops) {
            const json* v = jget(w, key);
            if(!v) continue;
            op = o;
            if(v->is_boolean()) num = v->get<bool>() ? 1 : 0;
            else if(v->is_number()) num = v->get<double>();
            else if(v->is_string()) { text = v->get<std::string>(); is_text = true; }
            else { why = std::string("\"when\": \"") + key + "\" takes a number, true / false or a text"; return false; }
            break;
        }
        return true;
    }
    bool holds() const
    {
        if(op == Op::Always) return true;
        const Value v = tag->get();
        if(op == Op::Truthy) return v.truthy();
        if(is_text) {
            const std::string s = v.is_string() ? v.str : v.format();
            return op == Op::Eq ? s == text : op == Op::Ne ? s != text : false;
        }
        const double a = v.as_number();
        switch(op) {
            case Op::Eq: return a == num;
            case Op::Ne: return a != num;
            case Op::Gt: return a > num;
            case Op::Lt: return a < num;
            case Op::Ge: return a >= num;
            case Op::Le: return a <= num;
            default: return true;
        }
    }
};

class SimDriver final : public Driver {
public:
    explicit SimDriver(const json& cfg);
    bool ok() const { return ok_; }

    const char* type() const override { return "sim"; }

    bool start() override
    {
        t0_ = millis();
        last_tick_ = t0_;
        for(Item& it : items_) {
            if(it.mode == Mode::Follow) it.state = it.tag->get_number();
            // a counter goes on from the value the tag has (its init), not from min
            if(it.mode == Mode::Counter) {
                const double v = it.tag->get_number();
                if(v >= it.min && v <= it.max) it.step = static_cast<int>(std::lround(v - it.min));
            }
        }
        timer_ = lv_timer_create(&SimDriver::tick, 50, this);
        return timer_ != nullptr;
    }

    void stop() override
    {
        if(timer_) { lv_timer_delete(timer_); timer_ = nullptr; }
    }

private:
    enum class Mode { Sine, Triangle, Ramp, Square, Random, Counter, Cycle, Follow };

    struct Item {
        Tag*     tag = nullptr;
        Mode     mode = Mode::Sine;
        double   min = 0, max = 1;
        uint32_t period_ms = 1000;
        uint32_t last_step = 0;
        int      step = 0;
        std::vector<std::string> values;
        SimCond  when;
        double   noise = 0;
        // follow
        Tag*     target_tag = nullptr;
        double   target = 0, idle = 0, rate = 10, state = 0;
    };

    static void tick(lv_timer_t* t) { static_cast<SimDriver*>(lv_timer_get_user_data(t))->on_tick(); }
    void on_tick();
    void write(Item& it, double v, bool clamp = false);

    std::vector<Item> items_;
    uint32_t          t0_ = 0, last_tick_ = 0;
    lv_timer_t*       timer_ = nullptr;
    std::mt19937      rng_{ 12345 };
    bool              ok_ = false;
};

SimDriver::SimDriver(const json& cfg)
{
    const json* items = jget(cfg, "items");
    if(!items || !items->is_array()) { HML_WARN("sim: \"items\" array missing"); return; }

    for(const json& it : *items) {
        std::string tagname = jstr(it, "tag", "");
        Tag* tag = tags().find(tagname);
        if(!tag) { HML_WARN("sim: unknown tag '%s'", tagname.c_str()); continue; }

        Item s;
        s.tag = tag;
        std::string mode = jstr(it, "mode", "sine");
        if(iequals(mode, "sine")) s.mode = Mode::Sine;
        else if(iequals(mode, "triangle")) s.mode = Mode::Triangle;
        else if(iequals(mode, "ramp") || iequals(mode, "sawtooth")) s.mode = Mode::Ramp;
        else if(iequals(mode, "square") || iequals(mode, "toggle")) s.mode = Mode::Square;
        else if(iequals(mode, "random")) s.mode = Mode::Random;
        else if(iequals(mode, "counter")) s.mode = Mode::Counter;
        else if(iequals(mode, "cycle")) s.mode = Mode::Cycle;
        else if(iequals(mode, "follow")) s.mode = Mode::Follow;
        else { HML_WARN("sim: unknown mode '%s'", mode.c_str()); continue; }

        double tmin = 0, tmax = 1;
        bool has_lim = tag->limits(tmin, tmax);
        s.min = jnum(it, "min", has_lim ? tmin : 0);
        s.max = jnum(it, "max", has_lim ? tmax : (tag->type() == TagType::Bool ? 1 : 100));
        s.period_ms = static_cast<uint32_t>(jnum(it, "period_ms", 1000));
        if(s.period_ms == 0) s.period_ms = 1;
        s.noise = std::fabs(jnum(it, "noise", 0));

        if(const json* vals = jget(it, "values"); vals && vals->is_array()) {
            for(const json& v : *vals) {
                if(v.is_string() || v.is_number()) s.values.push_back(jscalar_to_string(v));
            }
        }
        if(const json* w = jget(it, "when")) {
            std::string why;
            if(!s.when.parse(*w, why)) { HML_WARN("sim: '%s': %s; item skipped", tagname.c_str(), why.c_str()); continue; }
        }
        if(s.mode == Mode::Follow) {
            const json* t = jget(it, "target");
            if(t && t->is_string()) {
                s.target_tag = tags().find(t->get<std::string>());
                if(!s.target_tag) { HML_WARN("sim: '%s': unknown target tag '%s'; item skipped", tagname.c_str(), t->get<std::string>().c_str()); continue; }
            }
            else if(t && t->is_number()) s.target = t->get<double>();
            else { HML_WARN("sim: '%s': mode \"follow\" needs \"target\" (a tag or a number); item skipped", tagname.c_str()); continue; }
            s.idle = jnum(it, "idle", s.min);
            s.rate = std::fabs(jnum(it, "rate", (s.max - s.min) / 5));
        }
        items_.push_back(std::move(s));
    }
    ok_ = true;
}

void SimDriver::write(Item& it, double v, bool clamp)
{
    if(it.noise > 0) {
        std::uniform_real_distribution<double> dist(-it.noise, it.noise);
        v += dist(rng_);
        if(clamp) v = std::clamp(v, std::min(it.min, it.max), std::max(it.min, it.max));
    }
    it.tag->set_number(v, this);
}

void SimDriver::on_tick()
{
    uint32_t now = millis();
    const double dt = static_cast<double>(now - last_tick_) / 1000.0;
    last_tick_ = now;
    for(Item& it : items_) {
        const bool on = it.when.holds();
        if(it.mode == Mode::Follow) {
            double goal = on ? (it.target_tag ? it.target_tag->get_number() : it.target) : it.idle;
            goal = std::clamp(goal, std::min(it.min, it.max), std::max(it.min, it.max));
            const double step = it.rate * dt;
            if(it.state < goal) it.state = std::min(goal, it.state + step);
            else if(it.state > goal) it.state = std::max(goal, it.state - step);
            if(on) write(it, it.state, true);
            else it.tag->set_number(it.state, this);            // (at rest: no noise, a stopped machine reads 0)
            continue;
        }
        if(!on) { it.last_step = now; continue; }           // (a counter starts a whole period after "when" comes)
        double period = static_cast<double>(it.period_ms);
        double phase = std::fmod(static_cast<double>(now - t0_), period) / period;   // 0..1
        double span = it.max - it.min;
        switch(it.mode) {
            case Mode::Sine:
                write(it, it.min + span * (0.5 + 0.5 * std::sin(2 * kPi * phase)));
                break;
            case Mode::Triangle:
                write(it, it.min + span * (phase < 0.5 ? phase * 2 : 2 - phase * 2));
                break;
            case Mode::Ramp:
                write(it, it.min + span * phase);
                break;
            case Mode::Square:
                it.tag->set_number(phase < 0.5 ? it.max : it.min, this);
                break;
            case Mode::Random:
            case Mode::Counter:
            case Mode::Cycle:
                if(now - it.last_step >= it.period_ms) {
                    it.last_step = now;
                    it.step++;
                    if(it.mode == Mode::Random) {
                        std::uniform_real_distribution<double> dist(0.0, 1.0);
                        it.tag->set_number(it.min + span * dist(rng_), this);
                    }
                    else if(it.mode == Mode::Counter) {
                        int lo = static_cast<int>(it.min), hi = static_cast<int>(it.max);
                        int n = hi - lo + 1;
                        if(n < 1) n = 1;
                        it.tag->set_number(lo + (it.step % n), this);
                    }
                    else if(!it.values.empty()) {
                        it.tag->set_string(it.values[static_cast<size_t>(it.step) % it.values.size()], this);
                    }
                }
                break;
            case Mode::Follow:
                break;
        }
    }
}

} // namespace

std::unique_ptr<Driver> make_sim_driver(const json& cfg)
{
    auto d = std::make_unique<SimDriver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml
