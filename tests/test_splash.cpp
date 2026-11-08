#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "splash.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

using namespace hml;
using hmltest::Lvgl;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct SplashProject {
    fs::path dir;
    SplashProject()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_splash_" + std::to_string(counter++));
        fs::create_directories(dir / "assets");
    }
    ~SplashProject()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string project(const std::string& display)
    {
        fs::path p = dir / "project.json";
        std::ofstream(p, std::ios::binary) << "{ \"title\": \"Pump Station\", \"display\": " << display << ", \"tags\": [] }";
        return p.generic_string();
    }
    void copy_asset(const std::string& from, const std::string& to)
    {
        fs::copy_file(fs::path(HML_TEST_SOURCE_DIR) / from, dir / "assets" / to, fs::copy_options::overwrite_existing);
    }
};

// Build the splash on the test display, run `check` on it, then put the previous screen back.
template<class F>
void with_splash(const ProjectInfo& info, F check)
{
    lv_obj_t* prev = lv_screen_active();
    lv_obj_t* scr = build_splash(Lvgl::get().disp, info);
    Lvgl::get().render();
    check(scr);
    lv_screen_load(prev);
    lv_obj_delete(scr);
}

} // namespace

TEST_CASE("Project::load reads the splash settings")
{
    Runtime rt;
    SplashProject sp;
    Project p;
    REQUIRE(p.load(sp.project("{ \"splash\": \"assets/boot.png\", \"splash_bg\": \"#102030\" }")));
    CHECK(p.info().splash == (sp.dir.generic_string() + "/assets/boot.png"));
    CHECK(lv_color_eq(p.info().splash_bg, lv_color_hex(0x102030)));

    REQUIRE(p.load(sp.project("{}")));
    CHECK(p.info().splash.empty());
    CHECK(lv_color_eq(p.info().splash_bg, lv_color_black()));
}

TEST_CASE("build_splash shows the title when no image is set or it cannot be read")
{
    Runtime rt;
    SplashProject sp;
    Project p;
    for(const char* disp : { "{}", "{ \"splash\": \"assets/missing.png\", \"splash_bg\": \"#ffffff\" }" }) {
        REQUIRE(p.load(sp.project(disp)));
        with_splash(p.info(), [&](lv_obj_t* scr) {
            CHECK(lv_screen_active() == scr);
            CHECK(lv_color_eq(lv_obj_get_style_bg_color(scr, LV_PART_MAIN), p.info().splash_bg));
            REQUIRE(lv_obj_get_child_count(scr) == 1);
            lv_obj_t* lbl = lv_obj_get_child(scr, 0);
            REQUIRE(lv_obj_check_type(lbl, &lv_label_class));
            CHECK(std::string(lv_label_get_text(lbl)) == "Pump Station");
            // readable on the background: white on black, black on white
            lv_color_t fg = lv_obj_get_style_text_color(lbl, LV_PART_MAIN);
            CHECK(lv_color_eq(fg, p.info().splash.empty() ? lv_color_white() : lv_color_black()));
        });
    }
}

TEST_CASE("build_splash centres the image and shrinks it only when it does not fit")
{
    Runtime rt;
    SplashProject sp;
    sp.copy_asset("projects/lcd35/assets/logo.png", "small.png");        // 48x48
    sp.copy_asset("tests/assets/splash_640x480.png", "big.png");         // larger than the 320x240 test display
    Project p;

    REQUIRE(p.load(sp.project("{ \"splash\": \"assets/small.png\" }")));
    with_splash(p.info(), [&](lv_obj_t* scr) {
        REQUIRE(lv_obj_get_child_count(scr) == 1);
        lv_obj_t* img = lv_obj_get_child(scr, 0);
        REQUIRE(lv_obj_check_type(img, &lv_image_class));
        CHECK(lv_image_get_scale(img) == LV_SCALE_NONE);
        lv_area_t a;
        lv_obj_get_coords(img, &a);
        CHECK(a.x1 == (320 - 48) / 2);
        CHECK(a.y1 == (240 - 48) / 2);
    });

    REQUIRE(p.load(sp.project("{ \"splash\": \"assets/big.png\" }")));
    with_splash(p.info(), [&](lv_obj_t* scr) {
        lv_obj_t* img = lv_obj_get_child(scr, 0);
        REQUIRE(lv_obj_check_type(img, &lv_image_class));
        CHECK(lv_image_get_scale(img) == LV_SCALE_NONE / 2);   // 640x480 -> 320x240
    });
}
