// on_press / on_release for commands held only while pressed: the release must come also when the
// finger slides off the button first (else the command would stay ON in the PLC).
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "ui.hpp"

using namespace hml;
using hmltest::Runtime;

namespace {
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
};
} // namespace

TEST_SUITE("reliability") {
TEST_CASE("on_release: a command held while pressed goes OFF also when the finger slides off the button")
{
    Runtime rt;
    Tag* cmd = rt.tag("cmd", TagType::Bool, 0);
    json scr = json::parse(R"json({ "name": "s", "children": [
        { "type": "button", "x": 20, "y": 20, "w": 120, "h": 60, "text": "RUN",
          "on_press": { "action": "set", "tag": "cmd", "value": true }, "on_release": { "action": "set", "tag": "cmd", "value": false } } ] })json");
    REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen("s", "none"));
    Finger f;
    rt.advance(50);

    // an ordinary press and release
    f.point = { 80, 50 }; f.pressed = true; rt.advance(100);
    CHECK(cmd->get_number() == 1);
    f.pressed = false; rt.advance(100);
    CHECK(cmd->get_number() == 0);

    // pressed, slides far off the button, released there
    f.point = { 80, 50 }; f.pressed = true; rt.advance(100);
    CHECK(cmd->get_number() == 1);
    for(int x = 80; x <= 400; x += 40) { f.point = { x, 300 }; rt.advance(30); }
    f.pressed = false; rt.advance(100);
    CHECK(cmd->get_number() == 0);
}
}
