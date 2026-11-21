// Addresses without a tag (System > Device) against another implementation of the protocols:
// the PLC simulator Karakuri. Skipped unless tools/karakuri_check.py runs it ("device"): the
// script puts the values into the simulator, names the ports in the environment, and looks at
// what was written afterwards.
#include <doctest.h>
#include "driver.hpp"
#include "lvgl_fixture.hpp"
#include "tag.hpp"
#include "util.hpp"

#include <cstdlib>

using namespace hml;
using hmltest::Runtime;

namespace {

using Strings = std::vector<std::string>;

int env_port(const char* name)
{
    const char* v = std::getenv(name);
    return v && *v ? std::atoi(v) : 0;
}

struct Link {
    Runtime rt;
    Driver* d = nullptr;
    explicit Link(json cfg)
    {
        rt.tag("online", TagType::Bool);
        cfg["host"] = "127.0.0.1";
        cfg["status_tag"] = "online";
        cfg["map"] = json::array();
        json arr = json::array({ cfg });
        REQUIRE(rt.drivers.load(&arr) == 1);
        d = rt.drivers.all().at(0);
        REQUIRE(wait([] { return tags().find("online")->get_number() == 1; }));
    }
    template <class Pred>
    static bool wait(Pred pred, uint32_t timeout_ms = 4000)
    {
        const uint32_t t0 = millis();
        for(;;) {
            tags().dispatch(false);
            if(pred()) return true;
            if(millis() - t0 > timeout_ms) return false;
            sleep_ms(5);
        }
    }
    DeviceResult run(const DeviceRequest& q)
    {
        const uint64_t id = d->device_post(q);
        REQUIRE(id != 0);
        DeviceResult r;
        REQUIRE(wait([&] { return d->device_take(id, r); }));
        return r;
    }
    Strings ask(const char* address, const char* type = "", int count = 1)
    {
        DeviceRequest q;
        q.address = address; q.type = type; q.count = count;
        const DeviceResult r = run(q);
        if(!r.ok) MESSAGE(address << ": " << r.error);
        return r.values;
    }
    DeviceResult put(const char* address, const char* value, const char* type = "")
    {
        DeviceRequest q;
        q.address = address; q.type = type; q.write = true; q.value = value;
        const DeviceResult r = run(q);
        if(!r.ok) MESSAGE(address << ": " << r.error);
        return r;
    }
};

} // namespace

TEST_CASE("karakuri: SLMP addresses without a tag")
{
    const int port = env_port("HML_KARAKURI_SLMP");
    if(!port) return;
    Link l(json{ { "type", "slmp" }, { "port", port } });
    CHECK(l.ask("D400") == Strings{ "-1234" });
    CHECK(l.ask("D402", "int32") == Strings{ "100000" });
    CHECK(l.ask("D400", "int16", 4).size() == 4);
    CHECK(l.ask("D404", "float") == Strings{ "12.5" });
    CHECK(l.ask("M40", "", 3) == Strings{ "1", "0", "1" });
    CHECK(l.ask("M40", "", 4).size() == 4);
    CHECK(l.ask("D400.1") == Strings{ "1" });                // -1234 = 0xFB2E
    CHECK(l.put("D410", "4321").ok);
    CHECK(l.put("D412", "-70000", "int32").ok);
    CHECK(l.put("M45", "1").ok);
    CHECK(l.put("M40", "0").ok);
}

TEST_CASE("karakuri: FINS addresses without a tag")
{
    const int port = env_port("HML_KARAKURI_FINS");
    if(!port) return;
    Link l(json{ { "type", "fins" }, { "port", port } });
    CHECK(l.ask("D400") == Strings{ "-1234" });
    CHECK(l.ask("D402", "int32") == Strings{ "100000" });
    CHECK(l.ask("D404", "float") == Strings{ "12.5" });
    CHECK(l.ask("D400", "hex", 2).size() == 2);
    CHECK(l.ask("CIO40.00", "", 3) == Strings{ "1", "0", "1" });
    CHECK(l.put("D410", "4321").ok);
    CHECK(l.put("D412", "-70000", "int32").ok);
    CHECK(l.put("CIO41.02", "1").ok);
}

TEST_CASE("karakuri: Modbus addresses without a tag")
{
    const int port = env_port("HML_KARAKURI_MODBUS");
    if(!port) return;
    Link l(json{ { "type", "modbus_tcp" }, { "port", port }, { "unit_id", 1 } });
    CHECK(l.ask("holding:30") == Strings{ "1234" });
    CHECK(l.ask("holding:31", "uint32") == Strings{ "100000" });
    CHECK(l.ask("holding:33", "float") == Strings{ "12.5" });
    CHECK(l.ask("holding:30", "uint16", 3).size() == 3);
    CHECK(l.ask("input:8") == Strings{ "777" });
    CHECK(l.ask("coil:30", "", 3) == Strings{ "1", "0", "1" });
    CHECK(l.ask("discrete:9") == Strings{ "1" });
    CHECK(l.put("holding:40", "4321").ok);
    CHECK(l.put("holding:41", "-70000", "int32").ok);
    CHECK(l.put("coil:35", "1").ok);
    CHECK(l.put("coil:30", "0").ok);
}
