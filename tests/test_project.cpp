#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "ui.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

// A scratch project directory under the system temp dir, removed on destruction.
struct TempProject {
    fs::path dir;

    TempProject()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::create_directories(dir / "screens");
    }
    ~TempProject()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string write(const std::string& rel, const std::string& text)
    {
        fs::path p = dir / rel;
        std::ofstream f(p, std::ios::binary);
        f << text;
        return p.generic_string();
    }
    std::string project(const std::string& text) { return write("project.json", text); }
};

std::string screen_json(const std::string& name)
{
    return "{ \"name\": \"" + name + "\", \"children\": [ { \"type\": \"label\", \"text\": \"" + name + "\" } ] }";
}

} // namespace

TEST_CASE("Project::load fails on a missing or malformed file")
{
    Runtime rt;
    TempProject tp;
    Project p;
    CHECK_FALSE(p.load((tp.dir / "nope.json").generic_string()));
    CHECK_FALSE(p.load(tp.project("{ not json")));
    CHECK_FALSE(p.load(tp.project("")));
    CHECK(tags().count() == 0);
}

TEST_CASE("Project::load tolerates comments and a UTF-8 BOM")
{
    Runtime rt;
    TempProject tp;
    Project p;
    std::string text = "\xEF\xBB\xBF{\n  // line comment\n  \"title\": \"T\", /* block */ \"tags\": []\n}";
    REQUIRE(p.load(tp.project(text)));
    CHECK(p.info().title == "T");
}

TEST_CASE("Project::load applies defaults for everything that is missing")
{
    Runtime rt;
    TempProject tp;
    Project p;
    std::string path = tp.project("{}");
    REQUIRE(p.load(path));
    const ProjectInfo& i = p.info();
    CHECK(i.path == path);
    CHECK(i.dir == tp.dir.generic_string());
    CHECK(i.title == "HML Runtime");
    CHECK(i.width == 800);
    CHECK(i.height == 480);
    CHECK(i.zoom == doctest::Approx(1.0));
    CHECK(i.rotation == 0);
    CHECK(i.dpi == 130);
    CHECK(i.fbdev.empty());
    CHECK(i.drm.empty());
    CHECK(i.input.empty());
    CHECK(i.console == "/dev/tty1");
    CHECK_FALSE(i.touch_swap_axes);
    CHECK_FALSE(i.touch_has_calib);
    CHECK(i.dark);
    CHECK(lv_color_eq(i.primary, lv_palette_main(LV_PALETTE_BLUE)));
    CHECK(lv_color_eq(i.secondary, lv_palette_main(LV_PALETTE_RED)));
    CHECK(i.start_screen.empty());
    CHECK(tags().count() == 0);
}

TEST_CASE("Project::load reads display, touch and theme settings")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.project(R"({
        "title": "Panel",
        "display": { "width": 480, "height": 320, "zoom": 1.5, "rotation": 270, "dpi": 200,
                     "fbdev": "/dev/fb1", "drm": "/dev/dri/card1", "input": "/dev/input/event3", "console": "none",
                     "touch": { "swap_axes": true, "calibration": [100, 200, 3900, 3800] } },
        "theme": { "dark": false, "primary": "#ff0000", "secondary": "green" },
        "start_screen": "home"
    })")));
    const ProjectInfo& i = p.info();
    CHECK(i.title == "Panel");
    CHECK(i.width == 480);
    CHECK(i.height == 320);
    CHECK(i.zoom == doctest::Approx(1.5));
    CHECK(i.rotation == 270);
    CHECK(i.dpi == 200);
    CHECK(i.fbdev == "/dev/fb1");
    CHECK(i.drm == "/dev/dri/card1");
    CHECK(i.input == "/dev/input/event3");
    CHECK(i.console == "none");
    CHECK(i.touch_swap_axes);
    REQUIRE(i.touch_has_calib);
    CHECK(i.touch_calib[0] == 100);
    CHECK(i.touch_calib[1] == 200);
    CHECK(i.touch_calib[2] == 3900);
    CHECK(i.touch_calib[3] == 3800);
    CHECK_FALSE(i.dark);
    CHECK(lv_color_eq(i.primary, lv_color_hex(0xff0000)));
    CHECK(lv_color_eq(i.secondary, lv_palette_main(LV_PALETTE_GREEN)));
    CHECK(i.start_screen == "home");
}

TEST_CASE("Project::load validates rotation, dpi and the calibration array")
{
    Runtime rt;
    TempProject tp;

    SUBCASE("rotation must be a multiple of 90 within 0..270") {
        for(const char* bad : { "45", "-90", "360" }) {
            Project p;
            REQUIRE(p.load(tp.project(std::string("{ \"display\": { \"rotation\": ") + bad + " } }")));
            CHECK(p.info().rotation == 0);
        }
        for(int good : { 90, 180 }) {
            Project p;
            REQUIRE(p.load(tp.project("{ \"display\": { \"rotation\": " + std::to_string(good) + " } }")));
            CHECK(p.info().rotation == good);
        }
    }
    SUBCASE("dpi outside 50..600 falls back to 130") {
        Project p;
        REQUIRE(p.load(tp.project(R"({ "display": { "dpi": 10 } })")));
        CHECK(p.info().dpi == 130);
        REQUIRE(p.load(tp.project(R"({ "display": { "dpi": 1000 } })")));
        CHECK(p.info().dpi == 130);
        REQUIRE(p.load(tp.project(R"({ "display": { "dpi": 50 } })")));
        CHECK(p.info().dpi == 50);
    }
    SUBCASE("a calibration array needs exactly four entries") {
        Project p;
        REQUIRE(p.load(tp.project(R"({ "display": { "touch": { "calibration": [1, 2, 3] } } })")));
        CHECK_FALSE(p.info().touch_has_calib);
        REQUIRE(p.load(tp.project(R"({ "display": { "touch": { "calibration": "1,2,3,4" } } })")));
        CHECK_FALSE(p.info().touch_has_calib);
    }
}

TEST_CASE("Project::load creates the tags with type, initial value and limits")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.project(R"({
        "tags": [
            { "name": "f" },
            { "name": "i", "type": "int", "init": 7.6 },
            { "name": "lim", "type": "int", "init": 500, "min": 0, "max": 100 },
            { "name": "lo", "type": "float", "init": -5, "min": 0 },
            { "name": "b", "type": "bool", "init": true },
            { "name": "bs", "type": "bool", "init": "true" },
            { "name": "s", "type": "string", "init": "hello" },
            { "name": "sn", "type": "string", "init": 2.5 },
            { "type": "int" },
            { "name": "f", "type": "string", "init": "dup" }
        ]
    })")));
    CHECK(tags().count() == 8);

    Tag* f = tags().find("f");
    REQUIRE(f);
    CHECK(f->type() == TagType::Float);                 // default type
    CHECK(f->get_number() == doctest::Approx(0));

    Tag* i = tags().find("i");
    REQUIRE(i);
    CHECK(i->type() == TagType::Int);
    CHECK(i->get_number() == doctest::Approx(8));       // rounded

    Tag* lim = tags().find("lim");
    REQUIRE(lim);
    CHECK(lim->get_number() == doctest::Approx(100));   // clamped
    double mn, mx;
    CHECK(lim->limits(mn, mx));
    CHECK(mn == doctest::Approx(0));
    CHECK(mx == doctest::Approx(100));

    Tag* lo = tags().find("lo");                        // only "min" given: max is open
    REQUIRE(lo);
    CHECK(lo->limits(mn, mx));
    CHECK(mn == doctest::Approx(0));
    CHECK(mx > 1e100);
    CHECK(lo->get_number() == doctest::Approx(0));
    lo->set_number(1e6, nullptr);
    CHECK(lo->get_number() == doctest::Approx(1e6));

    Tag* b = tags().find("b");
    REQUIRE(b);
    CHECK(b->type() == TagType::Bool);
    CHECK(b->get_number() == doctest::Approx(1));
    CHECK(tags().find("bs")->get_number() == doctest::Approx(1));

    Tag* s = tags().find("s");
    REQUIRE(s);
    CHECK(s->type() == TagType::String);
    CHECK(s->get().str == "hello");
    CHECK(tags().find("sn")->get().str == "2.5");

    CHECK_FALSE(tags().find(""));
}

TEST_CASE("Project::start_drivers loads the drivers array through the global registry")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.project(R"({
        "tags": [ { "name": "x" } ],
        "drivers": [
            { "type": "sim", "items": [ { "tag": "x", "mode": "square", "min": 0, "max": 1, "period_ms": 3600000 } ] },
            { "type": "unknown_driver" }
        ]
    })")));
    static bool builtins_registered = (drivers().register_builtins(), true);   // main() does this at startup
    (void)builtins_registered;
    CHECK(p.start_drivers() == 1);
    rt.advance(50, 50);
    CHECK(tags().find("x")->get_number() == doctest::Approx(1));
    drivers().stop_all();

    Project none;
    REQUIRE(none.load(tp.project("{}")));
    CHECK(none.start_drivers() == 0);
}

TEST_CASE("Project::build_screens builds file and inline screens and shows the start screen")
{
    Runtime rt;
    TempProject tp;
    tp.write("screens/main.json", screen_json("main"));
    tp.write("screens/settings.json", screen_json("settings"));
    std::string abs = tp.write("screens/abs.json", screen_json("abs"));

    Project p;
    REQUIRE(p.load(tp.project(R"({
        "start_screen": "settings",
        "screens": [
            "screens/main.json",
            "screens/settings.json",
            ")" + abs + R"(",
            "screens/missing.json",
            { "name": "inline", "children": [] },
            { "children": [] },
            42
        ]
    })")));

    CHECK(p.build_screens() == 4);
    CHECK(ui().screen_count() == 4);
    CHECK(ui().current_screen() == std::optional<std::string>("settings"));

    SUBCASE("the first screen is shown when start_screen is unknown and the current one is gone") {
        REQUIRE(p.load(tp.project(R"({ "start_screen": "nope", "screens": [ "screens/main.json", ")" + abs + R"(" ] })")));
        CHECK(p.build_screens() == 2);
        CHECK(ui().current_screen() == std::optional<std::string>("main"));
    }
    SUBCASE("a rebuild keeps the screen the operator was looking at") {
        REQUIRE(ui().show_screen("main", "none"));
        CHECK(p.build_screens() == 4);
        CHECK(ui().current_screen() == std::optional<std::string>("main"));
    }
    SUBCASE("a rebuild re-reads the project file") {
        tp.project(R"({ "start_screen": "settings", "screens": [ "screens/main.json" ] })");
        CHECK(p.build_screens() == 1);
        CHECK(ui().screen_count() == 1);
        CHECK(ui().current_screen() == std::optional<std::string>("main"));
    }
}

TEST_CASE("Project::build_screens with nothing buildable shows a fallback message")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.project(R"({ "screens": [ "screens/missing.json" ] })")));
    CHECK(p.build_screens() == 0);
    CHECK(ui().screen_count() == 0);
    CHECK_FALSE(ui().current_screen().has_value());

    lv_obj_t* scr = lv_screen_active();
    REQUIRE(lv_obj_get_child_count(scr) >= 1);
    lv_obj_t* label = lv_obj_get_child(scr, 0);
    REQUIRE(lv_obj_check_type(label, &lv_label_class));
    CHECK(std::string(lv_label_get_text(label)).find("no screens") != std::string::npos);

    Project empty;
    REQUIRE(empty.load(tp.project("{}")));
    CHECK(empty.build_screens() == 0);
}

TEST_CASE("Project::apply_theme installs a theme on the display")
{
    Runtime rt;
    TempProject tp;
    Project p;
    REQUIRE(p.load(tp.project(R"({ "theme": { "dark": false, "primary": "teal", "font": 20 } })")));
    lv_display_t* disp = hmltest::Lvgl::get().disp;
    p.apply_theme(disp);
    CHECK(lv_display_get_theme(disp) != nullptr);

    Project nofont;
    REQUIRE(nofont.load(tp.project("{}")));
    nofont.apply_theme(disp);
    CHECK(lv_display_get_theme(disp) != nullptr);
}

TEST_CASE("the global project() is a singleton and unload clears it")
{
    Runtime rt;
    TempProject tp;
    CHECK(&project() == &project());
    REQUIRE(project().load(tp.project(R"({ "screens": [ { "name": "a", "children": [] } ] })")));
    CHECK(project().build_screens() == 1);
    project().unload();
    CHECK(project().build_screens() == 1);   // the file is re-read, so screens still build
}
