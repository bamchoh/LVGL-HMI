// Images: PNG / JPEG / BMP / GIF, "fit", background images, images that follow a tag.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "util.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifndef HML_TEST_SOURCE_DIR
#  error "HML_TEST_SOURCE_DIR must point at the repository root"
#endif

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

std::string images_dir()
{
    const char* env = std::getenv("HML_TEST_SOURCE_DIR");
    return std::string(env && *env ? env : HML_TEST_SOURCE_DIR) + "/tests/assets/images";
}

std::mutex g_mu;
std::vector<std::string> g_log;
void capture(LogLevel, const std::string& m) { std::lock_guard<std::mutex> l(g_mu); g_log.push_back(m); }
struct LogCapture {
    LogCapture() { { std::lock_guard<std::mutex> l(g_mu); g_log.clear(); } set_log_sink(capture); }
    ~LogCapture() { set_log_sink(nullptr); }
    bool has(const std::string& part) const
    {
        std::lock_guard<std::mutex> l(g_mu);
        for(const auto& m : g_log) if(m.find(part) != std::string::npos) return true;
        return false;
    }
};

lv_obj_t* show(Runtime& rt, const std::string& children)
{
    static int n = 0;
    const std::string name = "img" + std::to_string(n++);
    json scr = json::parse("{ \"name\": \"" + name + "\", \"children\": " + children + " }");
    REQUIRE(ui().build_screen(scr, images_dir()));
    REQUIRE(ui().show_screen(name, "none"));
    rt.advance(60);
    hmltest::Lvgl::get().render();
    return lv_screen_active();
}

std::string src_of(lv_obj_t* img)
{
    const void* s = lv_image_get_src(img);
    return s ? static_cast<const char*>(s) : "";
}
bool ends_with(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }

} // namespace

TEST_CASE("images: PNG, JPEG and BMP are shown in their own size; a GIF plays")
{
    Runtime rt;
    lv_obj_t* scr = show(rt, R"~([
        { "type": "image", "src": "pic.png" }, { "type": "image", "x": 50, "src": "pic.jpg" },
        { "type": "image", "x": 100, "src": "pic.bmp" }, { "type": "image", "x": 150, "src": "spin.gif" } ])~");
    for(int i = 0; i < 3; i++) {
        lv_obj_t* img = lv_obj_get_child(scr, i);
        CAPTURE(i);
        CHECK(lv_obj_check_type(img, &lv_image_class));
        CHECK_FALSE(src_of(img).empty());
        CHECK(lv_obj_get_width(img) == 40);
        CHECK(lv_obj_get_height(img) == 30);
    }
    lv_obj_t* gif = lv_obj_get_child(scr, 3);
    CHECK(lv_obj_check_type(gif, &lv_gif_class));
    CHECK(lv_obj_get_width(gif) == 16);
    rt.advance(400);                                            // a few frames
    hmltest::Lvgl::get().render();
}

TEST_CASE("images: fit fills the widget's size; unknown values are named")
{
    LogCapture log;
    Runtime rt;
    lv_obj_t* scr = show(rt, R"~([
        { "type": "image", "w": 200, "h": 100, "src": "pic.png", "fit": "contain" },
        { "type": "image", "w": 200, "h": 100, "src": "pic.png", "fit": "stretch" },
        { "type": "image", "w": 200, "h": 100, "src": "pic.png", "fit": "tile" },
        { "type": "image", "w": 200, "h": 100, "src": "pic.png", "fit": "cover" },
        { "type": "image", "w": 200, "h": 100, "src": "pic.jpg", "fit": "stretch", "zoom": 2 },
        { "type": "image", "src": "pic.png", "zoom": 2, "rotation": 90 } ])~");
    CHECK(lv_image_get_inner_align(lv_obj_get_child(scr, 0)) == LV_IMAGE_ALIGN_CONTAIN);
    CHECK(lv_image_get_inner_align(lv_obj_get_child(scr, 1)) == LV_IMAGE_ALIGN_STRETCH);
    CHECK(lv_image_get_inner_align(lv_obj_get_child(scr, 2)) == LV_IMAGE_ALIGN_TILE);
    CHECK(lv_obj_get_width(lv_obj_get_child(scr, 0)) == 200);
    CHECK(log.has("unknown fit 'cover'"));
    // a JPEG / BMP is drawn piece by piece: its own size only
    CHECK(lv_image_get_inner_align(lv_obj_get_child(scr, 4)) != LV_IMAGE_ALIGN_STRETCH);
    CHECK(lv_image_get_scale(lv_obj_get_child(scr, 4)) == 256);
    CHECK(log.has("image 'pic.jpg': fit, zoom and rotation are for PNG only"));
    CHECK(lv_image_get_scale(lv_obj_get_child(scr, 5)) == 512);
    CHECK(lv_image_get_rotation(lv_obj_get_child(scr, 5)) == 900);
}

// ---- reliability / security

TEST_CASE("images: broken, missing, too large, and files outside the project are not shown")
{
    LogCapture log;
    Runtime rt;
    lv_obj_t* scr = show(rt, R"~([
        { "type": "image", "src": "broken.png" }, { "type": "image", "src": "nothing.png" },
        { "type": "image", "src": "huge.png" }, { "type": "image", "src": "../images/pic.png" },
        { "type": "image", "src": "C:/Windows/win.ini" }, { "type": "image", "src": "/etc/passwd" },
        { "type": "image", "src": "sub/../../images/pic.png" },
        { "type": "label", "text": "still here" } ])~");
    for(int i = 0; i < 7; i++) { CAPTURE(i); CHECK(src_of(lv_obj_get_child(scr, i)).empty()); }
    CHECK(log.has("image 'broken.png': no such file, or not a PNG"));
    CHECK(log.has("image 'nothing.png': no such file"));
    CHECK(log.has("image 'huge.png': 3000x2000 is too large"));
    CHECK(log.has("image '../images/pic.png': only files inside the project folder"));
    CHECK(log.has("image 'C:/Windows/win.ini': only files inside the project folder"));
    CHECK(log.has("image '/etc/passwd': only files inside the project folder"));
    CHECK(lv_obj_get_child_count(scr) == 8);                    // the rest of the screen is built
}

TEST_CASE("images: a background image on a panel and on a pressed button")
{
    Runtime rt;
    lv_obj_t* scr = show(rt, R"~([
        { "type": "panel", "w": 200, "h": 100, "style": { "bg_image": "pic.png", "bg_image_tiled": true, "bg_image_opa": 50 } },
        { "type": "button", "y": 120, "text": "x", "style_pressed": { "bg_image": "pic.jpg" } },
        { "type": "panel", "y": 200, "style": { "bg_image": "nothing.png" } } ])~");
    lv_obj_t* panel = lv_obj_get_child(scr, 0);
    const void* bg = lv_obj_get_style_bg_image_src(panel, LV_PART_MAIN);
    REQUIRE(bg);
    CHECK(ends_with(static_cast<const char*>(bg), "/pic.png"));
    CHECK(lv_obj_get_style_bg_image_tiled(panel, LV_PART_MAIN));
    CHECK(lv_obj_get_style_bg_image_opa(panel, LV_PART_MAIN) < LV_OPA_COVER);
    lv_obj_t* button = lv_obj_get_child(scr, 1);
    CHECK(lv_obj_get_style_bg_image_src(button, LV_PART_MAIN) == nullptr);
    lv_obj_add_state(button, LV_STATE_PRESSED);
    CHECK(lv_obj_get_style_bg_image_src(button, LV_PART_MAIN) != nullptr);
    CHECK(lv_obj_get_style_bg_image_src(lv_obj_get_child(scr, 2), LV_PART_MAIN) == nullptr);   // a missing file: no background
}

TEST_CASE("images: the picture follows a tag (map of values, or the file name in a text tag)")
{
    LogCapture log;
    Runtime rt;
    Tag* state = rt.tag("state", TagType::Int, 0);
    Tag* run = rt.tag("run", TagType::Bool, 0);
    Tag* file = rt.tag("file", "pic.bmp");
    lv_obj_t* scr = show(rt, R"~([
        { "type": "image", "bind": { "src": { "tag": "state", "map": { "0": "pic.png", "1": "pic.jpg", "default": "pic.bmp" } } } },
        { "type": "image", "x": 60, "bind": { "src": { "tag": "run", "map": { "true": "pic.jpg" } } } },
        { "type": "image", "x": 120, "bind": { "src": "file" } } ])~");
    lv_obj_t *a = lv_obj_get_child(scr, 0), *b = lv_obj_get_child(scr, 1), *c = lv_obj_get_child(scr, 2);
    CHECK(ends_with(src_of(a), "/pic.png"));
    CHECK(lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN));               // no picture for false: nothing is shown
    CHECK(ends_with(src_of(c), "/pic.bmp"));

    state->set_number(1, nullptr); run->set_number(1, nullptr); file->set_string("pic.png", nullptr);
    rt.advance(30);
    CHECK(ends_with(src_of(a), "/pic.jpg"));
    CHECK_FALSE(lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN));
    CHECK(ends_with(src_of(b), "/pic.jpg"));
    CHECK(ends_with(src_of(c), "/pic.png"));
    state->set_number(7, nullptr);
    rt.advance(30);
    CHECK(ends_with(src_of(a), "/pic.bmp"));                     // "default"

    // a file name from a tag cannot leave the project folder either
    file->set_string("../../secret.png", nullptr);
    rt.advance(30);
    CHECK(lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN));
    CHECK(log.has("only files inside the project folder"));
    hmltest::Lvgl::get().render();
}
