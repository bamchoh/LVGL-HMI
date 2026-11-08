// System settings: pages -> tags -> generated screens -> apply.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
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
        "drivers": [
            { "type": "sim", "items": [ { "tag": "x", "mode": "square", "min": 0, "max": 1, "period_ms": 3600000 } ] },
            { "type": "modbus_tcp", "name": "PLC", "host": "10.0.0.5", "port": 502, "poll_ms": 200, "enabled": false, "map": [] },
            { "type": "sim", "items": [] }
        ]
    })json")));
    SystemSettings& sys = system_settings();
    CHECK(sys.page_ids() == std::vector<std::string>{ "about", "network", "driver_sim", "driver_plc", "driver_sim_2" });
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
    CHECK(ui().screen_count() == 6);                           // ...menu + 5 pages exist
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
        CHECK(overlay["drivers"]["PLC"]["host"] == "192.168.0.9");
        CHECK(overlay["drivers"]["PLC"]["port"] == 5020.0);
        CHECK(overlay["drivers"]["PLC"]["enabled"] == false);
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
