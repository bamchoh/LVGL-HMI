// What a project can do beyond drawing screens (capabilities.hpp): the list a person sees
// before running a project somebody else made.
#include <doctest.h>
#include "capabilities.hpp"
#include "lvgl_fixture.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>

using namespace hml;
namespace fs = std::filesystem;

namespace {

bool has(const std::vector<std::string>& lines, const std::string& part)
{
    for(const std::string& l : lines) if(l.find(part) != std::string::npos) return true;
    return false;
}

ProjectCapabilities caps(const char* project_json) { return project_capabilities(json::parse(project_json, nullptr, true, true)); }

} // namespace

TEST_CASE("capabilities: a project that only draws screens has none")
{
    const ProjectCapabilities c = caps(R"json({ "title": "T", "tags": [ { "name": "a", "type": "int" } ], "screens": [ "screens/a.json" ],
        "drivers": [ { "type": "sim", "items": [ { "tag": "a", "mode": "ramp" } ] } ],
        "scripts": [ { "name": "by_button", "code": "tag.a = 1" } ],
        "alarms": [], "data_log": { "groups": [] } })json");
    CHECK(c.empty());
    CHECK_FALSE(c.unattended_writes());
    CHECK(c.text().find("only draws screens") != std::string::npos);
    CHECK(c.brief() == "It only draws screens.");
    CHECK(c.fingerprint().size() == 64);
}

TEST_CASE("capabilities: devices written to, data sent out, ports opened, what runs by itself")
{
    const ProjectCapabilities c = caps(R"json({
        "drivers": [
            { "type": "modbus_tcp", "host": "192.168.0.10", "port": 502, "map": [
                { "tag": "run", "area": "coil", "addr": 0 }, { "tag": "speed_sp", "area": "holding", "addr": 1 },
                { "tag": "temp", "area": "input", "addr": 0, "access": "ro" } ] },
            { "type": "slmp", "host": "192.168.0.20", "map": [ { "tag": "count", "addr": "D100", "access": "ro" } ] },
            { "type": "fins", "host": "192.168.0.30", "enabled": false, "map": [ { "tag": "x", "addr": "D0" } ] },
            { "type": "mqtt", "host": "broker.example", "port": 8883, "publish": [ { "topic": "line/1", "tag": "temp" } ], "subscribe": [ { "topic": "cmd/run", "tag": "run" } ] },
            { "type": "opcua_server", "port": 4841, "security": "none" },
            { "type": "opcua_server", "port": 4842 },
            { "type": "sim", "items": [] } ],
        "mail": { "host": "smtp.example.com", "to": [ "a@example.com", "b@example.com" ], "alarms": {} },
        "uploads": [ { "name": "n", "url": "https://files.example.com/x/", "logs": [ "alarms" ] }, { "name": "off", "enabled": false, "url": "https://nowhere.example/" } ],
        "remote": { "enabled": true, "port": 5901 },
        "scripts": [ { "name": "auto", "trigger": "interval", "interval_ms": 250, "code": "tag.run = true" },
                     { "name": "boot", "trigger": "startup", "code": "" }, { "name": "manual", "code": "" }, { "name": "off", "trigger": "startup", "enabled": false, "code": "" } ],
        "schedules": [ { "name": "start", "at": "06:00", "do": { "action": "set", "tag": "run", "value": true } },
                       { "name": "send", "at": "23:00", "do": [ { "action": "call", "fn": "upload.run", "arg": "n" } ] } ],
        "transfers": [ { "from": "temp", "to": "speed_sp" } ] })json");

    // devices: what can be written, not what is only read; a driver that is off does not count
    REQUIRE(c.devices.size() == 2);
    CHECK(has(c.devices, "modbus_tcp 192.168.0.10:502: can write 2 of 3 value(s) (run, speed_sp)"));
    CHECK(has(c.devices, "MQTT broker broker.example:8883: takes values for tags from 1 topic(s)"));
    CHECK_FALSE(has(c.devices, "slmp"));
    CHECK_FALSE(has(c.devices, "192.168.0.30"));
    // sends
    REQUIRE(c.sends.size() == 3);
    CHECK(has(c.sends, "MQTT broker broker.example:8883: publishes 1 topic(s)"));
    CHECK(has(c.sends, "mail through smtp.example.com to a@example.com, b@example.com"));
    CHECK(has(c.sends, "uploads the recorded logs to https://files.example.com/x/ (alarms)"));
    CHECK_FALSE(has(c.sends, "nowhere.example"));
    // ports
    REQUIRE(c.listens.size() == 3);
    CHECK(has(c.listens, "OPC UA server on port 4841: anybody on the network"));
    CHECK(has(c.listens, "OPC UA server on port 4842: the clients the device trusts"));
    CHECK(has(c.listens, "VNC on port 5901"));
    // by itself: the scripts with a trigger (not the one a button runs, not the one that is off)
    CHECK(has(c.automatic, "2 script(s) run by themselves and can write any tag: auto (every 250 ms), boot (startup)"));
    CHECK(has(c.automatic, "schedule start at 06:00: writes run"));
    CHECK(has(c.automatic, "schedule send at 23:00: calls upload.run"));
    CHECK(has(c.automatic, "1 transfer(s) copy values from tag to tag: temp > speed_sp"));

    CHECK(c.unattended_writes());
    const std::string text = c.text();
    CHECK(text.find("Writes to devices\n  - modbus_tcp") != std::string::npos);
    CHECK(text.find("values can reach a device without anybody at the screen") != std::string::npos);
    CHECK(c.brief().find("Writes to: modbus_tcp 192.168.0.10:502") != std::string::npos);
    CHECK(c.brief().find("(+1 more)") != std::string::npos);
    const json j = c.to_json();
    CHECK(j["devices"].size() == 2);
    CHECK(j["unattended_writes"] == true);
    CHECK(j["fingerprint"] == c.fingerprint());
}

TEST_CASE("capabilities: the fingerprint follows the list, not the screens")
{
    const char* base = R"json({ "title": "A", "theme": { "dark": true }, "tags": [ { "name": "run", "type": "bool" } ],
        "drivers": [ { "type": "modbus_tcp", "host": "10.0.0.1", "map": [ { "tag": "run", "area": "coil", "addr": 0 } ] } ] })json";
    const std::string fp = caps(base).fingerprint();
    // another title, another theme, more tags: the same list
    CHECK(caps(R"json({ "title": "B", "theme": { "dark": false }, "tags": [ { "name": "run", "type": "bool" }, { "name": "x", "type": "int" } ],
        "drivers": [ { "type": "modbus_tcp", "host": "10.0.0.1", "map": [ { "tag": "run", "area": "coil", "addr": 0 } ] } ] })json").fingerprint() == fp);
    // another device, one more value that can be written, a script that starts to run by itself: another list
    CHECK(caps(R"json({ "drivers": [ { "type": "modbus_tcp", "host": "10.0.0.2", "map": [ { "tag": "run", "area": "coil", "addr": 0 } ] } ] })json").fingerprint() != fp);
    CHECK(caps(R"json({ "drivers": [ { "type": "modbus_tcp", "host": "10.0.0.1", "map": [ { "tag": "run", "area": "coil", "addr": 0 }, { "tag": "y", "area": "coil", "addr": 1 } ] } ] })json").fingerprint() != fp);
    CHECK(caps(R"json({ "drivers": [ { "type": "modbus_tcp", "host": "10.0.0.1", "map": [ { "tag": "run", "area": "coil", "addr": 0 } ] } ],
        "scripts": [ { "name": "s", "trigger": "startup", "code": "" } ] })json").fingerprint() != fp);
    CHECK(caps("{}").fingerprint() != fp);
    // nonsense is no list, and no crash
    CHECK(caps(R"json({ "drivers": "x", "scripts": 3, "schedules": {}, "mail": [], "uploads": "y", "remote": 1, "transfers": null })json").empty());
    CHECK(caps("[]").empty());
}

TEST_CASE("capabilities: from the files of a project - the scripts its buttons carry, nothing outside its folder")
{
    static std::atomic<int> n{ 0 };
    const fs::path dir = fs::temp_directory_path() / ("hml_caps_test_" + std::to_string(n++));
    fs::remove_all(dir);
    fs::create_directories(dir / "p" / "screens");
    auto write = [](const fs::path& p, const std::string& t) { std::ofstream f(p, std::ios::binary); f << t; };
    write(dir / "p" / "project.json", std::string("\xEF\xBB\xBF") + R"json({ // a comment
        "screens": [ "screens/a.json", "../outside.json", { "name": "inline", "children": [ { "type": "button", "on_click": { "action": "script", "code": "tag.x = 1" } } ] } ] })json");
    write(dir / "p" / "screens" / "a.json", R"json({ "name": "a", "children": [
        { "type": "button", "on_click": [ { "action": "script", "code": "tag.x = 2" }, { "action": "script", "name": "named" } ] },
        { "type": "panel", "children": [ { "type": "switch", "on_change": { "action": "script", "code": [ "tag.x = 3" ] } } ] } ] })json");
    write(dir / "outside.json", R"json({ "name": "o", "children": [ { "type": "button", "on_click": { "action": "script", "code": "x" } } ] })json");

    ProjectCapabilities c;
    std::string err;
    REQUIRE_MESSAGE(project_capabilities_of((dir / "p" / "project.json").generic_u8string(), c, &err), err);
    REQUIRE(c.automatic.size() == 1);
    CHECK(c.automatic[0].find("3 script(s) on the screens") == 0);        // two in a.json, one inline; not the one outside the folder
    CHECK_FALSE(project_capabilities_of((dir / "nope.json").generic_u8string(), c, &err));
    CHECK(err.find("cannot read") != std::string::npos);
    write(dir / "bad.json", "{ nope");
    CHECK_FALSE(project_capabilities_of((dir / "bad.json").generic_u8string(), c, &err));
    CHECK(err.find("not valid JSON") != std::string::npos);

    // the projects that come with the repository can all be read
    for(const char* name : { "demo", "lcd35", "bakery", "bottling", "factory", "factory35", "metro" }) {
        ProjectCapabilities pc;
        CHECK_MESSAGE(project_capabilities_of((hmltest::source_dir() / "projects" / name / "project.json").generic_string(), pc, &err), name << ": " << err);
        CHECK(pc.fingerprint().size() == 64);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
