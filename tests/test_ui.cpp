#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "ui.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace hml;
using hmltest::Runtime;

namespace {

json J(const char* text) { return json::parse(text); }

// LVGL creates a dropdown's list as an extra child of the screen; the tests index the widgets
// the JSON defined, so skip those helper objects.
bool is_widget(lv_obj_t* o) { return !lv_obj_check_type(o, &lv_dropdownlist_class); }

uint32_t widget_count(lv_obj_t* parent)
{
    uint32_t n = 0;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) if(is_widget(lv_obj_get_child(parent, static_cast<int32_t>(i)))) n++;
    return n;
}

lv_obj_t* nth(lv_obj_t* parent, int32_t idx)
{
    int32_t seen = 0;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t* c = lv_obj_get_child(parent, static_cast<int32_t>(i));
        if(!is_widget(c)) continue;
        if(seen++ == idx) return c;
    }
    return nullptr;
}

// Build a screen from JSON and make it the active one (no animation).
lv_obj_t* build(const json& screen)
{
    lv_obj_t* scr = ui().build_screen(screen, "/base");
    if(scr) ui().show_screen(jstr(screen, "name", ""), "none");
    return scr;
}

// Build a screen "s" holding exactly the given widgets, return the first widget.
lv_obj_t* build_widgets(const json& widgets)
{
    json scr = { { "name", "s" }, { "children", widgets } };
    lv_obj_t* s = build(scr);
    REQUIRE(s != nullptr);
    return widget_count(s) ? nth(s, 0) : nullptr;
}

lv_obj_t* build_widget(const char* widget_json) { return build_widgets(json::array({ J(widget_json) })); }

std::string label_text(lv_obj_t* label) { return lv_label_get_text(label); }

// A 3x2 RGBA PNG. LVGL only keeps an image source it can decode, so the tests need real files.
const uint8_t kPng3x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x9d, 0x74, 0x66, 0x1a, 0x00, 0x00, 0x00, 0x11, 0x49, 0x44, 0x41,
    0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x86, 0x19, 0x90, 0x39, 0x00, 0x9b, 0x7e, 0x0b, 0xf5, 0x0f, 0x5f,
    0x26, 0x22, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

struct TempDir {
    std::filesystem::path dir;
    TempDir()
    {
        dir = std::filesystem::temp_directory_path() / ("hml_ui_test_" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this))));
        std::filesystem::create_directories(dir / "assets");
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    std::string png(const std::string& rel)
    {
        std::filesystem::path p = dir / rel;
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(kPng3x2), sizeof(kPng3x2));
        return p.generic_string();
    }
    std::string base() const { return dir.generic_string(); }
};

std::string image_src(lv_obj_t* img)
{
    const void* src = lv_image_get_src(img);
    return src ? static_cast<const char*>(src) : std::string("<null>");
}

void click(lv_obj_t* obj) { lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr); }
void value_changed(lv_obj_t* obj) { lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, nullptr); }

} // namespace

// ================================================================ helpers exported by ui.hpp

TEST_CASE("parse_color")
{
    lv_color_t dflt = lv_color_hex(0x123456);
    CHECK(lv_color_eq(parse_color("#ff0000", dflt), lv_color_hex(0xff0000)));
    CHECK(lv_color_eq(parse_color("#00FF80", dflt), lv_color_hex(0x00ff80)));
    CHECK(lv_color_eq(parse_color("#f00", dflt), lv_color_hex(0xff0000)));          // #rgb expands each nibble
    CHECK(lv_color_eq(parse_color("#1a2", dflt), lv_color_hex(0x11aa22)));
    CHECK(lv_color_eq(parse_color("red", dflt), lv_palette_main(LV_PALETTE_RED)));
    CHECK(lv_color_eq(parse_color("Blue_Grey", dflt), lv_palette_main(LV_PALETTE_BLUE_GREY)));
    CHECK(lv_color_eq(parse_color("gray", dflt), lv_palette_main(LV_PALETTE_GREY)));
    CHECK(lv_color_eq(parse_color("grey", dflt), lv_palette_main(LV_PALETTE_GREY)));
    CHECK(lv_color_eq(parse_color("WHITE", dflt), lv_color_white()));
    CHECK(lv_color_eq(parse_color("black", dflt), lv_color_black()));
    CHECK(lv_color_eq(parse_color("", dflt), dflt));
    CHECK(lv_color_eq(parse_color("no-such-color", dflt), dflt));
}

TEST_CASE("font_from_json picks the Montserrat size")
{
    json n16 = 16, n24 = 24, s32 = "montserrat_32", s14 = "14", bad = 13, str = "huge", b = true;
    CHECK(font_from_json(&n16) == &lv_font_montserrat_16);
    CHECK(font_from_json(&n24) == &lv_font_montserrat_24);
    CHECK(font_from_json(&s32) == &lv_font_montserrat_32);
    CHECK(font_from_json(&s14) == &lv_font_montserrat_14);
    CHECK(font_from_json(&bad) == LV_FONT_DEFAULT);
    CHECK(font_from_json(&str) == LV_FONT_DEFAULT);
    CHECK(font_from_json(&b) == LV_FONT_DEFAULT);
    CHECK(font_from_json(nullptr) == LV_FONT_DEFAULT);
}

// ================================================================ screens

TEST_CASE("build_screen validates its input")
{
    Runtime rt;
    CHECK(ui().build_screen(json(5), "") == nullptr);
    CHECK(ui().build_screen(J(R"({ "children": [] })"), "") == nullptr);          // no name
    REQUIRE(ui().build_screen(J(R"({ "name": "a" })"), "") != nullptr);
    CHECK(ui().build_screen(J(R"({ "name": "a" })"), "") == nullptr);             // duplicate
    CHECK(ui().screen_count() == 1);
    CHECK_FALSE(ui().current_screen().has_value());                               // built, not shown
}

TEST_CASE("screen level style, scrollable and layout")
{
    Runtime rt;
    lv_obj_t* scr = build(J(R"({ "name": "s", "scrollable": false, "flex": "column",
                                 "style": { "bg_color": "#102030", "pad": 7 },
                                 "children": [ { "type": "label", "text": "a" }, { "type": "label", "text": "b" } ] })"));
    REQUIRE(scr);
    CHECK(lv_color_eq(lv_obj_get_style_bg_color(scr, LV_PART_MAIN), lv_color_hex(0x102030)));
    CHECK(lv_obj_get_style_pad_left(scr, LV_PART_MAIN) == 7);
    CHECK_FALSE(lv_obj_has_flag(scr, LV_OBJ_FLAG_SCROLLABLE));
    CHECK(lv_obj_get_style_flex_flow(scr, LV_PART_MAIN) == LV_FLEX_FLOW_COLUMN);
    CHECK(widget_count(scr) == 2);
    CHECK(lv_screen_active() == scr);
}

TEST_CASE("show_screen, back and history")
{
    Runtime rt;
    lv_obj_t* a = ui().build_screen(J(R"({ "name": "a" })"), "");
    lv_obj_t* b = ui().build_screen(J(R"({ "name": "b" })"), "");
    lv_obj_t* c = ui().build_screen(J(R"({ "name": "c" })"), "");
    REQUIRE((a && b && c));
    CHECK(ui().screen_count() == 3);

    CHECK_FALSE(ui().show_screen("zzz", "none"));
    CHECK_FALSE(ui().back());

    REQUIRE(ui().show_screen("a", "none"));
    CHECK(ui().current_screen() == std::optional<std::string>("a"));
    CHECK(lv_screen_active() == a);
    CHECK_FALSE(ui().back());                     // first screen: nothing to go back to

    REQUIRE(ui().show_screen("b", "none"));
    REQUIRE(ui().show_screen("b", "none"));       // same screen again is a no-op
    REQUIRE(ui().show_screen("c", "none"));
    CHECK(lv_screen_active() == c);

    CHECK(ui().back());
    CHECK(ui().current_screen() == std::optional<std::string>("b"));
    rt.advance(400);                              // back() animates (slide right): let it finish
    CHECK(lv_screen_active() == b);
    CHECK(ui().back());
    CHECK(ui().current_screen() == std::optional<std::string>("a"));
    rt.advance(400);
    CHECK(lv_screen_active() == a);
    CHECK_FALSE(ui().back());

    SUBCASE("the history keeps at most 16 entries") {
        for(int i = 0; i < 20; i++) ui().show_screen(i % 2 ? "b" : "c", "none");
        int steps = 0;
        while(ui().back()) steps++;
        CHECK(steps == 16);
    }
    SUBCASE("an animated switch completes after the animation time") {
        REQUIRE(ui().show_screen("c", "fade"));
        CHECK(ui().current_screen() == std::optional<std::string>("c"));
        rt.advance(400);
        CHECK(lv_screen_active() == c);
        CHECK(lv_anim_count_running() == 0);
    }
    SUBCASE("unknown animation names fall back to a fade, 'none' switches at once") {
        REQUIRE(ui().show_screen("b", "wobble"));
        rt.advance(400);
        CHECK(lv_screen_active() == b);
        REQUIRE(ui().show_screen("c", ""));
        CHECK(lv_screen_active() == c);
    }
}

TEST_CASE("unload_all removes screens, bindings and UI subscriptions")
{
    Runtime rt;
    Tag* t = rt.tag("t", TagType::Int, 1);
    lv_obj_t* scr = build(J(R"({ "name": "s", "children": [ { "type": "label", "bind": "t" } ] })"));
    REQUIRE(scr);
    int ui_subs = 0;
    t->subscribe(SubGroup::UI, [&](Tag&, const Value&, const void*) { ui_subs++; });
    int drv_subs = 0;
    t->subscribe(SubGroup::Driver, [&](Tag&, const Value&, const void*) { drv_subs++; });

    ui().unload_all();
    CHECK(ui().screen_count() == 0);
    CHECK_FALSE(ui().current_screen().has_value());
    CHECK(lv_screen_active() != scr);
    CHECK(widget_count(lv_screen_active()) == 0);

    t->set_number(2, nullptr);
    rt.pump();
    CHECK(ui_subs == 0);
    CHECK(drv_subs == 1);

    // building again after an unload works and the blank screen is replaced
    lv_obj_t* again = build(J(R"({ "name": "s" })"));
    REQUIRE(again);
    CHECK(lv_screen_active() == again);
    ui().unload_all();
    CHECK(ui().screen_count() == 0);
}

// ================================================================ widget construction

TEST_CASE("every widget type is created with the matching LVGL class")
{
    Runtime rt;
    rt.tag("v", TagType::Int, 0);
    lv_obj_t* scr = build(J(R"json({ "name": "s", "children": [
        { "type": "panel" }, { "type": "container" }, { "type": "obj" },
        { "type": "label", "text": "L" },
        { "type": "button", "text": "B" },
        { "type": "switch" }, { "type": "checkbox", "text": "C" },
        { "type": "slider" }, { "type": "bar" }, { "type": "arc" }, { "type": "led" },
        { "type": "dropdown", "options": ["a", "b"] }, { "type": "roller", "options": ["a", "b"] },
        { "type": "textarea", "text": "T" }, { "type": "image" },
        { "type": "chart", "series": [ { "tag": "v" } ] }, { "type": "spinner" }, { "type": "keyboard" },
        { "type": "line", "points": [[0, 0], [10, 10]] },
        { "type": "hologram" }, 42, "label"
    ] })json"));
    REQUIRE(scr);
    REQUIRE(widget_count(scr) == 19);          // unknown type and non-objects are skipped
    const lv_obj_class_t* expected[] = {
        &lv_obj_class, &lv_obj_class, &lv_obj_class, &lv_label_class, &lv_button_class, &lv_switch_class,
        &lv_checkbox_class, &lv_slider_class, &lv_bar_class, &lv_arc_class, &lv_led_class, &lv_dropdown_class,
        &lv_roller_class, &lv_textarea_class, &lv_image_class, &lv_chart_class, &lv_spinner_class,
        &lv_keyboard_class, &lv_line_class,
    };
    for(uint32_t i = 0; i < 19; i++) {
        CAPTURE(i);
        CHECK(lv_obj_check_type(nth(scr, static_cast<int32_t>(i)), expected[i]));
    }
    CHECK(lv_obj_has_flag(nth(scr, 17), LV_OBJ_FLAG_HIDDEN));   // keyboard starts hidden
}

TEST_CASE("widget specific properties")
{
    Runtime rt;

    SUBCASE("label") {
        lv_obj_t* l = build_widget(R"({ "type": "label", "text": "Hello", "long_mode": "dots" })");
        REQUIRE(l);
        CHECK(label_text(l) == "Hello");
        CHECK(lv_label_get_long_mode(l) == LV_LABEL_LONG_MODE_DOTS);
        lv_obj_t* l2 = ui().build_screen(J(R"({ "name": "s2", "children": [ { "type": "label", "long_mode": "scroll" } ] })"), "");
        CHECK(lv_label_get_long_mode(nth(l2, 0)) == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }
    SUBCASE("button with and without text") {
        lv_obj_t* b = build_widgets(J(R"([ { "type": "button", "text": "Go", "style_label": { "font": 24 } }, { "type": "button" } ])"));
        REQUIRE(b);
        REQUIRE(widget_count(b) == 1);
        lv_obj_t* lbl = nth(b, 0);
        CHECK(lv_obj_check_type(lbl, &lv_label_class));
        CHECK(label_text(lbl) == "Go");
        CHECK(lv_obj_get_style_text_font(lbl, LV_PART_MAIN) == &lv_font_montserrat_24);
        lv_obj_t* plain = nth(lv_obj_get_parent(b), 1);
        CHECK(widget_count(plain) == 0);
    }
    SUBCASE("checkbox") {
        lv_obj_t* c = build_widget(R"({ "type": "checkbox", "text": "Enable" })");
        CHECK(std::string(lv_checkbox_get_text(c)) == "Enable");
    }
    SUBCASE("slider / bar / arc ranges and values") {
        lv_obj_t* s = build_widgets(J(R"([
            { "type": "slider", "min": -10, "max": 10, "value": 5 },
            { "type": "bar", "min": 0, "max": 200, "value": 150 },
            { "type": "arc", "min": 0, "max": 40, "value": 12, "rotation": 90, "start_angle": 0, "end_angle": 180, "adjustable": false },
            { "type": "arc" }
        ])"));
        REQUIRE(s);
        lv_obj_t* parent = lv_obj_get_parent(s);
        CHECK(lv_slider_get_min_value(s) == -10);
        CHECK(lv_slider_get_max_value(s) == 10);
        CHECK(lv_slider_get_value(s) == 5);
        lv_obj_t* bar = nth(parent, 1);
        CHECK(lv_bar_get_max_value(bar) == 200);
        CHECK(lv_bar_get_value(bar) == 150);
        lv_obj_t* arc = nth(parent, 2);
        CHECK(lv_arc_get_max_value(arc) == 40);
        CHECK(lv_arc_get_value(arc) == 12);
        CHECK(lv_arc_get_rotation(arc) == 90);
        CHECK_FALSE(lv_obj_has_flag(arc, LV_OBJ_FLAG_CLICKABLE));
        CHECK(lv_obj_has_flag(nth(parent, 3), LV_OBJ_FLAG_CLICKABLE));
    }
    SUBCASE("led") {
        lv_obj_t* on = build_widgets(J(R"([ { "type": "led", "on": true, "color": "red" }, { "type": "led" } ])"));
        CHECK(lv_led_get_brightness(on) == LV_LED_BRIGHT_MAX);
        CHECK(lv_led_get_brightness(nth(lv_obj_get_parent(on), 1)) == LV_LED_BRIGHT_MIN);
    }
    SUBCASE("dropdown / roller options") {
        lv_obj_t* d = build_widgets(J(R"([
            { "type": "dropdown", "options": ["One", 2, 2.5, true], "selected": 1 },
            { "type": "roller", "options": ["x", "y", "z"], "selected": 2, "visible_rows": 2 },
            { "type": "dropdown" }
        ])"));
        REQUIRE(d);
        CHECK(std::string(lv_dropdown_get_options(d)) == "One\n2\n2.5\ntrue");
        CHECK(lv_dropdown_get_selected(d) == 1);
        lv_obj_t* r = nth(lv_obj_get_parent(d), 1);
        CHECK(std::string(lv_roller_get_options(r)) == "x\ny\nz");
        CHECK(lv_roller_get_selected(r) == 2);
        lv_obj_t* empty = nth(lv_obj_get_parent(d), 2);
        CHECK(std::string(lv_dropdown_get_options(empty)).empty());
    }
    SUBCASE("textarea") {
        lv_obj_t* t = build_widgets(J(R"([
            { "type": "textarea", "text": "abc", "placeholder": "type here", "one_line": false, "password": true, "max_length": 8, "accepted_chars": "0123456789" },
            { "type": "textarea" }
        ])"));
        REQUIRE(t);
        CHECK(std::string(lv_textarea_get_text(t)) == "abc");
        CHECK(std::string(lv_textarea_get_placeholder_text(t)) == "type here");
        CHECK_FALSE(lv_textarea_get_one_line(t));
        CHECK(lv_textarea_get_password_mode(t));
        CHECK(lv_textarea_get_max_length(t) == 8);
        CHECK(std::string(lv_textarea_get_accepted_chars(t)) == "0123456789");
        lv_obj_t* d = nth(lv_obj_get_parent(t), 1);
        CHECK(lv_textarea_get_one_line(d));                 // default
        CHECK_FALSE(lv_textarea_get_password_mode(d));
    }
    SUBCASE("image source paths are resolved against the screen directory") {
        TempDir td;
        td.png("assets/logo.png");
        std::string abs = td.png("abs.png");                 // "C:/..." on Windows, "/tmp/..." on Linux
        json scr = { { "name", "img" }, { "children", json::array({
            json{ { "type", "image" }, { "src", "assets/logo.png" }, { "zoom", 0.5 }, { "rotation", 90 } },
            json{ { "type", "image" }, { "src", abs } },
            json{ { "type", "image" }, { "src", "assets/missing.png" } },
            json{ { "type", "image" } },
        }) } };
        lv_obj_t* s = ui().build_screen(scr, td.base());
        REQUIRE(s);
        lv_obj_t* img = nth(s, 0);
        CHECK(image_src(img) == "A:" + td.base() + "/assets/logo.png");
        CHECK(lv_image_get_src_width(img) == 3);
        CHECK(lv_image_get_src_height(img) == 2);
        CHECK(lv_image_get_scale(img) == 128);
        CHECK(lv_image_get_rotation(img) == 900);
        CHECK(image_src(nth(s, 1)) == "A:" + abs);
        CHECK(lv_image_get_src_width(nth(s, 1)) == 3);
        CHECK(image_src(nth(s, 2)) == "<null>");            // unreadable file: LVGL drops the source
        CHECK(image_src(nth(s, 3)) == "<null>");
    }
    SUBCASE("line points") {
        lv_obj_t* ln = build_widgets(J(R"([
            { "type": "line", "points": [[0, 0], [10, 20], [30, 40], "bad", [5]] },
            { "type": "line", "points": [[0, 0]] }
        ])"));
        REQUIRE(ln);
        CHECK(lv_line_get_point_count(ln) == 3);
        CHECK(lv_line_get_point_count(nth(lv_obj_get_parent(ln), 1)) == 0);   // fewer than 2 points: ignored
    }
}

TEST_CASE("chart: series follow their tags on the sample timer")
{
    Runtime rt;
    Tag* a = rt.tag("a", TagType::Float, 1.5);
    rt.tag("b", TagType::Int, 3);
    lv_obj_t* chart = build_widget(R"json({ "type": "chart", "chart_type": "bar", "points": 4, "min": -10, "max": 10, "sample_ms": 100,
        "series": [ { "tag": "a", "scale": 10, "color": "red" }, { "tag": "b" }, { "tag": "nope" } ] })json");
    REQUIRE(chart);
    CHECK(lv_chart_get_type(chart) == LV_CHART_TYPE_BAR);
    CHECK(lv_chart_get_point_count(chart) == 4);

    lv_chart_series_t* s1 = lv_chart_get_series_next(chart, nullptr);
    lv_chart_series_t* s2 = lv_chart_get_series_next(chart, s1);
    REQUIRE((s1 && s2));
    CHECK(lv_chart_get_series_next(chart, s2) == nullptr);        // the unknown tag added no series

    // series are added in order, so s1 is the last added? LVGL prepends: find the one holding 15.
    auto has_value = [&](lv_chart_series_t* s, int32_t v) {
        const int32_t* ys = lv_chart_get_series_y_array(chart, s);
        for(uint32_t i = 0; i < 4; i++) if(ys[i] == v) return true;
        return false;
    };
    CHECK((has_value(s1, 15) || has_value(s2, 15)));             // 1.5 * 10 sampled once at build time
    CHECK((has_value(s1, 3) || has_value(s2, 3)));

    a->set_number(-0.7, nullptr);
    rt.advance(100);
    CHECK((has_value(s1, -7) || has_value(s2, -7)));
}

TEST_CASE("keyboard shows for a focused textarea and hides when done")
{
    Runtime rt;
    lv_obj_t* ta = build_widgets(J(R"([ { "type": "textarea" }, { "type": "keyboard" } ])"));
    REQUIRE(ta);
    lv_obj_t* kb = nth(lv_obj_get_parent(ta), 1);
    REQUIRE(lv_obj_check_type(kb, &lv_keyboard_class));
    CHECK(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));

    click(ta);
    CHECK_FALSE(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_keyboard_get_textarea(kb) == ta);

    lv_obj_send_event(ta, LV_EVENT_READY, nullptr);
    CHECK(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));

    lv_obj_send_event(ta, LV_EVENT_FOCUSED, nullptr);
    CHECK_FALSE(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(kb, LV_EVENT_CANCEL, nullptr);          // the keyboard's own ok/close buttons
    CHECK(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
}

// ================================================================ common properties and styles

TEST_CASE("common properties: size, position, flags and states")
{
    Runtime rt;
    lv_obj_t* p = build_widgets(J(R"json([
        { "type": "panel", "w": 120, "h": "50%", "x": 10, "y": "20", "align": "bottom_right", "hidden": true, "scrollable": false, "scrollbar": "off" },
        { "type": "label", "text": "x", "w": "content", "h": "full", "clickable": true, "checkable": true, "checked": true, "disabled": true },
        { "type": "button", "clickable": false }
    ])json"));
    REQUIRE(p);
    CHECK(lv_obj_get_style_width(p, LV_PART_MAIN) == 120);
    CHECK(lv_obj_get_style_height(p, LV_PART_MAIN) == lv_pct(50));
    CHECK(lv_obj_get_style_x(p, LV_PART_MAIN) == 10);
    CHECK(lv_obj_get_style_y(p, LV_PART_MAIN) == 20);
    CHECK(lv_obj_get_style_align(p, LV_PART_MAIN) == LV_ALIGN_BOTTOM_RIGHT);
    CHECK(lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE));
    CHECK(lv_obj_get_scrollbar_mode(p) == LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* l = nth(lv_obj_get_parent(p), 1);
    CHECK(lv_obj_get_style_width(l, LV_PART_MAIN) == LV_SIZE_CONTENT);
    CHECK(lv_obj_get_style_height(l, LV_PART_MAIN) == lv_pct(100));
    CHECK(lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_obj_has_flag(l, LV_OBJ_FLAG_CHECKABLE));
    CHECK(lv_obj_has_state(l, LV_STATE_CHECKED));
    CHECK(lv_obj_has_state(l, LV_STATE_DISABLED));

    lv_obj_t* b = nth(lv_obj_get_parent(p), 2);
    CHECK_FALSE(lv_obj_has_flag(b, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_obj_has_flag(b, LV_OBJ_FLAG_SCROLLABLE) == lv_obj_has_flag(lv_button_create(lv_screen_active()), LV_OBJ_FLAG_SCROLLABLE));
}

TEST_CASE("align names")
{
    Runtime rt;
    struct { const char* name; lv_align_t a; } cases[] = {
        { "center", LV_ALIGN_CENTER }, { "top_left", LV_ALIGN_TOP_LEFT }, { "TOP_MID", LV_ALIGN_TOP_MID },
        { "top_right", LV_ALIGN_TOP_RIGHT }, { "left_mid", LV_ALIGN_LEFT_MID }, { "right_mid", LV_ALIGN_RIGHT_MID },
        { "bottom_left", LV_ALIGN_BOTTOM_LEFT }, { "bottom_mid", LV_ALIGN_BOTTOM_MID }, { "bottom_right", LV_ALIGN_BOTTOM_RIGHT },
        { "top", LV_ALIGN_TOP_MID }, { "bottom", LV_ALIGN_BOTTOM_MID }, { "left", LV_ALIGN_LEFT_MID }, { "right", LV_ALIGN_RIGHT_MID },
        { "diagonal", LV_ALIGN_TOP_LEFT },
    };
    json widgets = json::array();
    for(const auto& c : cases) widgets.push_back(json{ { "type", "panel" }, { "align", c.name } });
    lv_obj_t* first = build_widgets(widgets);
    REQUIRE(first);
    lv_obj_t* parent = lv_obj_get_parent(first);
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CAPTURE(cases[i].name);
        CHECK(lv_obj_get_style_align(nth(parent, static_cast<int32_t>(i)), LV_PART_MAIN) == cases[i].a);
    }
}

TEST_CASE("flex layout")
{
    Runtime rt;
    lv_obj_t* p = build_widgets(J(R"json([
        { "type": "panel", "flex": "row_wrap", "flex_main": "space_between", "flex_cross": "center", "flex_track": "end",
          "children": [ { "type": "panel", "flex_grow": 2 }, { "type": "panel" } ] },
        { "type": "panel", "flex": "column_reverse" },
        { "type": "panel", "flex": "whatever" }
    ])json"));
    REQUIRE(p);
    CHECK(lv_obj_get_style_flex_flow(p, LV_PART_MAIN) == LV_FLEX_FLOW_ROW_WRAP);
    CHECK(lv_obj_get_style_flex_main_place(p, LV_PART_MAIN) == LV_FLEX_ALIGN_SPACE_BETWEEN);
    CHECK(lv_obj_get_style_flex_cross_place(p, LV_PART_MAIN) == LV_FLEX_ALIGN_CENTER);
    CHECK(lv_obj_get_style_flex_track_place(p, LV_PART_MAIN) == LV_FLEX_ALIGN_END);
    CHECK(lv_obj_get_style_flex_grow(nth(p, 0), LV_PART_MAIN) == 2);
    CHECK(lv_obj_get_style_flex_grow(nth(p, 1), LV_PART_MAIN) == 0);
    lv_obj_t* parent = lv_obj_get_parent(p);
    CHECK(lv_obj_get_style_flex_flow(nth(parent, 1), LV_PART_MAIN) == LV_FLEX_FLOW_COLUMN_REVERSE);
    CHECK(lv_obj_get_style_flex_flow(nth(parent, 2), LV_PART_MAIN) == LV_FLEX_FLOW_ROW);   // unknown -> row
}

TEST_CASE("style properties map to LVGL local styles")
{
    Runtime rt;
    lv_obj_t* p = build_widget(R"json({ "type": "panel", "style": {
        "bg_color": "#112233", "bg_grad_color": "#445566", "bg_grad_dir": "hor",
        "border_color": "red", "border_width": 3, "border_opa": 50, "outline_width": 2, "outline_color": "#000",
        "radius": 9, "shadow_width": 12, "shadow_color": "black", "shadow_opa": 20,
        "text_color": "#abcdef", "text_opa": 80, "text_align": "center", "text_letter_space": 2, "text_line_space": 4, "font": 20,
        "pad": 5, "pad_left": 6, "pad_row": 7, "pad_column": 8, "margin": 3, "opa": 40,
        "line_color": "green", "line_width": 4, "line_rounded": true, "arc_color": "blue", "arc_width": 11, "arc_rounded": 1,
        "image_recolor": "white", "width": "25%", "height": 33,
        "not_a_property": 1
    } })json");
    REQUIRE(p);
    CHECK(lv_color_eq(lv_obj_get_style_bg_color(p, LV_PART_MAIN), lv_color_hex(0x112233)));
    CHECK(lv_obj_get_style_bg_opa(p, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(lv_color_eq(lv_obj_get_style_bg_grad_color(p, LV_PART_MAIN), lv_color_hex(0x445566)));
    CHECK(lv_obj_get_style_bg_grad_dir(p, LV_PART_MAIN) == LV_GRAD_DIR_HOR);
    CHECK(lv_color_eq(lv_obj_get_style_border_color(p, LV_PART_MAIN), lv_palette_main(LV_PALETTE_RED)));
    CHECK(lv_obj_get_style_border_width(p, LV_PART_MAIN) == 3);
    CHECK(lv_obj_get_style_border_opa(p, LV_PART_MAIN) == 127);
    CHECK(lv_obj_get_style_outline_width(p, LV_PART_MAIN) == 2);
    CHECK(lv_obj_get_style_radius(p, LV_PART_MAIN) == 9);
    CHECK(lv_obj_get_style_shadow_width(p, LV_PART_MAIN) == 12);
    CHECK(lv_obj_get_style_shadow_opa(p, LV_PART_MAIN) == 51);
    CHECK(lv_color_eq(lv_obj_get_style_text_color(p, LV_PART_MAIN), lv_color_hex(0xabcdef)));
    CHECK(lv_obj_get_style_text_opa(p, LV_PART_MAIN) == 204);
    CHECK(lv_obj_get_style_text_align(p, LV_PART_MAIN) == LV_TEXT_ALIGN_CENTER);
    CHECK(lv_obj_get_style_text_letter_space(p, LV_PART_MAIN) == 2);
    CHECK(lv_obj_get_style_text_line_space(p, LV_PART_MAIN) == 4);
    CHECK(lv_obj_get_style_text_font(p, LV_PART_MAIN) == &lv_font_montserrat_20);
    CHECK(lv_obj_get_style_pad_left(p, LV_PART_MAIN) == 6);       // pad_left after pad overrides it
    CHECK(lv_obj_get_style_pad_right(p, LV_PART_MAIN) == 5);
    CHECK(lv_obj_get_style_pad_top(p, LV_PART_MAIN) == 5);
    CHECK(lv_obj_get_style_pad_row(p, LV_PART_MAIN) == 7);
    CHECK(lv_obj_get_style_pad_column(p, LV_PART_MAIN) == 8);
    CHECK(lv_obj_get_style_margin_left(p, LV_PART_MAIN) == 3);
    CHECK(lv_obj_get_style_opa(p, LV_PART_MAIN) == 102);
    CHECK(lv_color_eq(lv_obj_get_style_line_color(p, LV_PART_MAIN), lv_palette_main(LV_PALETTE_GREEN)));
    CHECK(lv_obj_get_style_line_width(p, LV_PART_MAIN) == 4);
    CHECK(lv_obj_get_style_line_rounded(p, LV_PART_MAIN));
    CHECK(lv_color_eq(lv_obj_get_style_arc_color(p, LV_PART_MAIN), lv_palette_main(LV_PALETTE_BLUE)));
    CHECK(lv_obj_get_style_arc_width(p, LV_PART_MAIN) == 11);
    CHECK(lv_obj_get_style_arc_rounded(p, LV_PART_MAIN));
    CHECK(lv_color_eq(lv_obj_get_style_image_recolor(p, LV_PART_MAIN), lv_color_white()));
    CHECK(lv_obj_get_style_image_recolor_opa(p, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(lv_obj_get_style_width(p, LV_PART_MAIN) == lv_pct(25));
    CHECK(lv_obj_get_style_height(p, LV_PART_MAIN) == 33);
}

TEST_CASE("style_* keys select parts and states")
{
    Runtime rt;
    // (a panel: focusable widgets join the default group and start in LV_STATE_FOCUSED)
    lv_obj_t* s = build_widget(R"json({ "type": "panel",
        "style": { "bg_color": "#010101" },
        "style_pressed": { "bg_color": "#020202" },
        "style_checked": { "bg_color": "#030303" },
        "style_disabled": { "bg_color": "#040404" },
        "style_focused": { "bg_color": "#050505" },
        "style_indicator": { "bg_color": "#060606" },
        "style_indicator_checked": { "bg_color": "#070707" },
        "style_knob": { "bg_color": "#080808" },
        "style_items": { "bg_color": "#090909" },
        "style_selected": { "bg_color": "#0a0a0a" },
        "style_scrollbar": { "bg_color": "#0b0b0b" }
    })json");
    REQUIRE(s);
    auto bg = [&](lv_part_t part) { return lv_color_to_u32(lv_obj_get_style_bg_color(s, part)) & 0xFFFFFF; };
    CHECK(bg(LV_PART_MAIN) == 0x010101);
    CHECK(bg(LV_PART_INDICATOR) == 0x060606);
    CHECK(bg(LV_PART_KNOB) == 0x080808);
    CHECK(bg(LV_PART_ITEMS) == 0x090909);
    CHECK(bg(LV_PART_SELECTED) == 0x0a0a0a);
    CHECK(bg(LV_PART_SCROLLBAR) == 0x0b0b0b);

    lv_obj_add_state(s, LV_STATE_PRESSED);
    CHECK(bg(LV_PART_MAIN) == 0x020202);
    lv_obj_remove_state(s, LV_STATE_PRESSED);
    lv_obj_add_state(s, LV_STATE_CHECKED);
    CHECK(bg(LV_PART_MAIN) == 0x030303);
    CHECK(bg(LV_PART_INDICATOR) == 0x070707);
    lv_obj_remove_state(s, LV_STATE_CHECKED);
    lv_obj_add_state(s, LV_STATE_DISABLED);
    CHECK(bg(LV_PART_MAIN) == 0x040404);
    lv_obj_remove_state(s, LV_STATE_DISABLED);
    lv_obj_add_state(s, LV_STATE_FOCUSED);
    CHECK(bg(LV_PART_MAIN) == 0x050505);
}

TEST_CASE("nested children")
{
    Runtime rt;
    lv_obj_t* p = build_widget(R"json({ "type": "panel", "children": [
        { "type": "panel", "children": [ { "type": "label", "text": "deep" } ] },
        { "type": "label", "text": "shallow" }
    ] })json");
    REQUIRE(p);
    REQUIRE(widget_count(p) == 2);
    lv_obj_t* inner = nth(p, 0);
    REQUIRE(widget_count(inner) == 1);
    CHECK(label_text(nth(inner, 0)) == "deep");
    CHECK(label_text(nth(p, 1)) == "shallow");
}

// ================================================================ bindings: tag -> widget

TEST_CASE("text bindings")
{
    Runtime rt;
    TempDir td;
    std::string png1 = td.png("one.png"), png2 = td.png("two.png");
    Tag* f = rt.tag("f", TagType::Float, 2.5);
    rt.tag("s", std::string("Idle"));
    rt.tag("b", TagType::Bool, 1);
    Tag* img = rt.tag("img", "A:" + png1);              // a bound src is an LVGL path: no base dir, no "A:" added
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "label", "bind": "f" },
        { "type": "label", "bind": { "text": { "tag": "f", "format": "%.2f bar" } } },
        { "type": "label", "bind": { "text": "s" } },
        { "type": "button", "bind": { "text": { "tag": "s", "format": "State: %s" } } },
        { "type": "checkbox", "bind": { "text": "b" } },
        { "type": "textarea", "bind": { "text": "s" } },
        { "type": "image", "bind": { "src": "img" } },
        { "type": "label", "text": "untouched", "bind": { "text": "no_such_tag" } },
        { "type": "label", "text": "untouched2", "bind": { "colour": "f" } }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    auto child = [&](int32_t i) { return nth(scr, i); };

    CHECK(label_text(child(0)) == "2.5");
    CHECK(label_text(child(1)) == "2.50 bar");
    CHECK(label_text(child(2)) == "Idle");
    CHECK(label_text(nth(child(3), 0)) == "State: Idle");
    CHECK(std::string(lv_checkbox_get_text(child(4))) == "true");
    CHECK(std::string(lv_textarea_get_text(child(5))) == "Idle");
    CHECK(image_src(child(6)) == "A:" + png1);
    CHECK(label_text(child(7)) == "untouched");
    CHECK(label_text(child(8)) == "untouched2");

    f->set_number(-1, nullptr);
    tags().find("s")->set_string("Run", nullptr);
    img->set_string("A:" + png2, nullptr);
    rt.pump();
    CHECK(image_src(child(6)) == "A:" + png2);
    CHECK(label_text(child(0)) == "-1");
    CHECK(label_text(child(1)) == "-1.00 bar");
    CHECK(label_text(child(2)) == "Run");
    CHECK(label_text(nth(child(3), 0)) == "State: Run");
    CHECK(std::string(lv_textarea_get_text(child(5))) == "Run");
}

TEST_CASE("value bindings with scale and offset")
{
    Runtime rt;
    Tag* v = rt.tag("v", TagType::Float, 2.0);
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "slider", "min": 0, "max": 100, "bind": "v" },
        { "type": "bar", "min": 0, "max": 100, "bind": { "value": { "tag": "v", "scale": 10 } } },
        { "type": "arc", "min": 0, "max": 100, "bind": { "value": { "tag": "v", "scale": 10, "offset": 5 } } },
        { "type": "dropdown", "options": ["a", "b", "c"], "bind": { "value": "v" } },
        { "type": "roller", "options": ["a", "b", "c"], "bind": { "value": "v" } },
        { "type": "led", "bind": { "value": { "tag": "v", "scale": 100 } } },
        { "type": "label", "bind": { "value": { "tag": "v", "scale": 1.5, "format": "%.1f" } } },
        { "type": "textarea", "bind": { "value": { "tag": "v", "format": "%d" } } }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    auto child = [&](int32_t i) { return nth(scr, i); };

    CHECK(lv_slider_get_value(child(0)) == 2);
    CHECK(lv_bar_get_value(child(1)) == 20);
    CHECK(lv_arc_get_value(child(2)) == 25);
    CHECK(lv_dropdown_get_selected(child(3)) == 2);
    CHECK(lv_roller_get_selected(child(4)) == 2);
    CHECK(lv_led_get_brightness(child(5)) == 200);
    CHECK(label_text(child(6)) == "3.0");
    CHECK(std::string(lv_textarea_get_text(child(7))) == "2");

    v->set_number(0.46, nullptr);
    rt.pump();
    CHECK(lv_slider_get_value(child(0)) == 0);
    CHECK(lv_bar_get_value(child(1)) == 5);               // 4.6 rounds to 5
    CHECK(lv_arc_get_value(child(2)) == 10);              // 4.6 + 5 = 9.6 -> 10
    CHECK(lv_dropdown_get_selected(child(3)) == 0);
    CHECK(lv_led_get_brightness(child(5)) == LV_LED_BRIGHT_MIN);   // 46 is below LVGL's minimum of 80

    SUBCASE("led brightness and selections are clamped") {
        v->set_number(9, nullptr);
        rt.pump();
        CHECK(lv_led_get_brightness(child(5)) == 255);
        CHECK(lv_dropdown_get_selected(child(3)) == 2);   // LVGL clamps to the last option
        v->set_number(-3, nullptr);
        rt.pump();
        CHECK(lv_led_get_brightness(child(5)) == LV_LED_BRIGHT_MIN);
        CHECK(lv_dropdown_get_selected(child(3)) == 0);
        CHECK(lv_roller_get_selected(child(4)) == 0);
    }
}

TEST_CASE("checked bindings and conditions")
{
    Runtime rt;
    Tag* n = rt.tag("n", TagType::Int, 0);
    Tag* s = rt.tag("s", std::string("stop"));
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "switch", "bind": "n" },
        { "type": "switch", "bind": { "checked": { "tag": "n", "eq": 2 } } },
        { "type": "switch", "bind": { "checked": { "tag": "n", "ne": 2 } } },
        { "type": "switch", "bind": { "checked": { "tag": "n", "gt": 1 } } },
        { "type": "switch", "bind": { "checked": { "tag": "n", "lt": 1 } } },
        { "type": "switch", "bind": { "checked": { "tag": "n", "ge": 2 } } },
        { "type": "switch", "bind": { "checked": { "tag": "n", "le": 2 } } },
        { "type": "switch", "bind": { "on": { "tag": "n", "invert": true } } },
        { "type": "switch", "bind": { "checked": { "tag": "s", "eq": "run" } } },
        { "type": "switch", "bind": { "checked": { "tag": "s", "ne": "run" } } },
        { "type": "switch", "bind": { "checked": { "tag": "n", "eq": "2" } } },
        { "type": "led", "bind": "n" },
        { "type": "button", "checkable": true, "bind": "n" }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    auto checked = [&](int32_t i) { return lv_obj_has_state(nth(scr, i), LV_STATE_CHECKED); };

    // n = 0, s = "stop"
    CHECK_FALSE(checked(0));
    CHECK_FALSE(checked(1));
    CHECK(checked(2));
    CHECK_FALSE(checked(3));
    CHECK(checked(4));
    CHECK_FALSE(checked(5));
    CHECK(checked(6));
    CHECK(checked(7));
    CHECK_FALSE(checked(8));
    CHECK(checked(9));
    CHECK_FALSE(checked(10));
    CHECK(lv_led_get_brightness(nth(scr, 11)) == LV_LED_BRIGHT_MIN);
    CHECK_FALSE(checked(12));

    n->set_number(2, nullptr);
    s->set_string("run", nullptr);
    rt.pump();
    CHECK(checked(0));
    CHECK(checked(1));
    CHECK_FALSE(checked(2));
    CHECK(checked(3));
    CHECK_FALSE(checked(4));
    CHECK(checked(5));
    CHECK(checked(6));
    CHECK_FALSE(checked(7));
    CHECK(checked(8));
    CHECK_FALSE(checked(9));
    CHECK(checked(10));
    CHECK(lv_led_get_brightness(nth(scr, 11)) == LV_LED_BRIGHT_MAX);
    CHECK(checked(12));

    n->set_number(3, nullptr);
    rt.pump();
    CHECK(checked(0));
    CHECK_FALSE(checked(1));
    CHECK(checked(3));
    CHECK_FALSE(checked(6));
}

TEST_CASE("visible / hidden / enabled / disabled bindings")
{
    Runtime rt;
    Tag* n = rt.tag("n", TagType::Int, 0);
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "label", "text": "a", "bind": { "visible": "n" } },
        { "type": "label", "text": "b", "bind": { "hidden": "n" } },
        { "type": "button", "bind": { "enabled": { "tag": "n", "ge": 1 } } },
        { "type": "button", "bind": { "disabled": { "tag": "n", "ge": 1 } } },
        { "type": "label", "text": "c", "bind": { "visible": { "tag": "n", "invert": true } } }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    auto hidden = [&](int32_t i) { return lv_obj_has_flag(nth(scr, i), LV_OBJ_FLAG_HIDDEN); };
    auto disabled = [&](int32_t i) { return lv_obj_has_state(nth(scr, i), LV_STATE_DISABLED); };

    CHECK(hidden(0));
    CHECK_FALSE(hidden(1));
    CHECK(disabled(2));
    CHECK_FALSE(disabled(3));
    CHECK_FALSE(hidden(4));

    n->set_number(1, nullptr);
    rt.pump();
    CHECK_FALSE(hidden(0));
    CHECK(hidden(1));
    CHECK_FALSE(disabled(2));
    CHECK(disabled(3));
    CHECK(hidden(4));
}

TEST_CASE("color map bindings")
{
    Runtime rt;
    Tag* alarm = rt.tag("alarm", TagType::Int, 0);
    Tag* online = rt.tag("online", TagType::Bool, 0);
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "panel", "style": { "bg_color": "#000000" }, "bind": { "bg_color": { "tag": "alarm", "map": { "0": "#00ff00", "1": "#ffff00", "default": "#ff0000" } } } },
        { "type": "label", "text": "PLC", "style": { "text_color": "#000000" }, "bind": { "text_color": { "tag": "online", "map": { "true": "#4caf50", "false": "#78909c" } } } },
        { "type": "button", "text": "B", "bind": { "text_color": { "tag": "online", "map": { "true": "#ffffff" } } } },
        { "type": "slider", "bind": { "bg_color": { "tag": "alarm", "part": "indicator", "map": { "0": "#111111", "1": "#222222" } } } },
        { "type": "panel", "style": { "bg_color": "#000000" }, "bind": { "bg_color": { "tag": "alarm", "map": { "7": "#777777", "ignored": 5 } } } }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    auto bg = [&](int32_t i, lv_part_t part = LV_PART_MAIN) { return lv_color_to_u32(lv_obj_get_style_bg_color(nth(scr, i), part)) & 0xFFFFFF; };
    auto fg = [&](lv_obj_t* o) { return lv_color_to_u32(lv_obj_get_style_text_color(o, LV_PART_MAIN)) & 0xFFFFFF; };

    CHECK(bg(0) == 0x00ff00);
    CHECK(fg(nth(scr, 1)) == 0x78909c);
    CHECK(bg(3, LV_PART_INDICATOR) == 0x111111);
    CHECK(bg(4) == 0x000000);                                  // no match, no default: untouched

    lv_obj_t* btn_label = nth(nth(scr, 2), 0);

    alarm->set_number(1, nullptr);
    online->set_number(1, nullptr);
    rt.pump();
    CHECK(bg(0) == 0xffff00);
    CHECK(fg(nth(scr, 1)) == 0x4caf50);
    CHECK(fg(btn_label) == 0xffffff);
    CHECK(bg(3, LV_PART_INDICATOR) == 0x222222);

    alarm->set_number(5, nullptr);
    rt.pump();
    CHECK(bg(0) == 0xff0000);                                  // default entry
    CHECK(bg(3, LV_PART_INDICATOR) == 0x222222);               // no default: keeps the last color
}

TEST_CASE("options bindings replace dropdown and roller entries")
{
    Runtime rt;
    Tag* opts = rt.tag("opts", std::string("A\nB"));
    lv_obj_t* d = build_widgets(J(R"json([
        { "type": "dropdown", "options": ["x"], "bind": { "options": "opts" } },
        { "type": "roller", "options": ["x"], "bind": { "options": "opts" } }
    ])json"));
    REQUIRE(d);
    lv_obj_t* r = nth(lv_obj_get_parent(d), 1);
    CHECK(std::string(lv_dropdown_get_options(d)) == "A\nB");
    CHECK(std::string(lv_roller_get_options(r)) == "A\nB");
    opts->set_string("1\n2\n3", nullptr);
    rt.pump();
    CHECK(lv_dropdown_get_option_count(d) == 3);
    CHECK(lv_roller_get_option_count(r) == 3);
}

// ================================================================ bindings: widget -> tag

TEST_CASE("two-way bindings write back to the tag")
{
    Runtime rt;
    Tag* v = rt.tag("v", TagType::Float, 0);
    Tag* on = rt.tag("on", TagType::Bool, 0);
    Tag* mode = rt.tag("mode", TagType::Int, 0);
    Tag* txt = rt.tag("txt", std::string(""));
    Tag* num = rt.tag("num", TagType::Float, 0);
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "slider", "min": 0, "max": 100, "bind": { "value": { "tag": "v", "scale": 10, "offset": 5 } } },
        { "type": "switch", "bind": "on" },
        { "type": "button", "checkable": true, "bind": { "checked": { "tag": "mode", "eq": 2 } } },
        { "type": "textarea", "bind": "txt" },
        { "type": "dropdown", "options": ["a", "b", "c"], "bind": "mode" },
        { "type": "textarea", "bind": { "value": "num" } },
        { "type": "switch", "bind": { "checked": { "tag": "on", "invert": true } } },
        { "type": "slider", "bind": { "value": { "tag": "v", "two_way": false } } },
        { "type": "bar", "bind": "v" },
        { "type": "label", "bind": { "text": { "tag": "v", "format": "%.1f" } } },
        { "type": "arc", "min": 0, "max": 100, "bind": "v" },
        { "type": "roller", "options": ["a", "b", "c"], "bind": "mode" }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    auto child = [&](int32_t i) { return nth(scr, i); };

    SUBCASE("slider: (widget - offset) / scale") {
        lv_slider_set_value(child(0), 45, LV_ANIM_OFF);
        value_changed(child(0));
        CHECK(v->get_number() == doctest::Approx(4));
        rt.pump();
        CHECK(lv_slider_get_value(child(0)) == 45);          // the echo does not disturb the widget
        CHECK(lv_bar_get_value(child(8)) == 4);               // but other bindings follow
        CHECK(label_text(child(9)) == "4.0");
        CHECK(lv_slider_get_value(child(7)) == 4);
    }
    SUBCASE("switch: checked state -> 0/1, inverted switch -> inverse") {
        lv_obj_add_state(child(1), LV_STATE_CHECKED);
        value_changed(child(1));
        CHECK(on->get_number() == doctest::Approx(1));
        rt.pump();
        CHECK_FALSE(lv_obj_has_state(child(6), LV_STATE_CHECKED));

        lv_obj_remove_state(child(1), LV_STATE_CHECKED);
        value_changed(child(1));
        CHECK(on->get_number() == doctest::Approx(0));

        lv_obj_add_state(child(6), LV_STATE_CHECKED);        // inverted: checked means "off"
        value_changed(child(6));
        CHECK(on->get_number() == doctest::Approx(0));
        lv_obj_remove_state(child(6), LV_STATE_CHECKED);
        value_changed(child(6));
        CHECK(on->get_number() == doctest::Approx(1));
    }
    SUBCASE("button with eq: checking writes the compared value, unchecking leaves the tag alone") {
        lv_obj_add_state(child(2), LV_STATE_CHECKED);
        value_changed(child(2));
        CHECK(mode->get_number() == doctest::Approx(2));
        lv_obj_remove_state(child(2), LV_STATE_CHECKED);
        value_changed(child(2));
        CHECK(mode->get_number() == doctest::Approx(2));
    }
    SUBCASE("textarea text and numeric value") {
        lv_textarea_set_text(child(3), "operator");
        value_changed(child(3));
        CHECK(txt->get().str == "operator");
        lv_textarea_set_text(child(5), "12.5");
        value_changed(child(5));
        CHECK(num->get_number() == doctest::Approx(12.5));
    }
    SUBCASE("dropdown / roller selection") {
        lv_dropdown_set_selected(child(4), 1);
        value_changed(child(4));
        CHECK(mode->get_number() == doctest::Approx(1));
        rt.pump();
        CHECK(lv_roller_get_selected(child(11)) == 1);
        CHECK(lv_obj_has_state(child(2), LV_STATE_CHECKED) == false);
        lv_roller_set_selected(child(11), 2, LV_ANIM_OFF);
        value_changed(child(11));
        CHECK(mode->get_number() == doctest::Approx(2));
        rt.pump();
        CHECK(lv_obj_has_state(child(2), LV_STATE_CHECKED));
    }
    SUBCASE("one-way widgets do not write") {
        lv_slider_set_value(child(7), 77, LV_ANIM_OFF);
        value_changed(child(7));
        CHECK(v->get_number() == doctest::Approx(0));
        lv_bar_set_value(child(8), 50, LV_ANIM_OFF);
        value_changed(child(8));
        CHECK(v->get_number() == doctest::Approx(0));
        lv_label_set_text(child(9), "zzz");
        value_changed(child(9));
        CHECK(v->get_number() == doctest::Approx(0));
    }
    SUBCASE("arc") {
        lv_arc_set_value(child(10), 33);
        value_changed(child(10));
        CHECK(v->get_number() == doctest::Approx(33));
    }
}

// ================================================================ actions

TEST_CASE("actions on click")
{
    Runtime rt;
    Tag* cnt = rt.tag("cnt", TagType::Int, 0);
    Tag* run = rt.tag("run", TagType::Bool, 0);
    Tag* txt = rt.tag("txt", std::string(""));
    Tag* f = rt.tag("f", TagType::Float, 0);
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "button", "on_click": { "action": "inc", "tag": "cnt" } },
        { "type": "button", "on_click": { "action": "inc", "tag": "cnt", "by": 10 } },
        { "type": "button", "on_click": { "action": "dec", "tag": "cnt", "by": 3 } },
        { "type": "button", "on_click": { "action": "toggle", "tag": "run" } },
        { "type": "button", "on_click": [ { "action": "set", "tag": "txt", "value": "hello" }, { "action": "set", "tag": "f", "value": 2.5 }, { "action": "set", "tag": "run", "value": true } ] },
        { "type": "button", "on_click": { "action": "set", "tag": "cnt" } },
        { "type": "button", "on_click": { "action": "reload" } },
        { "type": "button", "on_click": { "action": "exit" } },
        { "type": "label", "text": "tap me", "on_click": { "action": "inc", "tag": "cnt" } },
        { "type": "button", "on_click": [ { "tag": "cnt" }, { "action": "explode", "tag": "cnt" }, { "action": "inc", "tag": "ghost" }, { "action": "inc", "tag": "cnt" } ] }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    auto child = [&](int32_t i) { return nth(scr, i); };

    click(child(0));
    CHECK(cnt->get_number() == doctest::Approx(1));
    click(child(1));
    CHECK(cnt->get_number() == doctest::Approx(11));
    click(child(2));
    CHECK(cnt->get_number() == doctest::Approx(8));

    click(child(3));
    CHECK(run->get_number() == doctest::Approx(1));
    click(child(3));
    CHECK(run->get_number() == doctest::Approx(0));

    click(child(4));
    CHECK(txt->get().str == "hello");
    CHECK(f->get_number() == doctest::Approx(2.5));
    CHECK(run->get_number() == doctest::Approx(1));

    click(child(5));
    CHECK(cnt->get_number() == doctest::Approx(0));           // set without value -> 0

    click(child(6));
    CHECK(rt.reload_requests == 1);
    click(child(7));
    CHECK(rt.exit_requests == 1);

    CHECK(lv_obj_has_flag(child(8), LV_OBJ_FLAG_CLICKABLE));  // labels with actions become clickable
    click(child(8));
    CHECK(cnt->get_number() == doctest::Approx(1));

    click(child(9));                                          // invalid entries are skipped, valid ones run
    CHECK(cnt->get_number() == doctest::Approx(2));
}

TEST_CASE("restart action reports its target to the application")
{
    Runtime rt;
    lv_obj_t* first = build_widgets(J(R"json([
        { "type": "button", "on_click": { "action": "restart" } },
        { "type": "button", "on_click": { "action": "restart", "what": "app" } },
        { "type": "button", "on_click": { "action": "restart", "what": "SYSTEM" } },
        { "type": "button", "on_click": { "action": "restart", "what": "reboot" } },
        { "type": "button", "on_click": { "action": "restart", "what": "nonsense" } }
    ])json"));
    REQUIRE(first);
    lv_obj_t* scr = lv_obj_get_parent(first);
    click(nth(scr, 0));
    CHECK(rt.restart_requests == 1);
    CHECK(rt.last_restart == RestartTarget::Default);
    click(nth(scr, 1));
    CHECK(rt.last_restart == RestartTarget::App);
    click(nth(scr, 2));
    CHECK(rt.last_restart == RestartTarget::System);
    click(nth(scr, 3));
    CHECK(rt.last_restart == RestartTarget::System);
    click(nth(scr, 4));
    CHECK(rt.last_restart == RestartTarget::Default);      // unknown target: warned, default
    CHECK(rt.restart_requests == 5);
    CHECK(rt.exit_requests == 0);
}

TEST_CASE("actions on other events")
{
    Runtime rt;
    Tag* cnt = rt.tag("cnt", TagType::Int, 0);
    lv_obj_t* b = build_widget(R"json({ "type": "button",
        "on_press": { "action": "set", "tag": "cnt", "value": 1 },
        "on_release": { "action": "set", "tag": "cnt", "value": 2 },
        "on_long_press": { "action": "set", "tag": "cnt", "value": 3 },
        "on_long_press_repeat": { "action": "set", "tag": "cnt", "value": 4 },
        "on_change": { "action": "set", "tag": "cnt", "value": 5 },
        "on_ready": { "action": "set", "tag": "cnt", "value": 6 }
    })json");
    REQUIRE(b);
    struct { lv_event_code_t ev; double expect; } cases[] = {
        { LV_EVENT_PRESSED, 1 }, { LV_EVENT_RELEASED, 2 }, { LV_EVENT_LONG_PRESSED, 3 },
        { LV_EVENT_LONG_PRESSED_REPEAT, 4 }, { LV_EVENT_VALUE_CHANGED, 5 }, { LV_EVENT_READY, 6 },
    };
    for(const auto& c : cases) {
        lv_obj_send_event(b, c.ev, nullptr);
        CHECK(cnt->get_number() == doctest::Approx(c.expect));
    }
    lv_obj_send_event(b, LV_EVENT_CLICKED, nullptr);          // no on_click: unchanged
    CHECK(cnt->get_number() == doctest::Approx(6));
}

TEST_CASE("goto and back actions navigate between screens")
{
    Runtime rt;
    lv_obj_t* home = build(J(R"json({ "name": "home", "children": [
        { "type": "button", "on_click": { "action": "goto", "screen": "settings", "anim": "none" } },
        { "type": "button", "on_click": { "action": "goto", "screen": "missing" } }
    ] })json"));
    lv_obj_t* settings = ui().build_screen(J(R"json({ "name": "settings", "children": [
        { "type": "button", "on_click": { "action": "back" } },
        { "type": "button", "on_click": { "action": "goto", "screen": "home" } }
    ] })json"), "");
    REQUIRE((home && settings));
    CHECK(ui().current_screen() == std::optional<std::string>("home"));

    click(nth(home, 0));
    CHECK(ui().current_screen() == std::optional<std::string>("settings"));
    CHECK(lv_screen_active() == settings);

    click(nth(settings, 0));                     // back
    CHECK(ui().current_screen() == std::optional<std::string>("home"));
    rt.advance(400);
    CHECK(lv_screen_active() == home);

    click(nth(home, 1));                         // unknown screen: stays
    CHECK(ui().current_screen() == std::optional<std::string>("home"));

    click(nth(home, 0));
    click(nth(settings, 1));                     // default anim is a fade
    CHECK(ui().current_screen() == std::optional<std::string>("home"));
    rt.advance(400);
    CHECK(lv_screen_active() == home);
}

TEST_CASE("Ui::init may be called again to replace the app callbacks")
{
    Runtime rt;
    int reloads = 0;
    ui().init(nullptr, AppCallbacks{ [&] { reloads++; }, nullptr, nullptr });
    lv_obj_t* b = build_widgets(J(R"json([ { "type": "button", "on_click": { "action": "reload" } }, { "type": "button", "on_click": { "action": "exit" } } ])json"));
    REQUIRE(b);
    click(b);
    click(nth(lv_obj_get_parent(b), 1));         // null exit callback is tolerated
    CHECK(reloads == 1);
    CHECK(rt.reload_requests == 0);
}
