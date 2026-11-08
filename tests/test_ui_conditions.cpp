// Condition bindings that compare against another tag: { "gt": { "tag": "limit" } }.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "ui.hpp"

#include <string>

using namespace hml;
using hmltest::Runtime;

namespace {

json J(const char* text) { return json::parse(text); }

lv_obj_t* build_widgets(const json& widgets)
{
    json scr = { { "name", "s" }, { "children", widgets } };
    lv_obj_t* s = ui().build_screen(scr, "");
    REQUIRE(s != nullptr);
    REQUIRE(ui().show_screen("s", "none"));
    return s;
}

lv_obj_t* nth(lv_obj_t* parent, int32_t idx) { return lv_obj_get_child(parent, idx); }
bool hidden(lv_obj_t* o) { return lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }
bool checked(lv_obj_t* o) { return lv_obj_has_state(o, LV_STATE_CHECKED); }

} // namespace

TEST_CASE("tag-to-tag numeric comparisons re-evaluate when either side changes")
{
    Runtime rt;
    Tag* temp = rt.tag("temp", TagType::Float, 25);
    Tag* hi = rt.tag("hi", TagType::Float, 30);
    Tag* lo = rt.tag("lo", TagType::Float, 20);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "label", "text": "HIGH",  "bind": { "visible": { "tag": "temp", "gt": { "tag": "hi" } } } },
        { "type": "label", "text": "LOW",   "bind": { "visible": { "tag": "temp", "lt": { "tag": "lo" } } } },
        { "type": "label", "text": "ok",    "bind": { "hidden":  { "tag": "temp", "ge": { "tag": "hi" } } } },
        { "type": "label", "text": "le",    "bind": { "visible": { "tag": "temp", "le": { "tag": "hi" } } } },
        { "type": "label", "text": "eq",    "bind": { "visible": { "tag": "temp", "eq": { "tag": "hi" } } } },
        { "type": "label", "text": "ne",    "bind": { "visible": { "tag": "temp", "ne": { "tag": "hi" } } } },
        { "type": "button", "bind": { "enabled": { "tag": "temp", "lt": { "tag": "hi" }, "invert": true } } }
    ])json"));

    // temp 25, hi 30, lo 20
    CHECK(hidden(nth(s, 0)));
    CHECK(hidden(nth(s, 1)));
    CHECK_FALSE(hidden(nth(s, 2)));
    CHECK_FALSE(hidden(nth(s, 3)));
    CHECK(hidden(nth(s, 4)));
    CHECK_FALSE(hidden(nth(s, 5)));
    CHECK(lv_obj_has_state(nth(s, 6), LV_STATE_DISABLED));

    SUBCASE("the bound tag moves") {
        temp->set_number(31, nullptr);
        rt.pump();
        CHECK_FALSE(hidden(nth(s, 0)));          // HIGH
        CHECK(hidden(nth(s, 1)));
        CHECK(hidden(nth(s, 2)));                // ok hidden (ge hi)
        CHECK(hidden(nth(s, 3)));
        CHECK_FALSE(lv_obj_has_state(nth(s, 6), LV_STATE_DISABLED));

        temp->set_number(10, nullptr);
        rt.pump();
        CHECK(hidden(nth(s, 0)));
        CHECK_FALSE(hidden(nth(s, 1)));          // LOW
    }
    SUBCASE("the compared tag moves") {
        hi->set_number(24, nullptr);             // limit lowered below the current temperature
        rt.pump();
        CHECK_FALSE(hidden(nth(s, 0)));
        CHECK(hidden(nth(s, 2)));
        lo->set_number(26, nullptr);
        rt.pump();
        CHECK_FALSE(hidden(nth(s, 1)));
    }
    SUBCASE("equality against a tag") {
        hi->set_number(25, nullptr);
        rt.pump();
        CHECK_FALSE(hidden(nth(s, 4)));          // eq
        CHECK(hidden(nth(s, 5)));                // ne
        CHECK_FALSE(hidden(nth(s, 3)));          // le
    }
}

TEST_CASE("string tags compare as text, mixed types compare as numbers")
{
    Runtime rt;
    Tag* state = rt.tag("state", std::string("Run"));
    Tag* want = rt.tag("want", std::string("Run"));
    Tag* num = rt.tag("num", TagType::Int, 3);
    Tag* text_num = rt.tag("text_num", std::string("3"));
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "label", "text": "match", "bind": { "visible": { "tag": "state", "eq": { "tag": "want" } } } },
        { "type": "label", "text": "mixed", "bind": { "visible": { "tag": "num", "eq": { "tag": "text_num" } } } },
        { "type": "label", "text": "gt-str", "bind": { "visible": { "tag": "state", "gt": { "tag": "want" } } } }
    ])json"));
    CHECK_FALSE(hidden(nth(s, 0)));
    CHECK_FALSE(hidden(nth(s, 1)));              // "3" parses to 3
    CHECK(hidden(nth(s, 2)));                    // ordering is not defined for two strings

    want->set_string("Stop", nullptr);
    rt.pump();
    CHECK(hidden(nth(s, 0)));
    state->set_string("Stop", nullptr);
    rt.pump();
    CHECK_FALSE(hidden(nth(s, 0)));

    text_num->set_string("4", nullptr);
    rt.pump();
    CHECK(hidden(nth(s, 1)));
    num->set_number(4, nullptr);
    rt.pump();
    CHECK_FALSE(hidden(nth(s, 1)));
}

TEST_CASE("checked with eq against a tag: a radio button that copies the compared tag")
{
    Runtime rt;
    Tag* mode = rt.tag("mode", TagType::Int, 0);
    Tag* preset = rt.tag("preset", TagType::Int, 2);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "button", "checkable": true, "bind": { "checked": { "tag": "mode", "eq": { "tag": "preset" } } } }
    ])json"));
    lv_obj_t* b = nth(s, 0);
    CHECK_FALSE(checked(b));

    lv_obj_add_state(b, LV_STATE_CHECKED);
    lv_obj_send_event(b, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(mode->get_number() == doctest::Approx(2));
    rt.pump();
    CHECK(checked(b));

    preset->set_number(5, nullptr);              // the preset changes: the button no longer matches
    rt.pump();
    CHECK_FALSE(checked(b));
    lv_obj_add_state(b, LV_STATE_CHECKED);
    lv_obj_send_event(b, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(mode->get_number() == doctest::Approx(5));

    lv_obj_remove_state(b, LV_STATE_CHECKED);    // unchecking leaves the tag alone
    lv_obj_send_event(b, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(mode->get_number() == doctest::Approx(5));
}

TEST_CASE("an unknown compare tag drops the condition and falls back to truthiness")
{
    Runtime rt;
    Tag* v = rt.tag("v", TagType::Int, 0);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "label", "text": "x", "bind": { "visible": { "tag": "v", "gt": { "tag": "nope" } } } },
        { "type": "label", "text": "y", "bind": { "visible": { "tag": "v", "gt": { "no_tag_key": 1 } } } }
    ])json"));
    CHECK(hidden(nth(s, 0)));
    CHECK(hidden(nth(s, 1)));
    v->set_number(1, nullptr);
    rt.pump();
    CHECK_FALSE(hidden(nth(s, 0)));
    CHECK_FALSE(hidden(nth(s, 1)));
}

TEST_CASE("constant comparisons still work alongside tag comparisons")
{
    Runtime rt;
    Tag* a = rt.tag("a", TagType::Float, 5);
    rt.tag("b", TagType::Float, 7);
    lv_obj_t* s = build_widgets(J(R"json([
        { "type": "label", "text": "c", "bind": { "visible": { "tag": "a", "gt": 4 } } },
        { "type": "label", "text": "t", "bind": { "visible": { "tag": "a", "gt": { "tag": "b" } } } },
        { "type": "label", "text": "s", "bind": { "visible": { "tag": "a", "eq": "5" } } }
    ])json"));
    CHECK_FALSE(hidden(nth(s, 0)));
    CHECK(hidden(nth(s, 1)));
    CHECK_FALSE(hidden(nth(s, 2)));
    a->set_number(8, nullptr);
    rt.pump();
    CHECK_FALSE(hidden(nth(s, 1)));
    CHECK(hidden(nth(s, 2)));
}
