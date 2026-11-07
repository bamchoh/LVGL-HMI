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
    register_type("opcua_server", make_opcua_server_driver);
    register_type("opcua", make_opcua_server_driver);
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

DriverRegistry& drivers()
{
    static DriverRegistry registry;
    return registry;
}

} // namespace hml
