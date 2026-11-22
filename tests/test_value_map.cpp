// "map" of bindings (value_map.hpp): value -> text for "text", value -> color with text keys.
// Grouped by ISO/IEC 25010 characteristic.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "texts.hpp"
#include "ui.hpp"
#include "value_map.hpp"

#include <string>

using namespace hml;
using hmltest::Runtime;

namespace {

json J(const char* text) { return json::parse(text); }

lv_obj_t* build_widgets(const json& widgets)
{
    json scr = { { "name", "s" }, { "children", widgets } };
    lv_obj_t* s = ui().build_screen(scr, "");
    REQUIRE(s != nullptr);
    REQUIRE(ui().show_screen("s", "none"));
    return s;
}

std::string label_text(lv_obj_t* o)
{
    if(!lv_obj_check_type(o, &lv_label_class)) o = lv_obj_get_child(o, 0);      // a button: its label
    return lv_label_get_text(o);
}

uint32_t bg(lv_obj_t* o) { return lv_color_to_u32(lv_obj_get_style_bg_color(o, LV_PART_MAIN)) & 0xffffff; }

} // namespace

TEST_SUITE("functional suitability") {
TEST_CASE("value map: numbers, true / false, text keys and the default")
{
    ValueMap<std::string> m;
    m.add("0", "stop");
    m.add("1", "run");
    m.add("2.5", "half");
    m.add("main", "MAIN");
    m.add("default", "?");
    CHECK(*m.find(Value::number(0)) == "stop");
    CHECK(*m.find(Value::number(1, TagType::Bool)) == "run");
    CHECK(*m.find(Value::number(2.5)) == "half");
    CHECK(*m.find(Value::string("main")) == "MAIN");
    CHECK(*m.find(Value::string("1")) == "run");          // a string tag holding "1"
    CHECK(*m.find(Value::number(7)) == "?");
    CHECK(*m.find(Value::string("other")) == "?");

    ValueMap<std::string> b;
    b.add("true", "ON");
    b.add("false", "OFF");
    CHECK(*b.find(Value::number(1, TagType::Bool)) == "ON");
    CHECK(*b.find(Value::number(0, TagType::Bool)) == "OFF");
    CHECK(b.find(Value::number(5)) == nullptr);           // no default
    CHECK(ValueMap<int>{}.empty());
}

TEST_CASE("bind text map: a state number shown as a word, following the tag")
{
    Runtime rt;
    Tag* state = rt.tag("state", TagType::Int, 0);
    Tag* run = rt.tag("run", TagType::Bool, 0);
    rt.tag("mode", std::string("auto"));
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "label",  "bind": { "text": { "tag": "state", "map": { "0": "Stopped", "1": "Running", "2": "Fault", "default": "---" } } } },
        { "type": "label",  "bind": { "text": { "tag": "state", "format": "%d", "map": { "1": "Running" } } } },
        { "type": "button", "w": 120, "h": 48, "bind": { "text": { "tag": "run", "map": { "true": "Stop", "false": "Start" } } } },
        { "type": "label",  "bind": { "text": { "tag": "mode", "map": { "auto": "AUTO", "manual": "MANUAL" } } } }
    ])json"));
    CHECK(label_text(lv_obj_get_child(s, 0)) == "Stopped");
    CHECK(label_text(lv_obj_get_child(s, 1)) == "0");          // no key, no default: the formatted value
    CHECK(label_text(lv_obj_get_child(s, 2)) == "Start");
    CHECK(label_text(lv_obj_get_child(s, 3)) == "AUTO");

    state->set_number(2, nullptr);
    run->set_number(1, nullptr);
    tags().find("mode")->set_string("manual", nullptr);
    rt.pump();
    CHECK(label_text(lv_obj_get_child(s, 0)) == "Fault");
    CHECK(label_text(lv_obj_get_child(s, 1)) == "2");
    CHECK(label_text(lv_obj_get_child(s, 2)) == "Stop");
    CHECK(label_text(lv_obj_get_child(s, 3)) == "MANUAL");

    state->set_number(9, nullptr);
    rt.pump();
    CHECK(label_text(lv_obj_get_child(s, 0)) == "---");
}

TEST_CASE("bind text map: \"@ID\" in the language of the screens")
{
    Runtime rt;
    rt.tag("state", TagType::Int, 1);
    texts().load(J(R"json({ "languages": ["ja", "en"], "language": "en",
        "texts": { "running": { "ja": "運転中", "en": "Running" }, "stopped": { "ja": "停止", "en": "Stopped" } } })json"));
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "label", "bind": { "text": { "tag": "state", "map": { "1": "@running", "default": "@stopped" } } } }
    ])json"));
    CHECK(label_text(lv_obj_get_child(s, 0)) == "Running");
    texts().clear();
}

TEST_CASE("bind color map: text keys match a string tag (a screen name)")
{
    Runtime rt;
    Tag* where = rt.tag("where", std::string("main"));
    Tag* n = rt.tag("n", TagType::Int, 1);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "button", "w": 120, "h": 48, "bind": { "bg_color": { "tag": "where", "map": { "main": "#26c6da", "default": "#192538" } } } },
        { "type": "panel",  "w": 40, "h": 40, "bind": { "bg_color": { "tag": "n", "map": { "1": "#36c98f", "2": "#f06464" } } } }
    ])json"));
    CHECK(bg(lv_obj_get_child(s, 0)) == 0x26c6da);
    CHECK(bg(lv_obj_get_child(s, 1)) == 0x36c98f);
    where->set_string("trend", nullptr);
    n->set_number(2, nullptr);
    rt.pump();
    CHECK(bg(lv_obj_get_child(s, 0)) == 0x192538);
    CHECK(bg(lv_obj_get_child(s, 1)) == 0xf06464);
}
}

TEST_SUITE("functional suitability") {
TEST_CASE("bind border_color and image_recolor: a card's frame and an icon in the state colour")
{
    Runtime rt;
    Tag* st = rt.tag("st", TagType::Int, 1);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "panel", "w": 100, "h": 60, "style": { "border_width": 3 },
          "bind": { "border_color": { "tag": "st", "map": { "1": "#36c98f", "2": "#f06464", "default": "#3b4a5e" } } } },
        { "type": "image", "bind": { "image_recolor": { "tag": "st", "map": { "1": "#36c98f", "2": "#f06464" } } } }
    ])json"));
    lv_obj_t* card = lv_obj_get_child(s, 0);
    lv_obj_t* icon = lv_obj_get_child(s, 1);
    auto border = [&] { return lv_color_to_u32(lv_obj_get_style_border_color(card, LV_PART_MAIN)) & 0xffffff; };
    auto recolor = [&] { return lv_color_to_u32(lv_obj_get_style_image_recolor(icon, LV_PART_MAIN)) & 0xffffff; };
    CHECK(border() == 0x36c98f);
    CHECK(recolor() == 0x36c98f);
    CHECK(lv_obj_get_style_image_recolor_opa(icon, LV_PART_MAIN) == LV_OPA_COVER);
    st->set_number(2, nullptr);
    rt.pump();
    CHECK(border() == 0xf06464);
    CHECK(recolor() == 0xf06464);
    st->set_number(7, nullptr);                  // default for the frame; the icon keeps its colour
    rt.pump();
    CHECK(border() == 0x3b4a5e);
    CHECK(recolor() == 0xf06464);
}
}
