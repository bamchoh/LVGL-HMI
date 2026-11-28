#include "locale_fmt.hpp"

#include <cctype>

#include "util.hpp"

namespace hml {
namespace {

DisplayLocale g_locale;

bool digits(const std::string& s, size_t at, size_t n)
{
    if(at + n > s.size()) return false;
    for(size_t i = 0; i < n; i++) if(!std::isdigit(static_cast<unsigned char>(s[at + i]))) return false;
    return true;
}

} // namespace

void set_display_locale(const json* section)
{
    g_locale = DisplayLocale{};
    if(!section || !section->is_object()) return;
    const std::string date = jstr(*section, "date", "ymd");
    if(iequals(date, "dmy")) g_locale.date = DisplayLocale::Date::DMY;
    else if(iequals(date, "mdy")) g_locale.date = DisplayLocale::Date::MDY;
    else if(!iequals(date, "ymd")) HML_WARN("locale: date \"%s\" is not ymd, dmy or mdy", date.c_str());
    const std::string dec = jstr(*section, "decimal", ".");
    if(dec == ",") g_locale.decimal = ',';
    else if(dec != ".") HML_WARN("locale: decimal \"%s\" is not \".\" or \",\"", dec.c_str());
}

const DisplayLocale& display_locale() { return g_locale; }

std::string local_time_text(const std::string& iso)
{
    const DisplayLocale::Date order = g_locale.date;
    if(order == DisplayLocale::Date::YMD) return iso;
    // "YYYY-MM-DD..." : the whole date
    if(digits(iso, 0, 4) && iso.size() >= 10 && iso[4] == '-' && digits(iso, 5, 2) && iso[7] == '-' && digits(iso, 8, 2)) {
        const std::string y = iso.substr(0, 4), m = iso.substr(5, 2), d = iso.substr(8, 2), rest = iso.substr(10);
        return (order == DisplayLocale::Date::DMY ? d + "." + m + "." + y : m + "/" + d + "/" + y) + rest;
    }
    // "MM-DD ..." or "MM-DD" : a date without its year (lists, where the width matters)
    if(digits(iso, 0, 2) && iso.size() >= 5 && iso[2] == '-' && digits(iso, 3, 2) && (iso.size() == 5 || iso[5] == ' ')) {
        const std::string m = iso.substr(0, 2), d = iso.substr(3, 2), rest = iso.substr(5);
        return (order == DisplayLocale::Date::DMY ? d + "." + m + "." : m + "/" + d) + rest;
    }
    return iso;
}

std::string local_number_text(const std::string& text)
{
    if(g_locale.decimal == '.') return text;
    std::string out = text;
    for(size_t i = 1; i + 1 < out.size(); i++)
        if(out[i] == '.' && std::isdigit(static_cast<unsigned char>(out[i - 1])) && std::isdigit(static_cast<unsigned char>(out[i + 1]))) {
            out[i] = g_locale.decimal;
            break;                                   // (a number has one; "1.2.3" is not a number)
        }
    return out;
}

std::string plain_number_text(const std::string& typed)
{
    if(g_locale.decimal == '.') return typed;
    std::string out = typed;
    for(char& c : out) if(c == g_locale.decimal) c = '.';
    return out;
}

} // namespace hml
