// sys.screen: the name of the screen shown; a template menu marks the screen it is on.
// Grouped by ISO/IEC 25010 characteristic.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "ui.hpp"

#include <string>

using namespace hml;
using hmltest::Runtime;

namespace {
json J(const char* text) { return json::parse(text); }
uint32_t bg(lv_obj_t* o) { return lv_color_to_u32(lv_obj_get_style_bg_color(o, LV_PART_MAIN)) & 0xffffff; }
std::string screen_tag() { Tag* t = tags().find("sys.screen"); return t ? t->get().format() : "<none>"; }
} // namespace

TEST_SUITE("functional suitability") {
TEST_CASE("sys.screen follows the screen shown; a template's menu marks it")
{
    Runtime rt;
    // the menu is in the template: each button colors itself when its screen is shown
    REQUIRE(ui().add_screen(J(R"json({ "name": "frame", "children": [
        { "type": "button", "x": 0,   "y": 0, "w": 100, "h": 48, "text": "Main",
          "bind": { "bg_color": { "tag": "sys.screen", "map": { "main": "#26c6da", "default": "#192538" } } } },
        { "type": "button", "x": 110, "y": 0, "w": 100, "h": 48, "text": "Trend",
          "bind": { "bg_color": { "tag": "sys.screen", "map": { "trend": "#26c6da", "default": "#192538" } } } },
        { "type": "label", "x": 0, "y": 60, "bind": { "text": "sys.screen" } } ] })json"), ""));
    REQUIRE(ui().add_screen(J(R"json({ "name": "main", "template": "frame", "children": [] })json"), ""));
    REQUIRE(ui().add_screen(J(R"json({ "name": "trend", "template": "frame", "children": [] })json"), ""));
    REQUIRE(ui().add_screen(J(R"json({ "name": "info", "popup": true, "children": [] })json"), ""));
    REQUIRE(tags().find("sys.screen"));

    REQUIRE(ui().show_screen("main", "none"));
    CHECK(screen_tag() == "main");
    lv_obj_t* s = lv_screen_active();
    CHECK(bg(lv_obj_get_child(s, 0)) == 0x26c6da);        // drawn right away, before the dispatch
    CHECK(bg(lv_obj_get_child(s, 1)) == 0x192538);
    rt.pump();
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(s, 2))) == "main");

    REQUIRE(ui().show_screen("trend", "none"));
    rt.advance(50);
    rt.pump();
    CHECK(screen_tag() == "trend");
    lv_obj_t* t = lv_screen_active();
    CHECK(bg(lv_obj_get_child(t, 0)) == 0x192538);
    CHECK(bg(lv_obj_get_child(t, 1)) == 0x26c6da);

    // a popup does not change it
    REQUIRE(ui().open_popup("info"));
    CHECK(screen_tag() == "trend");
    ui().close_popup("info");

    // back to main: so does "back"
    REQUIRE(ui().back());
    rt.advance(300);
    rt.pump();
    CHECK(screen_tag() == "main");
    CHECK(bg(lv_obj_get_child(lv_screen_active(), 0)) == 0x26c6da);
}
}
