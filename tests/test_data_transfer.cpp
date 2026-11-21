// Transfers (data_transfer.hpp): values of one device copied to another through the tags.
// Grouped by ISO/IEC 25010 characteristic (issue #66). No communication: the drivers here only
// say whether they are connected and what they have read; the clock is the test's.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "check.hpp"
#include "data_transfer.hpp"
#include "driver.hpp"
#include "system_settings.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

// A device as the transfers see it: connected or not, has read all its values again or not.
class FakeDriver final : public Driver {
public:
    explicit FakeDriver(bool cyclic) : cyclic_(cyclic) { link_set(false); }
    const char* type() const override { return "fake"; }
    bool start() override { return true; }
    void stop() override {}
    bool link_polls_cyclically() const override { return cyclic_; }
    void up(bool on) { link_set(on); }
    void polled() { link_polled(); }
    // the device holds this value now (as the driver's thread would report it)
    void read(Tag* t, double v) { t->set_number_from_device(v, this); }
    // ... and the writes the HMI side sent to the device
    void watch(Tag* t)
    {
        t->subscribe(SubGroup::Driver, [this](Tag&, const Value& v, const void* src) { if(src != this) writes.push_back(v.num); });
    }
    std::vector<double> writes;

private:
    bool cyclic_;
};

struct Fixture {
    Runtime rt;
    uint32_t now = 1000;
    FakeDriver* a = nullptr;        // "plc_a": reads in cycles
    FakeDriver* b = nullptr;        // "plc_b": reads in cycles
    FakeDriver* m = nullptr;        // "mq": values come when they come
    json drivers_cfg = json::parse(R"([
        { "name": "plc_a", "type": "fake", "map": [ { "tag": "a1" }, { "tag": "a2" }, { "tag": "a3" } ] },
        { "name": "plc_b", "type": "fake", "map": [ { "tag": "b1" }, { "tag": "b2" }, { "tag": "b3" } ] },
        { "name": "mq", "type": "fake", "subscribe": [ { "tag": "m1" } ], "publish": [ "m2" ] },
        { "name": "sim", "type": "sim", "items": [ { "tag": "s1" } ] } ])");
    Tag *a1, *a2, *b1, *b2, *m1, *m2, *h1, *h2;
    Fixture()
    {
        auto add = [&](const char* name, bool cyclic) {
            auto d = std::make_unique<FakeDriver>(cyclic);
            d->set_name(name);
            FakeDriver* p = d.get();
            rt.drivers.add(std::move(d));
            return p;
        };
        a = add("plc_a", true);
        b = add("plc_b", true);
        m = add("mq", false);
        a1 = rt.tag("a1", TagType::Float, 0);
        a2 = rt.tag("a2", TagType::Int, 0);
        rt.tag("a3", TagType::Float, 0);
        b1 = rt.tag("b1", TagType::Float, 0);
        b2 = rt.tag("b2", TagType::Int, 0, -100, 100);
        rt.tag("b3", TagType::Float, 0);
        m1 = rt.tag("m1", TagType::Float, 0);
        m2 = rt.tag("m2", TagType::Float, 0);
        h1 = rt.tag("h1", TagType::Float, 0);        // tags of the HMI: no driver
        h2 = rt.tag("h2", TagType::Float, 0);
        rt.tag("run", TagType::Bool, 0);
        rt.tag("s1", TagType::Float, 0);
        for(Tag* t : { b1, b2 }) b->watch(t);
        m->watch(m2);
    }
    ~Fixture() { transfers().clear(); }
    int load(const std::string& defs)
    {
        const json j = json::parse(defs);
        return transfers().load(&j, &drivers_cfg, rt.drivers);
    }
    // the main loop for a while: tag changes are delivered, the transfers look every 100 ms
    int run(uint32_t ms = 100)
    {
        int n = 0;
        for(uint32_t t = 0; t < ms; t += 100) { rt.pump(); now += 100; n += transfers().poll(now); rt.pump(); }
        return n;
    }
    // both PLCs connected and read once
    void online()
    {
        a->up(true); b->up(true); m->up(true);
        run();
        a->polled(); b->polled();
        run();
    }
    TransferState state(size_t i = 0) { return transfers().items()[i].state; }
};

TransferItem item(const char* text)
{
    TransferItem t;
    std::string err;
    REQUIRE_MESSAGE(transfer_parse(json::parse(text), t, &err), err);
    return t;
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("transfer: an entry as it is written; what is none says why")
{
    TransferItem t = item(R"({ "name": "x", "from": "a", "to": "b" })");
    CHECK(t.from == "a");
    CHECK(t.to == "b");
    CHECK_FALSE(t.cyclic);
    CHECK(t.enabled);
    CHECK_FALSE(t.clamp);
    CHECK(t.scale == 1);
    CHECK(t.offset == 0);
    CHECK(t.when.op == TransferCond::Op::None);
    t = item(R"({ "name": "x", "from": "a", "to": "b", "trigger": "cyclic", "every_ms": 250, "scale": 0.1, "offset": -5, "out_of_range": "clamp", "enabled": false,
                  "when": { "tag": "run", "ge": 2 } })");
    CHECK(t.cyclic);
    CHECK(t.every_ms == 250);
    CHECK(t.scale == 0.1);
    CHECK(t.offset == -5);
    CHECK(t.clamp);
    CHECK_FALSE(t.enabled);
    CHECK(t.when.op == TransferCond::Op::Ge);
    CHECK(t.when.tag == "run");
    CHECK(t.when.number == 2);
    CHECK(item(R"({ "name": "x", "from": "a", "to": "b", "when": "run" })").when.op == TransferCond::Op::Truthy);
    CHECK(item(R"({ "name": "x", "from": "a", "to": "b", "when": { "tag": "run" } })").when.op == TransferCond::Op::Truthy);
    CHECK(item(R"({ "name": "x", "from": "a", "to": "b", "when": { "tag": "mode", "eq": "auto" } })").when.is_text);
    CHECK(item(R"({ "name": "x", "from": "a", "to": "b", "when": { "tag": "t", "gt": { "tag": "limit" } } })").when.other_tag == "limit");

    TransferItem x;
    std::string err;
    for(const char* bad : { R"(5)", R"("a>b")", R"([])", R"({})", R"({ "from": "a", "to": "b" })", R"({ "name": "x", "to": "b" })", R"({ "name": "x", "from": "a" })",
                            R"({ "name": "x", "from": "a", "to": "a" })", R"({ "name": "x", "from": 5, "to": "b" })", R"({ "name": "x", "from": "a", "to": "" })",
                            R"({ "name": "x", "from": "a", "to": "sys.user.level" })", R"({ "name": "x", "from": "a", "to": "sys.sound.mute" })",
                            R"({ "name": "x", "from": "a", "to": "b", "trigger": "always" })", R"({ "name": "x", "from": "a", "to": "b", "every_ms": 5 })",
                            R"({ "name": "x", "from": "a", "to": "b", "every_ms": "fast" })", R"({ "name": "x", "from": "a", "to": "b", "every_ms": 1e12 })",
                            R"({ "name": "x", "from": "a", "to": "b", "scale": "2" })", R"({ "name": "x", "from": "a", "to": "b", "out_of_range": "wrap" })",
                            R"({ "name": "x", "from": "a", "to": "b", "enabled": "yes" })", R"({ "name": "x", "from": "a", "to": "b", "when": 5 })",
                            R"({ "name": "x", "from": "a", "to": "b", "when": { "eq": 1 } })", R"({ "name": "x", "from": "a", "to": "b", "when": { "tag": "r", "eq": 1, "gt": 0 } })",
                            R"({ "name": "x", "from": "a", "to": "b", "when": { "tag": "r", "like": 1 } })", R"({ "name": "x", "from": "a", "to": "b", "when": { "tag": "r", "eq": [1] } })" }) {
        CAPTURE(bad);
        err.clear();
        CHECK_FALSE(transfer_parse(json::parse(bad), x, &err));
        CHECK(err != "");
    }
    CHECK_FALSE(transfer_parse(json::parse(R"({ "name": ")" + std::string(100, 'n') + R"(", "from": "a", "to": "b" })"), x, &err));
}

TEST_CASE("transfer: on a change of the source - once, with the conversion to the type of the destination")
{
    Fixture f;
    Tag* txt = f.rt.tag("txt", "");
    Tag* flag = f.rt.tag("flag", TagType::Bool, 0);
    Tag* num = f.rt.tag("num", TagType::Int, 0);
    REQUIRE(f.load(R"([ { "name": "f2f", "from": "a1", "to": "b1" },
                         { "name": "f2i", "from": "a1", "to": "b2" },
                         { "name": "f2s", "from": "a1", "to": "txt" },
                         { "name": "f2b", "from": "a1", "to": "flag" },
                         { "name": "scaled", "from": "a2", "to": "h1", "scale": 0.1, "offset": 20 },
                         { "name": "s2i", "from": "txt", "to": "num" } ])") == 6);
    f.online();
    f.a->read(f.a1, 12.6);
    f.a->read(f.a2, 123);
    CHECK(f.run() == 6);                                         // (the text went on to the integer in the same look)
    CHECK(f.b1->get_number() == 12.6);
    CHECK(f.b2->get_number() == 13);                             // rounded to the integer
    CHECK(txt->get().format() == "12.6");
    CHECK(flag->get_number() == 1);
    CHECK(f.h1->get_number() == doctest::Approx(32.3));
    CHECK(num->get_number() == 13);                              // "12.6" is a number: rounded
    CHECK(f.b->writes == std::vector<double>{ 12.6, 13 });       // each once
    CHECK(f.run(2000) == 0);                                     // nothing changes: nothing is written
    f.a->read(f.a1, 0);
    f.run(200);
    CHECK(f.b1->get_number() == 0);
    CHECK(flag->get_number() == 0);
    CHECK(txt->get().format() == "0");
    CHECK(f.b->writes.size() == 4);
    // a text that is no number is not written anywhere
    txt->set_string("hot", nullptr);
    f.run(200);
    CHECK(num->get_number() == 0);
    CHECK(f.state(5) == TransferState::BadValue);
    txt->set_string(" 42 ", nullptr);
    f.run(200);
    CHECK(num->get_number() == 42);
    CHECK(f.state(5) == TransferState::Ok);
    for(const char* bad : { "", "12abc", "0x", "1,5", "--3" }) {
        CAPTURE(bad);
        txt->set_string(bad, nullptr);
        f.run(200);
        CHECK(num->get_number() == 42);
    }
}

TEST_CASE("transfer: cyclic - every period, and what the destination got from elsewhere is set right again")
{
    Fixture f;
    REQUIRE(f.load(R"([ { "name": "c", "from": "a1", "to": "b1", "trigger": "cyclic", "every_ms": 1000 } ])") == 1);
    f.online();
    f.a->read(f.a1, 5);
    f.run();
    CHECK(f.b1->get_number() == 5);
    CHECK(f.b->writes == std::vector<double>{ 5 });
    f.run(5000);
    CHECK(f.b->writes.size() == 1);                              // the destination has it: no write for the sake of writing
    // somebody changes the value at the destination device
    f.b->read(f.b1, 77);
    f.run(1000);
    CHECK(f.b1->get_number() == 5);                              // within the period
    CHECK(f.b->writes == std::vector<double>{ 5, 5 });
    // a change of the source does not wait for the period
    f.a->read(f.a1, 6);
    f.run(100);
    CHECK(f.b1->get_number() == 6);
    // a transfer on change leaves the destination alone when somebody else writes it
    REQUIRE(f.load(R"([ { "name": "c", "from": "a1", "to": "b1" } ])") == 1);
    f.b->writes.clear();
    f.run(300);
    f.b->read(f.b1, 77);
    f.run(5000);
    CHECK(f.b1->get_number() == 77);
    CHECK(f.b->writes.empty());
}

TEST_CASE("transfer: only while the condition holds; when it comes to hold, the value of that moment is written")
{
    Fixture f;
    Tag* run = tags().find("run");
    Tag* mode = f.rt.tag("mode", "manual");
    Tag* limit = f.rt.tag("limit", TagType::Float, 50);
    REQUIRE(f.load(R"([ { "name": "r", "from": "a1", "to": "b1", "when": "run" },
                         { "name": "m", "from": "a1", "to": "h1", "when": { "tag": "mode", "eq": "auto" } },
                         { "name": "l", "from": "a1", "to": "h2", "when": { "tag": "a1", "gt": { "tag": "limit" } } },
                         { "name": "n", "from": "a1", "to": "m2", "when": { "tag": "nowhere", "eq": 1 } } ])") == 4);
    f.online();
    f.a->read(f.a1, 10);
    f.run(500);
    CHECK(f.b1->get_number() == 0);
    CHECK(f.h1->get_number() == 0);
    CHECK(f.h2->get_number() == 0);
    CHECK(f.state(0) == TransferState::ConditionFalse);
    f.a->read(f.a1, 20);
    f.a->read(f.a1, 30);
    f.run(300);
    CHECK(f.b1->get_number() == 0);
    run->set_number(1, nullptr);
    f.run();
    CHECK(f.b1->get_number() == 30);                             // not 10, not 20
    CHECK(f.b->writes == std::vector<double>{ 30 });
    CHECK(f.state(0) == TransferState::Ok);
    run->set_number(0, nullptr);
    f.a->read(f.a1, 40);
    f.run(300);
    CHECK(f.b1->get_number() == 30);
    mode->set_string("auto", nullptr);
    f.run();
    CHECK(f.h1->get_number() == 40);
    f.a->read(f.a1, 60);                                         // above the limit tag
    f.run();
    CHECK(f.h2->get_number() == 60);
    limit->set_number(100, nullptr);
    f.a->read(f.a1, 70);
    f.run();
    CHECK(f.h2->get_number() == 60);
    CHECK(f.m2->get_number() == 0);                              // a condition on a tag that is not there never holds
    CHECK(f.m->writes.empty());
}

// ================================================================ security: what is written to a PLC

TEST_CASE("transfer: a value outside the limits of the destination, NaN and infinity are not written")
{
    Fixture f;
    Tag* big = f.rt.tag("big", TagType::Int, 0);
    REQUIRE(f.load(R"([ { "name": "skip", "from": "a1", "to": "b2" },
                         { "name": "clamp", "from": "a1", "to": "b3", "out_of_range": "clamp" },
                         { "name": "free", "from": "a1", "to": "b1" },
                         { "name": "int", "from": "a1", "to": "big" },
                         { "name": "over", "from": "a1", "to": "h1", "scale": 1e300 } ])") == 5);
    Tag* b3 = tags().find("b3");
    f.online();
    f.a->read(f.a1, 50);
    f.run();
    CHECK(f.b2->get_number() == 50);
    f.a->read(f.a1, 250);                                        // b2 is -100..100
    f.run();
    CHECK(f.b2->get_number() == 50);                             // left as it was: not 100, not 250
    CHECK(f.state(0) == TransferState::OutOfRange);
    CHECK(f.b1->get_number() == 250);                            // a tag without limits takes it
    f.a->read(f.a1, -100);
    f.run();
    CHECK(f.b2->get_number() == -100);
    CHECK(f.state(0) == TransferState::Ok);
    for(double bad : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
        f.a->read(f.a1, bad);
        f.run(200);
        CHECK(f.b1->get_number() == -100);
        CHECK(f.b2->get_number() == -100);
        CHECK(b3->get_number() == -100);
        CHECK(f.state(2) == TransferState::BadValue);
    }
    f.a->read(f.a1, 1e30);
    f.run(200);
    CHECK(f.b1->get_number() == 1e30);
    CHECK(big->get_number() == -100);                            // more than an integer holds: not written
    CHECK(f.state(3) == TransferState::OutOfRange);
    CHECK(f.h1->get_number() != 1e30);                           // 1e30 * 1e300: no number any more
    CHECK(std::isfinite(f.h1->get_number()));
    CHECK(f.state(4) == TransferState::BadValue);
    for(double v : f.b->writes) CHECK(std::isfinite(v));

    // "clamp": the nearest limit instead
    Tag* c = f.rt.tag("c", TagType::Float, 0, 0, 10);
    REQUIRE(f.load(R"([ { "name": "clamp", "from": "a2", "to": "c", "out_of_range": "clamp" } ])") == 1);
    f.online();
    f.a->read(f.a2, 99);
    f.run(200);
    CHECK(c->get_number() == 10);
    f.a->read(f.a2, -99);
    f.run(200);
    CHECK(c->get_number() == 0);
}

// ================================================================ reliability: connections

TEST_CASE("transfer: nothing is written before the source was read - not at the start, not what was read before a break")
{
    Fixture f;
    REQUIRE(f.load(R"([ { "name": "c", "from": "a1", "to": "b1", "trigger": "cyclic", "every_ms": 500 } ])") == 1);
    f.b->up(true);
    f.run();
    f.b->polled();
    f.b->read(f.b1, 33);                                         // what the destination holds
    // the source is not connected yet: its tag still has the 0 it started with
    f.run(3000);
    CHECK(f.b->writes.empty());
    CHECK(f.b1->get_number() == 33);
    CHECK(f.state() == TransferState::SourceOffline);
    CHECK(transfers().items()[0].from_driver == "plc_a");
    CHECK(transfers().items()[0].to_driver == "plc_b");
    // connected, but nothing read yet
    f.a->up(true);
    f.run(3000);
    CHECK(f.b->writes.empty());
    CHECK(f.state() == TransferState::SourceWait);
    f.a->read(f.a1, 5);
    f.a->polled();
    f.run();
    CHECK(f.b->writes == std::vector<double>{ 5 });
    CHECK(f.state() == TransferState::Ok);

    // the source is lost. Somebody sets the destination to 0 at its device: the old 5 is NOT written back
    f.a->up(false);
    f.b->read(f.b1, 0);
    f.run(10000);
    CHECK(f.b1->get_number() == 0);
    CHECK(f.b->writes.size() == 1);
    CHECK(f.state() == TransferState::SourceOffline);
    // it is back, the first round of reading is not finished: still not
    f.a->up(true);
    f.run(2000);
    CHECK(f.b1->get_number() == 0);
    CHECK(f.state() == TransferState::SourceWait);
    // the round is finished: the device holds 9 now
    f.a->read(f.a1, 9);
    f.a->polled();
    f.run();
    CHECK(f.b1->get_number() == 9);
    CHECK(f.b->writes == std::vector<double>{ 5, 9 });           // the 5 from before the break never went out again

    // lost and back between two looks (we never saw it offline): it waits all the same
    f.b->read(f.b1, 1);
    f.a->up(false);
    f.a->up(true);
    f.run(3000);
    CHECK(f.b1->get_number() == 1);
    CHECK(f.state() == TransferState::SourceWait);
    f.a->polled();                                               // (the value is still 9 at the device)
    f.run(600);
    CHECK(f.b1->get_number() == 9);
}

TEST_CASE("transfer: while the destination is away nothing is written; back, it gets the newest value - once")
{
    Fixture f;
    REQUIRE(f.load(R"([ { "name": "c", "from": "a1", "to": "b1" } ])") == 1);
    f.online();
    f.a->read(f.a1, 1);
    f.run();
    CHECK(f.b->writes == std::vector<double>{ 1 });
    f.b->up(false);
    for(double v : { 2.0, 3.0, 4.0 }) { f.a->read(f.a1, v); f.run(500); }
    CHECK(f.b->writes.size() == 1);                              // none of them was handed to the driver
    CHECK(f.b1->get_number() == 1);
    CHECK(f.state() == TransferState::DestOffline);
    // back. The device was switched off and on and holds 0 again.
    f.b->up(true);
    f.run(1000);
    CHECK(f.b->writes.size() == 1);
    CHECK(f.state() == TransferState::DestWait);
    f.b->read(f.b1, 0);
    f.b->polled();
    f.run();
    CHECK(f.b->writes == std::vector<double>{ 1, 4 });           // 2 and 3 are history
    CHECK(f.b1->get_number() == 4);
    // the device comes back holding what we wrote last: nothing to do
    f.b->up(false);
    f.run(300);
    f.b->up(true);
    f.b->polled();
    f.run(1000);
    CHECK(f.b->writes.size() == 2);
}

TEST_CASE("transfer: a driver that does not read in cycles - a value from the device, or two seconds, end the wait")
{
    Fixture f;
    REQUIRE(f.load(R"([ { "name": "in", "from": "m1", "to": "h1" }, { "name": "out", "from": "h2", "to": "m2" } ])") == 2);
    f.h2->set_number(7, nullptr);
    f.run(3000);
    CHECK(f.m->writes.empty());                                  // not connected
    CHECK(f.state(0) == TransferState::SourceOffline);
    CHECK(f.state(1) == TransferState::DestOffline);
    f.m->up(true);
    f.run(1000);
    CHECK(f.h1->get_number() == 0);
    CHECK(f.m->writes.empty());
    f.m->read(f.m1, 3);                                          // a message arrives
    f.run();
    CHECK(f.h1->get_number() == 3);
    f.run(1000);                                                 // two seconds after the connection: the other direction goes
    CHECK(f.m->writes == std::vector<double>{ 7 });
    // tags without a driver, and tags of a driver that has no connection to lose, are always there
    REQUIRE(f.load(R"([ { "name": "hh", "from": "h1", "to": "h2" }, { "name": "sim", "from": "s1", "to": "run" } ])") == 2);
    f.h1->set_number(11, nullptr);
    tags().find("s1")->set_number(1, nullptr);
    f.run();
    CHECK(f.h2->get_number() == 11);
    CHECK(tags().find("run")->get_number() == 1);
    CHECK(transfers().items()[1].from_driver == "");
}

TEST_CASE("transfer: a ring of transfers is found and not run; the others run")
{
    auto rings = [](const char* defs) {
        std::vector<TransferItem> items;
        for(const json& j : json::parse(defs)) { TransferItem t; REQUIRE(transfer_parse(j, t)); items.push_back(t); }
        return transfer_rings(items);
    };
    CHECK(rings(R"([ { "name": "1", "from": "a", "to": "b" }, { "name": "2", "from": "b", "to": "c" }, { "name": "3", "from": "a", "to": "c" } ])").empty());
    CHECK(rings(R"([ { "name": "1", "from": "a", "to": "b" }, { "name": "2", "from": "b", "to": "a" } ])") == std::vector<std::string>{ "1", "2" });
    CHECK(rings(R"([ { "name": "1", "from": "a", "to": "b" }, { "name": "2", "from": "b", "to": "c" }, { "name": "3", "from": "c", "to": "a" }, { "name": "4", "from": "c", "to": "d" },
                     { "name": "5", "from": "x", "to": "a" } ])") == std::vector<std::string>{ "1", "2", "3" });
    // a long chain is no ring, a long ring is one (and found quickly)
    std::string chain = "[", ring;
    for(int i = 0; i < 400; i++) chain += std::string(i ? "," : "") + "{ \"name\": \"n" + std::to_string(i) + "\", \"from\": \"t" + std::to_string(i) + "\", \"to\": \"t" + std::to_string(i + 1) + "\" }";
    ring = chain + ", { \"name\": \"back\", \"from\": \"t400\", \"to\": \"t0\" } ]";
    chain += "]";
    const uint32_t t0 = millis();
    CHECK(rings(chain.c_str()).empty());
    CHECK(rings(ring.c_str()).size() == 401);
    CHECK(millis() - t0 < 5000);

    Fixture f;
    REQUIRE(f.load(R"([ { "name": "ab", "from": "a1", "to": "b1" }, { "name": "ba", "from": "b1", "to": "a1" }, { "name": "ok", "from": "a2", "to": "b2" } ])") == 3);
    f.online();
    f.a->read(f.a1, 5);
    f.a->read(f.a2, 6);
    f.b->read(f.b1, 7);
    f.run(1000);
    CHECK(f.a1->get_number() == 5);
    CHECK(f.b1->get_number() == 7);
    CHECK(f.b2->get_number() == 6);
    CHECK(f.state(0) == TransferState::Ring);
    CHECK(f.state(1) == TransferState::Ring);
    CHECK(f.state(2) == TransferState::Ok);
    // a chain a -> b -> c goes through, one step per look
    REQUIRE(f.load(R"([ { "name": "1", "from": "a1", "to": "h1" }, { "name": "2", "from": "h1", "to": "h2" }, { "name": "3", "from": "h2", "to": "b3" } ])") == 3);
    f.online();
    f.a->read(f.a1, 42);
    f.run(500);
    CHECK(tags().find("b3")->get_number() == 42);
}

TEST_CASE("transfer: entries that are none, unknown tags, switched off, two with one name - the rest runs")
{
    Fixture f;
    CHECK(transfers().load(nullptr, nullptr, f.rt.drivers) == 0);
    const json not_list = json::parse(R"({ "name": "x" })");
    CHECK(transfers().load(&not_list, nullptr, f.rt.drivers) == 0);
    CHECK(transfers().poll(5) == 0);
    REQUIRE(f.load(R"([ 5, null, "x", {}, { "name": "bad", "from": "a1", "to": "a1" },
                         { "name": "ghost", "from": "nowhere", "to": "b1" }, { "name": "ghost2", "from": "a1", "to": "nowhere" },
                         { "name": "off", "from": "a1", "to": "b3", "enabled": false },
                         { "name": "ok", "from": "a1", "to": "b1" }, { "name": "ok", "from": "a2", "to": "b1" } ])") == 4);
    f.online();
    f.a->read(f.a1, 8);
    f.a->read(f.a2, 9);
    f.run(300);
    CHECK(f.b1->get_number() == 8);
    CHECK(tags().find("b3")->get_number() == 0);
    CHECK(f.state(0) == TransferState::NoTag);
    CHECK(f.state(1) == TransferState::NoTag);
    CHECK(f.state(2) == TransferState::Disabled);
    CHECK(f.state(3) == TransferState::Ok);
    CHECK(transfers().items()[3].written == 1);
    // the drivers go away (the project is loaded again): the transfers must be gone first, and nothing hangs on
    transfers().clear();
    f.a->read(f.a1, 1);
    f.rt.pump();
    CHECK(transfers().poll(f.now + 100) == 0);
    CHECK(f.b1->get_number() == 8);
    // the tags a driver has, by the "drivers" of the project
    const auto owners = transfer_tag_drivers(&f.drivers_cfg);
    auto owner = [&](const char* tag) { for(const auto& [t, d] : owners) if(t == tag) return d; return std::string("-"); };
    CHECK(owner("a1") == "plc_a");
    CHECK(owner("m1") == "mq");
    CHECK(owner("m2") == "mq");
    CHECK(owner("s1") == "-");                                   // a simulation has no connection to lose
    CHECK(owner("h1") == "-");
    const json odd = json::parse(R"([ 5, { "type": "modbus_tcp", "enabled": false, "map": [ { "tag": "x" } ] }, { "map": [ { "tag": "y" } ] }, { "type": "slmp", "map": [ 5, { "tag": 7 }, { "tag": "z" } ] } ])");
    const auto o2 = transfer_tag_drivers(&odd);
    REQUIRE(o2.size() == 1);
    CHECK(o2[0].first == "z");
    CHECK(o2[0].second == "slmp");
    CHECK(transfer_tag_drivers(nullptr).empty());
}

// ================================================================ performance efficiency

TEST_CASE("transfer: 100 transfers are looked at ten times a second without being felt; more than 500 are not taken")
{
    Fixture f;
    std::string defs = "[";
    for(int i = 0; i < 100; i++) {
        f.rt.tag("src" + std::to_string(i), TagType::Float, 0);
        f.rt.tag("dst" + std::to_string(i), TagType::Float, 0);
        defs += std::string(i ? "," : "") + "{ \"name\": \"t" + std::to_string(i) + "\", \"from\": \"src" + std::to_string(i) + "\", \"to\": \"dst" + std::to_string(i) + "\"" +
                (i % 2 ? ", \"trigger\": \"cyclic\", \"every_ms\": 100, \"when\": { \"tag\": \"run\", \"eq\": 0 }" : "") + " }";
    }
    defs += "]";
    REQUIRE(f.load(defs) == 100);
    f.run(200);
    // an hour of looks with nothing to do, then every source changing at every look
    const uint32_t t0 = millis();
    for(int i = 0; i < 36000; i++) transfers().poll(f.now += 100);
    const uint32_t idle = millis() - t0;
    const uint32_t t1 = millis();
    int written = 0;
    for(int k = 1; k <= 200; k++) {
        for(int i = 0; i < 100; i++) tags().find("src" + std::to_string(i))->set_number(k, nullptr);
        f.rt.pump();
        written += transfers().poll(f.now += 100);
    }
    const uint32_t busy = millis() - t1;
    CHECK(written == 100 * 200);
    CHECK(tags().find("dst99")->get_number() == 200);
    MESSAGE("100 transfers: " << (idle / 36000.0) << " ms per look with nothing to do, " << (busy / 200.0) << " ms per look with 100 writes");
    CHECK(idle < 20000);
    CHECK(busy < 20000);

    std::string many = "[";
    for(int i = 0; i < 600; i++) many += std::string(i ? "," : "") + "{ \"name\": \"t" + std::to_string(i) + "\", \"from\": \"src0\", \"to\": \"dst" + std::to_string(i % 100) + "\" }";
    many += "]";
    CHECK(f.load(many) == static_cast<int>(kTransferMax));
}

// ================================================================ usability: the page of the device

TEST_CASE("System > Transfers: what runs, and for what does not run, why")
{
    Fixture f;
    REQUIRE(f.load(R"([ { "name": "speed", "from": "a1", "to": "b1" }, { "name": "limit", "from": "a2", "to": "b2" }, { "name": "cond", "from": "a1", "to": "h1", "when": "run" },
                         { "name": "off", "from": "a1", "to": "h2", "enabled": false }, { "name": "mq", "from": "m1", "to": "b3" }, { "name": "six", "from": "h1", "to": "m2" },
                         { "name": "seven", "from": "s1", "to": "a3" } ])") == 7);
    SystemSettings& sys = system_settings();
    sys.add_page(make_transfers_page());
    sys.create_tags();
    auto text = [](const char* key) { return tags().find(std::string("sys.transfers.") + key)->get().format(); };
    auto on = [](const char* key) { return tags().find(std::string("sys.transfers.") + key)->get_number() != 0; };
    f.a->up(true); f.b->up(true);
    f.run();
    f.a->polled(); f.b->polled();
    f.a->read(f.a1, 5);
    f.a->read(f.a2, 500);
    f.run(300);
    sys.refresh("transfers");
    CHECK(text("n1") == "speed");
    CHECK(text("p1") == "a1 > b1");
    CHECK(text("s1") == "ok  1");
    CHECK_FALSE(on("b1"));
    CHECK(text("s2") == "out of range");
    CHECK(on("b2"));
    CHECK(text("s3") == "condition not met");
    CHECK_FALSE(on("b3"));                                       // (as it is meant to be: no warning)
    CHECK(text("s4") == "off");
    CHECK(text("s5") == "source offline (mq)");
    CHECK(on("b5"));
    CHECK(on("more"));
    CHECK(text("range") == "1 - 5 / 7    3 do not run");
    sys.command("transfers", "next");
    CHECK(text("n1") == "six");
    CHECK(text("s1") == "destination offline (mq)");
    CHECK(text("n2") == "seven");
    CHECK_FALSE(on("r3_used"));
    sys.command("transfers", "next");
    CHECK(text("n1") == "six");
    sys.command("transfers", "prev");
    sys.command("transfers", "prev");
    sys.command("transfers", "explode");
    CHECK(text("n1") == "speed");
    f.a->up(false);
    f.run(300);
    sys.refresh("transfers");
    CHECK(text("s1") == "source offline (plc_a)");
    for(const json& scr : sys.screens(480, 320)) REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen(SystemSettings::page_screen("transfers"), "none"));
    f.rt.advance(300);
    ui().unload_all();
    sys.clear();
}

// ================================================================ maintainability: the checker

TEST_CASE("transfer: --check finds unknown tags, a ring, a wrong trigger and a destination that is the runtime's")
{
    const fs::path dir = fs::temp_directory_path() / "hml_transfer_check_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "screens");
    auto problems = [&](const std::string& tr) {
        { std::ofstream o(dir / "project.json", std::ios::binary);
          o << "{ \"start_screen\": \"s\", \"tags\": [ { \"name\": \"a\", \"type\": \"int\" }, { \"name\": \"b\", \"type\": \"int\" }, { \"name\": \"c\", \"type\": \"int\" } ], \"transfers\": " << tr << ", \"screens\": [\"screens/s.json\"] }"; }
        { std::ofstream o(dir / "screens" / "s.json", std::ios::binary); o << "{ \"name\": \"s\", \"children\": [] }"; }
        std::string all;
        for(const CheckProblem& p : check_project((dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems(R"([ { "name": "1", "from": "a", "to": "b", "trigger": "cyclic", "every_ms": 500, "scale": 2, "when": { "tag": "c", "gt": 0 } }, { "name": "2", "from": "b", "to": "c", "out_of_range": "clamp" } ])") == "");
    CHECK(problems("{}").find("transfers: a list is needed") != std::string::npos);
    const std::string out = problems(R"([ { "name": "1", "from": "a", "to": "b" }, { "name": "2", "from": "b", "to": "a" }, { "name": "3", "from": "x", "to": "y", "when": "z" },
        { "name": "3", "from": "a", "to": "c" }, { "name": "5", "from": "a", "to": "c", "trigger": "often" }, { "name": "6", "from": "a", "to": "sys.user.level" },
        { "name": "7", "from": "a", "to": "c", "every_ms": 500, "speed": 1 }, 5 ])");
    CHECK(out.find("transfers[0] (1): part of a ring") != std::string::npos);
    CHECK(out.find("transfers[1] (2): part of a ring") != std::string::npos);
    CHECK(out.find("transfers[2] (3): from: unknown tag \"x\"") != std::string::npos);
    CHECK(out.find("transfers[2] (3): to: unknown tag \"y\"") != std::string::npos);
    CHECK(out.find("transfers[2] (3): when: unknown tag \"z\"") != std::string::npos);
    CHECK(out.find("transfers[3] (3): the name is used twice") != std::string::npos);
    CHECK(out.find("transfers[4] (5): \"trigger\" is") != std::string::npos);
    CHECK(out.find("transfers[5] (6): \"to\" is a tag of the runtime") != std::string::npos);
    CHECK(out.find("transfers[6] (7): unknown key \"speed\"") != std::string::npos);
    CHECK(out.find("transfers[6] (7): \"every_ms\" has no meaning") != std::string::npos);
    CHECK(out.find("transfers[7]: a transfer is") != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
