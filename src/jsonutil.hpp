// nlohmann::json plus tolerant accessors (missing keys / wrong types fall back to defaults).
#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace hml {

using json = nlohmann::json;

// Child by key, or nullptr when absent or when `o` is not an object.
const json* jget(const json& o, const char* key);
const json* jget(const json* o, const char* key);

std::string                jstr(const json& o, const char* key, const std::string& dflt);
std::optional<std::string> jstr_opt(const json& o, const char* key);
double                     jnum(const json& o, const char* key, double dflt);   // number, bool or numeric string
bool                       jbool(const json& o, const char* key, bool dflt);
bool                       jhas(const json& o, const char* key);

// Scalar to text: strings as-is, numbers with %g, booleans as true/false, otherwise "".
std::string jscalar_to_string(const json& v);
double      jscalar_to_number(const json& v, double dflt = 0);

} // namespace hml
