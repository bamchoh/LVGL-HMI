// hml_runtime --check (check.hpp): what is wrong in a project, found from its files.
#include <doctest.h>
#include "check.hpp"
#include "jsonutil.hpp"
#include "lvgl_fixture.hpp"
#include "screenshot.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef HML_TEST_SOURCE_DIR
#  error "HML_TEST_SOURCE_DIR must point at the repository root"
#endif

using namespace hml;
namespace fs = std::filesystem;

namespace {

struct Proj {
    fs::path dir;
    Proj()
    {
        static int n = 0;
        dir = fs::temp_directory_path() / ("hml_check_test_" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir / "screens");
    }
    ~Proj() { std::error_code ec; fs::remove_all(dir, ec); }
    void write(const std::string& rel, const std::string& text)
    {
        std::ofstream f(dir / rel, std::ios::binary);
        f << text;
    }
    std::vector<std::string> check(std::vector<std::string>* screens = nullptr)
    {
        std::vector<std::string> out;
        for(const CheckProblem& p : check_project((dir / "project.json").generic_string(), screens)) out.push_back(p.line());
        return out;
    }
};

bool has(const std::vector<std::string>& lines, const std::string& part)
{
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

const char* kProject = R"json({
  "start_screen": "a", "languages": ["ja", "en"],
  "tags": [ { "name": "temp", "type": "float" }, { "name": "run", "type": "bool" } ],
  "texts": { "title": { "ja": "T", "en": "T" } },
  "scripts": [ { "name": "reset", "code": "tag.run = false" } ],
  "data_log": { "groups": [ { "name": "g", "tags": ["temp"] } ] },
  "drivers": [ { "type": "sim", "items": [ { "tag": "temp" }, { "tag": "ghost" } ], "status_tag": "nobody" } ],
  "screens": [ "screens/a.json", "screens/p.json" ] })json";

} // namespace

TEST_CASE("check: a correct project has no problems; the screens are named")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "tags": [ { "name": "temp", "type": "float" } ],
        "screens": [ "screens/a.json", { "name": "inline", "children": [ { "type": "label", "bind": "temp" } ] } ] })json");
    p.write("screens/a.json", R"json({ // comments are allowed
        "name": "a", "style": { "bg_color": "#000" }, "children": [
        { "type": "panel", "flex": "row", "_note": "ignored", "children": [
          { "type": "label", "text": "@not_an_id_without_texts", "bind": { "text": { "tag": "temp", "format": "%.1f" }, "visible": { "tag": "temp", "gt": { "tag": "temp" } } } },
          { "type": "button", "text": "Go", "on_click": [ { "action": "goto", "screen": "inline", "anim": "fade" }, { "action": "set", "tag": "sys.lang", "value": "en" } ] } ] } ] })json");
    std::vector<std::string> screens;
    const auto lines = p.check(&screens);
    for(const auto& l : lines) MESSAGE(l);
    CHECK(lines.empty());
    CHECK(screens == std::vector<std::string>{ "a", "inline" });
}

TEST_CASE("check: what the runtime would silently ignore is found, with the place")
{
    Proj p;
    p.write("project.json", kProject);
    p.write("screens/p.json", R"json({ "name": "p", "popup": true, "children": [] })json");
    p.write("notes.txt", "not a picture");
    p.write("screens/a.json", R"json({ "name": "a", "bind": "temp", "children": [
        { "type": "gauge" },
        { "type": "label", "colour": "red", "text": "@nope", "style": { "bg_colour": "#fff" } },
        { "type": "label", "bind": "nope" },
        { "type": "label", "bind": { "colour": "temp", "text": { "tag": "nope2", "fmt": "%d" }, "visible": { "tag": "temp", "gt": { "tag": "nope3" } } } },
        { "type": "button", "on_click": [ { "action": "goto", "screen": "zzz" }, { "action": "fly" }, { "action": "popup", "screen": "a" },
                                          { "action": "set", "tag": "nope4", "valeu": 1 }, { "action": "script", "name": "nope5" },
                                          { "action": "language", "lang": "fr" }, { "action": "goto", "screen": "p", "anim": "spin" } ] },
        { "type": "image", "src": "assets/none.png", "style": { "bg_image": "../x.png" },
          "bind": { "src": { "tag": "temp", "map": { "0": "assets/gone.jpg", "default": "notes.txt" } } } },
        { "type": "trend", "group": "g", "tags": ["temp", "run"] },
        { "type": "trend", "group": "nogroup" },
        { "type": "recipe_table", "book": "nobook" },
        { "type": "alarm_list" },
        { "type": "panel", "children": [ { "type": "switch", "bind": "nope6" } ] },
        { "type": "label", "animate": [ { "preset": "blink", "when": "temp" }, "spin", { "prop": "x", "from": 0, "to": 9, "when": { "tag": "temp", "gt": 5 } },
                                        { "prop": "colour", "to": 1 }, { "preset": "wobble" }, { "prop": "y" }, { "prop": "opa", "to": 0, "time": 50, "speed": 2 },
                                        { "prop": "frames", "files": ["a.png"] }, { "preset": "pulse", "when": "nope7", "path": "bouncy" } ] } ] })json");
    const auto lines = p.check();
    auto expect = [&](const char* part) { CAPTURE(part); CHECK(has(lines, part)); };
    expect("screens/a.json: unknown key \"bind\"");
    expect("screens/a.json: children[0] (gauge): unknown widget type \"gauge\"");
    expect("children[1] (label): unknown key \"colour\"");
    expect("children[1] (label): text: \"@nope\" is not an id of \"texts\"");
    expect("children[1] (label): style: unknown style property \"bg_colour\"");
    expect("children[2] (label): bind: unknown tag \"nope\"");
    expect("children[3] (label): unknown bind \"colour\"");
    expect("bind.text: unknown tag \"nope2\"");
    expect("bind.text: unknown key \"fmt\"");
    expect("bind.visible.gt: unknown tag \"nope3\"");
    expect("on_click (goto): unknown screen \"zzz\"");
    expect("on_click: unknown action \"fly\"");
    expect("on_click (popup): screen \"a\" is not a popup");
    expect("on_click (set): unknown tag \"nope4\"");
    expect("on_click (set): unknown key \"valeu\"");
    expect("on_click (script): unknown script \"nope5\"");
    expect("on_click (language): \"fr\" is not a language");
    expect("on_click (goto): unknown anim \"spin\"");
    expect("children[5] (image): src: no file \"assets/none.png\"");
    expect("children[5] (image): style.bg_image: \"../x.png\" is outside the project folder");
    expect("children[5] (image): bind.src.map.0: no file \"assets/gone.jpg\"");
    expect("children[5] (image): bind.src.map.default: \"notes.txt\" is not a PNG, JPEG, BMP or GIF");
    expect("children[6] (trend): data log group \"g\" does not record \"run\"");
    expect("children[7] (trend): unknown data log group \"nogroup\"");
    expect("children[8] (recipe_table): unknown recipe book \"nobook\"");
    expect("children[9] (alarm_list): the project has no \"alarms\"");
    expect("children[10].children[0] (switch): bind: unknown tag \"nope6\"");
    expect("children[11] (label): animate: unknown prop \"colour\"");
    expect("children[11] (label): animate: unknown preset \"wobble\"");
    expect("children[11] (label): animate: \"y\" needs \"to\"");
    expect("children[11] (label): animate: unknown key \"speed\"");
    expect("children[11] (label): animate: 50 ms flashes faster than 3 times a second");
    expect("children[11] (label): animate: \"frames\" is for image widgets");
    expect("children[11] (label): animate: \"frames\" needs \"files\" with two pictures or more");
    expect("children[11] (label): animate.when: unknown tag \"nope7\"");
    expect("children[11] (label): animate: unknown path \"bouncy\"");
    expect("project.json: drivers[0] (sim): items: unknown tag \"ghost\"");
    expect("project.json: drivers[0] (sim): status_tag: unknown tag \"nobody\"");
    CHECK(lines.size() == 38);                                  // and nothing else
}

TEST_CASE("check: broken files and a broken project")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "nowhere", "tags": [ { "name": "t" }, { "name": "t" } ],
        "font": { "file": "assets/none.otf" },
        "screens": [ "screens/a.json", "screens/missing.json", "screens/b.json", "screens/c.json", 5 ] })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [ { "type": "label" )json");
    p.write("screens/b.json", R"json({ "name": "same", "children": {} })json");
    p.write("screens/c.json", R"json({ "name": "same" })json");
    const auto lines = p.check();
    CHECK(has(lines, "screens/a.json: JSON syntax error"));
    CHECK(has(lines, "screens/missing.json: cannot be read"));
    CHECK(has(lines, "screens/b.json: \"children\" is not an array"));
    CHECK(has(lines, "screens/c.json: screen name \"same\" is used twice"));
    CHECK(has(lines, "project.json: screens[4]: neither a file name nor a screen object"));
    CHECK(has(lines, "project.json: start_screen: unknown screen \"nowhere\""));
    CHECK(has(lines, "project.json: tags: tag \"t\" is defined twice"));
    CHECK(has(lines, "project.json: font: no file \"assets/none.otf\""));

    Proj q;
    q.write("project.json", "{ not json");
    CHECK(has(q.check(), "project.json: JSON syntax error"));
    Proj r;
    CHECK(has(r.check(), "project.json: cannot be read"));
    Proj e;
    e.write("project.json", "{}");
    CHECK(has(e.check(), "the project has no screens"));
}

TEST_CASE("check: the shipped projects and the skill's example have no problems")
{
    const fs::path root = hmltest::source_dir();
    for(const char* rel : { "projects/demo", "projects/lcd35", "tools/skill/hml-screens/examples" }) {
        CAPTURE(rel);
        const auto problems = check_project((root / rel / "project.json").generic_string());
        for(const CheckProblem& p : problems) MESSAGE(p.line());
        CHECK(problems.empty());
    }
}

TEST_CASE("save_screenshot writes a PNG when the name ends in .png")
{
    hmltest::Runtime rt;
    json scr = json::parse(R"~({ "name": "s", "style": { "bg_color": "#ff0000" }, "children": [ { "type": "label", "text": "x" } ] })~");
    REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen("s", "none"));
    rt.advance(50);
    const fs::path file = fs::temp_directory_path() / "hml_check_test_shot.PNG";
    REQUIRE(save_screenshot(hmltest::Lvgl::get().disp, file.generic_string()));
    std::ifstream f(file, std::ios::binary);
    unsigned char h[24] = {};
    f.read(reinterpret_cast<char*>(h), sizeof(h));
    const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    CHECK(std::equal(sig, sig + 8, h));
    const uint32_t w = static_cast<uint32_t>(h[16]) << 24 | h[17] << 16 | h[18] << 8 | h[19];
    const uint32_t ht = static_cast<uint32_t>(h[20]) << 24 | h[21] << 16 | h[22] << 8 | h[23];
    CHECK(w == static_cast<uint32_t>(lv_display_get_horizontal_resolution(hmltest::Lvgl::get().disp)));
    CHECK(ht == static_cast<uint32_t>(lv_display_get_vertical_resolution(hmltest::Lvgl::get().disp)));
    f.close();
    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("data displays: one line of a value has no scrollbar, unless asked for")
{
    hmltest::Runtime rt;
    json scr = json::parse(R"~({ "name": "s", "children": [
        { "type": "numeric_display", "w": 160, "h": 64 },
        { "type": "text_display", "w": 160, "h": 64, "scrollbar": "auto" } ] })~");
    REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen("s", "none"));
    CHECK(lv_obj_get_scrollbar_mode(lv_obj_get_child(lv_screen_active(), 0)) == LV_SCROLLBAR_MODE_OFF);
    CHECK(lv_obj_get_scrollbar_mode(lv_obj_get_child(lv_screen_active(), 1)) == LV_SCROLLBAR_MODE_AUTO);
}

TEST_CASE("check: characters the font has no glyph for are named")
{
    hmltest::Runtime rt;
    const lv_font_t* f = LV_FONT_DEFAULT;                       // the built-in font: Latin letters and symbols
    CHECK(missing_glyphs(f, "Temperature 21.5 C") == "");
    CHECK(missing_glyphs(f, LV_SYMBOL_OK " ok") == "");
    CHECK(missing_glyphs(f, "温度 temp 温") == "温 度");        // each character once
    CHECK(missing_glyphs(f, "line 1\nline 2") == "");          // control characters are not glyphs

    json scr = json::parse(R"~({ "name": "s", "children": [
        { "type": "label", "text": "ok" },
        { "type": "label", "text": "圧力" },
        { "type": "label", "text": "隠", "hidden": true },
        { "type": "checkbox", "text": "自動" },
        { "type": "panel", "children": [ { "type": "button", "text": "停止" } ] } ] })~");
    REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen("s", "none"));
    rt.advance(50);
    const auto lines = missing_glyphs_on(lv_screen_active());
    auto has_line = [&](const char* part) { return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; }); };
    CHECK(lines.size() == 3);
    CHECK(has_line("\"圧力\": no glyph for: 圧 力"));
    CHECK(has_line("\"自動\": no glyph for: 自 動"));
    CHECK(has_line("\"停止\": no glyph for: 停 止"));
    CHECK_FALSE(has_line("隠"));                                // what is hidden is not drawn
}

TEST_CASE("check: the runtime's tags (sys.) are noted with their place and looked up later")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "tags": [ { "name": "temp", "type": "float" } ],
        "screens": [ "screens/a.json" ] })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [
        { "type": "label", "bind": { "text": "sys.datetime" } },
        { "type": "label", "bind": { "text": "sys.tme" } },
        { "type": "button", "on_click": { "action": "set", "tag": "sys.sound.mute", "value": true } } ] })json");
    std::vector<SysTagUse> uses;
    const auto problems = check_project((p.dir / "project.json").generic_string(), nullptr, &uses);
    CHECK(problems.empty());                                    // (not reported by the file check itself)
    REQUIRE(uses.size() == 3);
    CHECK(uses[1].tag == "sys.tme");
    CHECK(uses[1].file == "screens/a.json");
    CHECK(uses[1].where.find("children[1]") != std::string::npos);

    // what exists depends on the project: here no sound
    const auto unknown = unknown_sys_tags(uses, [](const std::string& t) { return t == "sys.datetime"; });
    REQUIRE(unknown.size() == 2);
    CHECK(unknown[0].line().find("unknown tag \"sys.tme\"") != std::string::npos);
    CHECK(unknown[0].line().rfind("screens/a.json: children[1]", 0) == 0);
    CHECK(unknown[1].line().find("sys.sound.mute") != std::string::npos);
}

TEST_CASE("check: a checkable button bound to a tag that also toggles it does nothing: reported")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "tags": [ { "name": "f", "type": "bool" } ], "screens": [ "screens/a.json" ] })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [
        { "type": "button", "checkable": true, "bind": { "checked": "f" }, "on_click": { "action": "toggle", "tag": "f" } },
        { "type": "button", "checkable": true, "bind": { "checked": "f" } },
        { "type": "button", "on_click": { "action": "toggle", "tag": "f" } } ] })json");
    const auto lines = p.check();
    REQUIRE(lines.size() == 1);
    CHECK(has(lines, "children[0] (button): on_click: toggles \"f\""));
}

TEST_CASE("check: driver maps: a tag twice, an address twice, a text on a number, a bit for a number, a Modbus address out of range")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "screens": [ "screens/a.json" ],
      "tags": [ { "name": "temp", "type": "float" }, { "name": "run", "type": "bool" }, { "name": "name", "type": "string" }, { "name": "cnt", "type": "int" } ],
      "drivers": [
        { "type": "slmp", "host": "10.0.0.1", "map": [ { "tag": "temp", "addr": "D100", "dtype": "float" }, { "tag": "cnt", "addr": "d100" },
            { "tag": "name", "addr": "D200" }, { "tag": "temp", "addr": "D300" }, { "tag": "run", "addr": "M0", "dtype": "string" } ] },
        { "type": "modbus_tcp", "host": "10.0.0.2", "map": [ { "tag": "cnt", "area": "coil", "addr": 3 }, { "tag": "run", "area": "holding", "addr": 70000 },
            { "tag": "temp", "area": "holding", "addr": 65535, "dtype": "float" }, { "tag": "name", "area": "holding", "addr": "5" } ] } ] })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [] })json");
    const auto lines = p.check();
    CHECK(has(lines, "drivers[0] (slmp): map[1]: address D100 is already in map[0]"));
    CHECK(has(lines, "map[2]: tag \"name\" is a string, the address holds a number"));
    CHECK(has(lines, "map[3]: tag \"temp\" is already in map[0]"));
    CHECK(has(lines, "map[4]: tag \"run\" is bool, the address holds a text"));
    CHECK(has(lines, "drivers[1] (modbus_tcp): map[0]: tag \"cnt\" is int, a coil is one bit"));
    CHECK(has(lines, "map[1]: addr 70000: a Modbus address"));
    CHECK(has(lines, "map[2]: addr 65535: a Modbus address"));                  // a float takes 65535 and 65536
    CHECK(has(lines, "map[3]: addr \"5\": a Modbus address"));
}

TEST_CASE("check: values read from the PLC are not simulated or written by a script; a real number does not go into an int tag")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "screens": [ "screens/a.json" ],
      "tags": [ { "name": "temp", "type": "float" }, { "name": "count", "type": "int" }, { "name": "sp", "type": "int" }, { "name": "hum", "type": "int" }, { "name": "local", "type": "int" } ],
      "drivers": [ { "type": "slmp", "host": "10.0.0.1", "map": [
          { "tag": "temp", "addr": "D100", "dtype": "float", "access": "ro" }, { "tag": "count", "addr": "D200", "dtype": "int32", "access": "ro" },
          { "tag": "sp", "addr": "D102", "dtype": "float", "access": "rw" }, { "tag": "hum", "addr": "D111", "scale": 0.1, "access": "ro" } ] } ],
      "simulation": { "items": [ { "tag": "temp", "mode": "follow", "target": 200, "when": "x_off" }, { "tag": "local", "mode": "counter" } ] },
      "scripts": [ { "name": "calc", "trigger": "interval", "code": ["tag.count = tag.local + 1", "if tag.temp == 3 then tag.sp = 5 end", "tag.local = tag.count"] } ] })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [] })json");
    const auto lines = p.check();
    CHECK(has(lines, "simulation: items[0]: \"temp\" is read from the device (drivers[0] (slmp): map[0])"));
    CHECK_FALSE(has(lines, "items[1]: \"local\""));
    CHECK(has(lines, "scripts[0] (calc): writes \"count\", which is read from the device"));
    CHECK_FALSE(has(lines, "writes \"temp\""));                   // (temp == 3 is a comparison)
    CHECK_FALSE(has(lines, "writes \"sp\""));                     // a setpoint (rw) may be written
    CHECK(has(lines, "map[2]: tag \"sp\" is int, the address holds a real number"));
    CHECK(has(lines, "map[3]: tag \"hum\" is int, \"scale\" / \"offset\" make a fraction"));
}

TEST_CASE("check: MQTT JSON messages: pointers, tags, topics")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "screens": [ "screens/a.json" ],
      "tags": [ { "name": "temp", "type": "float" }, { "name": "sp", "type": "int" } ],
      "drivers": [ { "type": "mqtt", "host": "b", "publish": [
          { "topic": "line/status", "fields": { "/oven/temp": "temp" }, "ts": "/ts" },
          { "topic": "x", "fields": { "oven/temp": "temp", "/y": "ghost" }, "ts": "ts" },
          { "fields": { "/a": "temp" } } ],
        "subscribe": [ { "topic": "cmd/#", "tag": "sp", "fields": { "/sp": "sp" } }, { "topic": "c", "fields": {} } ] } ] })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [] })json");
    const auto lines = p.check();
    CHECK_FALSE(has(lines, "publish[0]"));
    CHECK(has(lines, "publish[1]: fields: \"oven/temp\" is not a JSON pointer"));
    CHECK(has(lines, "publish[1]: fields \"/y\": unknown tag \"ghost\""));
    CHECK(has(lines, "publish[1]: ts: \"ts\" is not a JSON pointer"));
    CHECK(has(lines, "publish[2]: a message needs a \"topic\""));
    CHECK(has(lines, "subscribe[0]: a message has \"fields\", not \"tag\""));
    CHECK(has(lines, "subscribe[0]: \"cmd/#\": wildcards"));
    CHECK(has(lines, "subscribe[1]: \"fields\" is {"));
}

TEST_CASE("check: remote: a password in the project is not used; unknown keys")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "screens": [ "screens/a.json" ], "tags": [],
      "remote": { "enabled": true, "port": 70000, "password": "abcd1234", "veiw_only": true } })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [] })json");
    auto lines = p.check();
    CHECK(has(lines, "remote: \"password\" is not used"));
    CHECK(has(lines, "remote: unknown key \"veiw_only\""));
    CHECK(has(lines, "remote: port"));
    p.write("project.json", R"json({ "start_screen": "a", "screens": [ "screens/a.json" ], "tags": [],
      "remote": { "enabled": true, "port": 5900, "view_only": true } })json");
    lines = p.check();
    CHECK_FALSE(has(lines, "remote"));
}

TEST_CASE("check: OPC UA server: security, certificate files, no password in the project")
{
    Proj p;
    p.write("project.json", R"json({ "start_screen": "a", "screens": [ "screens/a.json" ], "tags": [],
      "drivers": [ { "type": "opcua_server", "security": "maybe", "cert_file": "certs/s.der", "client_certs": ["certs/scada.der"], "password": "x" },
                   { "type": "opcua_server", "security": "none" } ] })json");
    p.write("screens/a.json", R"json({ "name": "a", "children": [] })json");
    const auto lines = p.check();
    CHECK(has(lines, "drivers[0] (opcua_server): security: \"maybe\""));
    CHECK(has(lines, "cert_file and key_file go together"));
    CHECK(has(lines, "cert_file: no file \"certs/s.der\""));
    CHECK(has(lines, "client_certs: no file \"certs/scada.der\""));
    CHECK(has(lines, "the password is in the project file"));
    CHECK_FALSE(has(lines, "drivers[1]"));                     // "none" is said at run time and in the designer
}
