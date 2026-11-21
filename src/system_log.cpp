#include "system_log.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <sqlite3.h>

namespace hml {
namespace {

constexpr size_t  kMaxQueue = 5000;         // rows kept in memory while the database is closed or stalled
constexpr size_t  kMaxMessage = 2000;       // bytes per message
constexpr int64_t kPruneEveryMs = 3600 * 1000;

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::tm local_tm(int64_t ms)
{
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string format_time(int64_t ms)
{
    std::tm tm = local_tm(ms);
    char buf[80];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return buf;
}

std::string csv_field(const std::string& s)
{
    if(s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for(char c : s) {
        if(c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::string parent_dir(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

struct Pending {
    int64_t     ts;
    int         level;
    std::string source, message;
};

void sink(LogLevel level, const std::string& msg)
{
    system_log().add(level, msg);
}

} // namespace

const char* log_level_name(LogLevel level)
{
    return level == LogLevel::Info ? "info" : level == LogLevel::Warn ? "warn" : "error";
}

LogLevel log_level_from_name(const std::string& name, LogLevel fallback)
{
    if(iequals(name, "info")) return LogLevel::Info;
    if(iequals(name, "warn") || iequals(name, "warning")) return LogLevel::Warn;
    if(iequals(name, "error")) return LogLevel::Error;
    return fallback;
}

struct SystemLog::Impl {
    // queue (any thread)
    std::mutex         qmu;
    std::deque<Pending> queue;
    bool               capturing = false;
    std::atomic<int>   min_level{ 0 };

    // database (writer thread, flush(), query()); lock order: dbmu, then qmu
    std::mutex    dbmu;
    sqlite3*      db = nullptr;
    sqlite3_stmt* ins = nullptr;
    sqlite3_stmt* upd = nullptr;
    bool          read_only = false;
    bool          write_failed = false;     // reported once, see write_pending()
    SystemLogConfig     cfg;
    std::atomic<bool> opened{ false };
    // the last row written, to fold repeats into it
    int64_t       last_id = 0;
    int           last_level = -1;
    std::string   last_source, last_message;
    int64_t       last_prune = 0;

    // writer thread
    std::thread             writer;
    std::mutex              wmu;
    std::condition_variable cv;
    bool                    stop = false;

    bool exec(const char* sql)
    {
        char* err = nullptr;
        if(sqlite3_exec(db, sql, nullptr, nullptr, &err) == SQLITE_OK) return true;
        std::fprintf(stderr, "[hml ERROR] system log: %s (%s)\n", err ? err : "sqlite error", sql);
        sqlite3_free(err);
        return false;
    }

    // dbmu held
    void prune()
    {
        last_prune = now_ms();
        if(cfg.retention_days > 0) {
            sqlite3_stmt* st = nullptr;
            if(sqlite3_prepare_v2(db, "DELETE FROM log WHERE last_ts < ?1", -1, &st, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(st, 1, last_prune - static_cast<int64_t>(cfg.retention_days) * 86400 * 1000);
                sqlite3_step(st);
            }
            sqlite3_finalize(st);
        }
        if(cfg.max_rows > 0) {
            sqlite3_stmt* st = nullptr;
            if(sqlite3_prepare_v2(db, "DELETE FROM log WHERE id <= (SELECT id FROM log ORDER BY id DESC LIMIT 1 OFFSET ?1)", -1, &st, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(st, 1, cfg.max_rows);
                sqlite3_step(st);
            }
            sqlite3_finalize(st);
        }
    }

    bool insert(const Pending& p)
    {
        sqlite3_reset(ins);
        sqlite3_bind_int64(ins, 1, p.ts);
        sqlite3_bind_int(ins, 2, p.level);
        sqlite3_bind_text(ins, 3, p.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 4, p.message.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(ins) != SQLITE_DONE) return false;
        last_id = sqlite3_last_insert_rowid(db);
        last_level = p.level;
        last_source = p.source;
        last_message = p.message;
        return true;
    }

    bool write(const Pending& p)
    {
        if(last_id && p.level == last_level && p.message == last_message && p.source == last_source) {
            sqlite3_reset(upd);
            sqlite3_bind_int64(upd, 1, p.ts);
            sqlite3_bind_int64(upd, 2, last_id);
            if(sqlite3_step(upd) != SQLITE_DONE) return false;
            if(sqlite3_changes(db) > 0) return true;        // (0: the row was pruned meanwhile)
        }
        return insert(p);
    }

    // Move the queue into the database in one transaction.
    void write_pending()
    {
        std::lock_guard<std::mutex> dl(dbmu);
        if(!db || read_only) return;
        std::deque<Pending> batch;
        {
            std::lock_guard<std::mutex> ql(qmu);
            batch.swap(queue);
        }
        if(!batch.empty()) {
            bool ok = exec("BEGIN");
            for(const Pending& p : batch) ok = ok && write(p);
            ok = exec(ok ? "COMMIT" : "ROLLBACK") && ok;
            // Not HML_ERROR: that would queue a new row and fail again on every flush.
            if(!ok && !write_failed) std::fprintf(stderr, "[hml ERROR] system log: cannot write to '%s': %s\n", cfg.path.c_str(), sqlite3_errmsg(db));
            write_failed = !ok;
            if(!ok) last_id = 0;
        }
        if(now_ms() - last_prune > kPruneEveryMs) prune();
    }

    void close_db()
    {
        if(writer.joinable()) {
            {
                std::lock_guard<std::mutex> l(wmu);
                stop = true;
            }
            cv.notify_all();
            writer.join();
            stop = false;
        }
        write_pending();
        std::lock_guard<std::mutex> dl(dbmu);
        sqlite3_finalize(ins);
        sqlite3_finalize(upd);
        ins = upd = nullptr;
        if(db) sqlite3_close(db);
        db = nullptr;
        opened = false;
        last_id = 0;
    }

    void stop_capture()
    {
        set_log_sink(nullptr);
        std::lock_guard<std::mutex> l(qmu);
        capturing = false;
        queue.clear();
    }
};

SystemLog::SystemLog() : impl_(std::make_unique<Impl>()) {}
SystemLog::~SystemLog() { close(); }

void SystemLog::attach()
{
    {
        std::lock_guard<std::mutex> l(impl_->qmu);
        impl_->capturing = true;
    }
    set_log_sink(sink);
}

bool SystemLog::open(const SystemLogConfig& cfg, bool read_only)
{
    Impl& m = *impl_;
    m.close_db();
    if((!cfg.enabled && !read_only) || cfg.path.empty()) {
        m.stop_capture();
        return false;
    }

    std::unique_lock<std::mutex> dl(m.dbmu);
    m.cfg = cfg;
    m.read_only = read_only;
    m.write_failed = false;
    m.min_level = static_cast<int>(cfg.level);
    std::error_code ec;
    if(!read_only) std::filesystem::create_directories(std::filesystem::u8path(parent_dir(cfg.path)), ec);

    // A reader opens read-write as well (never creating the file): a WAL database cannot always
    // be opened read-only. Read-only is the fallback for a file we may not write.
    int rc = sqlite3_open_v2(cfg.path.c_str(), &m.db, read_only ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if(rc != SQLITE_OK && read_only) {
        sqlite3_close(m.db);
        rc = sqlite3_open_v2(cfg.path.c_str(), &m.db, SQLITE_OPEN_READONLY, nullptr);
    }
    bool ok = rc == SQLITE_OK;
    std::string err = ok ? "" : sqlite3_errmsg(m.db);
    if(ok) {
        sqlite3_busy_timeout(m.db, 3000);
        if(!read_only) {
            // WAL + synchronous=NORMAL: a commit does not wait for the disk (kind to SD cards),
            // and the file stays consistent after a power cut (the last second may be lost).
            sqlite3_exec(m.db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);   // not on every file system: fine
            sqlite3_exec(m.db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
            ok = m.exec("CREATE TABLE IF NOT EXISTS log("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL, last_ts INTEGER NOT NULL,"
                        "level INTEGER NOT NULL, source TEXT NOT NULL, message TEXT NOT NULL, count INTEGER NOT NULL DEFAULT 1)")
              && m.exec("CREATE INDEX IF NOT EXISTS log_ts ON log(ts)")
              && sqlite3_prepare_v2(m.db, "INSERT INTO log(ts, last_ts, level, source, message) VALUES(?1, ?1, ?2, ?3, ?4)", -1, &m.ins, nullptr) == SQLITE_OK
              && sqlite3_prepare_v2(m.db, "UPDATE log SET count = count + 1, last_ts = ?1 WHERE id = ?2", -1, &m.upd, nullptr) == SQLITE_OK;
            if(!ok) err = sqlite3_errmsg(m.db);
        }
    }
    if(!ok) {
        sqlite3_finalize(m.ins);
        sqlite3_finalize(m.upd);
        m.ins = m.upd = nullptr;
        sqlite3_close(m.db);
        m.db = nullptr;
        dl.unlock();
        m.stop_capture();
        HML_ERROR("system log: cannot open '%s': %s", cfg.path.c_str(), err.c_str());
        return false;
    }
    m.opened = true;
    if(read_only) return true;

    m.prune();
    dl.unlock();
    attach();
    m.writer = std::thread([&m] {
        std::unique_lock<std::mutex> l(m.wmu);
        while(!m.stop) {
            m.cv.wait_for(l, std::chrono::seconds(1), [&m] { return m.stop; });
            l.unlock();
            m.write_pending();
            l.lock();
        }
    });
    HML_INFO("system log: recording to '%s' (level %s, keep %d days / %d rows)", cfg.path.c_str(), log_level_name(cfg.level), cfg.retention_days, cfg.max_rows);
    return true;
}

void SystemLog::close()
{
    impl_->close_db();
    impl_->stop_capture();
}

bool SystemLog::is_open() const { return impl_->opened; }
const std::string& SystemLog::path() const { return impl_->cfg.path; }

std::string SystemLog::source_of(const std::string& msg)
{
    auto starts = [&msg](const char* p) { return msg.rfind(p, 0) == 0; };
    if(starts("script") || starts("[script]")) return "script";
    for(const char* p : { "modbus", "opcua", "sim:", "driver" }) if(starts(p)) return "driver";
    return "system";
}

void SystemLog::add(LogLevel level, const std::string& message)
{
    add(level, source_of(message), message);
}

void SystemLog::add(LogLevel level, const std::string& source, const std::string& message)
{
    Impl& m = *impl_;
    if(static_cast<int>(level) < m.min_level) return;
    int64_t ts = now_ms();
    std::lock_guard<std::mutex> l(m.qmu);
    if(!m.capturing) return;
    m.queue.push_back({ ts, static_cast<int>(level), source, message.size() > kMaxMessage ? message.substr(0, kMaxMessage) : message });
    if(m.queue.size() > kMaxQueue) m.queue.pop_front();
}

void SystemLog::flush()
{
    impl_->write_pending();
}

std::vector<SystemLogEntry> SystemLog::query(const SystemLogQuery& q)
{
    Impl& m = *impl_;
    flush();
    std::vector<SystemLogEntry> out;
    std::lock_guard<std::mutex> dl(m.dbmu);
    if(!m.db) return out;
    std::string sql = "SELECT id, ts, last_ts, level, source, message, count FROM log WHERE level >= ?1"
                      " AND (?2 = '' OR source = ?2) AND (?3 = '' OR instr(message, ?3) > 0)"
                      " AND (?4 = 0 OR last_ts >= ?4) AND (?5 = 0 OR ts <= ?5)";
    sql += q.newest_first ? " ORDER BY id DESC" : " ORDER BY id ASC";
    sql += " LIMIT ?6 OFFSET ?7";
    sqlite3_stmt* st = nullptr;
    if(sqlite3_prepare_v2(m.db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        HML_ERROR("system log: query failed: %s", sqlite3_errmsg(m.db));
        sqlite3_finalize(st);
        return out;
    }
    sqlite3_bind_int(st, 1, static_cast<int>(q.min_level));
    sqlite3_bind_text(st, 2, q.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, q.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, q.since_ms);
    sqlite3_bind_int64(st, 5, q.until_ms);
    sqlite3_bind_int(st, 6, q.limit > 0 ? q.limit : -1);
    sqlite3_bind_int(st, 7, q.offset > 0 ? q.offset : 0);
    auto text = [st](int col) { const unsigned char* s = sqlite3_column_text(st, col); return std::string(s ? reinterpret_cast<const char*>(s) : ""); };
    while(sqlite3_step(st) == SQLITE_ROW) {
        SystemLogEntry e;
        e.id = sqlite3_column_int64(st, 0);
        e.ts_ms = sqlite3_column_int64(st, 1);
        e.last_ts_ms = sqlite3_column_int64(st, 2);
        int lv = sqlite3_column_int(st, 3);
        e.level = lv <= 0 ? LogLevel::Info : lv == 1 ? LogLevel::Warn : LogLevel::Error;
        e.source = text(4);
        e.message = text(5);
        e.count = sqlite3_column_int(st, 6);
        out.push_back(std::move(e));
    }
    sqlite3_finalize(st);
    return out;
}

int64_t SystemLog::count(const SystemLogQuery& q)
{
    Impl& m = *impl_;
    flush();
    std::lock_guard<std::mutex> dl(m.dbmu);
    if(!m.db) return 0;
    const char* sql = "SELECT COUNT(*) FROM log WHERE level >= ?1 AND (?2 = '' OR source = ?2) AND (?3 = '' OR instr(message, ?3) > 0)"
                      " AND (?4 = 0 OR last_ts >= ?4) AND (?5 = 0 OR ts <= ?5)";
    sqlite3_stmt* st = nullptr;
    if(sqlite3_prepare_v2(m.db, sql, -1, &st, nullptr) != SQLITE_OK) {
        HML_ERROR("system log: query failed: %s", sqlite3_errmsg(m.db));
        sqlite3_finalize(st);
        return 0;
    }
    sqlite3_bind_int(st, 1, static_cast<int>(q.min_level));
    sqlite3_bind_text(st, 2, q.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, q.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, q.since_ms);
    sqlite3_bind_int64(st, 5, q.until_ms);
    const int64_t n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

int64_t SystemLog::export_csv(const std::string& csv_path, const SystemLogQuery& q)
{
    if(!is_open()) { HML_ERROR("system log: export to '%s' failed: system_log is not enabled", csv_path.c_str()); return -1; }
    std::vector<SystemLogEntry> rows = query(q);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(parent_dir(csv_path)), ec);
    std::ofstream f(std::filesystem::u8path(csv_path), std::ios::binary | std::ios::trunc);
    if(!f) { HML_ERROR("system log: cannot write '%s'", csv_path.c_str()); return -1; }
    f << "\xEF\xBB\xBF" << "time,level,source,message,count,last_time\r\n";      // BOM: Excel reads it as UTF-8
    for(const SystemLogEntry& e : rows) {
        f << format_time(e.ts_ms) << ',' << log_level_name(e.level) << ',' << csv_field(e.source) << ',' << csv_field(e.message) << ','
          << e.count << ',' << (e.count > 1 ? format_time(e.last_ts_ms) : std::string()) << "\r\n";
    }
    f.flush();
    if(!f) { HML_ERROR("system log: cannot write '%s'", csv_path.c_str()); return -1; }
    return static_cast<int64_t>(rows.size());
}

std::string SystemLog::default_export_path() const
{
    std::tm tm = local_tm(now_ms());
    char buf[80];
    std::snprintf(buf, sizeof buf, "system-log-%04d%02d%02d-%02d%02d%02d.csv", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return parent_dir(impl_->cfg.path) + "/" + buf;
}

SystemLog& system_log()
{
    static SystemLog instance;
    return instance;
}

} // namespace hml
