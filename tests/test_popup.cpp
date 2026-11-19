// Popups: screens with "popup": true, shown over the current screen (ui.hpp). Grouped by ISO/IEC
// 25010 characteristic.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "users.hpp"

#include <functional>

using namespace hml;
using hmltest::Runtime;

namespace {

json J(const char* t) { return json::parse(t); }

void add(const char* text) { REQUIRE(ui().add_screen(J(text), "/base")); }

void click(lv_obj_t* o) { lv_obj_send_event(o, LV_EVENT_CLICKED, nullptr); }

// the n-th widget of a screen / popup box
lv_obj_t* box_of(const char* name)
{
    for(uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); i++) {
        lv_obj_t* back = lv_obj_get_child(lv_layer_top(), static_cast<int32_t>(i));
        lv_obj_t* box = lv_obj_get_child(back, 0);
        if(box && lv_obj_get_child_count(box) > 0) {
            lv_obj_t* first = lv_obj_get_child(box, 0);
            if(lv_obj_check_type(first, &lv_label_class) && std::string(lv_label_get_text(first)) == name) return box;
        }
    }
    return nullptr;
}
bool visible(lv_obj_t* box) { return box && !lv_obj_has_flag(lv_obj_get_parent(box), LV_OBJ_FLAG_HIDDEN); }

struct Fixture {
    Runtime rt;
    Tag* unit;
    Tag* speed;
    Fixture()
    {
        unit = rt.tag("unit", TagType::Int, 0);
        speed = rt.tag("speed", TagType::Int, 10);
        add(R"~({ "name": "main", "children": [
            { "type": "button", "text": "open", "on_click": { "action": "popup", "screen": "detail", "set": { "unit": 2, "nope": 1 } } },
            { "type": "button", "text": "goto", "on_click": { "action": "goto", "screen": "detail" } },
            { "type": "label", "bind": "speed" } ] })~");
        add(R"~({ "name": "other", "children": [ { "type": "label", "text": "other" } ] })~");
        add(R"~({ "name": "detail", "popup": true, "w": 300, "h": 200, "children": [
            { "type": "label", "text": "detail" },
            { "type": "label", "bind": "speed" },
            { "type": "button", "text": "more", "on_click": { "action": "popup", "screen": "more" } },
            { "type": "button", "text": "x", "on_click": { "action": "close" } },
            { "type": "button", "text": "go", "on_click": { "action": "goto", "screen": "other" } } ] })~");
        add(R"~({ "name": "more", "popup": true, "dim": false, "level": 0, "children": [
            { "type": "label", "text": "more" },
            { "type": "button", "text": "back", "on_click": { "action": "back" } },
            { "type": "button", "text": "close detail", "on_click": { "action": "close", "screen": "detail" } } ] })~");
        add(R"~({ "name": "secret", "popup": true, "level": 2, "children": [ { "type": "label", "text": "secret" } ] })~");
        REQUIRE(ui().show_screen("main", "none"));
    }
    lv_obj_t* main_obj() { return lv_screen_active(); }
};

} // namespace

// ================================================================ functional suitability

TEST_CASE("popup: opens over the screen, is modal, nests, closes")
{
    Fixture f;
    click(lv_obj_get_child(f.main_obj(), 0));                  // "open"
    CHECK(ui().open_popups() == std::vector<std::string>{ "detail" });
    CHECK(f.unit->get_number() == 2);                          // "set" came first
    CHECK(ui().current_screen() == "main");                    // the screen stays
    lv_obj_t* box = box_of("detail");
    REQUIRE(box);
    CHECK(visible(box));
    lv_obj_t* back = lv_obj_get_parent(box);
    CHECK(lv_obj_has_flag(back, LV_OBJ_FLAG_CLICKABLE));       // takes every touch: the screen below cannot be used
    CHECK(lv_obj_get_style_bg_opa(back, LV_PART_MAIN) == LV_OPA_50);
    lv_obj_update_layout(back);
    CHECK(lv_obj_get_width(box) == 300);
    CHECK(lv_obj_get_height(box) == 200);

    click(lv_obj_get_child(box, 2));                           // "more": a popup on the popup
    CHECK(ui().open_popups() == std::vector<std::string>{ "detail", "more" });
    lv_obj_t* more = box_of("more");
    REQUIRE(more);
    CHECK(lv_obj_get_index(lv_obj_get_parent(more)) > lv_obj_get_index(back));   // on top
    CHECK(lv_obj_get_style_bg_opa(lv_obj_get_parent(more), LV_PART_MAIN) == LV_OPA_TRANSP);   // "dim": false
    click(lv_obj_get_child(more, 1));                          // "back" in a popup closes that popup
    CHECK(ui().open_popups() == std::vector<std::string>{ "detail" });
    CHECK(ui().current_screen() == "main");
    CHECK_FALSE(visible(more));

    click(lv_obj_get_child(box, 2));
    click(lv_obj_get_child(more, 2));                          // close one by name, not the top one
    CHECK(ui().open_popups() == std::vector<std::string>{ "more" });
    CHECK(ui().close_popup());
    CHECK(ui().open_popups().empty());
    CHECK_FALSE(ui().close_popup());

    click(lv_obj_get_child(f.main_obj(), 0));
    click(lv_obj_get_child(box, 3));                           // "close"
    CHECK(ui().open_popups().empty());
    CHECK_FALSE(visible(box));
}

TEST_CASE("popup: a screen change closes it; a goto to a popup opens it; not a popup cannot be")
{
    Fixture f;
    click(lv_obj_get_child(f.main_obj(), 1));                  // "goto" a popup screen: opens it
    CHECK(ui().open_popups() == std::vector<std::string>{ "detail" });
    CHECK(ui().current_screen() == "main");
    lv_obj_t* box = box_of("detail");
    click(lv_obj_get_child(box, 4));                           // "go" to another screen from the popup
    CHECK(ui().current_screen() == "other");
    CHECK(ui().open_popups().empty());
    CHECK_FALSE(visible(box));
    CHECK_FALSE(ui().open_popup("other"));                     // not a popup
    CHECK_FALSE(ui().open_popup("nothing"));
    CHECK(ui().back());
    CHECK(ui().current_screen() == "main");
}

TEST_CASE("popup: its widgets follow the tags only while it is open, and are up to date when it opens")
{
    Fixture f;
    CHECK(ui().open_popup("detail"));
    lv_obj_t* lbl = lv_obj_get_child(box_of("detail"), 1);
    f.speed->set_number(20, nullptr); f.rt.pump();
    CHECK(std::string(lv_label_get_text(lbl)) == "20");
    CHECK(ui().close_popup());
    f.speed->set_number(30, nullptr); f.rt.pump();
    CHECK(std::string(lv_label_get_text(lbl)) == "20");        // closed: not touched
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(f.main_obj(), 2))) == "30");   // the screen below is
    CHECK(ui().open_popup("detail"));
    CHECK(std::string(lv_label_get_text(lbl)) == "30");        // brought up to date when it opens
}

// ================================================================ security

TEST_CASE("popup: its level is needed, as for a screen")
{
    Fixture f;
    json cfg = J(R"~({ "levels": ["Op", "Boss"], "list": [ { "name": "boss", "level": 2, "pin": "2222" } ] })~");
    users().load(&cfg, nullptr, "");
    CHECK_FALSE(ui().open_popup("secret"));
    CHECK(ui().open_popups().empty());
    CHECK(users().required() == 2);
    users().finish_login(false);
    REQUIRE(users().login("boss", "2222") == Users::LoginResult::Ok);
    CHECK(ui().open_popup("secret"));
    users().clear();
}

// ================================================================ reliability

TEST_CASE("popup: opening and closing many times does not make anything new")
{
    Fixture f;
    CHECK(ui().open_popup("detail"));
    CHECK(ui().open_popup("more"));
    CHECK(ui().close_popup());
    CHECK(ui().close_popup());
    auto count = [] {
        uint32_t n = 0;
        std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* o) { n++; for(uint32_t i = 0; i < lv_obj_get_child_count(o); i++) walk(lv_obj_get_child(o, static_cast<int32_t>(i))); };
        walk(lv_layer_top());
        walk(lv_screen_active());
        return n;
    };
    const uint32_t before = count();
    for(int i = 0; i < 1000; i++) {
        CHECK(ui().open_popup("detail"));
        CHECK(ui().open_popup("more"));
        CHECK(ui().open_popup("detail"));                      // again: to the top, not twice
        CHECK(ui().open_popups().size() == 2);
        ui().close_popup();
        ui().close_popup();
    }
    CHECK(count() == before);
    ui().unload_all();                                         // the popups go with the screens
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
