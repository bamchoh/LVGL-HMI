// One-table SQLite log shared by the data log, the alarm log and the operation log.
//
// The table is  <table>(id INTEGER PRIMARY KEY, ts INTEGER, <columns...>)  with ts = Unix time in
// milliseconds. add() may be called from any thread: rows are queued and written by a background
// thread about once a second in one transaction (the UI thread never waits for the disk, and an
// SD card sees few writes). Old rows are removed by age and by number.
//
// The system log (system_log.hpp) has its own store because it folds repeated messages.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

struct LogCell {
    enum Type { Null, Int, Real, Text };
    Type        type = Null;
    int64_t     i = 0;
    double      d = 0;
    std::string s;

    LogCell() = default;
    LogCell(int64_t v) : type(Int), i(v) {}
    LogCell(int v) : type(Int), i(v) {}
    LogCell(double v) : type(Real), d(v) {}
    LogCell(std::string v) : type(Text), s(std::move(v)) {}
    LogCell(const char* v) : type(Text), s(v) {}

    std::string str() const;            // for CSV / display
};
using LogRow = std::vector<LogCell>;

struct LogDbConfig {
    bool        enabled = false;
    std::string path;                   // resolved against the project directory
    int         retention_days = 30;    // 0 = keep forever
    int         max_rows = 100000;      // 0 = unlimited
    // Size of the database in MB (0 = unlimited). Rows can be of any size (a data log group
    // with many tags), so a number of rows alone does not bound the disk space. When the data
    // grows beyond this, the oldest rows are removed until it fits again.
    int         max_size_mb = 1000;
};

// { "enabled": true, "path": "...", "retention_days": 30, "max_rows": 100000, "max_size_mb": 1000 } - enabled unless
// the section is missing or says "enabled": false.
LogDbConfig parse_log_config(const json* section, const std::string& base_dir, const std::string& default_path, int default_max_rows);

class LogDb {
public:
    struct Column { std::string name, type; };     // type: "INTEGER" | "REAL" | "TEXT"

    // label is used in messages ("data log"); index_columns, if not empty, gets an index ("grp, ts").
    // A column that a database written by an older version does not have yet is added on open.
    LogDb(std::string label, std::string table, std::vector<Column> columns, std::string index_columns = "");
    ~LogDb();

    // With read_only the database is neither created nor written (export from a second process).
    bool open(const LogDbConfig& cfg, bool read_only = false);
    void close();                       // writes what is pending first
    bool is_open() const;
    bool read_only() const;
    const std::string& path() const;

    void add(LogRow row);               // one cell per column, ts = now
    void add(int64_t ts_ms, LogRow row);
    void flush();

    // Rows as [id, ts, columns...]. where is SQL after WHERE with ?1, ?2 ... placeholders ("grp = ?1");
    // values always go through binds, never into the SQL text. offset skips rows (paging).
    std::vector<LogRow> query(const std::string& where = "", const LogRow& binds = {}, int limit = 0, bool newest_first = false, int offset = 0);
    int64_t count(const std::string& where = "", const LogRow& binds = {});

    // "<database dir>/<stem>-YYYYMMDD-HHMMSS.csv"
    std::string export_path(const std::string& stem) const;

    static int64_t     now_ms();
    static std::string format_time(int64_t ts_ms);          // local time, "YYYY-MM-DD HH:MM:SS.mmm"
    // UTF-8 with BOM, CRLF. Returns the number of rows written, -1 on failure (logged).
    static int64_t     write_csv(const std::string& path, const std::vector<std::string>& header, const std::vector<std::vector<std::string>>& rows);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hml
