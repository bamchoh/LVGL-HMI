#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "util.hpp"

#include <string>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

// The sim driver runs from a 50 ms LVGL timer but samples the wall clock (millis()) for its
// waveforms. Tests therefore use very long periods (phase stays ~0) or 1 ms periods (every
// tick steps) and give the wall clock a few ms between ticks.
constexpr double kHour = 3600000;

int load_sim(Runtime& rt, const json& items)
{
    json arr = json::array({ json{ { "type", "sim" }, { "items", items } } });
    return rt.drivers.load(&arr);
}

void tick(Runtime& rt)
{
    sleep_ms(3);
    rt.advance(50, 50);
}

} // namespace

TEST_CASE("sim: a configuration without items is rejected, unknown tags and modes are skipped")
{
    Runtime rt;
    rt.tag("ok", TagType::Float);

    json arr = json::parse(R"([{ "type": "sim" }])");
    CHECK(rt.drivers.load(&arr) == 0);

    json items = json::parse(R"([
        { "tag": "missing", "mode": "sine" },
        { "tag": "ok", "mode": "warble" },
        { "tag": "ok", "mode": "square", "min": 3, "max": 7, "period_ms": 3600000 }
    ])");
    CHECK(load_sim(rt, items) == 1);
    tick(rt);
    CHECK(tags().find("ok")->get_number() == doctest::Approx(7));   // the valid item still runs
}

TEST_CASE("sim: continuous waveforms at phase ~0")
{
    Runtime rt;
    rt.tag("sine", TagType::Float);
    rt.tag("tri", TagType::Float);
    rt.tag("ramp", TagType::Float);
    rt.tag("sq", TagType::Float);
    json items = json::array({
        json{ { "tag", "sine" }, { "mode", "sine" }, { "min", 10 }, { "max", 20 }, { "period_ms", kHour } },
        json{ { "tag", "tri" }, { "mode", "triangle" }, { "min", -5 }, { "max", 5 }, { "period_ms", kHour } },
        json{ { "tag", "ramp" }, { "mode", "ramp" }, { "min", 100 }, { "max", 200 }, { "period_ms", kHour } },
        json{ { "tag", "sq" }, { "mode", "square" }, { "min", 1 }, { "max", 2 }, { "period_ms", kHour } },
    });
    REQUIRE(load_sim(rt, items) == 1);

    SUBCASE("nothing is written before the first timer tick") {
        CHECK(tags().find("sine")->get_number() == doctest::Approx(0));
    }
    SUBCASE("after a tick every item has its phase-0 value") {
        tick(rt);
        CHECK(tags().find("sine")->get_number() == doctest::Approx(15).epsilon(0.01));   // mid point
        CHECK(tags().find("tri")->get_number() == doctest::Approx(-5).epsilon(0.01));    // min
        CHECK(tags().find("ramp")->get_number() == doctest::Approx(100).epsilon(0.01));  // min
        CHECK(tags().find("sq")->get_number() == doctest::Approx(2));                    // first half = max
    }
}

TEST_CASE("sim: ranges default to the tag limits, or 0..100 / 0..1 for bools")
{
    Runtime rt;
    rt.tag("lim", TagType::Float, 0, 40, 60);
    rt.tag("free", TagType::Float);
    rt.tag("b", TagType::Bool);
    json items = json::array({
        json{ { "tag", "lim" }, { "mode", "square" }, { "period_ms", kHour } },
        json{ { "tag", "free" }, { "mode", "square" }, { "period_ms", kHour } },
        json{ { "tag", "b" }, { "mode", "square" }, { "period_ms", kHour } },
    });
    REQUIRE(load_sim(rt, items) == 1);
    tick(rt);
    CHECK(tags().find("lim")->get_number() == doctest::Approx(60));
    CHECK(tags().find("free")->get_number() == doctest::Approx(100));
    CHECK(tags().find("b")->get_number() == doctest::Approx(1));
}

TEST_CASE("sim: counter wraps through min..max one step per period")
{
    Runtime rt;
    rt.tag("cnt", TagType::Int);
    json items = json::array({ json{ { "tag", "cnt" }, { "mode", "counter" }, { "min", 5 }, { "max", 7 }, { "period_ms", 1 } } });
    REQUIRE(load_sim(rt, items) == 1);

    std::vector<double> seen;
    for(int i = 0; i < 4; i++) { tick(rt); seen.push_back(tags().find("cnt")->get_number()); }
    REQUIRE(seen.size() == 4);
    CHECK(seen[0] == doctest::Approx(6));    // step 1 -> 5 + 1 % 3
    CHECK(seen[1] == doctest::Approx(7));
    CHECK(seen[2] == doctest::Approx(5));
    CHECK(seen[3] == doctest::Approx(6));
}

TEST_CASE("sim: cycle walks through its values, numbers become text")
{
    Runtime rt;
    rt.tag("txt", std::string("init"));
    json items = json::array({ json{ { "tag", "txt" }, { "mode", "cycle" }, { "values", json::array({ "A", "B", 1, 2.5, true }) }, { "period_ms", 1 } } });
    REQUIRE(load_sim(rt, items) == 1);

    std::vector<std::string> seen;
    for(int i = 0; i < 5; i++) { tick(rt); seen.push_back(tags().find("txt")->get().str); }
    REQUIRE(seen.size() == 5);
    CHECK(seen[0] == "B");       // step 1
    CHECK(seen[1] == "1");       // booleans are not accepted as values, so the list is A B 1 2.5
    CHECK(seen[2] == "2.5");
    CHECK(seen[3] == "A");
    CHECK(seen[4] == "B");
}

TEST_CASE("sim: cycle without values leaves the tag alone")
{
    Runtime rt;
    rt.tag("txt", std::string("keep"));
    json items = json::array({ json{ { "tag", "txt" }, { "mode", "cycle" }, { "period_ms", 1 } } });
    REQUIRE(load_sim(rt, items) == 1);
    tick(rt);
    tick(rt);
    CHECK(tags().find("txt")->get().str == "keep");
}

TEST_CASE("sim: random stays inside min..max and changes once per period")
{
    Runtime rt;
    rt.tag("r", TagType::Float);
    json items = json::array({ json{ { "tag", "r" }, { "mode", "random" }, { "min", 5 }, { "max", 6 }, { "period_ms", 1 } } });
    REQUIRE(load_sim(rt, items) == 1);
    for(int i = 0; i < 10; i++) {
        tick(rt);
        double v = tags().find("r")->get_number();
        CHECK(v >= 5.0);
        CHECK(v <= 6.0);
    }
}

TEST_CASE("sim: period_ms 0 is tolerated")
{
    Runtime rt;
    rt.tag("x", TagType::Float);
    json items = json::array({ json{ { "tag", "x" }, { "mode", "ramp" }, { "min", 0 }, { "max", 10 }, { "period_ms", 0 } } });
    REQUIRE(load_sim(rt, items) == 1);
    tick(rt);
    double v = tags().find("x")->get_number();
    CHECK(v >= 0.0);
    CHECK(v <= 10.0);
}

TEST_CASE("sim: writes are delivered to UI subscribers with the driver as source, and stop quiets them")
{
    Runtime rt;
    Tag* t = rt.tag("sq", TagType::Float);
    int hits = 0;
    const void* last_src = nullptr;
    t->subscribe(SubGroup::UI, [&](Tag&, const Value&, const void* src) { hits++; last_src = src; });

    json items = json::array({ json{ { "tag", "sq" }, { "mode", "square" }, { "min", 0 }, { "max", 1 }, { "period_ms", kHour } } });
    REQUIRE(load_sim(rt, items) == 1);
    tick(rt);
    CHECK(hits == 1);
    CHECK(last_src != nullptr);

    tick(rt);                       // same value again: no notification
    CHECK(hits == 1);

    rt.drivers.stop_all();
    t->set_number(0, nullptr);
    rt.pump();
    hits = 0;
    tick(rt);
    tick(rt);
    CHECK(hits == 0);               // timer deleted, nothing written anymore
    CHECK(t->get_number() == doctest::Approx(0));
}

TEST_CASE("sim: several drivers may run side by side")
{
    Runtime rt;
    rt.tag("a", TagType::Float);
    rt.tag("b", TagType::Float);
    json arr = json::array({
        json{ { "type", "sim" }, { "items", json::array({ json{ { "tag", "a" }, { "mode", "square" }, { "min", 0 }, { "max", 1 }, { "period_ms", kHour } } }) } },
        json{ { "type", "sim" }, { "name", "second" }, { "items", json::array({ json{ { "tag", "b" }, { "mode", "square" }, { "min", 0 }, { "max", 2 }, { "period_ms", kHour } } }) } },
    });
    CHECK(rt.drivers.load(&arr) == 2);
    tick(rt);
    CHECK(tags().find("a")->get_number() == doctest::Approx(1));
    CHECK(tags().find("b")->get_number() == doctest::Approx(2));
}
