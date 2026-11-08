#include <doctest.h>
#include "tag.hpp"

#include <string>
#include <vector>

using namespace hml;

// ---------------------------------------------------------------- types

TEST_CASE("type_from_name is case-insensitive and knows the aliases")
{
    CHECK(type_from_name("bool", TagType::String) == TagType::Bool);
    CHECK(type_from_name("Boolean", TagType::String) == TagType::Bool);
    CHECK(type_from_name("INT", TagType::String) == TagType::Int);
    CHECK(type_from_name("integer", TagType::String) == TagType::Int);
    CHECK(type_from_name("float", TagType::String) == TagType::Float);
    CHECK(type_from_name("Real", TagType::String) == TagType::Float);
    CHECK(type_from_name("number", TagType::String) == TagType::Float);
    CHECK(type_from_name("string", TagType::Int) == TagType::String);
    CHECK(type_from_name("TEXT", TagType::Int) == TagType::String);

    SUBCASE("unknown names fall back to the default") {
        CHECK(type_from_name("double", TagType::Int) == TagType::Int);
        CHECK(type_from_name("", TagType::Bool) == TagType::Bool);
    }
}

TEST_CASE("type_name round-trips through type_from_name")
{
    for(TagType t : { TagType::Bool, TagType::Int, TagType::Float, TagType::String }) {
        CHECK(type_from_name(type_name(t), TagType::Float) == t);
    }
    CHECK(std::string(type_name(TagType::Bool)) == "bool");
    CHECK(std::string(type_name(TagType::String)) == "string");
}

// ---------------------------------------------------------------- Value

TEST_CASE("Value::as_number and truthy")
{
    CHECK(Value::number(2.5).as_number() == doctest::Approx(2.5));
    CHECK(Value::string("3.5").as_number() == doctest::Approx(3.5));
    CHECK(Value::string("abc").as_number() == doctest::Approx(0));
    CHECK(Value::string("").as_number() == doctest::Approx(0));

    CHECK(Value::number(1).truthy());
    CHECK(Value::number(-0.5).truthy());
    CHECK_FALSE(Value::number(0).truthy());
    CHECK(Value::string("x").truthy());
    CHECK(Value::string("0").truthy());        // any non-empty string is truthy
    CHECK_FALSE(Value::string("").truthy());
}

TEST_CASE("Value::format with an empty format uses a per-type default")
{
    CHECK(Value::number(1, TagType::Bool).format() == "true");
    CHECK(Value::number(0, TagType::Bool).format() == "false");
    CHECK(Value::number(42, TagType::Int).format() == "42");
    CHECK(Value::number(-7, TagType::Int).format() == "-7");
    CHECK(Value::number(2.5, TagType::Float).format() == "2.5");
    CHECK(Value::number(3.0, TagType::Float).format() == "3");
    CHECK(Value::string("hello").format() == "hello");
}

TEST_CASE("Value::format with printf-style formats")
{
    Value f = Value::number(2.56, TagType::Float);

    SUBCASE("integer conversions round to nearest") {
        CHECK(f.format("%d") == "3");
        CHECK(Value::number(2.4).format("%d") == "2");
        CHECK(Value::number(-2.6).format("%i") == "-3");
        CHECK(Value::number(42).format("%05d") == "00042");
        CHECK(Value::number(42).format("%+d") == "+42");
    }
    SUBCASE("length modifiers written by the user are ignored") {
        CHECK(Value::number(42).format("%ld") == "42");
        CHECK(Value::number(42).format("%lld") == "42");
        CHECK(Value::number(42).format("%hd") == "42");
    }
    SUBCASE("hex / octal / unsigned") {
        CHECK(Value::number(255).format("%x") == "ff");
        CHECK(Value::number(255).format("%X") == "FF");
        CHECK(Value::number(255).format("%04X") == "00FF");
        CHECK(Value::number(8).format("%o") == "10");
        CHECK(Value::number(8).format("%u") == "8");
    }
    SUBCASE("floating point conversions") {
        CHECK(f.format("%.1f") == "2.6");
        CHECK(f.format("%.0f") == "3");
        CHECK(f.format("%6.2f") == "  2.56");
        CHECK(Value::number(1234.5).format("%g") == "1234.5");
        CHECK(Value::number(1234.5).format("%.2e") == "1.23e+03");
    }
    SUBCASE("%s takes the string, or the default rendering of a number") {
        CHECK(Value::string("abc").format("%s") == "abc");
        CHECK(Value::string("abc").format("[%5s]") == "[  abc]");
        CHECK(Value::number(1.5, TagType::Float).format("%s") == "1.5");
        CHECK(Value::number(7, TagType::Int).format("%s") == "7");
        CHECK(Value::number(1, TagType::Bool).format("%s") == "true");
    }
    SUBCASE("%c") {
        CHECK(Value::number(65).format("%c") == "A");
    }
    SUBCASE("surrounding text and %% are kept") {
        CHECK(Value::number(42).format("%d rpm") == "42 rpm");
        CHECK(Value::number(42).format("Speed: %d") == "Speed: 42");
        CHECK(Value::number(42).format("%d%%") == "42%");
        CHECK(Value::number(42).format("%% %d") == "% 42");
    }
    SUBCASE("a format without a conversion is returned literally") {
        CHECK(Value::number(42).format("rpm") == "rpm");
    }
    SUBCASE("a string value is parsed before numeric formatting") {
        CHECK(Value::string("12.7").format("%d") == "13");
        CHECK(Value::string("12.7").format("%.2f") == "12.70");
    }
}

TEST_CASE("Value::parse")
{
    SUBCASE("bool accepts true/false, on/off, 1/0 in any case") {
        for(const char* s : { "true", "TRUE", "On", "1" }) {
            auto v = Value::parse(TagType::Bool, s);
            REQUIRE(v.has_value());
            CHECK(v->type == TagType::Bool);
            CHECK(v->num == doctest::Approx(1));
        }
        for(const char* s : { "false", "OFF", "0" }) {
            auto v = Value::parse(TagType::Bool, s);
            REQUIRE(v.has_value());
            CHECK(v->type == TagType::Bool);
            CHECK(v->num == doctest::Approx(0));
        }
        CHECK_FALSE(Value::parse(TagType::Bool, "yes").has_value());
        CHECK_FALSE(Value::parse(TagType::Bool, "2").has_value());
        CHECK_FALSE(Value::parse(TagType::Bool, "").has_value());
    }
    SUBCASE("int / float use strtod") {
        auto i = Value::parse(TagType::Int, "42");
        REQUIRE(i.has_value());
        CHECK(i->type == TagType::Int);
        CHECK(i->num == doctest::Approx(42));

        auto f = Value::parse(TagType::Float, "-1.5e2");
        REQUIRE(f.has_value());
        CHECK(f->type == TagType::Float);
        CHECK(f->num == doctest::Approx(-150));

        auto partial = Value::parse(TagType::Float, "12abc");   // strtod consumes the prefix
        REQUIRE(partial.has_value());
        CHECK(partial->num == doctest::Approx(12));

        CHECK_FALSE(Value::parse(TagType::Int, "abc").has_value());
        CHECK_FALSE(Value::parse(TagType::Float, "").has_value());
    }
    SUBCASE("string accepts anything") {
        auto s = Value::parse(TagType::String, "");
        REQUIRE(s.has_value());
        CHECK(s->is_string());
        CHECK(s->str.empty());
        CHECK(Value::parse(TagType::String, "a b")->str == "a b");
    }
}

// ---------------------------------------------------------------- Tag / TagStore

namespace {

struct Delivery {
    std::string tag;
    Value       value;
    const void* source;
};

struct Recorder {
    std::vector<Delivery> got;
    Tag::Callback cb()
    {
        return [this](Tag& t, const Value& v, const void* src) { got.push_back({ t.name(), v, src }); };
    }
};

} // namespace

TEST_CASE("TagStore::create / find / all / count / clear")
{
    TagStore store;
    CHECK(store.count() == 0);
    CHECK(store.find("a") == nullptr);

    Tag* a = store.create("a", TagType::Int, nullptr, false, 0, 0);
    Tag* b = store.create("b", TagType::String, nullptr, false, 0, 0);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->name() == "a");
    CHECK(a->type() == TagType::Int);
    CHECK(b->type() == TagType::String);
    CHECK(store.count() == 2);
    CHECK(store.find("a") == a);
    CHECK(store.find("b") == b);
    CHECK(store.find("c") == nullptr);

    SUBCASE("all() preserves creation order") {
        auto all = store.all();
        REQUIRE(all.size() == 2);
        CHECK(all[0] == a);
        CHECK(all[1] == b);
    }
    SUBCASE("an empty name is rejected") {
        CHECK(store.create("", TagType::Int, nullptr, false, 0, 0) == nullptr);
        CHECK(store.count() == 2);
    }
    SUBCASE("a duplicate name returns the first tag unchanged") {
        Tag* again = store.create("a", TagType::String, nullptr, false, 0, 0);
        CHECK(again == a);
        CHECK(again->type() == TagType::Int);
        CHECK(store.count() == 2);
    }
    SUBCASE("clear() drops everything") {
        store.clear();
        CHECK(store.count() == 0);
        CHECK(store.find("a") == nullptr);
    }
}

TEST_CASE("a tag without an initial value starts at the zero of its type")
{
    TagStore store;
    CHECK(store.create("i", TagType::Int, nullptr, false, 0, 0)->get().num == 0);
    CHECK(store.create("f", TagType::Float, nullptr, false, 0, 0)->get().num == 0);
    CHECK(store.create("b", TagType::Bool, nullptr, false, 0, 0)->get().num == 0);
    Value s = store.create("s", TagType::String, nullptr, false, 0, 0)->get();
    CHECK(s.is_string());
    CHECK(s.str.empty());
}

TEST_CASE("the initial value is coerced and clamped like a write")
{
    TagStore store;
    Value init = Value::number(250.4);
    Tag* t = store.create("t", TagType::Int, &init, true, 0, 100);
    REQUIRE(t != nullptr);
    CHECK(t->get().type == TagType::Int);
    CHECK(t->get_number() == doctest::Approx(100));

    double lo, hi;
    CHECK(t->limits(lo, hi));
    CHECK(lo == doctest::Approx(0));
    CHECK(hi == doctest::Approx(100));

    Tag* free_tag = store.create("free", TagType::Float, nullptr, false, 0, 0);
    CHECK_FALSE(free_tag->limits(lo, hi));
}

TEST_CASE("Tag::set coerces to the tag type")
{
    TagStore store;

    SUBCASE("int rounds to nearest") {
        Tag* t = store.create("i", TagType::Int, nullptr, false, 0, 0);
        t->set_number(2.4, nullptr);
        CHECK(t->get_number() == doctest::Approx(2));
        t->set_number(2.5, nullptr);
        CHECK(t->get_number() == doctest::Approx(3));
        t->set_number(-2.5, nullptr);
        CHECK(t->get_number() == doctest::Approx(-3));
        t->set_string("7.6", nullptr);
        CHECK(t->get_number() == doctest::Approx(8));
        CHECK(t->get().type == TagType::Int);
    }
    SUBCASE("float keeps fractions and parses strings") {
        Tag* t = store.create("f", TagType::Float, nullptr, false, 0, 0);
        t->set_number(2.25, nullptr);
        CHECK(t->get_number() == doctest::Approx(2.25));
        t->set_string("1.5", nullptr);
        CHECK(t->get_number() == doctest::Approx(1.5));
        t->set_string("abc", nullptr);     // unparsable text becomes 0
        CHECK(t->get_number() == doctest::Approx(0));
    }
    SUBCASE("bool normalizes to 0 / 1") {
        Tag* t = store.create("b", TagType::Bool, nullptr, false, 0, 0);
        t->set_number(5, nullptr);
        CHECK(t->get_number() == doctest::Approx(1));
        t->set_number(0, nullptr);
        CHECK(t->get_number() == doctest::Approx(0));
        t->set_string("true", nullptr);
        CHECK(t->get_number() == doctest::Approx(1));
        t->set_string("0", nullptr);
        CHECK(t->get_number() == doctest::Approx(0));
        t->set_string("2", nullptr);       // any non-zero numeric text is true
        CHECK(t->get_number() == doctest::Approx(1));
        t->set_string("false", nullptr);
        CHECK(t->get_number() == doctest::Approx(0));
        t->set_string("yes", nullptr);     // not "true" and not numeric -> false
        CHECK(t->get_number() == doctest::Approx(0));
    }
    SUBCASE("string stores text and renders numbers") {
        Tag* t = store.create("s", TagType::String, nullptr, false, 0, 0);
        t->set_string("hello", nullptr);
        CHECK(t->get().str == "hello");
        t->set_number(2.5, nullptr);
        CHECK(t->get().str == "2.5");
        t->set(Value::number(3, TagType::Int), nullptr);
        CHECK(t->get().str == "3");
        t->set(Value::number(1, TagType::Bool), nullptr);
        CHECK(t->get().str == "true");
        CHECK(t->get_number() == doctest::Approx(0));   // "true" is not a number
    }
}

TEST_CASE("Tag::set clamps numeric tags to their limits")
{
    TagStore store;
    Tag* t = store.create("t", TagType::Float, nullptr, true, -10, 10);
    t->set_number(50, nullptr);
    CHECK(t->get_number() == doctest::Approx(10));
    t->set_number(-50, nullptr);
    CHECK(t->get_number() == doctest::Approx(-10));
    t->set_number(3.5, nullptr);
    CHECK(t->get_number() == doctest::Approx(3.5));

    SUBCASE("limits are applied before int rounding") {
        Tag* i = store.create("i", TagType::Int, nullptr, true, 0, 10);
        i->set_number(10.4, nullptr);
        CHECK(i->get_number() == doctest::Approx(10));
    }
    SUBCASE("string and bool tags ignore limits") {
        Tag* b = store.create("b", TagType::Bool, nullptr, true, 5, 6);
        b->set_number(1, nullptr);
        CHECK(b->get_number() == doctest::Approx(1));
    }
}

TEST_CASE("Tag::set reports whether the stored value changed")
{
    TagStore store;
    Tag* i = store.create("i", TagType::Int, nullptr, false, 0, 0);
    CHECK(i->set_number(5, nullptr));
    CHECK_FALSE(i->set_number(5, nullptr));
    CHECK_FALSE(i->set_number(5.2, nullptr));       // rounds to the same int
    CHECK(i->set_number(6, nullptr));

    Tag* c = store.create("c", TagType::Int, nullptr, true, 0, 10);
    CHECK(c->set_number(50, nullptr));
    CHECK_FALSE(c->set_number(99, nullptr));        // both clamp to 10

    Tag* s = store.create("s", TagType::String, nullptr, false, 0, 0);
    CHECK(s->set_string("a", nullptr));
    CHECK_FALSE(s->set_string("a", nullptr));
    CHECK(s->set_string("", nullptr));
}

TEST_CASE("dispatch delivers dirty tags once, with the writer's source")
{
    TagStore store;
    Tag* t = store.create("t", TagType::Int, nullptr, false, 0, 0);
    Recorder ui, drv;
    t->subscribe(SubGroup::UI, ui.cb());
    t->subscribe(SubGroup::Driver, drv.cb());

    SUBCASE("nothing is pending right after creation") {
        store.dispatch(false);
        CHECK(ui.got.empty());
        CHECK(drv.got.empty());
    }
    SUBCASE("a write is delivered to every group exactly once") {
        int writer = 0;
        t->set_number(7, &writer);
        store.dispatch(false);
        REQUIRE(ui.got.size() == 1);
        REQUIRE(drv.got.size() == 1);
        CHECK(ui.got[0].tag == "t");
        CHECK(ui.got[0].value.num == doctest::Approx(7));
        CHECK(ui.got[0].source == &writer);
        CHECK(drv.got[0].source == &writer);

        store.dispatch(false);
        CHECK(ui.got.size() == 1);
        CHECK(drv.got.size() == 1);
    }
    SUBCASE("an unchanged write does not mark the tag dirty") {
        t->set_number(7, nullptr);
        store.dispatch(false);
        ui.got.clear();
        drv.got.clear();
        t->set_number(7, nullptr);
        store.dispatch(false);
        CHECK(ui.got.empty());
        CHECK(drv.got.empty());
    }
    SUBCASE("several writes before a dispatch collapse to the latest value and source") {
        int a = 0, b = 0;
        t->set_number(1, &a);
        t->set_number(2, &b);
        store.dispatch(false);
        REQUIRE(ui.got.size() == 1);
        CHECK(ui.got[0].value.num == doctest::Approx(2));
        CHECK(ui.got[0].source == &b);
    }
}

TEST_CASE("a forced dispatch refreshes the UI only and keeps driver notifications pending")
{
    TagStore store;
    Value init = Value::number(3);
    Tag* a = store.create("a", TagType::Int, &init, false, 0, 0);
    Tag* b = store.create("b", TagType::Int, nullptr, false, 0, 0);
    Recorder ui, drv;
    a->subscribe(SubGroup::UI, ui.cb());
    a->subscribe(SubGroup::Driver, drv.cb());
    b->subscribe(SubGroup::UI, ui.cb());
    b->subscribe(SubGroup::Driver, drv.cb());

    SUBCASE("every tag goes to the UI, dirty or not, with a null source") {
        store.dispatch(true);
        REQUIRE(ui.got.size() == 2);
        CHECK(ui.got[0].tag == "a");
        CHECK(ui.got[0].value.num == doctest::Approx(3));
        CHECK(ui.got[0].source == nullptr);
        CHECK(ui.got[1].tag == "b");
        CHECK(drv.got.empty());
    }
    SUBCASE("a pending write survives the forced dispatch") {
        int writer = 0;
        b->set_number(9, &writer);
        store.dispatch(true);
        CHECK(drv.got.empty());
        REQUIRE(ui.got.size() == 2);
        CHECK(ui.got[1].source == nullptr);     // forced refresh hides the writer

        ui.got.clear();
        store.dispatch(false);
        REQUIRE(drv.got.size() == 1);
        CHECK(drv.got[0].tag == "b");
        CHECK(drv.got[0].value.num == doctest::Approx(9));
        CHECK(drv.got[0].source == &writer);
        REQUIRE(ui.got.size() == 1);            // only the dirty tag this time
        CHECK(ui.got[0].tag == "b");
    }
}

TEST_CASE("unsubscribe_group removes one group and leaves the other")
{
    TagStore store;
    Tag* t = store.create("t", TagType::Int, nullptr, false, 0, 0);
    Recorder ui, drv;
    t->subscribe(SubGroup::UI, ui.cb());
    t->subscribe(SubGroup::UI, ui.cb());
    t->subscribe(SubGroup::Driver, drv.cb());

    store.unsubscribe_group(SubGroup::UI);
    t->set_number(1, nullptr);
    store.dispatch(false);
    CHECK(ui.got.empty());
    CHECK(drv.got.size() == 1);

    SUBCASE("a new UI subscription after hot reload works again") {
        t->subscribe(SubGroup::UI, ui.cb());
        t->set_number(2, nullptr);
        store.dispatch(false);
        CHECK(ui.got.size() == 1);
        CHECK(drv.got.size() == 2);
    }
}

TEST_CASE("a subscriber may write other tags from inside dispatch")
{
    TagStore store;
    Tag* src = store.create("src", TagType::Int, nullptr, false, 0, 0);
    Tag* dst = store.create("dst", TagType::Int, nullptr, false, 0, 0);
    Recorder dst_rec;
    dst->subscribe(SubGroup::UI, dst_rec.cb());

    // mirror src into dst * 2 (what a UI binding or a scripted driver would do)
    src->subscribe(SubGroup::UI, [dst](Tag&, const Value& v, const void*) { dst->set_number(v.num * 2, nullptr); });

    src->set_number(21, nullptr);
    store.dispatch(false);           // src delivered; dst written and delivered in the same pass
    CHECK(dst->get_number() == doctest::Approx(42));
    REQUIRE(dst_rec.got.size() == 1);
    CHECK(dst_rec.got[0].value.num == doctest::Approx(42));

    store.dispatch(false);
    CHECK(dst_rec.got.size() == 1);  // nothing left pending
}

TEST_CASE("the global tags() store is a singleton")
{
    CHECK(&tags() == &tags());
}
