// The "table" widget (table_view.hpp). Grouped by ISO/IEC 25010 characteristic (issue #69).
#include <doctest.h>
#include "check.hpp"
#include "lvgl_fixture.hpp"
#include "table_view.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
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

} // namespace

// ================================================================ functional suitability

TEST_CASE("table: texts, numbers and tags in rows and columns; the cells follow their tags")
{
    Runtime rt;
    rt.tag("c1", TagType::Int, 128);
    rt.tag("t1", TagType::Float, 36.52);
    rt.tag("s1", TagType::Int, 1);
    rt.tag("run", TagType::Bool, 1);
    rt.tag("lot", std::string("A-2031"));
    lv_obj_t* s = show("t", R"({ "type": "table", "w": 300, "h": 200,
        "columns": [ "Machine", { "title": "Count", "width": 90, "align": "right" }, { "title": "Temp" }, { "title": "State", "align": "center" } ],
        "rows": [ [ "Loader", { "tag": "c1" }, { "tag": "t1", "format": "%.1f C" }, { "tag": "s1", "map": { "0": "Stop", "1": "Run", "default": "?" } } ],
                  [ "Press", { "tag": "c1", "format": "%d pcs" }, 21.5, { "tag": "run", "map": { "true": "Run", "false": "Stop" } } ],
                  [ "Lot", { "tag": "lot" }, true, { "tag": "lot", "map": { "A-2031": "first", "B-1": "second" } } ],
                  [ 7 ] ] })");
    lv_obj_t* t = lv_obj_get_child(s, 0);
    rt.advance(300);
    CHECK(table_view_rows(t) == 4);
    CHECK(table_view_columns(t) == 4);
    CHECK(lv_table_get_row_count(t) == 5);                        // with the titles
    CHECK(table_view_cell(t, -1, 0) == "Machine");
    CHECK(table_view_cell(t, -1, 1) == "Count");
    CHECK(table_view_cell(t, -1, 3) == "State");
    CHECK(table_view_cell(t, 0, 0) == "Loader");
    CHECK(table_view_cell(t, 0, 1) == "128");
    CHECK(table_view_cell(t, 0, 2) == "36.5 C");
    CHECK(table_view_cell(t, 0, 3) == "Run");
    CHECK(table_view_cell(t, 1, 1) == "128 pcs");
    CHECK(table_view_cell(t, 1, 2) == "21.5");
    CHECK(table_view_cell(t, 1, 3) == "Run");
    CHECK(table_view_cell(t, 2, 1) == "A-2031");
    CHECK(table_view_cell(t, 2, 2) == "true");
    CHECK(table_view_cell(t, 2, 3) == "first");
    CHECK(table_view_cell(t, 3, 0) == "7");
    CHECK(table_view_cell(t, 3, 1) == "");                        // a short row: the rest is empty
    // the widths: what is given, the rest shared
    lv_obj_update_layout(t);
    CHECK(lv_table_get_column_width(t, 1) == 90);
    CHECK(lv_table_get_column_width(t, 0) == lv_table_get_column_width(t, 2));
    CHECK(lv_table_get_column_width(t, 0) >= 40);

    tags().find("c1")->set_number(129, nullptr);
    tags().find("t1")->set_number(-4.06, nullptr);
    tags().find("s1")->set_number(0, nullptr);
    tags().find("run")->set_number(0, nullptr);
    tags().find("lot")->set_string("B-1", nullptr);
    rt.advance(300);
    CHECK(table_view_cell(t, 0, 1) == "129");
    CHECK(table_view_cell(t, 0, 2) == "-4.1 C");
    CHECK(table_view_cell(t, 0, 3) == "Stop");
    CHECK(table_view_cell(t, 1, 1) == "129 pcs");
    CHECK(table_view_cell(t, 1, 3) == "Stop");
    CHECK(table_view_cell(t, 2, 1) == "B-1");
    CHECK(table_view_cell(t, 2, 3) == "second");
    tags().find("s1")->set_number(9, nullptr);                    // not in the map: its "default"
    tags().find("lot")->set_string("C", nullptr);                 // not in a map without one: the value itself
    rt.advance(300);
    CHECK(table_view_cell(t, 0, 3) == "?");
    CHECK(table_view_cell(t, 2, 3) == "C");
    CHECK(table_view_cell(t, 0, 0) == "Loader");                  // (the fixed cells stay)
    ui().unload_all();
}

TEST_CASE("table: without titles; a table on a screen that is not shown waits")
{
    Runtime rt;
    rt.tag("n", TagType::Int, 1);
    lv_obj_t* s = show("t", R"({ "type": "table", "header": false, "rows": [ [ "Shipped", { "tag": "n" } ], [ "NG", 0 ] ] })");
    lv_obj_t* t = lv_obj_get_child(s, 0);
    rt.advance(300);
    CHECK(lv_table_get_row_count(t) == 2);
    CHECK(table_view_rows(t) == 2);
    CHECK(table_view_cell(t, -1, 0) == "");
    CHECK(table_view_cell(t, 0, 1) == "1");
    REQUIRE(ui().build_screen(json::parse(R"({ "name": "other", "children": [] })"), ""));
    REQUIRE(ui().show_screen("other", "none"));
    tags().find("n")->set_number(2, nullptr);
    rt.advance(600);
    CHECK(table_view_cell(t, 0, 1) == "1");                       // not looked at
    REQUIRE(ui().show_screen("t", "none"));
    rt.advance(300);
    CHECK(table_view_cell(t, 0, 1) == "2");                       // up to date as soon as it is
    ui().unload_all();
}

// ================================================================ reliability / security

TEST_CASE("table: what is no table, endless rows and columns, hostile texts and values")
{
    Runtime rt;
    rt.tag("v", TagType::Float, 1);
    rt.tag("txt", std::string("ok"));
    std::string wide = "[";
    for(int i = 0; i < 100; i++) wide += std::string(i ? "," : "") + "\"c" + std::to_string(i) + "\"";
    wide += "]";
    std::string many = "[";
    for(int i = 0; i < 2500; i++) many += std::string(i ? "," : "") + "[" + std::to_string(i) + ", { \"tag\": \"v\" }]";
    many += "]";
    lv_obj_t* s = show("t", std::string(R"(
        { "type": "table" },
        { "type": "table", "columns": "no", "rows": 5 },
        { "type": "table", "columns": [ null, 5, [], { "title": 7, "width": -100, "align": "sideways" }, { "width": 1e12 } ], "rows": [ null, "x", 5, {}, [ null, [], {}, { "tag": 5 }, { "tag": "nope" }, { "text": "t" } ] ] },
        { "type": "table", "columns": )") + wide + R"(, "rows": [ )" + wide + R"( ] },
        { "type": "table", "rows": )" + many + R"( },
        { "type": "table", "w": 2, "h": 2, "rows": [ [ { "tag": "v" } ] ] },
        { "type": "table", "rows": [ [ { "tag": "txt" }, { "tag": "v", "format": "%s%s%n%999999d" }, { "tag": "v", "map": "no" }, { "tag": "v", "map": { "": "empty", "1e999": "inf", "x": 5, "default": [] } } ] ] })");
    rt.advance(300);
    CHECK(table_view_rows(lv_obj_get_child(s, 0)) == 0);
    CHECK(table_view_rows(lv_obj_get_child(s, 1)) == 0);
    lv_obj_t* odd = lv_obj_get_child(s, 2);
    CHECK(table_view_rows(odd) == 5);
    CHECK(table_view_columns(odd) == 6);
    CHECK(table_view_cell(odd, 4, 4) == "");                      // an unknown tag: an empty cell
    CHECK(table_view_cell(odd, 4, 5) == "t");
    CHECK(table_view_columns(lv_obj_get_child(s, 3)) == 32);      // the first 32 of 100 columns
    lv_obj_t* big = lv_obj_get_child(s, 4);
    CHECK(table_view_rows(big) == 2000);                          // the first 2000 of 2500 rows
    CHECK(table_view_cell(big, 1999, 0) == "1999");
    CHECK(table_view_cell(big, 1999, 1) == "1");
    // texts from a device: one line, no control characters, not endless; values that are no numbers
    lv_obj_t* nasty = lv_obj_get_child(s, 6);
    tags().find("txt")->set_string(std::string("a\nb\x1b[31m\xff") + std::string(5000, 'z'), nullptr);
    for(double x : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1e308, 1e-320 }) { tags().find("v")->set_number(x, nullptr); rt.advance(300); }
    const std::string cell = table_view_cell(nasty, 0, 0);
    CHECK(cell.find_first_of("\n\x1b") == std::string::npos);
    CHECK(cell.size() <= 203);
    CHECK(table_view_cell(nasty, 0, 1).size() < 400);             // (a format the project wrote; still bounded)
    // out of the table
    CHECK(table_view_cell(nasty, 5, 0) == "");
    CHECK(table_view_cell(nasty, 0, 99) == "");
    CHECK(table_view_cell(nasty, -5, 0) == "");
    CHECK(table_view_cell(s, 0, 0) == "");
    CHECK(table_view_rows(nullptr) == 0);
    ui().unload_all();
}

// ================================================================ performance efficiency

TEST_CASE("table: a thousand rows of live cells are built and kept up to date quickly")
{
    Runtime rt;
    const int n = 1000;
    std::string rows = "[";
    for(int i = 0; i < n; i++) {
        rt.tag("a" + std::to_string(i), TagType::Int, i);
        rows += std::string(i ? "," : "") + "[\"row " + std::to_string(i) + "\", { \"tag\": \"a" + std::to_string(i) + "\" }, { \"tag\": \"a" + std::to_string(i) + "\", \"format\": \"%.2f\" }]";
    }
    rows += "]";
    uint32_t t0 = millis();
    lv_obj_t* s = show("big", "{ \"type\": \"table\", \"w\": 320, \"h\": 240, \"columns\": [\"Name\", \"Value\", \"Scaled\"], \"rows\": " + rows + " }");
    lv_obj_t* t = lv_obj_get_child(s, 0);
    rt.advance(300);
    const uint32_t build_ms = millis() - t0;
    CHECK(table_view_cell(t, 999, 1) == "999");
    // nothing changes: a look at 2000 cells
    t0 = millis();
    for(int i = 0; i < 20; i++) rt.advance(250);
    const double idle_ms = static_cast<double>(millis() - t0) / 20;
    // every tag changes every time
    t0 = millis();
    for(int f = 0; f < 20; f++) {
        for(int i = 0; i < n; i++) tags().find("a" + std::to_string(i))->set_number(i + f + 1, nullptr);
        rt.advance(250);
    }
    const double busy_ms = static_cast<double>(millis() - t0) / 20;
    CHECK(table_view_cell(t, 999, 1) == "1019");
    CHECK(table_view_cell(t, 0, 2) == "20.00");
    MESSAGE("1000 rows, 2000 live cells: built in " << build_ms << " ms, a look when nothing changed " << idle_ms << " ms, when everything changed " << busy_ms << " ms");
    CHECK(build_ms < 5000);                                       // (loose: a debug build on a busy CI machine)
    CHECK(idle_ms < 100);
    CHECK(busy_ms < 500);
    ui().unload_all();
}

// ================================================================ maintainability: the checker knows the widget

TEST_CASE("table: --check takes its keys and finds a wrong row, cell and tag")
{
    static std::atomic<int> counter{ 0 };
    const fs::path dir = fs::temp_directory_path() / ("hml_table_test_" + std::to_string(counter++));
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
    CHECK(problems(R"({ "type": "table", "header": false, "columns": [ "A", { "title": "B", "width": 80, "align": "right" } ],
                        "rows": [ [ "x", { "tag": "t", "format": "%.1f", "map": { "0": "zero" } } ], [ 1, 2.5, true ] ] })") == "");
    const std::string out = problems(R"({ "type": "table", "columns": "A,B", "rows": [ "x", [ { "tag": "nope" }, { "text": "t" }, { "tag": "t", "colour": "red" } ] ] },
                                        { "type": "table", "rows": {}, "row": [] })");
    CHECK(out.find("children[0] (table): columns: a list is needed") != std::string::npos);
    CHECK(out.find("children[0] (table): rows[0]: a list of cells is needed") != std::string::npos);
    CHECK(out.find("children[0] (table): rows[1][0]: unknown tag \"nope\"") != std::string::npos);
    CHECK(out.find("children[0] (table): rows[1][1]: a cell is a text or { \"tag\": ... }") != std::string::npos);
    CHECK(out.find("children[0] (table): rows[1][2]: unknown key \"colour\"") != std::string::npos);
    CHECK(out.find("children[1] (table): rows: a list of rows is needed") != std::string::npos);
    CHECK(out.find("children[1] (table): unknown key \"row\"") != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
