// Small shared helpers: case-insensitive compare, monotonic clock, logging.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hml {

bool     iequals(std::string_view a, std::string_view b);
uint32_t millis();                 // monotonic, wraps after ~49 days (only differences are used)
void     sleep_ms(uint32_t ms);

enum class LogLevel { Info, Warn, Error };
void log(LogLevel level, const std::string& msg);
// Everything passed to log() also goes to the sink (the system log, see system_log.hpp).
// log() may be called from any thread, so the sink must be thread-safe. nullptr = none.
using LogSink = void (*)(LogLevel level, const std::string& msg);
void set_log_sink(LogSink sink);

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
std::string vformat(const char* fmt, ...);   // printf-style formatting into a std::string

// Replace a file so that it is either the old or the new content, never half written (a power
// cut while saving): the data goes to "<path>.tmp", is flushed, and is renamed over the file.
// The directory is created. False (and *err) on failure; the old file is then untouched.
bool write_file_atomic(const std::string& path, const std::string& data, std::string* err = nullptr);
// The whole file, or nullopt. Files larger than max_bytes are not read (nullopt, *err says so).
std::optional<std::string> read_file(const std::string& path, size_t max_bytes, std::string* err = nullptr);

} // namespace hml

#define HML_INFO(...)  ::hml::log(::hml::LogLevel::Info,  ::hml::vformat(__VA_ARGS__))
#define HML_WARN(...)  ::hml::log(::hml::LogLevel::Warn,  ::hml::vformat(__VA_ARGS__))
#define HML_ERROR(...) ::hml::log(::hml::LogLevel::Error, ::hml::vformat(__VA_ARGS__))
