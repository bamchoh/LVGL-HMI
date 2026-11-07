#include "jsonutil.hpp"
#include "util.hpp"
#include <cstdlib>

namespace hml {

const json* jget(const json& o, const char* key)
{
    if(!o.is_object()) return nullptr;
    auto it = o.find(key);
    return it == o.end() ? nullptr : &*it;
}

const json* jget(const json* o, const char* key)
{
    return o ? jget(*o, key) : nullptr;
}

std::string jstr(const json& o, const char* key, const std::string& dflt)
{
    const json* v = jget(o, key);
    return (v && v->is_string()) ? v->get<std::string>() : dflt;
}

std::optional<std::string> jstr_opt(const json& o, const char* key)
{
    const json* v = jget(o, key);
    if(v && v->is_string()) return v->get<std::string>();
    return std::nullopt;
}

double jnum(const json& o, const char* key, double dflt)
{
    const json* v = jget(o, key);
    return v ? jscalar_to_number(*v, dflt) : dflt;
}

bool jbool(const json& o, const char* key, bool dflt)
{
    const json* v = jget(o, key);
    if(!v) return dflt;
    if(v->is_boolean()) return v->get<bool>();
    if(v->is_number()) return v->get<double>() != 0;
    if(v->is_string()) return iequals(v->get_ref<const std::string&>(), "true");
    return dflt;
}

bool jhas(const json& o, const char* key)
{
    return jget(o, key) != nullptr;
}

std::string jscalar_to_string(const json& v)
{
    if(v.is_string()) return v.get<std::string>();
    if(v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if(v.is_number()) return vformat("%g", v.get<double>());
    return "";
}

double jscalar_to_number(const json& v, double dflt)
{
    if(v.is_number()) return v.get<double>();
    if(v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if(v.is_string()) return std::atof(v.get_ref<const std::string&>().c_str());
    return dflt;
}

} // namespace hml
