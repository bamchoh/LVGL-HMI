// The "meter" widget (meter_view.hpp). Grouped by ISO/IEC 25010 characteristic (issue #69).
#include <doctest.h>
#include "check.hpp"
#include "lvgl_fixture.hpp"
#include "meter_view.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

lv_obj_t* show(const char* screen, const std::string& widgets)
{
    lv_obj_t* s = ui().build_screen(json::parse(std::string("{ \"name\": \"") + screen + "\", \"style\": { \"pad\": 0 }, \"children\": [ " + widgets + " ] }"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen(screen, "none"));
    return s;
}
lv_obj_t* scale_of(lv_obj_t* meter) { return lv_obj_get_child(meter, 0); }

} // namespace

// ================================================================ functional suitability

TEST_CASE("meter: the needle and the number follow the tag; the needle stops at the ends, the number does not")
{
    Runtime rt;
    rt.tag("temp", TagType::Float, 25);
    lv_obj_t* s = show("m", R"({ "type": "meter", "w": 200, "h": 200, "bind": "temp", "min": 0, "max": 100, "decimals": 1, "unit": "C" })");
    lv_obj_t* m = lv_obj_get_child(s, 0);
    rt.advance(50);
    CHECK(meter_needle_value(m) == doctest::Approx(25));
    CHECK(meter_value_text(m) == "25.0 C");
    tags().find("temp")->set_number(72.56, nullptr);
    rt.advance(50);
    CHECK(meter_needle_value(m) == doctest::Approx(72.6).epsilon(0.001));        // (the scale counts in tenths here)
    CHECK(meter_value_text(m) == "72.6 C");
    tags().find("temp")->set_number(150, nullptr);
    rt.advance(50);
    CHECK(meter_needle_value(m) == 100);
    CHECK(meter_value_text(m) == "150.0 C");
    tags().find("temp")->set_number(-40, nullptr);
    rt.advance(50);
    CHECK(meter_needle_value(m) == 0);
    CHECK(meter_value_text(m) == "-40.0 C");
    // the scale is a square in the middle of the widget
    lv_obj_update_layout(m);
    CHECK(lv_obj_get_width(scale_of(m)) == 200);
    CHECK(lv_obj_get_height(scale_of(m)) == 200);
    // a meter only shows: touching it changes nothing
    lv_obj_send_event(m, LV_EVENT_CLICKED, nullptr);
    CHECK(tags().find("temp")->get_number() == -40);
    ui().unload_all();
}

TEST_CASE("meter: small ranges, negative ranges, a scaled binding, no number, a fixed value")
{
    Runtime rt;
    rt.tag("p", TagType::Float, 0.83);
    rt.tag("lvl", TagType::Float, -30);
    rt.tag("rpm", TagType::Int, 4200);
    lv_obj_t* s = show("m", R"(
        { "type": "meter", "x": 0, "y": 0, "bind": "p", "min": 0, "max": 1.5, "decimals": 2, "unit": "MPa", "ticks": 16, "major_every": 3, "angle": 240 },
        { "type": "meter", "x": 0, "y": 0, "bind": "lvl", "min": -50, "max": 50, "show_value": false },
        { "type": "meter", "x": 0, "y": 0, "bind": { "value": { "tag": "rpm", "scale": 0.001 } }, "min": 0, "max": 8, "decimals": 1 },
        { "type": "meter", "x": 0, "y": 0, "min": 0, "max": 10, "value": 7 },
        { "type": "meter", "x": 0, "y": 0, "w": 300, "h": 120 },
        { "type": "meter", "x": 0, "y": 0, "w": 100, "h": 100, "angle": 360 })");
    rt.advance(50);
    lv_obj_t* p = lv_obj_get_child(s, 0);
    CHECK(meter_needle_value(p) == doctest::Approx(0.83).epsilon(0.001));         // (the needle moves in 1 / 10000 of the scale)
    CHECK(meter_value_text(p) == "0.83 MPa");
    tags().find("p")->set_number(0.004, nullptr);
    rt.advance(50);
    CHECK(meter_needle_value(p) == doctest::Approx(0.004).epsilon(0.3));          // (thousandths at the finest)
    CHECK(meter_value_text(p) == "0.00 MPa");
    lv_obj_t* lvl = lv_obj_get_child(s, 1);
    CHECK(meter_needle_value(lvl) == -30);
    CHECK(meter_value_text(lvl) == "");                                           // no number asked for
    CHECK(lv_obj_get_child_count(lvl) == 1);
    lv_obj_t* rpm = lv_obj_get_child(s, 2);
    CHECK(meter_needle_value(rpm) == doctest::Approx(4.2));
    CHECK(meter_value_text(rpm) == "4.2");
    lv_obj_t* fixed = lv_obj_get_child(s, 3);
    CHECK(meter_needle_value(fixed) == 7);
    CHECK(meter_value_text(fixed) == "7");
    // nothing bound and no value: the needle rests at the start, the number says that there is none
    lv_obj_t* idle = lv_obj_get_child(s, 4);
    CHECK(meter_needle_value(idle) == 0);
    CHECK(meter_value_text(idle) == "---");
    lv_obj_update_layout(idle);
    CHECK(lv_obj_get_width(scale_of(idle)) == 120);                               // the smaller side
    CHECK(lv_scale_get_label_show(scale_of(lv_obj_get_child(s, 0))));
    CHECK_FALSE(lv_scale_get_label_show(scale_of(idle)));                         // too small for the numbers of the scale
    CHECK_FALSE(lv_scale_get_label_show(scale_of(lv_obj_get_child(s, 5))));
    ui().unload_all();
}

// ================================================================ reliability / security: what a project or a device can throw at it

TEST_CASE("meter: values that are no numbers, impossible ranges and endless lists are survived")
{
    Runtime rt;
    rt.tag("v", TagType::Float, 5);
    std::string zones = "[";
    for(int i = 0; i < 200; i++) zones += std::string(i ? "," : "") + "{ \"from\": " + std::to_string(i % 90) + ", \"to\": " + std::to_string(i % 90 + 5) + ", \"color\": \"#ff0000\" }";
    zones += "]";
    lv_obj_t* s = show("m", std::string(R"(
        { "type": "meter", "bind": "v" },
        { "type": "meter", "bind": "v", "min": 100, "max": 0 },
        { "type": "meter", "bind": "v", "min": 5, "max": 5 },
        { "type": "meter", "bind": "v", "min": -1e300, "max": 1e300, "decimals": 99 },
        { "type": "meter", "bind": "v", "min": 0, "max": 3000000000 },
        { "type": "meter", "bind": "v", "min": 0, "max": 0.000001, "decimals": 3 },
        { "type": "meter", "bind": "v", "ticks": -5, "major_every": 0, "angle": 100000, "decimals": -3 },
        { "type": "meter", "bind": "v", "ticks": 100000, "major_every": 99999, "angle": -1 },
        { "type": "meter", "bind": "v", "w": 4, "h": 4 },
        { "type": "meter", "bind": "v", "w": 0, "h": 0 },
        { "type": "meter", "bind": "v", "unit": ")") + std::string(3000, 'u') + R"(\n\t", "needle_color": "not a colour" },
        { "type": "meter", "bind": "v", "zones": "all of it" },
        { "type": "meter", "bind": "v", "zones": [ 1, null, "x", {}, { "from": 90, "to": 10 }, { "from": "a", "to": [] }, { "from": -1e308, "to": 1e308, "color": 7 } ] },
        { "type": "meter", "bind": "v", "zones": )" + zones + " }");
    rt.advance(50);
    Tag* v = tags().find("v");
    for(double x : { 0.0, 1e308, -1e308, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 4.9999999, 50.0 }) {
        v->set_number(x, nullptr);
        rt.advance(30);
        for(uint32_t i = 0; i < lv_obj_get_child_count(s); i++) {
            const double n = meter_needle_value(lv_obj_get_child(s, static_cast<int32_t>(i)));
            CHECK(std::isfinite(n));
        }
    }
    // a value that is no number: the needle at the start, "---" instead of a number that is none
    v->set_number(std::numeric_limits<double>::quiet_NaN(), nullptr);
    rt.advance(30);
    lv_obj_t* first = lv_obj_get_child(s, 0);
    CHECK(meter_needle_value(first) == 0);
    CHECK(meter_value_text(first) == "---");
    // an impossible range became 0 .. 100
    v->set_number(50, nullptr);
    rt.advance(30);
    CHECK(meter_needle_value(lv_obj_get_child(s, 1)) == 50);
    CHECK(meter_needle_value(lv_obj_get_child(s, 2)) == 50);
    // the unit is one short line
    const std::string unit_text = meter_value_text(lv_obj_get_child(s, 10));
    CHECK(unit_text.size() < 40);
    CHECK(unit_text.find('\n') == std::string::npos);
    for(int i = 0; i < 5; i++) rt.advance(100);                                   // (drawn a few times)
    // asking about something that is no meter does nothing
    meter_set_value(s, 5);
    meter_set_value(nullptr, 5);
    CHECK(meter_needle_value(s) == 0);
    CHECK(meter_value_text(nullptr) == "");
    ui().unload_all();
    meter_set_value(first, 1);                                                    // (deleted: forgotten)
}

// ================================================================ performance efficiency

TEST_CASE("meter: twelve meters that change with every frame are drawn quickly")
{
    Runtime rt;
    std::string widgets;
    for(int i = 0; i < 12; i++) {
        rt.tag("t" + std::to_string(i), TagType::Float, 0);
        widgets += std::string(i ? "," : "") + "{ \"type\": \"meter\", \"x\": " + std::to_string((i % 4) * 78) + ", \"y\": " + std::to_string((i / 4) * 78) +
                   ", \"w\": 76, \"h\": 76, \"bind\": \"t" + std::to_string(i) + "\", \"zones\": [ { \"from\": 80, \"to\": 100 } ] }";
    }
    show("many", widgets);
    rt.advance(100);
    const uint32_t t0 = millis();
    const int frames = 200;
    for(int f = 0; f < frames; f++) {
        for(int i = 0; i < 12; i++) tags().find("t" + std::to_string(i))->set_number((f * 3 + i * 7) % 100, nullptr);
        rt.advance(33);
    }
    const double per_frame = static_cast<double>(millis() - t0) / frames;
    MESSAGE("12 meters, every one changing: " << per_frame << " ms per frame (320 x 240, software rendering)");
    CHECK(per_frame < 100);                                                       // (loose: a debug build on a busy CI machine)
    ui().unload_all();
}

// ================================================================ maintainability: the checker knows the widget

TEST_CASE("meter: --check takes its keys and finds a wrong one and an unknown tag")
{
    static std::atomic<int> counter{ 0 };
    const fs::path dir = fs::temp_directory_path() / ("hml_meter_test_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir / "screens");
    auto problems = [&](const std::string& widgets) {
        { std::ofstream f(dir / "project.json", std::ios::binary);
          f << R"({ "start_screen": "s", "tags": [ { "name": "t", "type": "float" } ], "screens": ["screens/s.json"] })"; }
        { std::ofstream f(dir / "screens" / "s.json", std::ios::binary); f << "{ \"name\": \"s\", \"children\": [ " << widgets << " ] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems(R"({ "type": "meter", "bind": "t", "min": 0, "max": 100, "value": 5, "angle": 240, "ticks": 11, "major_every": 2, "decimals": 1, "unit": "bar",
                        "show_value": false, "needle_color": "#ff0000", "zones": [ { "from": 80, "to": 100, "color": "#e53935" } ], "level": 1 })") == "");
    const std::string out = problems(R"({ "type": "meter", "bind": "nope", "maximum": 5 }, { "type": "meter", "bind": { "value": { "tag": "gone" } } })");
    CHECK(out.find("children[0] (meter): bind: unknown tag \"nope\"") != std::string::npos);
    CHECK(out.find("children[0] (meter): unknown key \"maximum\"") != std::string::npos);
    CHECK(out.find("children[1] (meter)") != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
