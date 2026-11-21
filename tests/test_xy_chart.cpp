// The "xy_chart" widget (xy_chart_view.hpp). Grouped by ISO/IEC 25010 characteristic (issue #69).
#include <doctest.h>
#include "check.hpp"
#include "lvgl_fixture.hpp"
#include "ui.hpp"
#include "util.hpp"
#include "xy_chart_view.hpp"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

using Points = std::vector<std::pair<double, double>>;

lv_obj_t* show(const char* screen, const std::string& widgets)
{
    lv_obj_t* s = ui().build_screen(json::parse(std::string("{ \"name\": \"") + screen + "\", \"style\": { \"pad\": 0 }, \"children\": [ " + widgets + " ] }"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen(screen, "none"));
    return s;
}
void set(const char* tag, double v) { tags().find(tag)->set_number(v, nullptr); }
lv_obj_t* chart_of(lv_obj_t* w) { return lv_obj_get_child(w, 0); }
std::string label(lv_obj_t* w, int i) { return lv_label_get_text(lv_obj_get_child(w, i)); }      // 1 x_min, 2 x_max, 3 y_min, 4 y_max

} // namespace

// ================================================================ functional suitability

TEST_CASE("xy_chart: a point per interval, the oldest dropped; the numbers at the ends of the axes")
{
    Runtime rt;
    rt.tag("px", TagType::Float, 10);
    rt.tag("py", TagType::Float, 1);
    lv_obj_t* s = show("xy", R"({ "type": "xy_chart", "w": 300, "h": 240, "x_tag": "px", "x_min": 0, "x_max": 100, "y_tag": "py", "y_min": -5, "y_max": 5,
                                  "points": 5, "sample_ms": 100, "decimals": 1 })");
    lv_obj_t* w = lv_obj_get_child(s, 0);
    CHECK(xy_chart_points(w).empty());
    CHECK(xy_chart_series(w) == 1);
    CHECK(label(w, 1) == "0.0");
    CHECK(label(w, 2) == "100.0");
    CHECK(label(w, 3) == "-5.0");
    CHECK(label(w, 4) == "5.0");
    CHECK(lv_chart_get_type(chart_of(w)) == LV_CHART_TYPE_SCATTER);
    CHECK(lv_chart_get_point_count(chart_of(w)) == 5);
    // the graph is to the right of and above the numbers
    lv_obj_update_layout(w);
    CHECK(lv_obj_get_x(chart_of(w)) > 0);
    CHECK(lv_obj_get_height(chart_of(w)) < 240);
    CHECK(lv_obj_get_width(chart_of(w)) > 200);

    for(int i = 1; i <= 3; i++) { set("px", i * 10); set("py", i); rt.advance(100); }
    Points p = xy_chart_points(w);
    REQUIRE(p.size() == 3);
    CHECK(p[0] == std::make_pair(10.0, 1.0));
    CHECK(p[2] == std::make_pair(30.0, 3.0));
    for(int i = 4; i <= 9; i++) { set("px", i * 10); set("py", i - 5); rt.advance(100); }
    p = xy_chart_points(w);
    REQUIRE(p.size() == 5);                                       // the five newest
    CHECK(p[0] == std::make_pair(50.0, 0.0));
    CHECK(p[4] == std::make_pair(90.0, 4.0));
    // an interval samples also when nothing changed (a machine standing still is a point too)
    rt.advance(300);
    p = xy_chart_points(w);
    CHECK(p[0] == std::make_pair(80.0, 3.0));
    CHECK(p[4] == std::make_pair(90.0, 4.0));
    ui().unload_all();
}

TEST_CASE("xy_chart: on change, several curves, dots, clearing by a tag; points come also while another screen is shown")
{
    Runtime rt;
    rt.tag("px", TagType::Float, 0);
    rt.tag("py", TagType::Float, 0);
    rt.tag("pz", TagType::Int, 0);
    rt.tag("go", TagType::Bool, 0);
    lv_obj_t* s = show("xy", R"({ "type": "xy_chart", "x_tag": "px", "y_tag": "py", "sample_ms": 0, "points": 50, "clear_tag": "go", "mode": "dots",
                                  "series": [ { "x_tag": "px", "y_tag": "pz", "color": "#ff0000" }, { "x_tag": "pz", "y_tag": "px" }, { "x_tag": "py", "y_tag": "px" },
                                              { "x_tag": "px", "y_tag": "px" } ] })");
    lv_obj_t* w = lv_obj_get_child(s, 0);
    CHECK(xy_chart_series(w) == 4);                               // four curves at most
    rt.advance(200);
    CHECK(xy_chart_points(w).size() == 1);                        // where it stands, once
    rt.advance(500);
    CHECK(xy_chart_points(w).size() == 1);                        // nothing changed: no new point
    set("px", 5);
    rt.advance(100);
    set("py", 7);
    rt.advance(100);
    set("pz", 3);
    rt.advance(100);
    CHECK(xy_chart_points(w, 0) == Points{ { 0, 0 }, { 5, 0 }, { 5, 7 } });
    CHECK(xy_chart_points(w, 1) == Points{ { 0, 0 }, { 5, 0 }, { 5, 3 } });
    CHECK(xy_chart_points(w, 2) == Points{ { 0, 0 }, { 0, 5 }, { 3, 5 } });
    CHECK(xy_chart_points(w, 4).empty());
    CHECK(xy_chart_points(w, -1).empty());

    // a new cycle: the curves start anew when the tag becomes true (not while it stays true)
    set("go", 1);
    rt.advance(100);
    CHECK(xy_chart_points(w, 0) == Points{ { 5, 7 } });
    set("px", 6);
    rt.advance(100);
    CHECK(xy_chart_points(w, 0).size() == 2);
    set("go", 0);
    rt.advance(100);
    CHECK(xy_chart_points(w, 0).size() == 2);

    REQUIRE(ui().build_screen(json::parse(R"({ "name": "other", "children": [] })"), ""));
    REQUIRE(ui().show_screen("other", "none"));
    set("px", 8);
    rt.advance(100);
    set("px", 9);
    rt.advance(100);
    CHECK(xy_chart_points(w, 0).size() == 4);
    ui().unload_all();
}

// ================================================================ reliability / security

TEST_CASE("xy_chart: values that are no numbers, points outside the ranges, impossible settings")
{
    Runtime rt;
    rt.tag("px", TagType::Float, 1);
    rt.tag("py", TagType::Float, 1);
    rt.tag("s", std::string("12.5"));
    rt.tag("b", TagType::Bool, 1);
    std::string many = "[";
    for(int i = 0; i < 50; i++) many += std::string(i ? "," : "") + R"({ "x_tag": "px", "y_tag": "py" })";
    many += "]";
    lv_obj_t* scr = show("xy", std::string(R"(
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "sample_ms": 0, "points": 20 },
        { "type": "xy_chart" },
        { "type": "xy_chart", "x_tag": "nope", "y_tag": "py", "clear_tag": "gone" },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "x_min": 50, "x_max": 50, "y_min": 1e308, "y_max": -1e308 },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "x_min": -1e300, "x_max": 1e300, "y_min": 0, "y_max": 1e-12 },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "points": -5, "sample_ms": -100, "hdiv": 9999, "vdiv": -3, "decimals": 99, "mode": "spiral", "color": "nope" },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "points": 1e9, "sample_ms": 1e12 },
        { "type": "xy_chart", "x_tag": "s", "y_tag": "b", "sample_ms": 0 },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "w": 3, "h": 3 },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "series": "all" },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "series": [ 1, null, "x", [], {}, { "x_tag": 5, "y_tag": [] } ] },
        { "type": "xy_chart", "x_tag": "px", "y_tag": "py", "series": )") + many + " }");
    lv_obj_t* w = lv_obj_get_child(scr, 0);
    rt.advance(100);
    const size_t before = xy_chart_points(w).size();
    CHECK(before == 1);
    // what is no number is no point
    for(double x : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
        set("px", x); rt.advance(100);
        set("px", 2); set("py", x); rt.advance(100);
        set("py", 1); rt.advance(100);
    }
    Points p = xy_chart_points(w);
    for(const auto& pt : p) { CHECK(std::isfinite(pt.first)); CHECK(std::isfinite(pt.second)); }
    // far outside the ranges: kept as it is, drawn without overflowing anything
    for(double x : { 1e308, -1e308, 1e15, -1e15, 1e-300 }) { set("px", x); rt.advance(100); set("py", -x); rt.advance(100); }
    for(int i = 0; i < 10; i++) rt.advance(100);                  // (drawn a few times)
    p = xy_chart_points(w);
    CHECK(p.size() > before);
    CHECK(p.size() <= 20);                                        // never more than asked for
    for(const auto& pt : p) { CHECK(std::isfinite(pt.first)); CHECK(std::isfinite(pt.second)); }
    CHECK(xy_chart_series(lv_obj_get_child(scr, 11)) == 4);       // 1 + 50 curves asked for
    CHECK(xy_chart_series(lv_obj_get_child(scr, 9)) == 1);
    CHECK(xy_chart_series(lv_obj_get_child(scr, 10)) == 3);       // the objects among them (their tags are unknown: no points)
    CHECK(xy_chart_points(lv_obj_get_child(scr, 1)).empty());     // no tags: an empty graph
    CHECK(xy_chart_points(lv_obj_get_child(scr, 2)).empty());
    CHECK(lv_chart_get_point_count(chart_of(lv_obj_get_child(scr, 5))) == 2);
    CHECK(lv_chart_get_point_count(chart_of(lv_obj_get_child(scr, 6))) == 2000);
    // a text that is a number and a bool are values too
    CHECK(xy_chart_points(lv_obj_get_child(scr, 7)) == Points{ { 12.5, 1 } });
    // an impossible range became 0 .. 100
    CHECK(label(lv_obj_get_child(scr, 3), 1) == "0");
    CHECK(label(lv_obj_get_child(scr, 3), 4) == "100");
    CHECK(xy_chart_points(scr).empty());                          // (not an xy_chart)
    CHECK(xy_chart_series(nullptr) == 0);
    ui().unload_all();
    CHECK(xy_chart_points(w).empty());                            // (deleted: forgotten)
}

// ================================================================ performance efficiency

TEST_CASE("xy_chart: 2000 points that keep moving are drawn in time, and not for every point")
{
    Runtime rt;
    rt.tag("px", TagType::Float, 0);
    rt.tag("py", TagType::Float, 0);
    lv_obj_t* s = show("xy", R"({ "type": "xy_chart", "w": 320, "h": 240, "x_tag": "px", "y_tag": "py", "points": 2000, "sample_ms": 20 })");
    lv_obj_t* w = lv_obj_get_child(s, 0);
    for(int i = 0; i < 2200; i++) { set("px", 50 + 45 * std::sin(i * 0.013)); set("py", 50 + 45 * std::cos(i * 0.017)); rt.advance(20); }
    REQUIRE(xy_chart_points(w).size() == 2000);
    // 50 points a second come in; the graph is drawn again four times a second
    const uint32_t draws0 = xy_chart_draws(w);
    const int steps = 150;                                        // 3 s
    const uint32_t t0 = millis();
    for(int i = 0; i < steps; i++) { set("px", 50 + 45 * std::sin(i * 0.3)); set("py", 50 + 45 * std::cos(i * 0.2)); rt.advance(20); }
    const double per_second = static_cast<double>(millis() - t0) / 3;
    const uint32_t draws = xy_chart_draws(w) - draws0;
    MESSAGE("2000 points joined by lines, 50 new points a second: " << draws << " redraws in 3 s, " << per_second << " ms of work per second (320 x 240, software rendering)");
    CHECK(draws >= 10);
    CHECK(draws <= 14);                                           // four a second, not fifty
    CHECK(per_second < 700);                                      // (loose: a debug build on a busy CI machine)
    CHECK(xy_chart_points(w).size() == 2000);
    ui().unload_all();
}

// ================================================================ maintainability: the checker knows the widget

TEST_CASE("xy_chart: --check takes its keys and finds missing and unknown tags, a wrong series and range")
{
    static std::atomic<int> counter{ 0 };
    const fs::path dir = fs::temp_directory_path() / ("hml_xy_test_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir / "screens");
    auto problems = [&](const std::string& widgets) {
        { std::ofstream f(dir / "project.json", std::ios::binary);
          f << R"({ "start_screen": "s", "tags": [ { "name": "a", "type": "float" }, { "name": "b", "type": "float" }, { "name": "go", "type": "bool" } ], "screens": ["screens/s.json"] })"; }
        { std::ofstream f(dir / "screens" / "s.json", std::ios::binary); f << "{ \"name\": \"s\", \"children\": [ " << widgets << " ] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems(R"({ "type": "xy_chart", "x_tag": "a", "x_min": -1, "x_max": 1, "y_tag": "b", "y_min": 0, "y_max": 0.5, "points": 100, "sample_ms": 0, "mode": "dots",
                        "color": "#ff0000", "hdiv": 3, "vdiv": 3, "decimals": 1, "clear_tag": "go", "series": [ { "x_tag": "b", "y_tag": "a", "color": "#00ff00" } ] })") == "");
    const std::string out = problems(R"({ "type": "xy_chart", "x_tag": "nope", "x_min": 5, "x_max": 5, "clear_tag": "gone", "series": [ 5, { "x_tag": "a" }, { "x_tag": "a", "y_tag": "zz", "width": 2 } ] },
                                        { "type": "xy_chart", "x_tag": "a", "y_tag": "b", "series": {}, "xtag": "a" })");
    CHECK(out.find("children[0] (xy_chart): x_tag: unknown tag \"nope\"") != std::string::npos);
    CHECK(out.find("children[0] (xy_chart): no \"y_tag\"") != std::string::npos);
    CHECK(out.find("children[0] (xy_chart): clear_tag: unknown tag \"gone\"") != std::string::npos);
    CHECK(out.find("children[0] (xy_chart): x_max has to be above x_min") != std::string::npos);
    CHECK(out.find("children[0] (xy_chart): series[0]:") != std::string::npos);
    CHECK(out.find("children[0] (xy_chart): series[1]: x_tag and y_tag are needed") != std::string::npos);
    CHECK(out.find("children[0] (xy_chart): series[2].y_tag: unknown tag \"zz\"") != std::string::npos);
    CHECK(out.find("children[0] (xy_chart): series[2]: unknown key \"width\"") != std::string::npos);
    CHECK(out.find("children[1] (xy_chart): series: a list is needed") != std::string::npos);
    CHECK(out.find("children[1] (xy_chart): unknown key \"xtag\"") != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
