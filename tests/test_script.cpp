#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "script.hpp"
#include "ui.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

json J(const char* text) { return json::parse(text); }

int load(const char* scripts_json, const std::string& base_dir = ".")
{
    json list = J(scripts_json);
    return scripts().load(&list, base_dir);
}

void build_screen(const char* screen_json)
{
    REQUIRE(ui().build_screen(J(screen_json), "/base") != nullptr);
}

struct TempDir {
    fs::path dir;
    TempDir()
    {
        dir = fs::temp_directory_path() / ("hml_script_test_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFF)));
        fs::create_directories(dir / "screens");
    }
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string write(const std::string& rel, const std::string& text)
    {
        fs::path p = dir / rel;
        std::ofstream f(p, std::ios::binary);
        f << text;
        return p.generic_string();
    }
};

} // namespace

TEST_CASE("script: tags are read and written with their own types")
{
    Runtime rt;
    Tag* b = rt.tag("run", TagType::Bool, 0);
    Tag* i = rt.tag("count", TagType::Int, 5);
    Tag* f = rt.tag("temp", TagType::Float, 1.5);
    Tag* s = rt.tag("state", "Idle");
    Tag* r = rt.tag("result", "");
    Tag* lim = rt.tag("speed", TagType::Int, 0, 0, 100);
    Tag* dotted = rt.tag("sys.x", TagType::Int, 0);

    CHECK(scripts().exec(R"~(
        result = type(tag.run) .. "," .. math.type(tag.count) .. "," .. math.type(tag.temp) .. "," .. type(tag.state)
        tag.result = result
        tag.run = not tag.run
        tag.count = tag.count + 1
        tag.temp = tag.temp * 2
        tag.state = tag.state .. "!"
        tag.speed = 250
        tag["sys.x"] = 7
    )~", "t"));
    CHECK(r->get().str == "boolean,integer,float,string");
    CHECK(b->get_number() == 1);
    CHECK(i->get_number() == 6);
    CHECK(f->get_number() == doctest::Approx(3.0));
    CHECK(s->get().str == "Idle!");
    CHECK(lim->get_number() == 100);                 // the tag's limits still apply
    CHECK(dotted->get_number() == 7);
    CHECK(scripts().error_count() == 0);

    // numbers / strings are coerced to the tag type like any other write
    CHECK(scripts().exec("tag.run = 0  tag.count = '42'  tag.state = 12", "t"));
    CHECK(b->get_number() == 0);
    CHECK(i->get_number() == 42);
    CHECK(s->get().str == "12");
}

TEST_CASE("script: errors are reported and do not stop later runs")
{
    Runtime rt;
    Tag* x = rt.tag("x", TagType::Int, 0);

    CHECK_FALSE(scripts().exec("tag.nope = 1", "bad"));
    CHECK(scripts().last_error().find("unknown tag 'nope'") != std::string::npos);
    CHECK(scripts().last_error().find("bad:1:") != std::string::npos);          // label and line number
    CHECK_FALSE(scripts().exec("local y = tag.nope", "bad"));
    CHECK_FALSE(scripts().exec("tag.x = {}", "bad"));
    CHECK_FALSE(scripts().exec("tag.x = nil", "bad"));
    CHECK_FALSE(scripts().exec("this is not lua", "syntax"));
    CHECK_FALSE(scripts().check("if then", "syntax"));
    CHECK(scripts().check("tag.x = 1", "ok"));
    CHECK(x->get_number() == 0);                     // check() compiles only
    CHECK(scripts().error_count() == 6);

    CHECK(scripts().exec("tag.x = 3", "ok"));
    CHECK(x->get_number() == 3);
}

TEST_CASE("script: the sandbox has no file, process or loader access")
{
    Runtime rt;
    Tag* r = rt.tag("r", "");
    CHECK(scripts().exec(R"~(
        local missing = {}
        for _, n in ipairs({ "io", "require", "dofile", "loadfile", "load", "debug", "package" }) do
            if _G[n] ~= nil then missing[#missing + 1] = n end
        end
        if os.execute or os.remove or os.exit or os.getenv then missing[#missing + 1] = "os" end
        tag.r = table.concat(missing, ",") .. "|" .. type(os.time()) .. "|" .. #os.date("%Y") .. "|" .. string.format("%.1f", math.max(1, 2.5))
    )~", "t"));
    CHECK(r->get().str == "|number|4|2.5");
}

TEST_CASE("script: a runaway script is aborted by the timeout")
{
    Runtime rt;
    Tag* x = rt.tag("x", TagType::Int, 0);
    scripts().set_timeout_ms(50);
    CHECK_FALSE(scripts().exec("while true do end", "loop"));
    CHECK(scripts().last_error().find("timed out") != std::string::npos);
    CHECK(scripts().exec("tag.x = 1", "after"));     // the engine is still usable
    CHECK(x->get_number() == 1);
    scripts().set_timeout_ms(500);
}

TEST_CASE("script: a script that takes too much memory fails; the runtime and later scripts go on")
{
    Runtime rt;
    Tag* x = rt.tag("x", TagType::Int, 0);
    CHECK(scripts().memory_limit() == 32u * 1024u * 1024u);       // the default
    CHECK(scripts().exec("tag.x = 1", "first"));
    const size_t idle = scripts().memory_used();
    CHECK(idle > 0);
    scripts().set_memory_limit(idle + 2u * 1024u * 1024u);

    // one huge request (never reaches malloc)
    CHECK_FALSE(scripts().exec("local s = string.rep('x', 1024 * 1024 * 1024)  tag.x = #s", "rep"));
    CHECK(scripts().last_error().find("not enough memory") != std::string::npos);
    // many small ones
    CHECK_FALSE(scripts().exec("local t = {}  for i = 1, 10000000 do t[i] = 'item ' .. i end  tag.x = #t", "grow"));
    CHECK(scripts().last_error().find("not enough memory") != std::string::npos);
    // a string that doubles
    CHECK_FALSE(scripts().exec("local s = 'x'  while true do s = s .. s end", "double"));
    CHECK(scripts().last_error().find("not enough memory") != std::string::npos);
    CHECK(x->get_number() == 1);

    // what the failed runs held is garbage: the next script has room again
    CHECK(scripts().exec("local s = string.rep('x', 256 * 1024)  tag.x = #s", "after"));
    CHECK(x->get_number() == 256 * 1024);
    CHECK(scripts().memory_used() < idle + 2u * 1024u * 1024u);

    // globals a script keeps count too: later scripts fail, the runtime does not
    scripts().exec("keep = {}  for i = 1, 10000000 do keep[i] = 'item ' .. i end", "hoard");
    CHECK(scripts().last_error().find("not enough memory") != std::string::npos);
    for(int i = 0; i < 20; i++) scripts().exec("local t = {}  for i = 1, 100000 do t[i] = {} end", "full");
    CHECK(scripts().exec("keep = nil  collectgarbage()  tag.x = 7", "free"));
    CHECK(x->get_number() == 7);

    // within the limit nothing changes
    scripts().set_memory_limit(32u * 1024u * 1024u);
    CHECK(scripts().exec("local s = string.rep('x', 4 * 1024 * 1024)  tag.x = #s", "ok"));
    CHECK(x->get_number() == 4 * 1024 * 1024);
    CHECK(load("[]") == 0);
}

TEST_CASE("script: globals persist between runs and are reset by load()")
{
    Runtime rt;
    Tag* x = rt.tag("x", TagType::Int, 0);
    CHECK(scripts().exec("counter = (counter or 0) + 1  tag.x = counter", "t"));
    CHECK(scripts().exec("counter = (counter or 0) + 1  tag.x = counter", "t"));
    CHECK(x->get_number() == 2);
    CHECK(load("[]") == 0);
    CHECK(scripts().exec("counter = (counter or 0) + 1  tag.x = counter", "t"));
    CHECK(x->get_number() == 1);
}

TEST_CASE("script: load() handles code strings, line arrays, files, disabled and broken entries")
{
    Runtime rt;
    Tag* x = rt.tag("x", TagType::Int, 0);
    TempDir tmp;
    tmp.write("add.lua", "\xEF\xBB\xBFtag.x = tag.x + 100\n");

    int n = load(R"~([
        { "name": "a", "code": "tag.x = tag.x + 1" },
        { "name": "b", "code": ["local d = 10", "tag.x = tag.x + d"] },
        { "name": "c", "file": "add.lua" },
        { "name": "off", "enabled": false, "code": "tag.x = -1" },
        { "name": "broken", "code": "tag.x = = 1" },
        { "name": "nofile", "file": "missing.lua" },
        { "name": "empty" },
        { "name": "a", "code": "tag.x = -1" },
        { "code": "tag.x = tag.x + 1000" },
        "not an object"
    ])~", tmp.dir.generic_string());
    CHECK(n == 4);
    CHECK(scripts().count() == 4);
    CHECK(scripts().error_count() == 1);             // the syntax error
    CHECK(scripts().has("a"));
    CHECK_FALSE(scripts().has("off"));
    CHECK_FALSE(scripts().has("broken"));
    CHECK(scripts().has("script9"));                 // unnamed: "script<position>"

    CHECK(scripts().run("a"));
    CHECK(scripts().run("b"));
    CHECK(scripts().run("c"));
    CHECK(scripts().run("script9"));
    CHECK_FALSE(scripts().run("off"));
    CHECK(x->get_number() == 1111);
    rt.advance(50);
    CHECK(x->get_number() == 1111);                  // manual scripts never run by themselves

    CHECK(scripts().load(nullptr, ".") == 0);
    CHECK(scripts().count() == 0);
}

TEST_CASE("script: startup scripts run in order on run_startup()")
{
    Runtime rt;
    Tag* s = rt.tag("s", "");
    load(R"~([
        { "name": "one", "trigger": "startup", "code": "function add(c) tag.s = tag.s .. c end  add('1')" },
        { "name": "manual", "code": "add('m')" },
        { "name": "two", "trigger": "startup", "code": "add('2:' .. event.trigger)" }
    ])~");
    CHECK(s->get().str == "");
    scripts().run_startup();
    CHECK(s->get().str == "12:startup");
    CHECK(scripts().run("manual"));                  // functions defined at startup are shared
    CHECK(s->get().str == "12:startupm");
}

TEST_CASE("script: interval scripts run from an LVGL timer")
{
    Runtime rt;
    Tag* n = rt.tag("n", TagType::Int, 0);
    load(R"~([ { "name": "tick", "trigger": "interval", "interval_ms": 100, "code": "tag.n = tag.n + 1" } ])~");
    rt.advance(50);
    CHECK(n->get_number() == 0);
    rt.advance(1000);
    CHECK(n->get_number() >= 9);
    CHECK(n->get_number() <= 11);

    scripts().clear();                               // the timer is gone
    double before = n->get_number();
    rt.advance(500);
    CHECK(n->get_number() == before);
}

TEST_CASE("script: tag scripts run on a change, see event.tag / event.value and chain")
{
    Runtime rt;
    Tag* temp = rt.tag("temp", TagType::Float, 20);
    Tag* hi = rt.tag("temp_hi", TagType::Float, 30);
    Tag* alarm = rt.tag("alarm", TagType::Bool, 0);
    Tag* msg = rt.tag("msg", "");
    Tag* runs = rt.tag("runs", TagType::Int, 0);
    load(R"~([
        { "name": "check", "trigger": "tag", "tags": ["temp", "temp_hi"],
          "code": "tag.alarm = tag.temp > tag.temp_hi  tag.runs = tag.runs + 1  last = event.tag .. '=' .. tostring(event.value)" },
        { "name": "on_alarm", "trigger": "tag", "tag": "alarm", "code": "tag.msg = (event.value and 'HIGH ' or 'ok ') .. last" },
        { "name": "bad", "trigger": "tag", "tags": ["no_such_tag"], "code": "tag.runs = 99" }
    ])~");
    rt.pump();
    CHECK(runs->get_number() == 0);                  // nothing changed yet

    temp->set_number(35, nullptr);
    rt.pump();                                       // check
    rt.pump();                                       // on_alarm (chained through the alarm tag)
    CHECK(alarm->get_number() == 1);
    CHECK(msg->get().str == "HIGH temp=35.0");
    CHECK(runs->get_number() == 1);

    hi->set_number(40, nullptr);
    rt.pump();
    rt.pump();
    CHECK(alarm->get_number() == 0);
    CHECK(msg->get().str == "ok temp_hi=40.0");

    temp->set_number(35, nullptr);                   // same value: no change, no run
    rt.pump();
    CHECK(runs->get_number() == 2);

    tags().dispatch(true);                           // a forced UI refresh is not a change
    CHECK(runs->get_number() == 2);
}

TEST_CASE("script: a tag script is not retriggered by its own write")
{
    Runtime rt;
    Tag* n = rt.tag("n", TagType::Int, 0);
    load(R"~([ { "name": "self", "trigger": "tag", "tags": ["n"], "code": "tag.n = tag.n + 1" } ])~");
    n->set_number(10, nullptr);
    for(int i = 0; i < 5; i++) rt.pump();
    CHECK(n->get_number() == 11);
}

TEST_CASE("script: actions run inline code or a named script")
{
    Runtime rt;
    Tag* n = rt.tag("n", TagType::Int, 0);
    Tag* trig = rt.tag("trig", "");
    load(R"~([ { "name": "bump", "code": "tag.n = tag.n + 10  tag.trig = event.trigger" } ])~");
    build_screen(R"~({ "name": "s", "children": [
        { "type": "button", "on_click": { "action": "script", "code": "tag.n = tag.n + 1" } },
        { "type": "button", "on_click": [ { "action": "script", "name": "bump" }, { "action": "inc", "tag": "n" } ] },
        { "type": "button", "on_click": { "action": "script", "code": ["if tag.n > 0 then", "  tag.n = 0", "end"] } },
        { "type": "button", "on_click": { "action": "script" } },
        { "type": "button", "on_click": { "action": "script", "code": "syntax error here" } }
    ] })~");
    ui().show_screen("s", "none");
    lv_obj_t* scr = lv_screen_active();
    auto click = [&](int32_t i) { lv_obj_send_event(lv_obj_get_child(scr, i), LV_EVENT_CLICKED, nullptr); };

    CHECK(scripts().error_count() == 1);             // the syntax error is found while building
    click(0);
    click(0);
    CHECK(n->get_number() == 2);
    click(1);
    CHECK(n->get_number() == 13);
    CHECK(trig->get().str == "action");
    click(2);
    CHECK(n->get_number() == 0);
    click(3);                                        // no-ops, must not crash
    click(4);
    CHECK(n->get_number() == 0);
}

TEST_CASE("script: screen functions and screen_show / screen_hide triggers")
{
    Runtime rt;
    Tag* log = rt.tag("log", "");
    Tag* cur = rt.tag("cur", "");
    load(R"~([
        { "name": "show_any", "trigger": "screen_show", "code": "tag.log = tag.log .. '+' .. event.screen" },
        { "name": "hide_a", "trigger": "screen_hide", "screen": "a", "code": "tag.log = tag.log .. '-' .. event.screen" },
        { "name": "show_c", "trigger": "screen_show", "screen": "c", "code": "goto_screen('a', 'none')" }
    ])~");
    build_screen(R"~({ "name": "a" })~");
    build_screen(R"~({ "name": "b" })~");
    build_screen(R"~({ "name": "c" })~");

    ui().show_screen("a", "none");
    CHECK(log->get().str == "+a");
    CHECK(scripts().exec("tag.cur = current_screen()  ok = goto_screen('b', 'none')  missing = goto_screen('zzz')", "t"));
    CHECK(cur->get().str == "a");
    CHECK(ui().current_screen().value_or("") == "b");
    CHECK(log->get().str == "+a-a+b");
    CHECK(scripts().exec("assert(ok == true and missing == false)  assert(back())", "t"));
    rt.advance(400);
    CHECK(ui().current_screen().value_or("") == "a");
    CHECK(log->get().str == "+a-a+b+a");

    log->set_string("", nullptr);
    ui().show_screen("c", "none");                   // show_c bounces straight back to "a"
    CHECK(ui().current_screen().value_or("") == "a");
    CHECK(log->get().str == "-a+c+a");
    CHECK(scripts().error_count() == 0);
}

TEST_CASE("script: endless navigation between screens is cut off")
{
    Runtime rt;
    load(R"~([
        { "name": "ping", "trigger": "screen_show", "screen": "a", "code": "goto_screen('b', 'none')" },
        { "name": "pong", "trigger": "screen_show", "screen": "b", "code": "goto_screen('a', 'none')" }
    ])~");
    build_screen(R"~({ "name": "a" })~");
    build_screen(R"~({ "name": "b" })~");
    ui().show_screen("a", "none");                   // must return
    CHECK(scripts().error_count() >= 1);
    CHECK(scripts().last_error().find("nested") != std::string::npos);
}

TEST_CASE("script: call() runs a registered action, run() another script, log() accepts anything")
{
    Runtime rt;
    Tag* n = rt.tag("n", TagType::Int, 0);
    std::string got;
    ui().register_action("test.fn", [&](const std::string& arg) { got = arg; });
    load(R"~([ { "name": "helper", "code": "tag.n = tag.n + 1" } ])~");

    CHECK(scripts().exec("call('test.fn', 'hello')  run('helper')  run('helper')  log('x', 1, nil, true, {})  print(millis())", "t"));
    CHECK(got == "hello");
    CHECK(n->get_number() == 2);
    CHECK_FALSE(scripts().exec("call('no.such.fn')", "t"));
    CHECK_FALSE(scripts().exec("run('no_such_script')", "t"));
}

TEST_CASE("script: Project::build_screens loads the scripts, runs startup first and reloads them")
{
    Runtime rt;
    TempDir tmp;
    tmp.write("screens/main.json", R"~({ "name": "main", "children": [ { "type": "button", "on_click": { "action": "script", "name": "press" } } ] })~");
    tmp.write("screens/other.json", R"~({ "name": "other" })~");
    auto project_json = [](const std::string& extra) {
        return std::string(R"~({ "start_screen": "main",
            "tags": [ { "name": "log", "type": "string", "init": "" }, { "name": "n", "type": "int", "init": 0 } ],
            "screens": [ "screens/main.json", "screens/other.json" ],
            "scripts": [
                { "name": "init", "trigger": "startup", "code": "tag.log = tag.log .. 'S'" },
                { "name": "shown", "trigger": "screen_show", "code": "tag.log = tag.log .. '+' .. event.screen" },
                { "name": "press", "code": "tag.n = tag.n + 1" })~") + extra + "] }";
    };
    std::string path = tmp.write("project.json", project_json(""));

    Project p;
    REQUIRE(p.load(path));
    CHECK(scripts().count() == 0);                   // scripts belong to build_screens()
    CHECK(p.build_screens() == 2);
    CHECK(scripts().count() == 3);
    CHECK(scripts().error_count() == 0);
    Tag* log = tags().find("log");
    Tag* n = tags().find("n");
    CHECK(log->get().str == "S+main");

    lv_obj_send_event(lv_obj_get_child(lv_screen_active(), 0), LV_EVENT_CLICKED, nullptr);
    CHECK(n->get_number() == 1);

    // hot reload: the edited project file is picked up, startup runs again, the shown screen is kept
    ui().show_screen("other", "none");
    log->set_string("", nullptr);
    tmp.write("project.json", project_json(R"~(, { "name": "extra", "trigger": "startup", "code": "tag.n = 50" })~"));
    CHECK(p.build_screens() == 2);
    CHECK(scripts().count() == 4);
    CHECK(n->get_number() == 50);
    CHECK(log->get().str == "S+other");
    CHECK(ui().current_screen().value_or("") == "other");
    p.unload();
}

TEST_CASE("script: a startup script may choose the first screen")
{
    Runtime rt;
    TempDir tmp;
    std::string path = tmp.write("project.json", R"~({ "start_screen": "main", "tags": [],
        "screens": [ { "name": "main" }, { "name": "login" } ],
        "scripts": [ { "name": "init", "trigger": "startup", "code": "goto_screen('login', 'none')" } ] })~");
    Project p;
    REQUIRE(p.load(path));
    CHECK(p.build_screens() == 2);
    CHECK(ui().current_screen().value_or("") == "login");
    p.unload();
}
