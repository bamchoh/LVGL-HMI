// Small shared helpers: case-insensitive compare, monotonic clock, logging.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace hml {

bool     iequals(std::string_view a, std::string_view b);
uint32_t millis();                 // monotonic, wraps after ~49 days (only differences are used)
void     sleep_ms(uint32_t ms);

enum class LogLevel { Info, Warn, Error };
void log(LogLevel level, const std::string& msg);

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
std::string vformat(const char* fmt, ...);   // printf-style formatting into a std::string

} // namespace hml

#define HML_INFO(...)  ::hml::log(::hml::LogLevel::Info,  ::hml::vformat(__VA_ARGS__))
#define HML_WARN(...)  ::hml::log(::hml::LogLevel::Warn,  ::hml::vformat(__VA_ARGS__))
#define HML_ERROR(...) ::hml::log(::hml::LogLevel::Error, ::hml::vformat(__VA_ARGS__))
