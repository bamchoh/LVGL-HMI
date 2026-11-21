#include "device_access.hpp"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "util.hpp"

namespace hml {
namespace {

std::string trimmed(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// A whole number, the text used up. Base 16 takes an optional "0x".
bool whole(const std::string& text, int base, long long& out)
{
    const std::string s = trimmed(text);
    if(s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, base);
    return errno == 0 && end && *end == '\0';
}

} // namespace

const std::vector<std::string>& device_types()
{
    static const std::vector<std::string> t = { "int16", "uint16", "int32", "uint32", "float", "hex" };
    return t;
}

int device_type_words(const std::string& type)
{
    if(type == "int16" || type == "uint16" || type == "hex") return 1;
    if(type == "int32" || type == "uint32" || type == "float") return 2;
    return 0;
}

std::string device_format(const std::string& type, const uint16_t* w)
{
    const uint32_t u32 = (static_cast<uint32_t>(w[1]) << 16) | w[0];
    if(type == "int16") return std::to_string(static_cast<int16_t>(w[0]));
    if(type == "uint16") return std::to_string(w[0]);
    if(type == "hex") return vformat("%04X", w[0]);
    if(type == "int32") return std::to_string(static_cast<int32_t>(u32));
    if(type == "uint32") return std::to_string(u32);
    if(type == "float") { float f; std::memcpy(&f, &u32, 4); return vformat("%g", static_cast<double>(f)); }
    return "";
}

bool device_parse(const std::string& type, const std::string& text, uint16_t* w, std::string* err)
{
    auto fail = [&](const std::string& why) { if(err) *err = why; return false; };
    long long v = 0;
    uint32_t u32 = 0;
    if(type == "float") {
        const std::string s = trimmed(text);
        errno = 0;
        char* end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if(s.empty() || errno != 0 || !end || *end != '\0' || !std::isfinite(d)) return fail("\"" + text + "\" is not a number");
        const float f = static_cast<float>(d);
        if(!std::isfinite(f)) return fail("too large for a float");
        std::memcpy(&u32, &f, 4);
    }
    else if(type == "hex") {
        if(!whole(text, 16, v) || v < 0 || v > 0xFFFF) return fail("a word in hexadecimal is 0000 .. FFFF");
        u32 = static_cast<uint32_t>(v);
    }
    else {
        long long lo = 0, hi = 0;
        if(type == "int16") { lo = -32768; hi = 32767; }
        else if(type == "uint16") { lo = 0; hi = 65535; }
        else if(type == "int32") { lo = -2147483648LL; hi = 2147483647LL; }
        else if(type == "uint32") { lo = 0; hi = 4294967295LL; }
        else return fail("unknown type '" + type + "'");
        if(!whole(text, 10, v)) return fail("\"" + text + "\" is not a whole number");
        if(v < lo || v > hi) return fail(vformat("%s is %lld .. %lld", type.c_str(), lo, hi));
        u32 = static_cast<uint32_t>(v);
    }
    w[0] = static_cast<uint16_t>(u32 & 0xFFFF);
    w[1] = static_cast<uint16_t>(u32 >> 16);
    return true;
}

bool device_parse_bit(const std::string& text, bool& on)
{
    const std::string s = trimmed(text);
    if(s == "1" || iequals(s, "true") || iequals(s, "on")) { on = true; return true; }
    if(s == "0" || iequals(s, "false") || iequals(s, "off")) { on = false; return true; }
    return false;
}

} // namespace hml
