// Data drivers: pluggable sources/sinks that move values between the tag store and
// the outside world (PLC, simulation, ...). Each driver is created from one object of
// the project's "drivers" array, keyed by its "type".
#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "device_access.hpp"
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

    // ---- addresses that no tag is mapped to (System > Device, device_access.hpp)
    // A driver that can do it answers true and looks at the requests in its own thread, between
    // its writes and its reads: device_next() ... device_done().
    virtual bool device_access() const { return false; }
    virtual std::string device_help() const { return ""; }      // how an address is written, in a line
    // UI thread. The number to ask for the answer with; 0 = too many requests are waiting.
    uint64_t device_post(const DeviceRequest& r);
    // UI thread. True once the request is done (the answer is handed out once).
    bool     device_take(uint64_t id, DeviceResult& out);

protected:
    bool device_next(uint64_t& id, DeviceRequest& r);           // the driver's thread
    void device_done(uint64_t id, DeviceResult r);
    void device_fail_all(const std::string& why);               // no connection: nobody waits in vain

private:
    std::string name_;
    std::mutex  device_lock_;
    std::deque<std::pair<uint64_t, DeviceRequest>> device_in_;
    std::deque<std::pair<uint64_t, DeviceResult>>  device_out_;
    uint64_t    device_id_ = 0;
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
    // The drivers that run, in the order of the project.
    std::vector<Driver*> all() const;
    // One more, already started (tests).
    void add(std::unique_ptr<Driver> d) { drivers_.push_back(std::move(d)); }

private:
    std::vector<std::pair<std::string, DriverFactory>> factories_;
    std::vector<std::unique_ptr<Driver>> drivers_;
};

DriverRegistry& drivers();

// Built-in drivers
std::unique_ptr<Driver> make_sim_driver(const json& cfg);
std::unique_ptr<Driver> make_modbus_tcp_driver(const json& cfg);
std::unique_ptr<Driver> make_modbus_rtu_driver(const json& cfg);     // serial line (or through a TCP gateway)
std::unique_ptr<Driver> make_modbus_ascii_driver(const json& cfg);
std::unique_ptr<Driver> make_fins_driver(const json& cfg);          // OMRON, FINS/TCP
std::unique_ptr<Driver> make_slmp_driver(const json& cfg);          // Mitsubishi MELSEC, SLMP / MC protocol 3E / 4E over TCP
std::unique_ptr<Driver> make_mqtt_driver(const json& cfg);          // MQTT 3.1.1 client (TLS by default)
std::unique_ptr<Driver> make_opcua_server_driver(const json& cfg);   // returns nullptr when built without HML_OPCUA
std::unique_ptr<Driver> make_opcua_client_driver(const json& cfg);   // nodes of another OPC UA server as tags (same)

} // namespace hml
