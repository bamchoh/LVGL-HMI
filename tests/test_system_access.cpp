// The system settings without a button of the project: a finger held in a corner of the screen
// (system_access.hpp). A touch panel the test drives stands in for the finger.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "screen_saver.hpp"
#include "system_access.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "users.hpp"
#include "util.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>

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
    void down(int x, int y) { point = { x, y }; pressed = true; }
    void up() { pressed = false; }
};

// A project in a folder of its own: one screen with a button over its top left corner that
// counts its clicks, and nothing that opens the system settings.
struct Device {
    fs::path dir;
    Runtime rt;
    Project p;
    Finger finger;
    int w = 0, h = 0;

    explicit Device(const std::string& extra = "")
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_system_access_test_" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir / "screens");
        {
            std::ofstream f(dir / "project.json", std::ios::binary);
            f << "{ \"title\": \"T\", \"start_screen\": \"s\", \"tags\": [ { \"name\": \"clicks\", \"type\": \"int\" } ], \"screens\": [\"screens/s.json\"]" << extra << " }";
        }
        {
            std::ofstream f(dir / "screens" / "s.json", std::ios::binary);
            f << R"~({ "name": "s", "style": { "pad": 0 }, "children": [
                { "type": "button", "x": 0, "y": 0, "w": 120, "h": 80, "text": "+1", "on_click": { "action": "inc", "tag": "clicks", "by": 1 } } ] })~";
        }
        REQUIRE(p.load((dir / "project.json").generic_u8string()));
        REQUIRE(p.build_screens() == 1);
        lv_display_t* d = lv_display_get_default();
        w = lv_display_get_horizontal_resolution(d);
        h = lv_display_get_vertical_resolution(d);
        system_access().start();
        rt.advance(100);
    }
    ~Device()
    {
        finger.up();
        rt.advance(100);
        system_access().stop();
        ui().unload_all();
        p.stop_logs();
        p.unload();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string screen() const { return ui().current_screen().value_or(""); }
    int clicks() const { return static_cast<int>(tags().find("clicks")->get_number()); }
    // the note in the corner ("System  2") is the only thing this feature puts on the system layer
    bool hint() const { return lv_obj_get_child_count(lv_layer_sys()) > 0; }
    void hold(int x, int y, uint32_t ms) { finger.down(x, y); rt.advance(ms); }
    void release() { finger.up(); rt.advance(200); }
};

} // namespace

TEST_CASE("system access: the corner held for three seconds opens the system settings of a project that has no button for them")
{
    CHECK(SystemAccess::region(480, 320) == 48);
    CHECK(SystemAccess::region(800, 480) == 60);
    CHECK(SystemAccess::region(1920, 1080) == 135);

    Device d;
    CHECK(system_access().corner() == SystemAccess::Corner::TopLeft);      // the defaults
    CHECK(system_access().hold_ms() == 3000);
    REQUIRE(d.screen() == "s");

    // a tap is a tap: the button under the corner works
    d.hold(5, 5, 150);
    d.release();
    CHECK(d.clicks() == 1);
    CHECK(d.screen() == "s");

    // held, but let go before the time: a note counts down from the first second, nothing opens
    d.hold(5, 5, 800);
    CHECK_FALSE(d.hint());
    d.rt.advance(700);
    CHECK(d.hint());
    d.rt.advance(1200);                                           // 2.7 s
    CHECK(d.screen() == "s");
    d.release();
    CHECK_FALSE(d.hint());
    CHECK(d.screen() == "s");
    CHECK(d.clicks() == 2);                                       // (an ordinary, long, click)

    // held to the end: the menu, and no click for the button under the finger
    d.hold(5, 5, 3200);
    CHECK(d.screen() == SystemSettings::menu_screen());
    CHECK_FALSE(d.hint());
    d.rt.advance(2000);                                           // still held: nothing more happens
    CHECK(d.screen() == SystemSettings::menu_screen());
    d.release();
    CHECK(d.clicks() == 2);
    CHECK(d.screen() == SystemSettings::menu_screen());

    // on the system screens themselves the corner is their own ("Back" is there)
    d.hold(5, 5, 2500);
    CHECK_FALSE(d.hint());
    d.finger.up();
    d.rt.advance(50);
    REQUIRE(ui().show_screen("s", "none"));
    d.rt.advance(200);

    // elsewhere on the screen; in the corner but sliding out; coming in from outside
    d.hold(d.w / 2, d.h / 2, 4000);
    CHECK(d.screen() == "s");
    d.release();
    const int r = SystemAccess::region(d.w, d.h);
    d.hold(r - 1, r - 1, 1500);
    CHECK(d.hint());
    d.hold(r + 5, r - 1, 100);                                    // out of the square
    CHECK_FALSE(d.hint());
    d.hold(5, 5, 4000);                                           // back in: this touch does not count any more
    CHECK(d.screen() == "s");
    d.release();
    d.hold(d.w / 2, d.h / 2, 500);
    d.hold(5, 5, 4000);
    CHECK(d.screen() == "s");
    d.release();
    d.hold(d.w - 3, d.h - 3, 4000);                               // another corner
    CHECK(d.screen() == "s");
    d.release();

    // and once more from the start: it still works
    d.hold(0, 0, 3200);
    CHECK(d.screen() == SystemSettings::menu_screen());
    d.release();
}

TEST_CASE("system access: the corner and the time are the project's; \"none\" turns it off")
{
    {
        Device d(R"(, "system_access": { "hold": "bottom_right", "hold_s": 2 })");
        CHECK(system_access().corner() == SystemAccess::Corner::BottomRight);
        CHECK(system_access().hold_ms() == 2000);
        d.hold(5, 5, 4000);
        CHECK(d.screen() == "s");
        d.release();
        d.hold(d.w - 3, d.h - 3, 1800);
        CHECK(d.screen() == "s");
        CHECK(d.hint());
        d.rt.advance(400);
        CHECK(d.screen() == SystemSettings::menu_screen());
        d.release();
    }
    {
        Device d(R"(, "system_access": { "hold": "none" })");
        CHECK(system_access().corner() == SystemAccess::Corner::None);
        for(int corner = 0; corner < 4; corner++) {
            d.hold(corner & 1 ? d.w - 3 : 3, corner & 2 ? d.h - 3 : 3, 4000);
            CHECK(d.screen() == "s");
            CHECK_FALSE(d.hint());
            d.release();
        }
    }
    {   // what cannot be used falls back to something that works (a typo must not lock the device)
        Device d(R"(, "system_access": { "hold": "middle", "hold_s": 0.1 })");
        CHECK(system_access().corner() == SystemAccess::Corner::TopLeft);
        CHECK(system_access().hold_ms() == 2000);
        d.hold(5, 5, 1500);
        CHECK(d.screen() == "s");                                 // not after a tenth of a second
        d.rt.advance(700);
        CHECK(d.screen() == SystemSettings::menu_screen());
        d.release();
    }
    // without a project nothing is watched
    CHECK(system_access().corner() == SystemAccess::Corner::None);
}

TEST_CASE("system access: with users the login comes first; the touch that wakes the dark screen does not count")
{
    {
        Device d(R"(, "users": { "list": [ { "name": "op", "level": 1, "pin": "1111" }, { "name": "admin", "level": 3, "pin": "9999" } ] })");
        REQUIRE(users().system_level() == 3);
        d.hold(5, 5, 3200);
        CHECK(d.screen() != "s");
        CHECK(d.screen() != SystemSettings::menu_screen());      // not without the level: the login screen
        CHECK(d.screen().rfind("__login", 0) == 0);
        d.release();
    }
    {
        Device d(R"(, "screen_saver": { "after_s": 60 })");
        uint32_t idle = 0;
        screen_saver().set_clock([&] { return idle; });
        idle = 61000;
        screen_saver().poll();
        REQUIRE(screen_saver().dark());
        d.rt.advance(300);                                        // (dark for a while, as it is on a device)
        REQUIRE(screen_saver().dark());
        d.hold(5, 5, 4000);                                       // wakes the screen, and that is all
        CHECK_FALSE(screen_saver().dark());
        CHECK(d.screen() == "s");
        CHECK(d.clicks() == 0);
        d.release();
        idle = 0;
        d.hold(5, 5, 3200);                                       // lit: now it counts
        CHECK(d.screen() == SystemSettings::menu_screen());
        d.release();
        screen_saver().set_clock(nullptr);
    }
}
