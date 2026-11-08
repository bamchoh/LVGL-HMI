#include <doctest.h>
#include "jsonutil.hpp"

using namespace hml;
using nlohmann::json;

TEST_CASE("jget returns the child or nullptr")
{
    json o = json::parse(R"({"a": 1, "n": null, "s": "x"})");

    SUBCASE("present keys, including explicit null") {
        REQUIRE(jget(o, "a") != nullptr);
        CHECK(jget(o, "a")->get<int>() == 1);
        REQUIRE(jget(o, "n") != nullptr);
        CHECK(jget(o, "n")->is_null());
    }
    SUBCASE("missing key") {
        CHECK(jget(o, "missing") == nullptr);
    }
    SUBCASE("non-object containers never have children") {
        json arr = json::array({ 1, 2 });
        json str = "text";
        json nul;
        CHECK(jget(arr, "0") == nullptr);
        CHECK(jget(str, "a") == nullptr);
        CHECK(jget(nul, "a") == nullptr);
    }
    SUBCASE("pointer overload tolerates nullptr") {
        const json* none = nullptr;
        CHECK(jget(none, "a") == nullptr);
        CHECK(jget(&o, "s")->get<std::string>() == "x");
        // chaining through a missing intermediate stays null
        CHECK(jget(jget(o, "missing"), "a") == nullptr);
    }
}

TEST_CASE("jhas is true for any present key, even null")
{
    json o = json::parse(R"({"a": 1, "n": null})");
    CHECK(jhas(o, "a"));
    CHECK(jhas(o, "n"));
    CHECK_FALSE(jhas(o, "zzz"));
    CHECK_FALSE(jhas(json::array(), "a"));
}

TEST_CASE("jstr / jstr_opt only accept strings")
{
    json o = json::parse(R"({"s": "hello", "n": 5, "b": true})");

    CHECK(jstr(o, "s", "d") == "hello");
    CHECK(jstr(o, "n", "d") == "d");          // numbers are not converted
    CHECK(jstr(o, "b", "d") == "d");
    CHECK(jstr(o, "missing", "d") == "d");
    CHECK(jstr(json(), "s", "d") == "d");

    CHECK(jstr_opt(o, "s") == std::optional<std::string>("hello"));
    CHECK_FALSE(jstr_opt(o, "n").has_value());
    CHECK_FALSE(jstr_opt(o, "missing").has_value());
}

TEST_CASE("jnum accepts numbers, booleans and numeric strings")
{
    json o = json::parse(R"({"i": 42, "f": 2.5, "neg": -3, "t": true, "fl": false,
                             "s": "12.5", "sneg": "-7", "bad": "abc", "arr": [1], "obj": {}, "nul": null})");

    CHECK(jnum(o, "i", 0) == doctest::Approx(42));
    CHECK(jnum(o, "f", 0) == doctest::Approx(2.5));
    CHECK(jnum(o, "neg", 0) == doctest::Approx(-3));
    CHECK(jnum(o, "t", 0) == doctest::Approx(1));
    CHECK(jnum(o, "fl", 9) == doctest::Approx(0));
    CHECK(jnum(o, "s", 0) == doctest::Approx(12.5));
    CHECK(jnum(o, "sneg", 0) == doctest::Approx(-7));

    SUBCASE("non-numeric string parses as 0, not the default") {
        CHECK(jnum(o, "bad", 9) == doctest::Approx(0));
    }
    SUBCASE("containers, null and missing keys fall back to the default") {
        CHECK(jnum(o, "arr", 9) == doctest::Approx(9));
        CHECK(jnum(o, "obj", 9) == doctest::Approx(9));
        CHECK(jnum(o, "nul", 9) == doctest::Approx(9));
        CHECK(jnum(o, "missing", 9) == doctest::Approx(9));
    }
}

TEST_CASE("jbool accepts booleans, numbers and the string true")
{
    json o = json::parse(R"({"t": true, "f": false, "one": 1, "two": 2, "zero": 0, "zf": 0.0,
                             "st": "true", "sT": "TRUE", "sf": "false", "syes": "yes", "s1": "1",
                             "arr": [true], "nul": null})");

    CHECK(jbool(o, "t", false));
    CHECK_FALSE(jbool(o, "f", true));
    CHECK(jbool(o, "one", false));
    CHECK(jbool(o, "two", false));
    CHECK_FALSE(jbool(o, "zero", true));
    CHECK_FALSE(jbool(o, "zf", true));

    SUBCASE("strings: only a case-insensitive 'true' is true") {
        CHECK(jbool(o, "st", false));
        CHECK(jbool(o, "sT", false));
        CHECK_FALSE(jbool(o, "sf", true));
        CHECK_FALSE(jbool(o, "syes", true));
        CHECK_FALSE(jbool(o, "s1", true));
    }
    SUBCASE("other types and missing keys fall back to the default") {
        CHECK(jbool(o, "arr", true));
        CHECK_FALSE(jbool(o, "arr", false));
        CHECK(jbool(o, "nul", true));
        CHECK(jbool(o, "missing", true));
        CHECK_FALSE(jbool(o, "missing", false));
    }
}

TEST_CASE("jscalar_to_string")
{
    CHECK(jscalar_to_string(json("abc")) == "abc");
    CHECK(jscalar_to_string(json("")) == "");
    CHECK(jscalar_to_string(json(true)) == "true");
    CHECK(jscalar_to_string(json(false)) == "false");
    CHECK(jscalar_to_string(json(3)) == "3");
    CHECK(jscalar_to_string(json(-3)) == "-3");
    CHECK(jscalar_to_string(json(2.5)) == "2.5");
    CHECK(jscalar_to_string(json(3.0)) == "3");          // %g drops the trailing .0
    CHECK(jscalar_to_string(json(1e6)) == "1e+06");      // %g switches to exponent form
    CHECK(jscalar_to_string(json()) == "");
    CHECK(jscalar_to_string(json::array({ 1 })) == "");
    CHECK(jscalar_to_string(json::object()) == "");
}

TEST_CASE("jscalar_to_number")
{
    CHECK(jscalar_to_number(json(7)) == doctest::Approx(7));
    CHECK(jscalar_to_number(json(-0.25)) == doctest::Approx(-0.25));
    CHECK(jscalar_to_number(json(true)) == doctest::Approx(1));
    CHECK(jscalar_to_number(json(false)) == doctest::Approx(0));
    CHECK(jscalar_to_number(json("3.5")) == doctest::Approx(3.5));
    CHECK(jscalar_to_number(json("  8")) == doctest::Approx(8));  // atof skips leading blanks
    CHECK(jscalar_to_number(json("8abc")) == doctest::Approx(8)); // atof stops at the first bad char
    CHECK(jscalar_to_number(json("abc"), 5) == doctest::Approx(0));
    CHECK(jscalar_to_number(json(), 5) == doctest::Approx(5));
    CHECK(jscalar_to_number(json::array(), 5) == doctest::Approx(5));
    CHECK(jscalar_to_number(json::object(), 5) == doctest::Approx(5));
    CHECK(jscalar_to_number(json::object()) == doctest::Approx(0)); // default dflt is 0
}
