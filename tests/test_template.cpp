// Template screens: "template": "<screen>" builds a screen on top of another one - a header, a
// navigation that are the same on every screen and are changed in one place (ui.hpp).
// Grouped by ISO/IEC 25010 characteristic (issue #71).
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "check.hpp"
#include "util.hpp"

#include <filesystem>
#include <fstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

json J(const char* t) { return json::parse(t); }
void add(const char* text) { REQUIRE(ui().add_screen(J(text), "/base")); }
void click(lv_obj_t* o) { lv_obj_send_event(o, LV_EVENT_CLICKED, nullptr); }

// the texts of the labels and buttons of the screen that is shown, in the order they were built
std::vector<std::string> texts()
{
    std::vector<std::string> out;
    lv_obj_t* s = lv_screen_active();
    for(uint32_t i = 0; i < lv_obj_get_child_count(s); i++) {
        lv_obj_t* o = lv_obj_get_child(s, static_cast<int32_t>(i));
        if(lv_obj_check_type(o, &lv_label_class)) out.push_back(lv_label_get_text(o));
        else if(lv_obj_check_type(o, &lv_button_class) && lv_obj_get_child_count(o) > 0) out.push_back(std::string("[") + lv_label_get_text(lv_obj_get_child(o, 0)) + "]");
        else out.push_back("?");
    }
    return out;
}
lv_obj_t* child(uint32_t i) { return lv_obj_get_child(lv_screen_active(), static_cast<int32_t>(i)); }
void show(Runtime& rt, const char* name)
{
    REQUIRE(ui().show_screen(name, "none"));
    rt.advance(50);
}

const char* kFrame = R"~({ "name": "frame", "style": { "bg_color": "#102030" }, "scrollable": false, "children": [
    { "type": "label", "x": 8, "y": 4, "text": "Plant - ${title}" },
    { "type": "label", "x": 200, "y": 4, "bind": { "text": { "tag": "clock", "format": "%d" } } },
    { "type": "button", "x": 8, "y": 200, "text": "Home", "on_click": { "action": "goto", "screen": "home" } },
    { "type": "button", "x": 100, "y": 200, "text": "+1", "on_click": { "action": "inc", "tag": "count" } } ] })~";

} // namespace

// ================================================================ functional suitability

TEST_CASE("template: the widgets of the template come first, the screen's own on top; texts take what the screen gives")
{
    Runtime rt;
    Tag* clock = rt.tag("clock", TagType::Int, 7);
    Tag* count = rt.tag("count", TagType::Int, 0);
    // (the screens that use it are added before the template: the order of the files does not matter)
    add(R"~({ "name": "home", "template": "frame", "template_args": { "title": "Overview" }, "children": [ { "type": "label", "x": 8, "y": 60, "text": "home body" } ] })~");
    add(R"~({ "name": "pump", "template": "frame", "template_args": { "title": "Pump 2" }, "style": { "bg_color": "#000000" }, "children": [
        { "type": "label", "x": 8, "y": 60, "text": "pump body" }, { "type": "label", "x": 8, "y": 90, "bind": { "text": { "tag": "count", "format": "n=%d" } } } ] })~");
    add(kFrame);
    show(rt, "home");
    CHECK(texts() == std::vector<std::string>{ "Plant - Overview", "7", "[Home]", "[+1]", "home body" });
    CHECK_FALSE(lv_obj_has_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE));        // what the template says about the screen itself
    CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0x102030)));
    show(rt, "pump");
    CHECK(texts() == std::vector<std::string>{ "Plant - Pump 2", "7", "[Home]", "[+1]", "pump body", "n=0" });
    CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(0x000000)));      // the screen's own style wins
    // the widgets of the template are alive on every screen: bindings and actions
    clock->set_number(8, nullptr); rt.pump(); rt.advance(20);
    CHECK(texts()[1] == "8");
    click(child(3));
    rt.pump(); rt.advance(20);
    CHECK(count->get_number() == 1);
    CHECK(texts()[5] == "n=1");
    click(child(2));                                             // "Home" of the template, pressed on the pump screen
    rt.advance(50);
    CHECK(ui().current_screen().value_or("") == "home");
    CHECK(texts()[1] == "8");                                    // (the other copy of the clock followed too)
    // the template is a screen like any other
    show(rt, "frame");
    CHECK(texts() == std::vector<std::string>{ "Plant - ${title}", "8", "[Home]", "[+1]" });
    ui().unload_all();
}

TEST_CASE("template: placeholders - the name of the screen, numbers, what nobody gives; a template on a template")
{
    Runtime rt;
    add(R"~({ "name": "base", "children": [ { "type": "label", "text": "base of ${screen}" } ] })~");
    add(R"~({ "name": "mid", "template": "base", "children": [ { "type": "label", "text": "${a}|${b}|${none}|${" }, { "type": "label", "text": "no placeholder $ {a} {a} $a" },
        { "type": "dropdown", "options": ["${a}", "x${b}y"] } ] })~");
    add(R"~({ "name": "top", "template": "mid", "template_args": { "a": "A", "b": 5, "c": true, "list": [1], "obj": {}, "screen": "mine" }, "children": [ { "type": "label", "text": "own ${a}" } ] })~");
    show(rt, "top");
    const auto t = texts();
    REQUIRE(t.size() >= 5);                                      // (a dropdown brings its list along)
    CHECK(t[0] == "base of mine");                               // (an argument named "screen" replaces the built-in one)
    CHECK(t[1] == "A|5||${");                                    // what is not closed stays as it is
    CHECK(t[2] == "no placeholder $ {a} {a} $a");
    CHECK(std::string(lv_dropdown_get_options(child(3))) == "A\nx5y");
    CHECK(t.back() == "own ${a}");                                   // only the template's texts are filled
    show(rt, "mid");
    CHECK(texts()[0] == "base of mid");
    CHECK(texts()[1] == "${a}|${b}|${none}|${");                 // shown by itself, its own texts are as they are written
    ui().unload_all();
}

// ================================================================ reliability

TEST_CASE("template: one that is not there, a popup, itself, a ring - the screen is built with what there is")
{
    Runtime rt;
    add(R"~({ "name": "ghost", "template": "nowhere", "children": [ { "type": "label", "text": "g" } ] })~");
    add(R"~({ "name": "self", "template": "self", "children": [ { "type": "label", "text": "s" } ] })~");
    add(R"~({ "name": "a", "template": "b", "children": [ { "type": "label", "text": "a" } ] })~");
    add(R"~({ "name": "b", "template": "c", "children": [ { "type": "label", "text": "b" } ] })~");
    add(R"~({ "name": "c", "template": "a", "children": [ { "type": "label", "text": "c" } ] })~");
    add(R"~({ "name": "pop", "popup": true, "children": [ { "type": "label", "text": "p" } ] })~");
    add(R"~({ "name": "onpop", "template": "pop", "children": [ { "type": "label", "text": "o" } ] })~");
    add(R"~({ "name": "odd", "template": 5, "template_args": "x", "children": [ { "type": "label", "text": "odd" } ] })~");
    show(rt, "ghost");
    CHECK(texts() == std::vector<std::string>{ "g" });
    show(rt, "self");
    CHECK(texts() == std::vector<std::string>{ "s" });
    show(rt, "a");
    CHECK(texts() == std::vector<std::string>{ "c", "b", "a" });  // as far as the ring goes, each once
    show(rt, "onpop");
    CHECK(texts() == std::vector<std::string>{ "o" });
    show(rt, "odd");
    CHECK(texts() == std::vector<std::string>{ "odd" });
    // twenty templates on top of each other: built up to the limit, no endless work
    for(int i = 0; i < 20; i++) {
        const std::string s = "{ \"name\": \"t" + std::to_string(i) + "\"" + (i < 19 ? ", \"template\": \"t" + std::to_string(i + 1) + "\"" : "") +
                              ", \"children\": [ { \"type\": \"label\", \"text\": \"" + std::to_string(i) + "\" } ] }";
        add(s.c_str());
    }
    show(rt, "t0");
    CHECK(texts().size() <= 10);
    CHECK(texts().back() == "0");
    ui().unload_all();
}

// ================================================================ performance efficiency

TEST_CASE("template: 50 screens on one template of 40 widgets are built in a moment")
{
    Runtime rt;
    rt.tag("v", TagType::Int, 1);
    std::string frame = R"({ "name": "frame", "children": [)";
    for(int i = 0; i < 40; i++) frame += std::string(i ? "," : "") + R"({ "type": "label", "x": )" + std::to_string(i * 5) + R"(, "y": 0, "bind": { "text": { "tag": "v", "format": "${title} %d" } } })";
    frame += "] }";
    add(frame.c_str());
    for(int i = 0; i < 50; i++) {
        const std::string s = "{ \"name\": \"s" + std::to_string(i) + "\", \"template\": \"frame\", \"template_args\": { \"title\": \"T" + std::to_string(i) + "\" }, \"children\": [ { \"type\": \"label\", \"text\": \"own\" } ] }";
        add(s.c_str());
    }
    const uint32_t t0 = millis();
    CHECK(ui().build_pending(1000) == 0);
    const uint32_t ms = millis() - t0;
    MESSAGE("51 screens, 2090 widgets, built in " << ms << " ms");
    CHECK(ms < 20000);
    show(rt, "s49");
    CHECK(texts().size() == 41);
    CHECK(texts()[0] == "T49 1");
    ui().unload_all();
}

// ================================================================ maintainability: the checker

TEST_CASE("template: --check finds a template that is not there, a ring, a popup, and a text nobody fills")
{
    const fs::path dir = fs::temp_directory_path() / "hml_template_check_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "screens");
    auto problems = [&](const std::vector<std::string>& screens) {
        std::string list;
        for(size_t i = 0; i < screens.size(); i++) {
            const std::string f = "screens/s" + std::to_string(i) + ".json";
            list += std::string(i ? "," : "") + "\"" + f + "\"";
            std::ofstream o(dir / f, std::ios::binary);
            o << screens[i];
        }
        { std::ofstream o(dir / "project.json", std::ios::binary); o << "{ \"start_screen\": \"home\", \"tags\": [], \"screens\": [" << list << "] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    const std::string frame = R"({ "name": "frame", "children": [ { "type": "label", "text": "${title} on ${screen}" }, { "type": "panel", "children": [ { "type": "label", "text": "${unit}" } ] } ] })";
    CHECK(problems({ R"({ "name": "home", "template": "frame", "template_args": { "title": "Home", "unit": 5 }, "children": [] })", frame }) == "");
    const std::string out = problems({
        R"({ "name": "home", "template": "frame", "template_args": { "title": "Home", "extra": 1, "bad": [1] }, "children": [] })", frame,
        R"({ "name": "a", "template": "nowhere", "children": [] })", R"({ "name": "b", "template": "b", "children": [] })",
        R"({ "name": "c", "template": "d", "children": [] })", R"({ "name": "d", "template": "c", "children": [] })",
        R"({ "name": "pop", "popup": true, "children": [] })", R"({ "name": "e", "template": "pop", "children": [] })",
        R"({ "name": "f", "template": 5, "template_args": "x", "children": [] })", R"({ "name": "g", "template_args": { "title": "x" }, "children": [] })" });
    CHECK(out.find("screens/s0.json: template: \"frame\" has ${unit}, which \"template_args\" does not give") != std::string::npos);
    CHECK(out.find("screens/s0.json: template_args: \"extra\" is not used by the template") != std::string::npos);
    CHECK(out.find("screens/s0.json: template_args: \"bad\" is a text, a number or true / false") != std::string::npos);
    CHECK(out.find("screens/s2.json: template: unknown screen \"nowhere\"") != std::string::npos);
    CHECK(out.find("screens/s3.json: template: a screen is not its own template") != std::string::npos);
    CHECK(out.find("screens/s4.json: template: the templates form a ring") != std::string::npos);
    CHECK(out.find("screens/s5.json: template: the templates form a ring") != std::string::npos);
    CHECK(out.find("screens/s7.json: template: \"pop\" is a popup") != std::string::npos);
    CHECK(out.find("screens/s8.json: template: the name of a screen") != std::string::npos);
    CHECK(out.find("screens/s8.json: template_args: an object { \"name\": value }") != std::string::npos);
    CHECK(out.find("screens/s9.json: template_args without \"template\"") != std::string::npos);
    CHECK(out.find("unknown key \"template\"") == std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
