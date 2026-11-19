#include "util.hpp"
#include <atomic>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <thread>

namespace hml {

bool iequals(std::string_view a, std::string_view b)
{
    if(a.size() != b.size()) return false;
    for(size_t i = 0; i < a.size(); i++) {
        if(std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

uint32_t millis()
{
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

void sleep_ms(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

namespace {
std::atomic<LogSink> s_log_sink{ nullptr };
std::atomic<bool> s_log_quiet{ false };
}

void set_log_quiet(bool quiet) { s_log_quiet = quiet; }

void set_log_sink(LogSink sink)
{
    s_log_sink = sink;
}

void log(LogLevel level, const std::string& msg)
{
    const char* name = level == LogLevel::Info ? "INFO" : level == LogLevel::Warn ? "WARN" : "ERROR";
    if(!s_log_quiet) {
        std::printf("[hml %-5s] %s\n", name, msg.c_str());
        std::fflush(stdout);
    }
    if(LogSink sink = s_log_sink.load()) sink(level, msg);
}

std::string vformat(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string s;
    if(n > 0) {
        s.resize(static_cast<size_t>(n));
        std::vsnprintf(&s[0], static_cast<size_t>(n) + 1, fmt, ap2);
    }
    va_end(ap2);
    return s;
}

bool write_file_atomic(const std::string& path, const std::string& data, std::string* err)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::u8path(path), tmp = fs::u8path(path + ".tmp");
    if(p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if(!f) { if(err) *err = "cannot write '" + path + ".tmp'"; return false; }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if(!f) { if(err) *err = "cannot write '" + path + ".tmp' (disk full?)"; f.close(); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, p, ec);                      // replaces the old file in one step
    if(ec) { if(err) *err = "cannot replace '" + path + "': " + ec.message(); fs::remove(tmp, ec); return false; }
    return true;
}

std::optional<std::string> read_file(const std::string& path, size_t max_bytes, std::string* err)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto size = fs::file_size(fs::u8path(path), ec);
    if(ec) { if(err) *err = "cannot read '" + path + "'"; return std::nullopt; }
    if(size > max_bytes) { if(err) *err = "'" + path + "' is too large (" + std::to_string(size) + " bytes, at most " + std::to_string(max_bytes) + ")"; return std::nullopt; }
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if(!f) { if(err) *err = "cannot read '" + path + "'"; return std::nullopt; }
    std::string data(static_cast<size_t>(size), char(0));
    f.read(data.data(), static_cast<std::streamsize>(size));
    data.resize(static_cast<size_t>(f.gcount()));
    return data;
}

} // namespace hml
