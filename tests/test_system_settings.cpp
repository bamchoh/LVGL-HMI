// System settings: pages -> tags -> generated screens -> apply.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "serial_ports.hpp"
#include "system_settings.hpp"
#include "ui.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

// A page with one field of every type that records what apply() received.
class FakePage final : public SettingsPage {
public:
    std::map<std::string, std::string> last_values;
    int applies = 0;
    std::string fail_with;
    std::string name_value = "Alice";
    double rate_value = 2.5;

    std::string id() const override { return "fake"; }
    std::string title() const override { return "Fake page"; }
    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        SettingField t; t.key = "name"; t.label = "Name"; t.type = SettingField::Type::Text; t.value = name_value; out.push_back(t);
        SettingField pw; pw.key = "secret"; pw.label = "Secret"; pw.type = SettingField::Type::Text; pw.password = true; pw.value = "1234"; out.push_back(pw);
        SettingField n; n.key = "rate"; n.label = "Rate"; n.type = SettingField::Type::Number; n.value = std::to_string(rate_value); n.decimals = 1; n.unit = " Hz"; out.push_back(n);
        SettingField b; b.key = "on"; b.label = "On"; b.type = SettingField::Type::Bool; b.value = "1"; out.push_back(b);
        SettingField s; s.key = "mode"; s.label = "Mode"; s.type = SettingField::Type::Select; s.options = { "dhcp", "static" }; s.value = "static"; out.push_back(s);
        SettingField i; i.key = "info"; i.label = "Info"; i.type = SettingField::Type::Info; i.value = "read only"; out.push_back(i);
        return out;
    }
    std::string apply(const std::map<std::string, std::string>& values) override
    {
        applies++;
        last_values = values;
        if(!fail_with.empty()) return fail_with;
        name_value = values.at("name");
        rate_value = std::atof(values.at("rate").c_str());
        return "";
    }
};

lv_obj_t* nth(lv_obj_t* parent, int32_t idx)
{
    int32_t seen = 0;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t* c = lv_obj_get_child(parent, static_cast<int32_t>(i));
        if(lv_obj_check_type(c, &lv_dropdownlist_class)) continue;
        if(seen++ == idx) return c;
    }
    return nullptr;
}

struct TempProject {
    fs::path dir;
    TempProject()
    {
        dir = fs::temp_directory_path() / ("hml_sys_test_" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this))));
        fs::create_directories(dir);
    }
    ~TempProject() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string write(const std::string& rel, const std::string& text)
    {
        fs::path p = dir / rel;
        std::ofstream f(p, std::ios::binary);
        f << text;
        return p.generic_string();
    }
};

} // namespace

TEST_CASE("pages become tags, screens and an apply round trip")
{
    Runtime rt;
    SystemSettings& sys = system_settings();
    auto page_owner = std::make_unique<FakePage>();
    FakePage* page = page_owner.get();
    sys.add_page(std::move(page_owner));
    CHECK(sys.page_count() == 1);
    CHECK(sys.page("fake") == page);
    CHECK(sys.page("nope") == nullptr);

    sys.create_tags();
    REQUIRE(tags().find("sys.fake.name"));
    CHECK(tags().find("sys.fake.name")->get().str == "Alice");
    CHECK(tags().find("sys.fake.secret")->get().str == "1234");
    CHECK(tags().find("sys.fake.rate")->type() == TagType::Float);
    CHECK(tags().find("sys.fake.rate")->get_number() == doctest::Approx(2.5));
    CHECK(tags().find("sys.fake.on")->type() == TagType::Bool);
    CHECK(tags().find("sys.fake.on")->get_number() == doctest::Approx(1));
    CHECK(tags().find("sys.fake.mode")->type() == TagType::Int);
    CHECK(tags().find("sys.fake.mode")->get_number() == doctest::Approx(1));      // index of "static"
    CHECK(tags().find("sys.fake.info")->get().str == "read only");
    CHECK(tags().find("sys.fake.status")->get().str.empty());
    CHECK(SystemSettings::is_system_tag("sys.fake.name"));
    CHECK_FALSE(SystemSettings::is_system_tag("system"));

    SUBCASE("screens: a menu plus one screen per page, built with the normal widgets") {
        std::vector<json> screens = sys.screens(480, 320);
        REQUIRE(screens.size() == 2);
        CHECK(jstr(screens[0], "name", "") == "__system");
        CHECK(jstr(screens[1], "name", "") == "__system_fake");
        for(const json& s : screens) REQUIRE(ui().build_screen(s, "") != nullptr);
        CHECK(ui().screen_count() == 2);

        REQUIRE(ui().show_screen("__system", "none"));
        lv_obj_t* menu = lv_screen_active();
        lv_obj_t* list = nth(menu, 1);
        REQUIRE(list);
        REQUIRE(lv_obj_get_child_count(list) == 1);
        lv_obj_t* item = lv_obj_get_child(list, 0);
        CHECK(lv_obj_check_type(item, &lv_button_class));
        lv_obj_send_event(item, LV_EVENT_CLICKED, nullptr);               // goto the page
        CHECK(ui().current_screen() == std::optional<std::string>("__system_fake"));

        rt.advance(400);
        lv_obj_t* pg = lv_screen_active();
        lv_obj_t* content = nth(pg, 1);
        REQUIRE(content);
        // 6 field rows + status label + Apply button
        REQUIRE(lv_obj_get_child_count(content) == 8);
        auto widget_of_row = [&](int32_t row) { return nth(lv_obj_get_child(content, row), 1); };
        CHECK(lv_obj_check_type(widget_of_row(0), &lv_textarea_class));
        CHECK(std::string(lv_textarea_get_text(widget_of_row(0))) == "Alice");
        CHECK(lv_textarea_get_password_mode(widget_of_row(1)));
        CHECK(lv_obj_check_type(widget_of_row(2), &lv_textarea_class));
        CHECK(std::string(lv_textarea_get_text(widget_of_row(2))) == "2.5 Hz");
        CHECK(lv_obj_check_type(widget_of_row(3), &lv_switch_class));
        CHECK(lv_obj_has_state(widget_of_row(3), LV_STATE_CHECKED));
        CHECK(lv_obj_check_type(widget_of_row(4), &lv_dropdown_class));
        CHECK(lv_dropdown_get_selected(widget_of_row(4)) == 1);
        CHECK(lv_obj_check_type(widget_of_row(5), &lv_label_class));
        CHECK(std::string(lv_label_get_text(widget_of_row(5))) == "read only");
        lv_obj_t* apply_btn = lv_obj_get_child(content, 7);
        REQUIRE(lv_obj_check_type(apply_btn, &lv_button_class));

        // edit through the widgets, then Apply (a "call" action registered by the fixture)
        lv_textarea_set_text(widget_of_row(0), "Bob");
        lv_obj_send_event(widget_of_row(0), LV_EVENT_READY, nullptr);
        lv_textarea_set_text(widget_of_row(2), "7");
        lv_obj_send_event(widget_of_row(2), LV_EVENT_READY, nullptr);
        lv_obj_remove_state(widget_of_row(3), LV_STATE_CHECKED);
        lv_obj_send_event(widget_of_row(3), LV_EVENT_VALUE_CHANGED, nullptr);
        lv_dropdown_set_selected(widget_of_row(4), 0);
        lv_obj_send_event(widget_of_row(4), LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_send_event(apply_btn, LV_EVENT_CLICKED, nullptr);

        CHECK(page->applies == 1);
        CHECK(page->last_values.at("name") == "Bob");
        CHECK(page->last_values.at("secret") == "1234");
        CHECK(page->last_values.at("rate") == "7");
        CHECK(page->last_values.at("on") == "0");
        CHECK(page->last_values.at("mode") == "dhcp");
        CHECK(page->last_values.at("info") == "read only");
        CHECK(tags().find("sys.fake.status")->get().str == "Applied");
        rt.pump();
        CHECK(std::string(lv_textarea_get_text(widget_of_row(2))) == "7.0 Hz");   // refreshed from the page

        // "< Back" in the header returns to the menu
        lv_obj_t* back = nth(nth(pg, 0), 0);
        lv_obj_send_event(back, LV_EVENT_CLICKED, nullptr);
        CHECK(ui().current_screen() == std::optional<std::string>("__system"));
    }
    SUBCASE("apply reports page errors through the status tag") {
        page->fail_with = "boom";
        CHECK(sys.apply("fake") == "boom");
        CHECK(tags().find("sys.fake.status")->get().str == "boom");
        CHECK(sys.apply("nope") == "unknown page");
    }
    SUBCASE("refresh re-reads the page values") {
        page->name_value = "Carol";
        sys.refresh("fake");
        CHECK(tags().find("sys.fake.name")->get().str == "Carol");
        tags().find("sys.fake.name")->set_string("edited", nullptr);
        sys.refresh();
        CHECK(tags().find("sys.fake.name")->get().str == "Carol");
    }
    SUBCASE("the 'system' action opens the menu") {
        for(const json& s : sys.screens(800, 480)) REQUIRE(ui().build_screen(s, ""));
        lv_obj_t* home = ui().build_screen(json::parse(R"json({ "name": "home", "children": [ { "type": "button", "on_click": { "action": "system", "anim": "none" } } ] })json"), "");
        REQUIRE(home);
        REQUIRE(ui().show_screen("home", "none"));
        lv_obj_send_event(lv_obj_get_child(home, 0), LV_EVENT_CLICKED, nullptr);
        CHECK(ui().current_screen() == std::optional<std::string>("__system"));
        CHECK(ui().back());
        CHECK(ui().current_screen() == std::optional<std::string>("home"));

        // home -> System -> page -> Back -> Back must land on home again (no history loop)
        lv_obj_send_event(lv_obj_get_child(home, 0), LV_EVENT_CLICKED, nullptr);
        REQUIRE(ui().current_screen() == std::optional<std::string>("__system"));
        lv_obj_t* menu = lv_screen_active();
        lv_obj_send_event(lv_obj_get_child(nth(menu, 1), 0), LV_EVENT_CLICKED, nullptr);   // first page
        REQUIRE(ui().current_screen() == std::optional<std::string>("__system_fake"));
        rt.advance(400);
        lv_obj_t* pg = lv_screen_active();
        lv_obj_send_event(nth(nth(pg, 0), 0), LV_EVENT_CLICKED, nullptr);                  // page "< Back"
        CHECK(ui().current_screen() == std::optional<std::string>("__system"));
        rt.advance(400);
        lv_obj_send_event(nth(nth(lv_screen_active(), 0), 0), LV_EVENT_CLICKED, nullptr);  // menu "< Back"
        CHECK(ui().current_screen() == std::optional<std::string>("home"));
        CHECK_FALSE(ui().back());                                                          // history fully unwound
    }
    SUBCASE("registering a page twice replaces it") {
        sys.add_page(std::make_unique<FakePage>());
        CHECK(sys.page_count() == 1);
        CHECK(sys.page("fake") != page);
    }
}

TEST_CASE("call actions: registered names run, unknown names are ignored")
{
    Runtime rt;
    std::string got;
    ui().register_action("echo", [&](const std::string& arg) { got = arg; });
    lv_obj_t* s = ui().build_screen(json::parse(R"json({ "name": "s", "children": [
        { "type": "button", "on_click": { "action": "call", "fn": "echo", "arg": "hello" } },
        { "type": "button", "on_click": { "action": "call", "fn": "nope", "arg": "x" } },
        { "type": "button", "on_click": [ { "action": "call" }, { "action": "call", "fn": "echo", "arg": "second" } ] }
    ] })json"), "");
    REQUIRE(s);
    lv_obj_send_event(lv_obj_get_child(s, 0), LV_EVENT_CLICKED, nullptr);
    CHECK(got == "hello");
    lv_obj_send_event(lv_obj_get_child(s, 1), LV_EVENT_CLICKED, nullptr);
    CHECK(got == "hello");
    lv_obj_send_event(lv_obj_get_child(s, 2), LV_EVENT_CLICKED, nullptr);
    CHECK(got == "second");
    ui().register_action("echo", [&](const std::string& arg) { got = "v2:" + arg; });
    lv_obj_send_event(lv_obj_get_child(s, 0), LV_EVENT_CLICKED, nullptr);
    CHECK(got == "v2:hello");
}

TEST_CASE("Project::load registers About, Network and one page per driver")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.write("project.json", R"json({
        "tags": [ { "name": "x" } ],
        "key_management": { "enabled": false },
        "drivers": [
            { "type": "sim", "items": [ { "tag": "x", "mode": "square", "min": 0, "max": 1, "period_ms": 3600000 } ] },
            { "type": "modbus_tcp", "name": "PLC", "host": "10.0.0.5", "port": 502, "poll_ms": 200, "enabled": false, "map": [] },
            { "type": "sim", "items": [] }
        ]
    })json")));
    SystemSettings& sys = system_settings();
    CHECK(sys.page_ids() == std::vector<std::string>{ "about", "network", "display", "driver_sim", "driver_plc", "driver_sim_2", "monitor", "storage" });
    json arr = json::parse(R"([{"type":"sim"},{"type":"modbus_tcp","name":"PLC"},{"type":"sim"}])");
    CHECK(driver_keys(&arr) == std::vector<std::string>{ "sim", "PLC", "sim#2" });
    CHECK(driver_keys(nullptr).empty());

    // about page values
    CHECK(tags().find("sys.about.project")->get().str == "HML Runtime");
    CHECK_FALSE(tags().find("sys.about.ip")->get().str.empty());

    // driver page tags mirror the configuration
    REQUIRE(tags().find("sys.driver_plc.host"));
    CHECK(tags().find("sys.driver_plc.host")->get().str == "10.0.0.5");
    CHECK(tags().find("sys.driver_plc.port")->get_number() == doctest::Approx(502));
    CHECK(tags().find("sys.driver_plc.enabled")->get_number() == doctest::Approx(0));
    CHECK(tags().find("sys.driver_sim.enabled")->get_number() == doctest::Approx(1));
    CHECK(tags().find("sys.driver_plc.map") == nullptr);       // arrays are not editable

    // screens are generated alongside the project's own
    REQUIRE(p.build_screens() == 0);                           // no project screens -> fallback label, but...
    CHECK(ui().screen_count() == 9);                           // ...menu + 8 pages exist
    CHECK(ui().show_screen("__system_driver_plc", "none"));

    SUBCASE("applying a driver page writes project.local.json and restarts the drivers") {
        static bool builtins = (drivers().register_builtins(), true);
        (void)builtins;
        CHECK(p.start_drivers() == 2);                         // sim + sim (PLC disabled)

        tags().find("sys.driver_plc.host")->set_string("192.168.0.9", nullptr);
        tags().find("sys.driver_plc.port")->set_number(5020, nullptr);
        tags().find("sys.driver_sim.enabled")->set_number(0, nullptr);
        CHECK(sys.apply("driver_plc") == "");
        CHECK(sys.apply("driver_sim") == "");

        std::ifstream of(tp.dir / "project.local.json");
        json overlay = json::parse(of);
        of.close();
        CHECK(overlay["drivers"]["PLC"]["host"] == "192.168.0.9");
        CHECK(overlay["drivers"]["PLC"]["port"] == 5020.0);
        CHECK(overlay["drivers"]["PLC"].size() == 2);          // only what differs from project.json is kept
        CHECK(overlay["drivers"]["sim"]["enabled"] == false);
        CHECK(overlay["drivers"].contains("sim#2") == false);

        // the merged configuration is what the pages and the drivers now see
        CHECK(jstr(*p.driver_config("PLC"), "host", "") == "192.168.0.9");
        CHECK(tags().find("sys.driver_plc.host")->get().str == "192.168.0.9");
        CHECK(tags().find("sys.driver_plc.status")->get().str == "Applied");

        // a fresh load picks the overlay up and project.json itself is untouched
        Project again;
        REQUIRE(again.load((tp.dir / "project.json").generic_string()));
        CHECK(jstr(*again.driver_config("PLC"), "host", "") == "192.168.0.9");
        CHECK(jbool(*again.driver_config("sim"), "enabled", true) == false);
        std::ifstream rf(tp.dir / "project.json");
        json raw = json::parse(rf);
        CHECK(jstr(raw["drivers"][1], "host", "") == "10.0.0.5");

        // setting everything back to the project's values removes the entries, then the file
        tags().find("sys.driver_plc.host")->set_string("10.0.0.5", nullptr);
        tags().find("sys.driver_plc.port")->set_number(502, nullptr);
        CHECK(sys.apply("driver_plc") == "");
        {
            std::ifstream of2(tp.dir / "project.local.json");
            json o2 = json::parse(of2);
            CHECK(o2["drivers"].contains("PLC") == false);
            CHECK(o2["drivers"]["sim"]["enabled"] == false);
        }
        tags().find("sys.driver_sim.enabled")->set_number(1, nullptr);
        CHECK(sys.apply("driver_sim") == "");
        CHECK_FALSE(std::filesystem::exists(tp.dir / "project.local.json"));
        drivers().stop_all();
    }
    SUBCASE("a broken overlay file is ignored") {
        tp.write("project.local.json", "{ nope");
        Project again;
        REQUIRE(again.load((tp.dir / "project.json").generic_string()));
        CHECK(jstr(*again.driver_config("PLC"), "host", "") == "10.0.0.5");
    }
}

TEST_CASE("run_command and local_ip_addresses")
{
    int rc = -1;
#ifdef _WIN32
    std::string out = run_command("echo hello", &rc);
#else
    std::string out = run_command("echo hello", &rc);
#endif
    CHECK(rc == 0);
    CHECK(out.find("hello") != std::string::npos);
    run_command("exit 3", &rc);
    CHECK(rc == 3);
    CHECK_FALSE(local_ip_addresses().empty());
}

TEST_CASE("serial ports: listing and names of the other operating system")
{
    for(const SerialPortInfo& p : list_serial_ports()) {         // whatever this machine has: well-formed entries
        CHECK_FALSE(p.device.empty());
        CHECK(p.device.find(' ') == std::string::npos);
        CHECK(serial_name_problem(p.device).empty());            // its own ports are never "of the other system"
    }
#ifdef _WIN32
    CHECK(serial_name_problem("COM4").empty());
    CHECK(serial_name_problem(R"(\\.\COM12)").empty());
    std::string msg = serial_name_problem("/dev/ttyUSB0");
    CHECK(msg.find("is a Linux port name") != std::string::npos);
#else
    CHECK(serial_name_problem("/dev/ttyUSB0").empty());
    CHECK(serial_name_problem("/dev/serial/by-id/usb-FTDI_x-if00-port0").empty());
    std::string msg = serial_name_problem("COM4");
    CHECK(msg.find("is a Windows port name") != std::string::npos);
    CHECK(serial_name_problem("com12").find("Windows") != std::string::npos);
    CHECK(serial_name_problem("COMPUTER").empty());              // not COM<number>
#endif
    CHECK(msg.find("local settings") != std::string::npos);      // says where to set this machine's port
    CHECK(serial_name_problem("").empty());
}

TEST_CASE("driver page: the serial port is picked from a list, the current name stays selectable")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.write("project.json", R"json({
        "tags": [ { "name": "x" } ], "key_management": { "enabled": false },
        "drivers": [ { "type": "modbus_rtu", "device": "/dev/hml-not-there", "baud": 9600, "enabled": false, "map": [] } ]
    })json")));
    SystemSettings& sys = system_settings();
    REQUIRE(sys.page("driver_modbus_rtu") != nullptr);
    Tag* dev = tags().find("sys.driver_modbus_rtu.device");
    Tag* opts = tags().find("sys.driver_modbus_rtu.device__options");
    REQUIRE(dev);
    REQUIRE(opts);
    CHECK(dev->type() == TagType::Int);                          // a dropdown: the index of the option
    CHECK(dev->get_number() == 0);
    const std::string options = opts->get().str;
    CHECK(options.rfind("/dev/hml-not-there", 0) == 0);          // what is configured comes first when no such port exists
    REQUIRE(tags().find("sys.driver_modbus_rtu.device_ports"));
    CHECK(tags().find("sys.driver_modbus_rtu.baud")->get_number() == doctest::Approx(9600));

    // applying without a change stores nothing (the name still comes from project.json)
    CHECK(sys.apply("driver_modbus_rtu") == "");
    CHECK_FALSE(std::filesystem::exists(tp.dir / "project.local.json"));

    // picking another port (if this machine has one) stores it in the local settings only
    size_t nl = options.find('\n');
    if(nl != std::string::npos) {
        std::string second = options.substr(nl + 1, options.find('\n', nl + 1) - nl - 1);
        dev->set_number(1, nullptr);
        CHECK(sys.apply("driver_modbus_rtu") == "");
        std::ifstream f(tp.dir / "project.local.json");
        json overlay = json::parse(f);
        f.close();
        CHECK(overlay["drivers"]["modbus_rtu"]["device"] == second);
        CHECK(overlay["drivers"]["modbus_rtu"].size() == 1);
        CHECK(jstr(*p.driver_config("modbus_rtu"), "device", "") == second);
        // ... and the list now starts with the ports, the selection follows the name
        CHECK(tags().find("sys.driver_modbus_rtu.device__options")->get().str.find("/dev/hml-not-there") == std::string::npos);
    }
    drivers().stop_all();
}

TEST_CASE("driver page: the MQTT user name and password are entered on the device, the password hidden")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.write("project.json", R"json({
        "tags": [ { "name": "x" } ], "key_management": { "enabled": false },
        "drivers": [ { "type": "mqtt", "host": "127.0.0.1", "port": 1, "tls": false, "enabled": false } ]
    })json")));
    SystemSettings& sys = system_settings();
    SettingsPage* page = sys.page("driver_mqtt");
    REQUIRE(page);
    bool user = false, pass_hidden = false;
    for(const SettingField& f : page->fields()) {
        if(f.key == "username") user = !f.password;
        if(f.key == "password") pass_hidden = f.password;
    }
    CHECK(user);
    CHECK(pass_hidden);                                          // not in project.json, still offered
    // nothing entered: nothing stored
    CHECK(sys.apply("driver_mqtt") == "");
    CHECK_FALSE(std::filesystem::exists(tp.dir / "project.local.json"));
    tags().find("sys.driver_mqtt.username")->set_string("op", nullptr);
    tags().find("sys.driver_mqtt.password")->set_string("pw-123", nullptr);
    CHECK(sys.apply("driver_mqtt") == "");
    std::ifstream f(tp.dir / "project.local.json");
    json overlay = json::parse(f);
    f.close();
    CHECK(overlay["drivers"]["mqtt"]["username"] == "op");
    CHECK(overlay["drivers"]["mqtt"]["password"] == "pw-123");
    CHECK(jstr(*p.driver_config("mqtt"), "password", "") == "pw-123");
    drivers().stop_all();
}

TEST_CASE("System > Tags: lists the tags, filters, pages, writes a value")
{
    Runtime rt;
    for(int i = 1; i <= 10; i++) rt.tag(vformat("t%02d", i), TagType::Int, i);
    rt.tag("flag", TagType::Bool);
    rt.tag("limited", TagType::Int, 5, 0, 100);
    rt.tag("text", std::string("abc"));
    rt.tag("sys.test.value", TagType::Int, 3);            // (one of the runtime's own)
    SystemSettings& sys = system_settings();
    sys.add_page(make_monitor_page());
    sys.create_tags();
    auto text = [](const char* key) { return tags().find(std::string("sys.monitor.") + key)->get().format(); };
    auto type = [](const char* key, const char* value) { tags().find(std::string("sys.monitor.") + key)->set_string(value, nullptr); };

    // the tags of the project, eight at a time, not the runtime's own
    CHECK(text("range") == "1 - 8 / 13");
    CHECK(text("r1") == "t01");
    CHECK(text("v1") == "1");
    CHECK(text("r8") == "t08");
    CHECK(tags().find("sys.monitor.selected")->get_number() == 0);
    sys.command("monitor", "next");
    CHECK(text("range") == "9 - 13 / 13");
    CHECK(text("r1") == "t09");
    CHECK(text("r5") == "text");
    CHECK(text("v5") == "abc");
    CHECK(tags().find("sys.monitor.r6_used")->get_number() == 0);
    sys.command("monitor", "next");                       // no further page
    CHECK(text("r1") == "t09");
    sys.command("monitor", "prev");
    CHECK(text("r1") == "t01");

    // a filter (any case); "sys." shows the runtime's tags
    type("filter", "LIM");
    sys.command("monitor", "find");
    CHECK(text("range") == "1 - 1 / 1");
    CHECK(text("r1") == "limited");
    type("filter", "sys.");
    sys.command("monitor", "find");
    CHECK(text("r1").rfind("sys.", 0) == 0);
    CHECK(text("r1").rfind("sys.monitor.", 0) != 0);      // (not the page's own tags)

    // pick a tag, write a value: the limits of the tag hold
    type("filter", "lim");
    sys.command("monitor", "find");
    sys.command("monitor", "select", "1");
    CHECK(tags().find("sys.monitor.selected")->get_number() == 1);
    CHECK(text("sel_name") == "limited  (int)");
    CHECK(text("sel_value") == "5");
    CHECK(text("value") == "5");
    type("value", "42");
    sys.command("monitor", "write");
    CHECK(tags().find("limited")->get_number() == 42);
    CHECK(text("status") == "limited = 42");
    CHECK(text("sel_value") == "42");
    type("value", "500");
    sys.command("monitor", "write");
    CHECK(tags().find("limited")->get_number() == 100);
    CHECK(text("status").find("inside the limits") != std::string::npos);
    type("value", "abc");                                 // not a number: nothing is written
    sys.command("monitor", "write");
    CHECK(tags().find("limited")->get_number() == 100);
    CHECK(text("status").find("not a value") != std::string::npos);
    CHECK(text("value") == "abc");                        // what was typed stays

    // a bool and a text
    type("filter", "flag");
    sys.command("monitor", "find");
    sys.command("monitor", "select", "1");
    type("value", "true");
    sys.command("monitor", "write");
    CHECK(tags().find("flag")->get_number() == 1);
    // the form follows the type: a bool has two buttons that write at once
    auto on = [](const char* key) { return tags().find(std::string("sys.monitor.") + key)->get_number() != 0; };
    CHECK(on("sel_bool"));
    CHECK_FALSE(on("sel_number"));
    CHECK_FALSE(on("sel_text"));
    CHECK_FALSE(on("sel_typed"));
    sys.command("monitor", "set", "false");
    CHECK(tags().find("flag")->get_number() == 0);
    CHECK(text("status") == "flag = false");
    CHECK(text("sel_value") == "false");
    sys.command("monitor", "set", "true");
    CHECK(tags().find("flag")->get_number() == 1);
    // a number is typed on the number pad, a text on the keyboard
    type("filter", "lim");
    sys.command("monitor", "find");
    sys.command("monitor", "select", "1");
    CHECK(on("sel_number"));
    CHECK(on("sel_typed"));
    CHECK_FALSE(on("sel_bool"));
    CHECK_FALSE(on("sel_text"));
    // the filter is emptied with one button
    CHECK(on("filtered"));
    sys.command("monitor", "clear");
    CHECK_FALSE(on("filtered"));
    CHECK(text("filter") == "");
    CHECK(text("range") == "1 - 8 / 13");
    type("filter", "text");
    sys.command("monitor", "find");
    sys.command("monitor", "select", "1");
    CHECK(on("sel_text"));
    CHECK(on("sel_typed"));
    type("value", "hello world");
    sys.command("monitor", "write");
    CHECK(tags().find("text")->get().str == "hello world");

    // the runtime's own tags are shown but not written here
    type("filter", "sys.test");
    sys.command("monitor", "find");
    sys.command("monitor", "select", "1");
    CHECK(text("sel_value") == "3");
    type("value", "9");
    sys.command("monitor", "write");
    CHECK(text("status").find("not written here") != std::string::npos);
    CHECK(tags().find("sys.test.value")->get_number() == 3);
    sys.command("monitor", "close");
    CHECK(tags().find("sys.monitor.selected")->get_number() == 0);

    // the page builds as a screen and follows the tags while it is shown
    for(const json& scr : sys.screens(800, 480)) REQUIRE(ui().build_screen(scr, ""));
    type("filter", "t01");
    sys.command("monitor", "find");
    REQUIRE(ui().show_screen("__system_monitor", "none"));
    rt.advance(100);
    tags().find("t01")->set_number(77, nullptr);
    for(int i = 0; i < 6; i++) sys.poll();
    CHECK(text("v1") == "77");
    ui().unload_all();
}
