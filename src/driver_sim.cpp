// Simulation driver: generates waveforms into tags so screens can be developed without a PLC.
//
// {
//   "type": "sim",
//   "items": [
//     { "tag": "temp",  "mode": "sine",     "min": 15, "max": 35, "period_ms": 10000 },
//     { "tag": "level", "mode": "ramp",     "min": 0,  "max": 100, "period_ms": 5000 },
//     { "tag": "blink", "mode": "square",   "period_ms": 1000 },
//     { "tag": "noise", "mode": "random",   "min": 0,  "max": 10, "period_ms": 500 },
//     { "tag": "count", "mode": "counter",  "min": 0,  "max": 9,  "period_ms": 1000 },
//     { "tag": "txt",   "mode": "cycle",    "values": ["Idle","Run","Alarm"], "period_ms": 2000 }
//   ]
// }
#include "driver.hpp"
#include "tag.hpp"
#include "util.hpp"
#include "lvgl.h"
#include <cmath>
#include <random>

namespace hml {
namespace {

constexpr double kPi = 3.14159265358979323846;

class SimDriver final : public Driver {
public:
    explicit SimDriver(const json& cfg);
    bool ok() const { return ok_; }

    const char* type() const override { return "sim"; }

    bool start() override
    {
        t0_ = millis();
        timer_ = lv_timer_create(&SimDriver::tick, 50, this);
        return timer_ != nullptr;
    }

    void stop() override
    {
        if(timer_) { lv_timer_delete(timer_); timer_ = nullptr; }
    }

private:
    enum class Mode { Sine, Triangle, Ramp, Square, Random, Counter, Cycle };

    struct Item {
        Tag*     tag = nullptr;
        Mode     mode = Mode::Sine;
        double   min = 0, max = 1;
        uint32_t period_ms = 1000;
        uint32_t last_step = 0;
        int      step = 0;
        std::vector<std::string> values;
    };

    static void tick(lv_timer_t* t) { static_cast<SimDriver*>(lv_timer_get_user_data(t))->on_tick(); }
    void on_tick();

    std::vector<Item> items_;
    uint32_t          t0_ = 0;
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
        else { HML_WARN("sim: unknown mode '%s'", mode.c_str()); continue; }

        double tmin = 0, tmax = 1;
        bool has_lim = tag->limits(tmin, tmax);
        s.min = jnum(it, "min", has_lim ? tmin : 0);
        s.max = jnum(it, "max", has_lim ? tmax : (tag->type() == TagType::Bool ? 1 : 100));
        s.period_ms = static_cast<uint32_t>(jnum(it, "period_ms", 1000));
        if(s.period_ms == 0) s.period_ms = 1;

        if(const json* vals = jget(it, "values"); vals && vals->is_array()) {
            for(const json& v : *vals) {
                if(v.is_string() || v.is_number()) s.values.push_back(jscalar_to_string(v));
            }
        }
        items_.push_back(std::move(s));
    }
    ok_ = true;
}

void SimDriver::on_tick()
{
    uint32_t now = millis();
    for(Item& it : items_) {
        double period = static_cast<double>(it.period_ms);
        double phase = std::fmod(static_cast<double>(now - t0_), period) / period;   // 0..1
        double span = it.max - it.min;
        switch(it.mode) {
            case Mode::Sine:
                it.tag->set_number(it.min + span * (0.5 + 0.5 * std::sin(2 * kPi * phase)), this);
                break;
            case Mode::Triangle:
                it.tag->set_number(it.min + span * (phase < 0.5 ? phase * 2 : 2 - phase * 2), this);
                break;
            case Mode::Ramp:
                it.tag->set_number(it.min + span * phase, this);
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
