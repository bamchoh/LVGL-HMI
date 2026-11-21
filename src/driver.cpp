#include "driver.hpp"
#include "tag.hpp"
#include "util.hpp"

namespace hml {

void DriverRegistry::register_type(std::string type, DriverFactory factory)
{
    factories_.emplace_back(std::move(type), std::move(factory));
}

void DriverRegistry::register_builtins()
{
    register_type("sim", make_sim_driver);
    register_type("modbus_tcp", make_modbus_tcp_driver);
    register_type("modbus_rtu", make_modbus_rtu_driver);
    register_type("modbus_ascii", make_modbus_ascii_driver);
    register_type("slmp", make_slmp_driver);
    register_type("fins", make_fins_driver);
    register_type("mqtt", make_mqtt_driver);
    register_type("opcua_server", make_opcua_server_driver);
    register_type("opcua", make_opcua_server_driver);
    register_type("opcua_client", make_opcua_client_driver);
}

int DriverRegistry::load(const json* array)
{
    if(!array || !array->is_array()) return 0;
    int started = 0;
    for(const json& cfg : *array) {
        auto type = jstr_opt(cfg, "type");
        if(!type) { HML_WARN("driver without \"type\" skipped"); continue; }
        if(!jbool(cfg, "enabled", true)) { HML_INFO("driver '%s' disabled", type->c_str()); continue; }

        const DriverFactory* factory = nullptr;
        for(const auto& f : factories_) {
            if(iequals(f.first, *type)) { factory = &f.second; break; }
        }
        if(!factory) { HML_WARN("unknown driver type '%s'", type->c_str()); continue; }

        std::unique_ptr<Driver> d = (*factory)(cfg);
        if(!d) { HML_WARN("driver '%s' failed to initialise", type->c_str()); continue; }
        d->set_name(jstr(cfg, "name", *type));
        if(!d->start()) { HML_WARN("driver '%s' failed to start", d->name().c_str()); continue; }
        HML_INFO("driver '%s' (%s) started", d->name().c_str(), d->type());
        drivers_.push_back(std::move(d));
        started++;
    }
    return started;
}

void DriverRegistry::stop_all()
{
    for(auto it = drivers_.rbegin(); it != drivers_.rend(); ++it) (*it)->stop();
    tags().unsubscribe_group(SubGroup::Driver);
    drivers_.clear();
}

std::vector<Driver*> DriverRegistry::all() const
{
    std::vector<Driver*> out;
    for(const auto& d : drivers_) out.push_back(d.get());
    return out;
}

// ---------------------------------------------------------------- addresses without a tag

uint64_t Driver::device_post(const DeviceRequest& r)
{
    std::lock_guard<std::mutex> lock(device_lock_);
    if(device_in_.size() >= 16) return 0;
    device_in_.emplace_back(++device_id_, r);
    return device_id_;
}

bool Driver::device_take(uint64_t id, DeviceResult& out)
{
    std::lock_guard<std::mutex> lock(device_lock_);
    for(auto it = device_out_.begin(); it != device_out_.end(); ++it) {
        if(it->first != id) continue;
        out = std::move(it->second);
        device_out_.erase(it);
        return true;
    }
    return false;
}

bool Driver::device_next(uint64_t& id, DeviceRequest& r)
{
    std::lock_guard<std::mutex> lock(device_lock_);
    if(device_in_.empty()) return false;
    id = device_in_.front().first;
    r = std::move(device_in_.front().second);
    device_in_.pop_front();
    return true;
}

void Driver::device_done(uint64_t id, DeviceResult r)
{
    // (an error may quote what was typed: never more than a screen can show)
    if(r.error.size() > 200) r.error = r.error.substr(0, 200) + " ...";
    std::lock_guard<std::mutex> lock(device_lock_);
    device_out_.emplace_back(id, std::move(r));
    while(device_out_.size() > 32) device_out_.pop_front();       // (answers nobody came for)
}

void Driver::device_fail_all(const std::string& why)
{
    std::lock_guard<std::mutex> lock(device_lock_);
    for(auto& q : device_in_) {
        DeviceResult r;
        r.error = why;
        device_out_.emplace_back(q.first, std::move(r));
    }
    device_in_.clear();
    while(device_out_.size() > 32) device_out_.pop_front();
}

DriverRegistry& drivers()
{
    static DriverRegistry registry;
    return registry;
}

} // namespace hml
