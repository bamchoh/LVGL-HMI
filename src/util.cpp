#include "util.hpp"
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

void log(LogLevel level, const std::string& msg)
{
    const char* name = level == LogLevel::Info ? "INFO" : level == LogLevel::Warn ? "WARN" : "ERROR";
    std::printf("[hml %-5s] %s\n", name, msg.c_str());
    std::fflush(stdout);
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

} // namespace hml
