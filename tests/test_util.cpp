#include <doctest.h>
#include "util.hpp"

#include <string>

using namespace hml;

TEST_CASE("iequals compares ASCII case-insensitively")
{
    CHECK(iequals("abc", "ABC"));
    CHECK(iequals("Modbus_TCP", "modbus_tcp"));
    CHECK(iequals("", ""));
    CHECK_FALSE(iequals("abc", "abd"));
    CHECK_FALSE(iequals("abc", "ab"));        // different length
    CHECK_FALSE(iequals("", "a"));
    CHECK_FALSE(iequals("a b", "a_b"));

    SUBCASE("works with std::string and string_view") {
        std::string a = "Hello";
        std::string_view b = "hELLO";
        CHECK(iequals(a, b));
    }
    SUBCASE("bytes above 0x7F are compared as-is") {
        CHECK(iequals("\xC3\xA9", "\xC3\xA9"));
        CHECK_FALSE(iequals("\xC3\xA9", "\xC3\x89"));
    }
}

TEST_CASE("vformat formats like printf into a std::string")
{
    CHECK(vformat("%d-%s", 42, "x") == "42-x");
    CHECK(vformat("plain") == "plain");
    CHECK(vformat("%%") == "%");
    CHECK(vformat("%5.2f|%-4d|%04x", 3.14159, 7, 255) == " 3.14|7   |00ff");
    CHECK(vformat("%lld", 1234567890123LL) == "1234567890123");
    CHECK(vformat("%s", "") == "");

    SUBCASE("output longer than any internal buffer is not truncated") {
        std::string big(5000, 'z');
        std::string out = vformat("[%s]", big.c_str());
        CHECK(out.size() == 5002);
        CHECK(out.front() == '[');
        CHECK(out.back() == ']');
    }
    SUBCASE("embedded multibyte text passes through") {
        CHECK(vformat("%s!", "\xE6\xB8\xA9\xE5\xBA\xA6") == "\xE6\xB8\xA9\xE5\xBA\xA6!");
    }
}

TEST_CASE("millis is monotonic and sleep_ms waits at least roughly that long")
{
    uint32_t a = millis();
    uint32_t b = millis();
    CHECK(b >= a);

    uint32_t t0 = millis();
    sleep_ms(30);
    uint32_t dt = millis() - t0;
    CHECK(dt >= 20);       // generous lower bound: timer granularity differs per OS
    CHECK(dt < 5000);
}

TEST_CASE("log macros format their arguments without crashing")
{
    HML_INFO("info %d", 1);
    HML_WARN("warn %s", "x");
    HML_ERROR("error %.1f", 2.5);
    log(LogLevel::Info, std::string("plain message"));
    CHECK(true);
}
