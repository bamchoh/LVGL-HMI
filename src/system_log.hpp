// System log: everything written with HML_INFO / HML_WARN / HML_ERROR (script errors, driver
// connects / disconnects, project load messages, Lua log() ...) is recorded in a SQLite database
// and can be exported as CSV.
//
//  - Enabled by "system_log" in project.json (see README.md, "システムログ").
//  - add() may be called from any thread. Rows are queued in memory and written by a background
//    thread about once a second in one transaction, so the UI thread never waits for the disk.
//  - A message that repeats back to back (a script failing on every interval tick) is stored once
//    with a repeat count instead of once per occurrence.
//  - Old rows are removed by age (retention_days) and by number (max_rows).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "util.hpp"

namespace hml {

struct SystemLogConfig {
    bool        enabled = false;
    std::string path;                       // database file (already resolved against the project dir)
    LogLevel    level = LogLevel::Info;     // minimum level that is recorded
    int         retention_days = 30;        // 0 = keep forever
    int         max_rows = 100000;          // 0 = unlimited
};

struct SystemLogEntry {
    int64_t     id = 0;
    int64_t     ts_ms = 0;                  // first occurrence, Unix time in milliseconds
    int64_t     last_ts_ms = 0;             // last occurrence (== ts_ms unless count > 1)
    LogLevel    level = LogLevel::Info;
    std::string source;                     // "system" | "script" | "driver"
    std::string message;
    int         count = 1;                  // consecutive repeats folded into this row
};

struct SystemLogQuery {
    LogLevel    min_level = LogLevel::Info;
    std::string source;                     // empty = any
    std::string text;                       // substring of the message, empty = any
    int64_t     since_ms = 0, until_ms = 0; // Unix ms, 0 = open
    int         limit = 0;                  // 0 = all
    bool        newest_first = false;
};

class SystemLog {
public:
    SystemLog();
    ~SystemLog();

    // Start capturing hml::log() into memory (call first thing in main). The rows are written
    // once open() succeeds, so nothing logged while the project loads is lost.
    void attach();
    // Open (create) the database and start the writer. With read_only the database is only
    // read (query / export from a second process): nothing is captured or written.
    // Returns false if system_log is disabled or the file cannot be opened.
    bool open(const SystemLogConfig& cfg, bool read_only = false);
    // Write what is pending, stop the writer and close the database. Capturing stops too.
    void close();
    bool is_open() const;
    const std::string& path() const;

    void add(LogLevel level, const std::string& message);                              // source from the message
    void add(LogLevel level, const std::string& source, const std::string& message);
    void flush();                                                                      // write pending rows now

    std::vector<SystemLogEntry> query(const SystemLogQuery& q = {});
    // CSV (UTF-8 with BOM, CRLF): time,level,source,message,count,last_time. Returns the number
    // of rows written, -1 on failure.
    int64_t export_csv(const std::string& csv_path, const SystemLogQuery& q = {});
    // "<database dir>/system-log-YYYYMMDD-HHMMSS.csv"
    std::string default_export_path() const;

    // "script 'x': ..." -> "script", "modbus ..." / "opcua: ..." / "driver ..." -> "driver", else "system"
    static std::string source_of(const std::string& message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

SystemLog& system_log();

const char* log_level_name(LogLevel level);                       // "info" | "warn" | "error"
LogLevel    log_level_from_name(const std::string& name, LogLevel fallback);

} // namespace hml
