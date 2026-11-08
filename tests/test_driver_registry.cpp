#include <doctest.h>
#include "driver.hpp"
#include "tag.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace hml;

namespace {

// Records lifecycle events into a shared log so the tests can check ordering.
struct Log {
    std::vector<std::string> events;
};

class FakeDriver final : public Driver {
public:
    FakeDriver(Log& log, std::string id, bool start_ok) : log_(log), id_(std::move(id)), start_ok_(start_ok) {}
    ~FakeDriver() override { log_.events.push_back("destroy " + id_); }
    const char* type() const override { return "fake"; }
    bool start() override { log_.events.push_back("start " + id_ + " as " + name()); return start_ok_; }
    void stop() override { log_.events.push_back("stop " + id_); }

private:
    Log&        log_;
    std::string id_;
    bool        start_ok_;
};

// Factory: cfg.id names the instance, cfg.fail_create -> nullptr, cfg.fail_start -> start() false.
DriverFactory fake_factory(Log& log)
{
    return [&log](const json& cfg) -> std::unique_ptr<Driver> {
        if(jbool(cfg, "fail_create", false)) return nullptr;
        return std::make_unique<FakeDriver>(log, jstr(cfg, "id", "?"), !jbool(cfg, "fail_start", false));
    };
}

} // namespace

TEST_CASE("DriverRegistry::load ignores unusable input")
{
    Log log;
    DriverRegistry reg;
    reg.register_type("fake", fake_factory(log));

    CHECK(reg.load(nullptr) == 0);
    json obj = json::object();
    CHECK(reg.load(&obj) == 0);
    json empty = json::array();
    CHECK(reg.load(&empty) == 0);
    CHECK(log.events.empty());
}

TEST_CASE("DriverRegistry::load starts the drivers it can and skips the rest")
{
    Log log;
    DriverRegistry reg;
    reg.register_type("fake", fake_factory(log));

    json arr = json::parse(R"([
        { "type": "fake", "id": "a" },
        { "id": "no-type" },
        { "type": "fake", "id": "off", "enabled": false },
        { "type": "nope", "id": "unknown" },
        { "type": "fake", "id": "bad-cfg", "fail_create": true },
        { "type": "fake", "id": "bad-start", "fail_start": true },
        { "type": "FAKE", "id": "b", "name": "second" }
    ])");
    CHECK(reg.load(&arr) == 2);

    REQUIRE(log.events.size() == 4);
    CHECK(log.events[0] == "start a as fake");           // name defaults to the type
    CHECK(log.events[1] == "start bad-start as fake");
    CHECK(log.events[2] == "destroy bad-start");         // a driver that fails to start is dropped
    CHECK(log.events[3] == "start b as second");         // explicit name, type matched case-insensitively

    SUBCASE("stop_all stops in reverse order and destroys the drivers") {
        log.events.clear();
        reg.stop_all();
        REQUIRE(log.events.size() == 4);
        CHECK(log.events[0] == "stop b");
        CHECK(log.events[1] == "stop a");
        CHECK(log.events[2] == "destroy a");
        CHECK(log.events[3] == "destroy b");

        log.events.clear();
        reg.stop_all();                                  // idempotent
        CHECK(log.events.empty());
    }
    reg.stop_all();
}

TEST_CASE("enabled accepts the usual truthy spellings")
{
    Log log;
    DriverRegistry reg;
    reg.register_type("fake", fake_factory(log));
    json arr = json::parse(R"([
        { "type": "fake", "id": "a", "enabled": "false" },
        { "type": "fake", "id": "b", "enabled": 0 },
        { "type": "fake", "id": "c", "enabled": "TRUE" },
        { "type": "fake", "id": "d", "enabled": 1 }
    ])");
    CHECK(reg.load(&arr) == 2);
    reg.stop_all();
}

TEST_CASE("the first registered factory for a type wins")
{
    Log log;
    DriverRegistry reg;
    int first = 0, second = 0;
    reg.register_type("x", [&](const json&) -> std::unique_ptr<Driver> { first++; return std::make_unique<FakeDriver>(log, "1", true); });
    reg.register_type("x", [&](const json&) -> std::unique_ptr<Driver> { second++; return std::make_unique<FakeDriver>(log, "2", true); });
    json arr = json::parse(R"([{ "type": "x" }])");
    CHECK(reg.load(&arr) == 1);
    CHECK(first == 1);
    CHECK(second == 0);
    reg.stop_all();
}

TEST_CASE("stop_all drops the drivers' tag subscriptions but keeps the UI's")
{
    tags().clear();
    Tag* t = tags().create("t", TagType::Int, nullptr, false, 0, 0);
    int drv_hits = 0, ui_hits = 0;
    t->subscribe(SubGroup::Driver, [&](Tag&, const Value&, const void*) { drv_hits++; });
    t->subscribe(SubGroup::UI, [&](Tag&, const Value&, const void*) { ui_hits++; });

    DriverRegistry reg;
    reg.stop_all();

    t->set_number(1, nullptr);
    tags().dispatch(false);
    CHECK(drv_hits == 0);
    CHECK(ui_hits == 1);

    tags().unsubscribe_group(SubGroup::UI);
    tags().clear();
}

TEST_CASE("built-in drivers reject configurations without their item lists")
{
    tags().clear();
    DriverRegistry reg;
    reg.register_builtins();
    json arr = json::parse(R"([
        { "type": "sim" },
        { "type": "modbus_tcp", "host": "127.0.0.1" },
        { "type": "modbus_tcp", "map": "not an array" }
    ])");
    CHECK(reg.load(&arr) == 0);
    reg.stop_all();
}

TEST_CASE("the global drivers() registry is a singleton")
{
    CHECK(&drivers() == &drivers());
}
