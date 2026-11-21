// Steadying a wandering touch panel (touch_filter.hpp). Reliability and usability of the touch
// on a resistive panel; the numbers are chosen on the device with "log".
#include <doctest.h>
#include "touch_filter.hpp"

#include <cmath>
#include <random>

using namespace hml;

namespace {

TouchFilterConfig cfg(int settle, int smooth, int jitter)
{
    TouchFilterConfig c;
    c.settle = settle; c.smooth = smooth; c.jitter = jitter;
    return c;
}

} // namespace

TEST_CASE("touch filter: with nothing set every reading goes through as it is")
{
    TouchFilter f;
    CHECK_FALSE(TouchFilterConfig().active());
    for(int i = 0; i < 20; i++) {
        const auto p = f.feed(100 + i * 3, 50 - i, true, static_cast<uint32_t>(i * 10));
        CHECK(p.x == 100 + i * 3);
        CHECK(p.y == 50 - i);
        CHECK(p.pressed);
    }
    const auto up = f.feed(0, 0, false, 200);
    CHECK_FALSE(up.pressed);
    CHECK(up.x == 157);                                          // released where it was last
    CHECK(up.y == 31);
    CHECK(f.finished());
    CHECK(f.last().samples == 20);
    CHECK(f.last().raw_dx == 57);
    CHECK(f.last().out_dx == 57);
    CHECK(f.last().ms == 190);
    CHECK_FALSE(f.feed(5, 5, false, 210).pressed);               // nothing touched: nothing ends
    CHECK_FALSE(f.finished());
}

TEST_CASE("touch filter: a finger that holds still is one point, however the readings wander")
{
    // a resistive panel under a finger: a wild first reading, then a few pixels of noise for two seconds
    TouchFilter f(cfg(1, 3, 8));
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> noise(-4, 4);
    auto first = f.feed(260, 90, true, 0);                       // the finger landing (far off)
    CHECK_FALSE(first.pressed);                                  // not a press yet
    int min_x = 9999, max_x = -9999, min_y = 9999, max_y = -9999, n = 0;
    for(int i = 1; i <= 100; i++) {
        const auto p = f.feed(200 + noise(rng), 150 + noise(rng), true, static_cast<uint32_t>(i * 20));
        REQUIRE(p.pressed);
        min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        n++;
    }
    CHECK(max_x - min_x == 0);                                   // the reported point never moved
    CHECK(max_y - min_y == 0);
    CHECK(std::abs(min_x - 200) <= 4);                           // and it is where the finger is
    CHECK(std::abs(min_y - 150) <= 4);
    const auto up = f.feed(0, 0, false, 2100);
    CHECK_FALSE(up.pressed);
    CHECK(up.x == min_x);
    REQUIRE(f.finished());
    CHECK(f.last().samples == 101);
    CHECK(f.last().raw_dx >= 60);                                // (the wild first reading is in the numbers)
    CHECK(f.last().out_dx == 0);
    CHECK(f.last().out_dy == 0);
    CHECK(f.describe().find("101 samples") != std::string::npos);
    CHECK(f.describe().find("reported 0 x 0 px") != std::string::npos);
    // the same readings without the filter tremble
    TouchFilter raw;
    std::mt19937 rng2(7);
    int rmin = 9999, rmax = -9999;
    for(int i = 1; i <= 100; i++) { const auto p = raw.feed(200 + noise(rng2), 150 + noise(rng2), true, 0); rmin = std::min(rmin, p.x); rmax = std::max(rmax, p.x); }
    CHECK(rmax - rmin >= 6);
}

TEST_CASE("touch filter: a real drag is followed, at most the jitter distance behind")
{
    TouchFilter f(cfg(0, 1, 8));
    auto p = f.feed(100, 100, true, 0);
    CHECK(p.x == 100);
    for(int x = 101; x <= 108; x++) { p = f.feed(x, 100, true, 0); CHECK(p.x == 100); }       // inside the string: nothing moves
    p = f.feed(109, 100, true, 0);
    CHECK(p.x == 101);                                           // pulled along
    for(int x = 110; x <= 300; x += 5) p = f.feed(x, 100, true, 0);
    CHECK(p.x == 300 - 5 - 8 + 5);                               // 8 behind the finger (the last reading was 300 - 5 + ... = 295 -> 287)
    CHECK(p.y == 100);
    // back the other way: first nothing, then it follows again
    const int stay = p.x;
    p = f.feed(290, 100, true, 0);
    CHECK(p.x == stay);
    p = f.feed(200, 100, true, 0);
    CHECK(p.x == 208);
    // a diagonal move keeps its direction
    TouchFilter g(cfg(0, 1, 10));
    g.feed(0, 0, true, 0);
    p = g.feed(300, 400, true, 0);                               // 500 away: 490 along the same line
    CHECK(p.x == 294);
    CHECK(p.y == 392);
}

TEST_CASE("touch filter: smoothing averages the last readings; settling leaves the first ones out")
{
    TouchFilter f(cfg(0, 4, 0));
    CHECK(f.feed(100, 0, true, 0).x == 100);
    CHECK(f.feed(104, 0, true, 0).x == 102);
    CHECK(f.feed(96, 0, true, 0).x == 100);
    CHECK(f.feed(100, 0, true, 0).x == 100);
    CHECK(f.feed(200, 0, true, 0).x == 125);                     // (104 + 96 + 100 + 200) / 4: one wild reading is a quarter of it
    TouchFilter g(cfg(3, 1, 0));
    CHECK_FALSE(g.feed(1, 1, true, 0).pressed);
    CHECK_FALSE(g.feed(2, 2, true, 0).pressed);
    CHECK_FALSE(g.feed(3, 3, true, 0).pressed);
    const auto p = g.feed(50, 60, true, 0);
    CHECK(p.pressed);
    CHECK(p.x == 50);
    // a touch shorter than the settling is no touch at all (a bounce of the panel)
    TouchFilter h(cfg(2, 1, 0));
    CHECK_FALSE(h.feed(10, 10, true, 0).pressed);
    const auto up = h.feed(0, 0, false, 5);
    CHECK_FALSE(up.pressed);
    CHECK(h.finished());
    CHECK(h.last().samples == 1);
    CHECK(h.last().out_dx == 0);
    // the next touch starts fresh (no readings of the last one in the average)
    TouchFilter k(cfg(0, 4, 0));
    k.feed(0, 0, true, 0); k.feed(0, 0, true, 0);
    k.feed(0, 0, false, 0);
    CHECK(k.feed(400, 300, true, 0).x == 400);
}

TEST_CASE("touch filter: the settings from a project are kept in bounds")
{
    CHECK_FALSE(parse_touch_filter(nullptr).active());
    json j = json::parse(R"({ "swap_axes": true, "calibration": [0, 0, 4095, 4095] })");
    CHECK_FALSE(parse_touch_filter(&j).active());                // nothing asked for: nothing changes
    j = json::parse(R"({ "jitter": 8, "smooth": 3, "settle": 1, "scroll_limit": 16, "log": true })");
    TouchFilterConfig c = parse_touch_filter(&j);
    CHECK(c.active());
    CHECK(c.jitter == 8);
    CHECK(c.smooth == 3);
    CHECK(c.settle == 1);
    CHECK(c.scroll_limit == 16);
    CHECK(c.log);
    j = json::parse(R"({ "jitter": -5, "smooth": 1000, "settle": 99, "scroll_limit": 100000 })");
    c = parse_touch_filter(&j);
    CHECK(c.jitter == 0);
    CHECK(c.smooth == 8);
    CHECK(c.settle == 5);
    CHECK(c.scroll_limit == 200);
    j = json::parse(R"({ "jitter": "wide", "smooth": null, "log": "yes" })");
    c = parse_touch_filter(&j);
    CHECK(c.smooth == 1);
    j = json::parse("[1, 2]");
    CHECK_FALSE(parse_touch_filter(&j).active());
    // readings from a broken panel do not break the arithmetic
    TouchFilter f(cfg(1, 8, 100));
    for(int v : { 2147483647, -2147483647, 0, 2147483647, -2147483647 }) f.feed(v, v, true, 0);
    f.feed(0, 0, false, 0);
    CHECK(f.finished());
}

TEST_CASE("touch filter: the finger lifting is left out too; the log tells the steady part from the ends")
{
    // a press as the ADS7846 gives it: two wild readings as the finger lands, steady ones, two wild ones as it lifts
    TouchFilterConfig c = cfg(2, 1, 0);
    c.release = 2;
    TouchFilter f(c);
    const int xs[] = { 260, 230, 200, 201, 199, 200, 202, 200, 170, 140 };
    const int ys[] = { 90, 120, 150, 150, 151, 149, 150, 150, 180, 220 };
    int min_x = 9999, max_x = -9999, min_y = 9999, max_y = -9999, pressed = 0;
    for(int i = 0; i < 10; i++) {
        const auto p = f.feed(xs[i], ys[i], true, static_cast<uint32_t>(i * 16));
        if(!p.pressed) continue;
        pressed++;
        min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
    }
    const auto up = f.feed(0, 0, false, 160);
    CHECK_FALSE(up.pressed);
    CHECK(pressed == 6);                                         // 10 readings, 2 at the start and 2 at the end left out
    CHECK(min_x == 199);
    CHECK(max_x == 202);                                         // none of the wild ones was passed on
    CHECK(min_y == 149);
    CHECK(max_y == 151);
    CHECK(up.x >= 199);
    CHECK(up.x <= 202);                                          // released where the finger rested
    REQUIRE(f.finished());
    CHECK(f.last().samples == 10);
    CHECK(f.last().raw_dx == 120);                               // all readings
    CHECK(f.last().raw_dy == 130);
    CHECK(f.last().core_dx == 3);                                // without the first and last two
    CHECK(f.last().core_dy == 2);
    CHECK(f.last().out_dx == 3);
    CHECK(f.describe().find("(3 x 2 without the first and last two)") != std::string::npos);

    // a tap too short to have a steady part is still a tap (one press, one release)
    TouchFilterConfig d = cfg(1, 1, 0);
    d.release = 1;
    TouchFilter g(d);
    int n = 0;
    for(int i = 0; i < 3; i++) if(g.feed(100 + i, 100, true, 0).pressed) n++;
    CHECK(n == 1);
    CHECK_FALSE(g.feed(0, 0, false, 0).pressed);
    CHECK(g.last().core_dx == 0);
    // ... and one shorter than what is left out is none
    TouchFilter h(d);
    CHECK_FALSE(h.feed(1, 1, true, 0).pressed);
    CHECK_FALSE(h.feed(2, 2, true, 0).pressed);
    CHECK_FALSE(h.feed(0, 0, false, 0).pressed);
    // "release" alone switches the filter on, and is kept in bounds
    json j = json::parse(R"({ "release": 2 })");
    CHECK(parse_touch_filter(&j).active());
    CHECK(parse_touch_filter(&j).release == 2);
    j = json::parse(R"({ "release": 99 })");
    CHECK(parse_touch_filter(&j).release == 3);
}
