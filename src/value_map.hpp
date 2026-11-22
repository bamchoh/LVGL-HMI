// "map" of a binding or a table cell: a value of a tag -> something (a color, a text).
//
//   "map": { "0": "#2e7d32", "1": "#c62828", "true": ..., "main": ..., "default": ... }
//
// A key that is a number (or true / false = 1 / 0) matches a number tag of that value; every key
// also matches a string tag whose text is that key ("main" for sys.screen). "default": the rest.
#pragma once
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "tag.hpp"

namespace hml {

template<class T>
struct ValueMap {
    std::vector<std::pair<double, T>>      by_number;
    std::vector<std::pair<std::string, T>> by_text;
    std::optional<T>                       fallback;

    bool empty() const { return by_number.empty() && by_text.empty() && !fallback; }

    void add(const std::string& key, T v)
    {
        if(key == "default") { fallback = std::move(v); return; }
        char* end = nullptr;
        const double n = std::strtod(key.c_str(), &end);
        if(!key.empty() && end && *end == '\0') by_number.emplace_back(n, v);
        else if(key == "true") by_number.emplace_back(1, v);
        else if(key == "false") by_number.emplace_back(0, v);
        by_text.emplace_back(key, std::move(v));
    }

    // what the value maps to, or the default (nullptr: neither)
    const T* find(const Value& v) const
    {
        if(!v.is_string()) {
            const double n = v.as_number();
            for(const auto& m : by_number) if(m.first == n) return &m.second;
        }
        const std::string s = v.format();
        for(const auto& m : by_text) if(m.first == s) return &m.second;
        return fallback ? &*fallback : nullptr;
    }
};

} // namespace hml
