// Data drivers: pluggable sources/sinks that move values between the tag store and
// the outside world (PLC, simulation, ...). Each driver is created from one object of
// the project's "drivers" array, keyed by its "type".
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

class Driver {
public:
    virtual ~Driver() = default;
    virtual const char* type() const = 0;
    virtual bool start() = 0;    // UI thread, LVGL is initialised
    virtual void stop() = 0;     // UI thread, LVGL still alive

    const std::string& name() const { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

private:
    std::string name_;
};

// Returns nullptr when the configuration is unusable.
using DriverFactory = std::function<std::unique_ptr<Driver>(const json& cfg)>;

class DriverRegistry {
public:
    void register_type(std::string type, DriverFactory factory);
    void register_builtins();

    // Create and start every driver of the JSON array. Returns the number started.
    int  load(const json* array);
    void stop_all();

private:
    std::vector<std::pair<std::string, DriverFactory>> factories_;
    std::vector<std::unique_ptr<Driver>> drivers_;
};

DriverRegistry& drivers();

// Built-in drivers
std::unique_ptr<Driver> make_sim_driver(const json& cfg);
std::unique_ptr<Driver> make_modbus_tcp_driver(const json& cfg);
std::unique_ptr<Driver> make_opcua_server_driver(const json& cfg);   // returns nullptr when built without HML_OPCUA

} // namespace hml
