// Screen saver (screen_saver.hpp). Grouped by ISO/IEC 25010 characteristic.
#include <doctest.h>
#include "alarm.hpp"
#include "lvgl_fixture.hpp"
#include "screen_saver.hpp"
#include "system_settings.hpp"
#include "users.hpp"
#include "util.hpp"

#include <atomic>
#include <filesystem>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

// A touch panel the test drives: LVGL reads it on every timer run (a real press, not an event).
struct Finger {
    lv_indev_t* indev = nullptr;
    lv_point_t  point{ 0, 0 };
    bool        pressed = false;
    static void read(lv_indev_t* i, lv_indev_data_t* d)
    {
        auto* self = static_cast<Finger*>(lv_indev_get_user_data(i));
        d->point = self->point;
        d->state = self->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    Finger()
    {
        indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, &Finger::read);
        lv_indev_set_user_data(indev, this);
    }
    ~Finger() { lv_indev_delete(indev); }
    void tap(lv_obj_t* obj, Runtime& rt)
    {
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        point = { (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2 };
        pressed = true; rt.advance(60, 10);
        pressed = false; rt.advance(60, 10);
    }
};

struct Fixture {
    Runtime rt;
    uint32_t idle = 0;
    Tag* count;
    lv_obj_t* button = nullptr;
    fs::path dir;
    Fixture(const char* cfg = R"~({ "after_s": 60 })~")
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_screen_saver_test_" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir);
        count = rt.tag("count", TagType::Int, 0);
        json c = json::parse(cfg);
        screen_saver().configure(&c, nullptr, (dir / "project.local.json").generic_string());
        screen_saver().set_clock([this] { return idle; });
        json scr = json::parse(R"~({ "name": "main", "children": [
            { "type": "button", "x": 0, "y": 0, "w": "100%", "h": "100%", "text": "+1", "on_click": { "action": "inc", "tag": "count" } } ] })~");
        REQUIRE(ui().build_screen(scr, ""));
        REQUIRE(ui().show_screen("main", "none"));
        button = lv_obj_get_child(lv_screen_active(), 0);
        rt.advance(50);
        lv_obj_update_layout(lv_screen_active());
    }
    ~Fixture()
    {
        screen_saver().set_clock(nullptr);
        screen_saver().clear();
        alarms().clear();
        users().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    double tag(const char* n) { return tags().find(n)->get_number(); }
};

} // namespace

// ================================================================ functional suitability

TEST_CASE("screen saver: dark after the time without input; the waking touch presses nothing")
{
    Fixture f;
    Finger finger;
    finger.tap(f.button, f.rt);
    CHECK(f.count->get_number() == 1);                          // the button works

    f.idle = 59000; screen_saver().poll();
    CHECK_FALSE(screen_saver().dark());
    f.idle = 60000; screen_saver().poll();
    CHECK(screen_saver().dark());
    CHECK(f.tag("sys.screen.off") == 1);

    // the touch that wakes the screen is not a click on the button under it
    f.idle = 0;
    finger.tap(f.button, f.rt);
    CHECK_FALSE(screen_saver().dark());
    CHECK(f.count->get_number() == 1);
    CHECK(f.tag("sys.screen.off") == 0);
    // the next touch is an ordinary one again
    finger.tap(f.button, f.rt);
    CHECK(f.count->get_number() == 2);
}

TEST_CASE("screen saver: the tag switches it; after_s 0 never; logout when it goes dark")
{
    Fixture f(R"~({ "after_s": 0, "logout": true })~");
    f.idle = 10u * 3600 * 1000; screen_saver().poll();
    CHECK_FALSE(screen_saver().dark());                         // 0 = never
    tags().find("sys.screen.off")->set_number(1, nullptr); f.rt.pump();
    CHECK(screen_saver().dark());                               // a "screen off" button
    tags().find("sys.screen.off")->set_number(0, nullptr); f.rt.pump();
    CHECK_FALSE(screen_saver().dark());

    json u = json::parse(R"~({ "list": [ { "name": "op", "level": 1, "pin": "1111" } ] })~");
    users().load(&u, nullptr, "");
    REQUIRE(users().login("op", "1111") == Users::LoginResult::Ok);
    screen_saver().blank();
    CHECK(users().level() == 0);                                // logged out with the screen
    screen_saver().wake();
}

// ================================================================ safety

TEST_CASE("screen saver: an alarm wakes the screen")
{
    Fixture f;
    Tag* hot = f.rt.tag("hot", TagType::Bool, 0);
    json a = json::parse(R"~({ "items": [ { "name": "h", "tag": "hot" } ] })~");
    REQUIRE(alarms().load(&a, f.dir.generic_string()) == 1);
    f.idle = 60000; screen_saver().poll();
    REQUIRE(screen_saver().dark());
    screen_saver().poll();
    CHECK(screen_saver().dark());                                // nothing new: stays dark
    hot->set_number(1, nullptr); f.rt.pump();
    f.idle = 0;
    screen_saver().poll();
    CHECK_FALSE(screen_saver().dark());

    // wake_on_alarm false: it stays dark
    json c = json::parse(R"~({ "after_s": 60, "wake_on_alarm": false })~");
    screen_saver().configure(&c, nullptr, "");
    screen_saver().blank();
    hot->set_number(0, nullptr); f.rt.pump();
    hot->set_number(1, nullptr); f.rt.pump();
    screen_saver().poll();
    CHECK(screen_saver().dark());
}

TEST_CASE("screen saver: it covers popups and dialogs too")
{
    Fixture f;
    screen_saver().blank();
    lv_obj_t* sys_layer = lv_layer_sys();
    REQUIRE(lv_obj_get_child_count(sys_layer) >= 1);
    lv_obj_t* black = lv_obj_get_child(sys_layer, -1);
    lv_obj_update_layout(black);
    CHECK(lv_obj_get_width(black) == lv_display_get_horizontal_resolution(lv_display_get_default()));
    CHECK(lv_obj_has_flag(black, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_obj_get_style_bg_opa(black, LV_PART_MAIN) == LV_OPA_COVER);
    screen_saver().wake();
    CHECK(lv_obj_has_flag(black, LV_OBJ_FLAG_HIDDEN));
}

// ================================================================ the device setting

TEST_CASE("screen saver: the time set on the device is kept in project.local.json, only if it differs")
{
    Fixture f;
    const fs::path local = f.dir / "project.local.json";
    auto page = make_display_page();
    auto fields = page->fields();
    REQUIRE(fields.size() == 2);
    CHECK(fields[0].value == "60");
    CHECK(page->apply({ { "after_s", "120" } }) == "");
    CHECK(screen_saver().after_s() == 120);
    REQUIRE(fs::exists(local));
    json saved = json::parse(*read_file(local.generic_string(), 1 << 16));
    CHECK(saved["screen_saver"]["after_s"] == 120);
    CHECK(page->apply({ { "after_s", "-5" } }) != "");          // refused
    CHECK(page->apply({ { "after_s", "999999" } }) != "");
    // back to the project's value: nothing is kept
    CHECK(page->apply({ { "after_s", "60" } }) == "");
    CHECK_FALSE(fs::exists(local));
    // the device's value comes back after a restart
    page->apply({ { "after_s", "30" } });
    json c = json::parse(R"~({ "after_s": 60 })~");
    json l = json::parse(*read_file(local.generic_string(), 1 << 16))["screen_saver"];
    screen_saver().configure(&c, &l, local.generic_string());
    CHECK(screen_saver().after_s() == 30);
    CHECK(screen_saver().project_after_s() == 60);
}
