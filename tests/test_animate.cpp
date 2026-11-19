// "animate": widgets that blink, move, turn, change colour or play frames, while a tag says so.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "util.hpp"

#include <mutex>
#include <set>
#include <string>
#include <vector>

#ifndef HML_TEST_SOURCE_DIR
#  error "HML_TEST_SOURCE_DIR must point at the repository root"
#endif

using namespace hml;
using hmltest::Runtime;

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

int s_n = 0;
std::string build(const std::string& children, const char* extra = "")
{
    const std::string name = "anim" + std::to_string(s_n++);
    json scr = json::parse("{ \"name\": \"" + name + "\"" + extra + ", \"children\": " + children + " }");
    REQUIRE(ui().build_screen(scr, images_dir()));
    return name;
}
lv_obj_t* show(Runtime& rt, const std::string& children)
{
    REQUIRE(ui().show_screen(build(children), "none"));
    rt.advance(20);
    return lv_screen_active();
}
int opa(lv_obj_t* o) { return lv_obj_get_style_opa(o, LV_PART_MAIN); }

} // namespace

// ================================================================ functional suitability

TEST_CASE("animate: blink runs while its tag says so, and the widget is as before when it stops")
{
    Runtime rt;
    Tag* alarm = rt.tag("alarm", TagType::Bool, 0);
    lv_obj_t* scr = show(rt, R"~([ { "type": "label", "text": "ALARM", "animate": { "preset": "blink", "when": "alarm" } } ])~");
    lv_obj_t* l = lv_obj_get_child(scr, 0);
    rt.advance(1200);
    CHECK(opa(l) == LV_OPA_COVER);                              // not running

    alarm->set_number(1, nullptr);
    std::set<int> seen;
    for(int i = 0; i < 40; i++) { rt.advance(50); seen.insert(opa(l)); }
    CHECK(seen.count(LV_OPA_COVER) == 1);                       // on ...
    CHECK(seen.count(LV_OPA_TRANSP) == 1);                      // ... and off
    CHECK(seen.size() == 2);                                    // blink is a step, nothing in between

    alarm->set_number(0, nullptr);
    rt.advance(50);
    CHECK(opa(l) == LV_OPA_COVER);
    rt.advance(1500);
    CHECK(opa(l) == LV_OPA_COVER);                              // and it stays
}

TEST_CASE("animate: move there and back, turn, zoom, colour; a condition with a value")
{
    Runtime rt;
    Tag* state = rt.tag("state", TagType::Int, 0);
    lv_obj_t* scr = show(rt, R"~([
        { "type": "panel", "x": 10, "y": 10, "w": 40, "h": 40,
          "animate": { "prop": "x", "from": 10, "to": 110, "time": 400, "playback": true, "when": { "tag": "state", "eq": 2 } } },
        { "type": "image", "x": 10, "y": 80, "src": "pic.png", "animate": [ { "preset": "spin", "time": 800 }, { "prop": "zoom", "from": 1, "to": 2, "time": 400, "playback": true } ] },
        { "type": "panel", "x": 10, "y": 140, "w": 40, "h": 40, "animate": { "prop": "rotation", "to": 90, "time": 400 } },
        { "type": "panel", "x": 10, "y": 200, "w": 40, "h": 40, "style": { "bg_color": "#000000" },
          "animate": { "prop": "bg_color", "to": "#ff0000", "time": 400, "playback": true } } ])~");
    lv_obj_t *mover = lv_obj_get_child(scr, 0), *img = lv_obj_get_child(scr, 1), *turner = lv_obj_get_child(scr, 2), *col = lv_obj_get_child(scr, 3);

    // without "when": running as soon as the screen is shown
    int rot_max = 0, zoom_max = 0, turn_max = 0, red_max = 0;
    for(int i = 0; i < 40; i++) {
        rt.advance(25);
        rot_max = std::max<int>(rot_max, lv_image_get_rotation(img));
        zoom_max = std::max<int>(zoom_max, static_cast<int>(lv_image_get_scale(img)));
        turn_max = std::max<int>(turn_max, lv_obj_get_style_transform_rotation(turner, LV_PART_MAIN));
        red_max = std::max<int>(red_max, lv_obj_get_style_bg_color(col, LV_PART_MAIN).red);
        CHECK(lv_obj_get_style_x(mover, LV_PART_MAIN) == 10);   // state is not 2: it does not move
    }
    CHECK(rot_max > 2700);                                      // degrees x 10, up to 3600
    CHECK(zoom_max > 450);                                      // 256 = as it is, 512 = twice
    CHECK(turn_max > 700);
    CHECK(red_max > 200);

    state->set_number(2, nullptr);
    int x_min = 1000, x_max = 0;
    for(int i = 0; i < 40; i++) { rt.advance(25); const int x = lv_obj_get_style_x(mover, LV_PART_MAIN); x_min = std::min(x_min, x); x_max = std::max(x_max, x); }
    CHECK(x_min >= 10);
    CHECK(x_max <= 110);
    CHECK(x_max > 90);                                          // there ...
    CHECK(x_min < 30);                                          // ... and back
    state->set_number(3, nullptr);
    rt.advance(30);
    CHECK(lv_obj_get_style_x(mover, LV_PART_MAIN) == 10);       // where it was
}

TEST_CASE("animate: frames show the pictures one after the other")
{
    Runtime rt;
    Tag* run = rt.tag("run", TagType::Bool, 1);
    lv_obj_t* scr = show(rt, R"~([ { "type": "image", "src": "pic.png",
        "animate": { "prop": "frames", "files": ["pic.png", "pic.jpg", "pic.bmp"], "time": 300, "when": "run" } } ])~");
    lv_obj_t* img = lv_obj_get_child(scr, 0);
    std::set<std::string> seen;
    for(int i = 0; i < 40; i++) { rt.advance(20); seen.insert(static_cast<const char*>(lv_image_get_src(img))); }
    CHECK(seen.size() == 3);
    run->set_number(0, nullptr);
    rt.advance(30);
    const std::string rest = static_cast<const char*>(lv_image_get_src(img));
    CHECK(rest.size() > 7);
    CHECK(rest.compare(rest.size() - 7, 7, "pic.png") == 0);    // the widget's own picture again
}

TEST_CASE("animate: a number of times, then it stays at the end")
{
    Runtime rt;
    lv_obj_t* scr = show(rt, R"~([ { "type": "panel", "w": 40, "h": 40, "animate": { "prop": "y", "from": 0, "to": 50, "time": 100, "repeat": 2 } } ])~");
    rt.advance(1000);
    CHECK(lv_obj_get_style_y(lv_obj_get_child(scr, 0), LV_PART_MAIN) == 50);
}

// ================================================================ performance efficiency

TEST_CASE("animate: nothing moves on a screen that is not shown, or in a closed popup")
{
    Runtime rt;
    const std::string a = build(R"~([ { "type": "label", "text": "x", "animate": "pulse" } ])~");
    const std::string b = build(R"~([ { "type": "label", "text": "y" } ])~");
    const std::string p = build(R"~([ { "type": "label", "text": "z", "animate": "blink" } ])~", ", \"popup\": true");
    REQUIRE(ui().show_screen(a, "none"));
    lv_obj_t* la = lv_obj_get_child(lv_screen_active(), 0);
    bool moved = false;
    for(int i = 0; i < 30; i++) { rt.advance(50); if(opa(la) != LV_OPA_COVER) moved = true; }
    CHECK(moved);
    CHECK(lv_anim_count_running() == 1);                        // (the popup's is not running)

    REQUIRE(ui().show_screen(b, "none"));
    rt.advance(500);                                            // (the screen change)
    CHECK(lv_anim_count_running() == 0);
    CHECK(opa(la) == LV_OPA_COVER);                             // left as it was

    REQUIRE(ui().open_popup(p));
    rt.advance(100);
    CHECK(lv_anim_count_running() == 1);
    ui().close_popup();
    rt.advance(100);
    CHECK(lv_anim_count_running() == 0);

    REQUIRE(ui().show_screen(a, "none"));
    rt.advance(500);
    CHECK(lv_anim_count_running() == 1);                        // back: it runs again
}

// ================================================================ usability (safety of flashing), reliability

TEST_CASE("animate: nothing flashes faster than three times a second")
{
    LogCapture log;
    Runtime rt;
    lv_obj_t* scr = show(rt, R"~([ { "type": "label", "text": "x", "animate": { "preset": "blink", "time": 40 } } ])~");
    lv_obj_t* l = lv_obj_get_child(scr, 0);
    CHECK(log.has("flashes faster than 3 times a second"));
    int changes = 0, last = opa(l);
    for(int i = 0; i < 200; i++) { rt.advance(10); if(opa(l) != last) { changes++; last = opa(l); } }   // 2 seconds
    CHECK(changes >= 8);
    CHECK(changes <= 12);                                       // 3 flashes a second = 6 changes
}

TEST_CASE("animate: what cannot be an animation is named; the screen is built and can be thrown away while it runs")
{
    LogCapture log;
    Runtime rt;
    lv_obj_t* scr = show(rt, R"~([
        { "type": "label", "text": "a", "animate": { "prop": "colour", "to": 1 } },
        { "type": "label", "text": "b", "animate": { "preset": "wobble" } },
        { "type": "label", "text": "c", "animate": { "prop": "x" } },
        { "type": "label", "text": "d", "animate": { "prop": "frames", "files": ["pic.png", "pic.jpg"] } },
        { "type": "label", "text": "e", "animate": { "preset": "pulse", "when": "no_such_tag" } },
        { "type": "label", "text": "f", "animate": { "prop": "opa", "to": 50, "path": "bouncy" } },
        { "type": "label", "text": "g", "animate": 5 },
        { "type": "label", "text": "h", "animate": "spin" } ])~");
    CHECK(lv_obj_get_child_count(scr) == 8);
    CHECK(log.has("animate: unknown prop 'colour'"));
    CHECK(log.has("animate: unknown preset 'wobble'"));
    CHECK(log.has("animate: 'x' needs \"to\""));
    CHECK(log.has("animate: \"frames\" is for image widgets"));
    CHECK(log.has("unknown tag 'no_such_tag'"));
    CHECK(log.has("animate: unknown path 'bouncy'"));
    CHECK(log.has("animate: an animation is an object"));
    rt.advance(300);
    ui().unload_all();                                          // running animations go with their widgets
    rt.advance(300);
    CHECK(lv_anim_count_running() == 0);
}
