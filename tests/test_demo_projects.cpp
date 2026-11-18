// The projects shipped in projects/ must load and build every screen on the headless display.
// This catches typos in the demo JSON (unknown widget types, missing tags, bad images) that
// would otherwise only show up as warnings on the console.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "script.hpp"
#include "system_settings.hpp"
#include "ui.hpp"
#include "users.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

#ifndef HML_TEST_SOURCE_DIR
#  error "HML_TEST_SOURCE_DIR must point at the repository root"
#endif

namespace {

std::vector<fs::path> shipped_projects()
{
    std::vector<fs::path> out;
    fs::path root = fs::path(HML_TEST_SOURCE_DIR) / "projects";
    for(const auto& e : fs::directory_iterator(root)) {
        if(e.is_directory() && fs::exists(e.path() / "project.json")) out.push_back(e.path() / "project.json");
    }
    std::sort(out.begin(), out.end());
    return out;
}

json read_json(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return json::parse(ss.str(), nullptr, true, true);
}

} // namespace

TEST_CASE("every shipped project loads and builds all of its screens")
{
    std::vector<fs::path> projects = shipped_projects();
    REQUIRE(projects.size() >= 2);                      // demo and lcd35 at least

    for(const fs::path& path : projects) {
        CAPTURE(path.generic_string());
        Runtime rt;
        Project p;
        REQUIRE(p.load(path.generic_string()));

        json raw = read_json(path);
        const json* screens = jget(raw, "screens");
        REQUIRE(screens);
        REQUIRE(screens->is_array());
        size_t user_tags = 0;
        for(Tag* t : tags().all()) if(!SystemSettings::is_system_tag(t->name())) user_tags++;
        CHECK(user_tags == jget(raw, "tags")->size());

        p.apply_theme(hmltest::Lvgl::get().disp);
        CHECK(p.build_screens() == static_cast<int>(screens->size()));
        // besides the project's screens: the system menu, one screen per page, and the login screen
        const size_t builtin = system_settings().page_count() + 1 + (users().enabled() ? 1 : 0);
        CHECK(ui().screen_count() - builtin == screens->size());
        CHECK(ui().current_screen() == std::optional<std::string>(p.info().start_screen));

        // with user management some screens need a level: log in as the highest one first
        if(users().enabled()) {
            REQUIRE(users().login("admin", "0000") == Users::LoginResult::Ok);
            CHECK(users().level() == users().max_level());
        }
        // every screen can be shown and rendered
        for(const json& s : *screens) {
            fs::path sp = s.is_string() ? fs::path(p.info().dir) / s.get<std::string>() : fs::path();
            std::string name = s.is_string() ? jstr(read_json(sp), "name", "") : jstr(s, "name", "");
            CAPTURE(name);
            CHECK(ui().show_screen(name, "none"));
            hmltest::Lvgl::get().render();
        }
        rt.advance(1000);                               // chart timers, animations, interval scripts

        // the shipped scripts compile and run cleanly; auto_speed drives the setpoint from the temperature
        if(const json* sc = jget(raw, "scripts")) CHECK(scripts().count() == sc->size());
        CHECK(scripts().error_count() == 0);
        if(scripts().has("auto_speed")) {
            tags().find("auto_mode")->set_number(1, nullptr);
            tags().find("run")->set_number(1, nullptr);
            tags().find("temp")->set_number(25, nullptr);
            rt.advance(600);
            CHECK(tags().find("speed_sp")->get_number() == 50);
            tags().find("auto_mode")->set_number(0, nullptr);
            tags().find("temp")->set_number(32, nullptr);
            rt.advance(600);
            CHECK(tags().find("speed_sp")->get_number() == 50);
            CHECK(scripts().error_count() == 0);
        }
    }
}

TEST_CASE("the demo project's drivers reference existing tags")
{
    fs::path path = fs::path(HML_TEST_SOURCE_DIR) / "projects" / "demo" / "project.json";
    json raw = read_json(path);
    std::vector<std::string> tag_names;
    for(const json& t : *jget(raw, "tags")) tag_names.push_back(jstr(t, "name", ""));
    auto known = [&](const std::string& n) { return std::find(tag_names.begin(), tag_names.end(), n) != tag_names.end(); };

    for(const json& d : *jget(raw, "drivers")) {
        std::string type = jstr(d, "type", "");
        CAPTURE(type);
        if(auto st = jstr_opt(d, "status_tag")) CHECK(known(*st));
        for(const char* key : { "items", "map" }) {
            if(const json* list = jget(d, key); list && list->is_array()) {
                for(const json& e : *list) {
                    std::string t = jstr(e, "tag", "");
                    CAPTURE(t);
                    CHECK(known(t));
                }
            }
        }
    }
}
