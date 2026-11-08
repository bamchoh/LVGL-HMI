// numeric_display / text_display / password_display: purpose-built data displays on top of
// lv_textarea with confirm-on-ready input.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "ui.hpp"

#include <string>

using namespace hml;
using hmltest::Runtime;

namespace {

json J(const char* text) { return json::parse(text); }

bool is_widget(lv_obj_t* o) { return !lv_obj_check_type(o, &lv_dropdownlist_class); }
lv_obj_t* nth(lv_obj_t* parent, int32_t idx)
{
    int32_t seen = 0;
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t* c = lv_obj_get_child(parent, static_cast<int32_t>(i));
        if(is_widget(c) && seen++ == idx) return c;
    }
    return nullptr;
}

lv_obj_t* build_widgets(const json& widgets)
{
    json scr = { { "name", "s" }, { "children", widgets } };
    lv_obj_t* s = ui().build_screen(scr, "");
    REQUIRE(s != nullptr);
    REQUIRE(ui().show_screen("s", "none"));
    return s;
}

std::string ta_text(lv_obj_t* ta) { return lv_textarea_get_text(ta); }
void send(lv_obj_t* o, lv_event_code_t c) { lv_obj_send_event(o, c, nullptr); }

// Type into an input field the way the keyboard does, then confirm.
void type_and_confirm(lv_obj_t* ta, const char* text)
{
    lv_textarea_set_text(ta, "");
    lv_textarea_add_text(ta, text);
    send(ta, LV_EVENT_READY);
}

} // namespace

TEST_CASE("numeric_display: display only by default")
{
    Runtime rt;
    Tag* sp = rt.tag("sp", TagType::Float, 42.26);
    rt.tag("i", TagType::Int, 7);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "numeric_display", "bind": "sp" },
        { "type": "numeric_display", "bind": "sp", "decimals": 1, "unit": " rpm" },
        { "type": "numeric_display", "bind": "sp", "format": "%05.1f" },
        { "type": "numeric_display", "bind": "sp", "decimals": 0, "unit": " %" },
        { "type": "numeric_display", "bind": "sp", "format": "%d", "decimals": 3 },
        { "type": "numeric_display", "bind": "i" },
        { "type": "numeric_display", "bind": { "value": { "tag": "sp", "scale": 10, "format": "%.0f" } }, "decimals": 2 },
        { "type": "numeric_display", "bind": { "value": { "tag": "sp", "scale": 10 } }, "decimals": 2 },
        { "type": "numeric_display", "text": "static" }
    ])json"));

    CHECK(lv_obj_check_type(nth(s, 0), &lv_textarea_class));
    CHECK(ta_text(nth(s, 0)) == "42.26");                   // default rendering of the tag type
    CHECK(ta_text(nth(s, 1)) == "42.3 rpm");
    CHECK(ta_text(nth(s, 2)) == "042.3");
    CHECK(ta_text(nth(s, 3)) == "42 %");                    // a percent sign in the unit is literal
    CHECK(ta_text(nth(s, 4)) == "42");                      // format wins over decimals
    CHECK(ta_text(nth(s, 5)) == "7");
    CHECK(ta_text(nth(s, 6)) == "423");                     // bind format wins over the widget's decimals (422.6)
    CHECK(ta_text(nth(s, 7)) == "422.60");
    CHECK(ta_text(nth(s, 8)) == "static");

    SUBCASE("not interactive") {
        lv_obj_t* d = nth(s, 1);
        CHECK_FALSE(lv_obj_has_flag(d, LV_OBJ_FLAG_CLICKABLE));
        CHECK_FALSE(lv_obj_has_flag(d, LV_OBJ_FLAG_CLICK_FOCUSABLE));
        CHECK(lv_obj_get_style_opa(d, LV_PART_CURSOR) == LV_OPA_TRANSP);
        CHECK(lv_obj_get_style_text_align(d, LV_PART_MAIN) == LV_TEXT_ALIGN_RIGHT);
        CHECK(lv_textarea_get_one_line(d));
        CHECK(std::string(lv_textarea_get_accepted_chars(d)) == "0123456789.-+");
        // edits never reach the tag
        type_and_confirm(d, "99");
        send(d, LV_EVENT_VALUE_CHANGED);
        send(d, LV_EVENT_DEFOCUSED);
        CHECK(sp->get_number() == doctest::Approx(42.26));
    }
    SUBCASE("follows the tag") {
        sp->set_number(-3.14159, nullptr);
        rt.pump();
        CHECK(ta_text(nth(s, 0)) == "-3.14159");
        CHECK(ta_text(nth(s, 1)) == "-3.1 rpm");
        CHECK(ta_text(nth(s, 7)) == "-31.42");
    }
}

TEST_CASE("numeric_display with input: writes on confirmation, clamps, reverts on cancel")
{
    Runtime rt;
    Tag* sp = rt.tag("sp", TagType::Float, 10);
    Tag* lim = rt.tag("lim", TagType::Int, 5, 0, 50);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "numeric_display", "bind": "sp", "input": true, "decimals": 1, "unit": " rpm", "min": 0, "max": 100 },
        { "type": "numeric_display", "bind": { "value": { "tag": "sp", "scale": 10, "offset": 5 } }, "input": true, "decimals": 0 },
        { "type": "numeric_display", "bind": "lim", "input": true },
        { "type": "numeric_display", "bind": { "value": { "tag": "sp", "two_way": false } }, "input": true }
    ])json"));
    lv_obj_t* in = nth(s, 0);
    CHECK(lv_obj_has_flag(in, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_obj_has_flag(in, LV_OBJ_FLAG_USER_1));
    CHECK(ta_text(in) == "10.0 rpm");

    SUBCASE("typing alone does not write, READY does, and the text is normalised") {
        lv_textarea_set_text(in, "");
        lv_textarea_add_text(in, "42.26");
        send(in, LV_EVENT_VALUE_CHANGED);
        CHECK(sp->get_number() == doctest::Approx(10));
        send(in, LV_EVENT_READY);
        CHECK(sp->get_number() == doctest::Approx(42.26));
        CHECK(ta_text(in) == "42.3 rpm");
        rt.pump();
        CHECK(ta_text(nth(s, 1)) == "428");                  // 42.26 * 10 + 5 = 427.6
    }
    SUBCASE("losing focus confirms too") {
        type_and_confirm(in, "7");
        lv_textarea_set_text(in, "8");
        send(in, LV_EVENT_DEFOCUSED);
        CHECK(sp->get_number() == doctest::Approx(8));
    }
    SUBCASE("cancel restores the tag value") {
        lv_textarea_set_text(in, "999");
        send(in, LV_EVENT_CANCEL);
        CHECK(sp->get_number() == doctest::Approx(10));
        CHECK(ta_text(in) == "10.0 rpm");
    }
    SUBCASE("min / max clamp what was typed and show the stored value") {
        type_and_confirm(in, "150");
        CHECK(sp->get_number() == doctest::Approx(100));
        CHECK(ta_text(in) == "100.0 rpm");
        type_and_confirm(in, "-5");
        CHECK(sp->get_number() == doctest::Approx(0));
        CHECK(ta_text(in) == "0.0 rpm");
    }
    SUBCASE("focusing clears the field; confirming without typing keeps the value") {
        send(in, LV_EVENT_FOCUSED);
        CHECK(ta_text(in) == "");
        send(in, LV_EVENT_READY);
        CHECK(sp->get_number() == doctest::Approx(10));
        CHECK(ta_text(in) == "10.0 rpm");
        send(in, LV_EVENT_FOCUSED);
        send(in, LV_EVENT_DEFOCUSED);
        CHECK(sp->get_number() == doctest::Approx(10));
        CHECK(ta_text(in) == "10.0 rpm");
        send(in, LV_EVENT_FOCUSED);
        lv_textarea_add_text(in, "3");
        send(in, LV_EVENT_READY);
        CHECK(sp->get_number() == doctest::Approx(3));
    }
    SUBCASE("accepted characters filter the keyboard input") {
        lv_textarea_set_text(in, "");
        lv_textarea_add_text(in, "1a2b.5");
        CHECK(ta_text(in) == "12.5");
    }
    SUBCASE("scale / offset are inverted on write") {
        lv_obj_t* scaled = nth(s, 1);
        type_and_confirm(scaled, "105");                     // (105 - 5) / 10
        CHECK(sp->get_number() == doctest::Approx(10));
        type_and_confirm(scaled, "255");
        CHECK(sp->get_number() == doctest::Approx(25));
        rt.pump();
        CHECK(ta_text(in) == "25.0 rpm");
    }
    SUBCASE("the tag's own limits and int rounding still apply") {
        lv_obj_t* li = nth(s, 2);
        type_and_confirm(li, "12.7");
        CHECK(lim->get_number() == doctest::Approx(13));
        CHECK(ta_text(li) == "13");
        type_and_confirm(li, "80");
        CHECK(lim->get_number() == doctest::Approx(50));
        CHECK(ta_text(li) == "50");
    }
    SUBCASE("an explicit two_way:false in the bind keeps it read-only even with input") {
        type_and_confirm(nth(s, 3), "77");
        CHECK(sp->get_number() == doctest::Approx(10));
    }
}

TEST_CASE("text_display and password_display")
{
    Runtime rt;
    Tag* op = rt.tag("op", std::string("Alice"));
    Tag* pin = rt.tag("pin", std::string(""));
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "text_display", "bind": "op" },
        { "type": "text_display", "bind": "op", "input": true, "max_length": 6, "placeholder": "name" },
        { "type": "password_display", "bind": "pin", "max_length": 4, "accepted_chars": "0123456789" },
        { "type": "password_display", "bind": "pin", "input": false },
        { "type": "text_display", "bind": { "text": { "tag": "op", "format": "Op: %s" } } }
    ])json"));
    lv_obj_t* ro = nth(s, 0);
    lv_obj_t* in = nth(s, 1);
    lv_obj_t* pw = nth(s, 2);

    CHECK(ta_text(ro) == "Alice");
    CHECK_FALSE(lv_obj_has_flag(ro, LV_OBJ_FLAG_CLICKABLE));
    CHECK_FALSE(lv_textarea_get_password_mode(ro));
    CHECK(ta_text(nth(s, 4)) == "Op: Alice");

    CHECK(lv_obj_has_flag(in, LV_OBJ_FLAG_CLICKABLE));
    CHECK_FALSE(lv_obj_has_flag(in, LV_OBJ_FLAG_USER_1));  // text keyboard
    CHECK(std::string(lv_textarea_get_placeholder_text(in)) == "name");
    CHECK(lv_textarea_get_max_length(in) == 6);

    CHECK(lv_textarea_get_password_mode(pw));
    CHECK(lv_obj_has_flag(pw, LV_OBJ_FLAG_CLICKABLE));      // password fields are inputs by default
    CHECK(lv_textarea_get_max_length(pw) == 4);
    CHECK_FALSE(lv_obj_has_flag(nth(s, 3), LV_OBJ_FLAG_CLICKABLE));

    SUBCASE("text input commits on READY only") {
        lv_textarea_set_text(in, "");
        lv_textarea_add_text(in, "Bob");
        send(in, LV_EVENT_VALUE_CHANGED);
        CHECK(op->get().str == "Alice");
        send(in, LV_EVENT_READY);
        CHECK(op->get().str == "Bob");
        rt.pump();
        CHECK(ta_text(ro) == "Bob");
        CHECK(ta_text(nth(s, 4)) == "Op: Bob");
    }
    SUBCASE("cancel reverts the text") {
        lv_textarea_set_text(in, "Zed");
        send(in, LV_EVENT_CANCEL);
        CHECK(ta_text(in) == "Alice");
        CHECK(op->get().str == "Alice");
    }
    SUBCASE("password entry") {
        lv_textarea_add_text(pw, "12x34");
        CHECK(ta_text(pw) == "1234");                        // accepted chars and max_length
        send(pw, LV_EVENT_READY);
        CHECK(pin->get().str == "1234");
        rt.pump();
        CHECK(ta_text(nth(s, 3)) == "1234");                 // the display-only copy follows
    }
}

TEST_CASE("the on-screen keyboard switches to the number pad for numeric inputs")
{
    Runtime rt;
    rt.tag("n", TagType::Float, 1);
    rt.tag("t", std::string("x"));
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "numeric_display", "bind": "n", "input": true },
        { "type": "text_display", "bind": "t", "input": true },
        { "type": "numeric_display", "bind": "n" },
        { "type": "keyboard" }
    ])json"));
    lv_obj_t* num = nth(s, 0);
    lv_obj_t* txt = nth(s, 1);
    lv_obj_t* ro = nth(s, 2);
    lv_obj_t* kb = nth(s, 3);
    REQUIRE(lv_obj_check_type(kb, &lv_keyboard_class));

    send(num, LV_EVENT_FOCUSED);                             // a tap focuses, then clicks
    send(num, LV_EVENT_CLICKED);
    CHECK_FALSE(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_NUMBER);
    CHECK(ta_text(num) == "");                               // cleared for a fresh entry
    CHECK(lv_keyboard_get_textarea(kb) == num);

    lv_textarea_add_text(num, "5");
    send(num, LV_EVENT_READY);
    CHECK(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    CHECK(tags().find("n")->get_number() == doctest::Approx(5));
    CHECK(ta_text(num) == "5");
    CHECK_FALSE(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN) == false);   // (no re-open for the next field)

    send(txt, LV_EVENT_FOCUSED);
    CHECK_FALSE(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_LOWER);
    send(txt, LV_EVENT_CANCEL);
    CHECK(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));

    send(ro, LV_EVENT_CLICKED);                              // display-only: no keyboard
    CHECK(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE("plain textarea keeps writing on every change")
{
    Runtime rt;
    Tag* t = rt.tag("t", std::string(""));
    lv_obj_t* s = build_widgets(J(R"json([ { "type": "textarea", "bind": "t" } ])json"));
    lv_obj_t* ta = nth(s, 0);
    lv_textarea_add_text(ta, "a");
    CHECK(t->get().str == "a");                              // LVGL sends VALUE_CHANGED itself
}
