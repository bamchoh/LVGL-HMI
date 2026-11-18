#include "logdb.hpp"
#include "util.hpp"
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

constexpr size_t  kMaxQueue = 20000;        // rows kept in memory while the disk is stalled
constexpr int64_t kPruneEveryMs = 3600 * 1000;

std::string parent_dir(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
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

void bind_cell(sqlite3_stmt* st, int idx, const LogCell& c)
{
    switch(c.type) {
        case LogCell::Int:  sqlite3_bind_int64(st, idx, c.i); break;
        case LogCell::Real: sqlite3_bind_double(st, idx, c.d); break;
        case LogCell::Text: sqlite3_bind_text(st, idx, c.s.c_str(), -1, SQLITE_TRANSIENT); break;
        default:            sqlite3_bind_null(st, idx); break;
    }
}

} // namespace

std::string LogCell::str() const
{
    switch(type) {
        case Int:  return std::to_string(i);
        case Real: return vformat("%.10g", d);
        case Text: return s;
        default:   return std::string();
    }
}

LogDbConfig parse_log_config(const json* section, const std::string& base_dir, const std::string& default_path, int default_max_rows)
{
    LogDbConfig c;
    c.max_rows = default_max_rows;
    if(!section || !section->is_object()) return c;
    c.enabled = jbool(*section, "enabled", true);
    std::string rel = jstr(*section, "path", default_path);
    bool absolute = !rel.empty() && (rel[0] == '/' || rel[0] == '\\' || (rel.size() > 1 && rel[1] == ':'));
    c.path = absolute ? rel : base_dir + "/" + rel;
    c.retention_days = static_cast<int>(jnum(*section, "retention_days", 30));
    c.max_rows = static_cast<int>(jnum(*section, "max_rows", default_max_rows));
    if(c.retention_days < 0) c.retention_days = 0;
    if(c.max_rows < 0) c.max_rows = 0;
    return c;
}

struct LogDb::Impl {
    std::string label, table, index_columns;
    std::vector<Column> columns;

    std::mutex qmu;
    std::deque<std::pair<int64_t, LogRow>> queue;

    std::mutex    dbmu;                 // lock order: dbmu, then qmu
    sqlite3*      db = nullptr;
    sqlite3_stmt* ins = nullptr;
    bool          ro = false;
    bool          write_failed = false;
    LogDbConfig   cfg;
    std::atomic<bool> opened{ false };
    int64_t       last_prune = 0;

    std::thread             writer;
    std::mutex              wmu;
    std::condition_variable cv;
    bool                    stop = false;

    bool exec(const std::string& sql)
    {
        char* err = nullptr;
        if(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) == SQLITE_OK) return true;
        HML_ERROR("%s: %s (%s)", label.c_str(), err ? err : "sqlite error", sql.c_str());
        sqlite3_free(err);
        return false;
    }

    void prune()
    {
        last_prune = LogDb::now_ms();
        if(cfg.retention_days > 0) {
            sqlite3_stmt* st = nullptr;
            if(sqlite3_prepare_v2(db, ("DELETE FROM " + table + " WHERE ts < ?1").c_str(), -1, &st, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(st, 1, last_prune - static_cast<int64_t>(cfg.retention_days) * 86400 * 1000);
                sqlite3_step(st);
            }
            sqlite3_finalize(st);
        }
        if(cfg.max_rows > 0) {
            sqlite3_stmt* st = nullptr;
            std::string sql = "DELETE FROM " + table + " WHERE id <= (SELECT id FROM " + table + " ORDER BY id DESC LIMIT 1 OFFSET ?1)";
            if(sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(st, 1, cfg.max_rows);
                sqlite3_step(st);
            }
            sqlite3_finalize(st);
        }
    }

    void write_pending()
    {
        std::lock_guard<std::mutex> dl(dbmu);
        if(!db || ro) return;
        std::deque<std::pair<int64_t, LogRow>> batch;
        {
            std::lock_guard<std::mutex> ql(qmu);
            batch.swap(queue);
        }
        if(!batch.empty()) {
            bool ok = sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK;
            for(const auto& r : batch) {
                if(!ok) break;
                sqlite3_reset(ins);
                sqlite3_bind_int64(ins, 1, r.first);
                for(size_t i = 0; i < columns.size(); i++) bind_cell(ins, static_cast<int>(i) + 2, i < r.second.size() ? r.second[i] : LogCell());
                ok = sqlite3_step(ins) == SQLITE_DONE;
            }
            ok = sqlite3_exec(db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK && ok;
            if(!ok && !write_failed) HML_ERROR("%s: cannot write to '%s': %s", label.c_str(), cfg.path.c_str(), sqlite3_errmsg(db));
            write_failed = !ok;
        }
        if(LogDb::now_ms() - last_prune > kPruneEveryMs) prune();
    }
};

LogDb::LogDb(std::string label, std::string table, std::vector<Column> columns, std::string index_columns) : impl_(std::make_unique<Impl>())
{
    impl_->label = std::move(label);
    impl_->table = std::move(table);
    impl_->columns = std::move(columns);
    impl_->index_columns = std::move(index_columns);
}

LogDb::~LogDb() { close(); }

bool LogDb::open(const LogDbConfig& cfg, bool read_only)
{
    Impl& m = *impl_;
    close();
    if((!cfg.enabled && !read_only) || cfg.path.empty()) return false;

    std::unique_lock<std::mutex> dl(m.dbmu);
    m.cfg = cfg;
    m.ro = read_only;
    m.write_failed = false;
    std::error_code ec;
    if(!read_only) std::filesystem::create_directories(std::filesystem::u8path(parent_dir(cfg.path)), ec);

    // (a reader opens read-write too, without creating the file: see system_log.cpp)
    int rc = sqlite3_open_v2(cfg.path.c_str(), &m.db, read_only ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if(rc != SQLITE_OK && read_only) {
        sqlite3_close(m.db);
        rc = sqlite3_open_v2(cfg.path.c_str(), &m.db, SQLITE_OPEN_READONLY, nullptr);
    }
    bool ok = rc == SQLITE_OK;
    std::string err = ok ? "" : sqlite3_errmsg(m.db);
    if(ok && !read_only) {
        sqlite3_busy_timeout(m.db, 3000);
        sqlite3_exec(m.db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        sqlite3_exec(m.db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        std::string create = "CREATE TABLE IF NOT EXISTS " + m.table + "(id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL";
        std::string insert = "INSERT INTO " + m.table + "(ts", marks = "?1";
        for(size_t i = 0; i < m.columns.size(); i++) {
            create += ", " + m.columns[i].name + " " + m.columns[i].type;
            insert += ", " + m.columns[i].name;
            marks += ", ?" + std::to_string(i + 2);
        }
        insert += ") VALUES(" + marks + ")";
        ok = m.exec(create + ")")
          && m.exec("CREATE INDEX IF NOT EXISTS " + m.table + "_ts ON " + m.table + "(ts)")
          && (m.index_columns.empty() || m.exec("CREATE INDEX IF NOT EXISTS " + m.table + "_key ON " + m.table + "(" + m.index_columns + ")"))
          && sqlite3_prepare_v2(m.db, insert.c_str(), -1, &m.ins, nullptr) == SQLITE_OK;
        if(!ok) err = sqlite3_errmsg(m.db);
    }
    else if(ok) sqlite3_busy_timeout(m.db, 3000);
    if(!ok) {
        sqlite3_finalize(m.ins);
        m.ins = nullptr;
        sqlite3_close(m.db);
        m.db = nullptr;
        dl.unlock();
        HML_ERROR("%s: cannot open '%s': %s", m.label.c_str(), cfg.path.c_str(), err.c_str());
        return false;
    }
    m.opened = true;
    if(read_only) return true;

    m.prune();
    dl.unlock();
    m.writer = std::thread([&m] {
        std::unique_lock<std::mutex> l(m.wmu);
        while(!m.stop) {
            m.cv.wait_for(l, std::chrono::seconds(1), [&m] { return m.stop; });
            l.unlock();
            m.write_pending();
            l.lock();
        }
    });
    return true;
}

void LogDb::close()
{
    Impl& m = *impl_;
    if(m.writer.joinable()) {
        {
            std::lock_guard<std::mutex> l(m.wmu);
            m.stop = true;
        }
        m.cv.notify_all();
        m.writer.join();
        m.stop = false;
    }
    m.write_pending();
    std::lock_guard<std::mutex> dl(m.dbmu);
    sqlite3_finalize(m.ins);
    m.ins = nullptr;
    if(m.db) sqlite3_close(m.db);
    m.db = nullptr;
    m.opened = false;
    std::lock_guard<std::mutex> ql(m.qmu);
    m.queue.clear();
}

bool LogDb::is_open() const { return impl_->opened; }
bool LogDb::read_only() const { return impl_->ro; }
const std::string& LogDb::path() const { return impl_->cfg.path; }

void LogDb::add(LogRow row) { add(now_ms(), std::move(row)); }

void LogDb::add(int64_t ts_ms, LogRow row)
{
    Impl& m = *impl_;
    if(!m.opened || m.ro) return;
    std::lock_guard<std::mutex> l(m.qmu);
    m.queue.emplace_back(ts_ms, std::move(row));
    if(m.queue.size() > kMaxQueue) m.queue.pop_front();
}

void LogDb::flush() { impl_->write_pending(); }

std::vector<LogRow> LogDb::query(const std::string& where, const LogRow& binds, int limit, bool newest_first)
{
    Impl& m = *impl_;
    flush();
    std::vector<LogRow> out;
    std::lock_guard<std::mutex> dl(m.dbmu);
    if(!m.db) return out;
    std::string sql = "SELECT id, ts";
    for(const Column& c : m.columns) sql += ", " + c.name;
    sql += " FROM " + m.table;
    if(!where.empty()) sql += " WHERE " + where;
    sql += newest_first ? " ORDER BY id DESC" : " ORDER BY id ASC";
    if(limit > 0) sql += " LIMIT " + std::to_string(limit);
    sqlite3_stmt* st = nullptr;
    if(sqlite3_prepare_v2(m.db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        // (a database written by nobody yet has no table: that is an empty log, not an error)
        if(!m.ro) HML_ERROR("%s: query failed: %s", m.label.c_str(), sqlite3_errmsg(m.db));
        sqlite3_finalize(st);
        return out;
    }
    for(size_t i = 0; i < binds.size(); i++) bind_cell(st, static_cast<int>(i) + 1, binds[i]);
    const int ncol = static_cast<int>(m.columns.size()) + 2;
    while(sqlite3_step(st) == SQLITE_ROW) {
        LogRow row;
        row.reserve(static_cast<size_t>(ncol));
        for(int c = 0; c < ncol; c++) {
            switch(sqlite3_column_type(st, c)) {
                case SQLITE_INTEGER: row.emplace_back(static_cast<int64_t>(sqlite3_column_int64(st, c))); break;
                case SQLITE_FLOAT:   row.emplace_back(sqlite3_column_double(st, c)); break;
                case SQLITE_NULL:    row.emplace_back(); break;
                default: {
                    const unsigned char* s = sqlite3_column_text(st, c);
                    row.emplace_back(std::string(s ? reinterpret_cast<const char*>(s) : ""));
                }
            }
        }
        out.push_back(std::move(row));
    }
    sqlite3_finalize(st);
    return out;
}

std::string LogDb::export_path(const std::string& stem) const
{
    std::tm tm = local_tm(now_ms());
    char buf[80];
    std::snprintf(buf, sizeof buf, "-%04d%02d%02d-%02d%02d%02d.csv", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return parent_dir(impl_->cfg.path) + "/" + stem + buf;
}

int64_t LogDb::now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string LogDb::format_time(int64_t ms)
{
    std::tm tm = local_tm(ms);
    char buf[80];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return buf;
}

int64_t LogDb::write_csv(const std::string& path, const std::vector<std::string>& header, const std::vector<std::vector<std::string>>& rows)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(parent_dir(path)), ec);
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if(!f) { HML_ERROR("cannot write '%s'", path.c_str()); return -1; }
    auto line = [&f](const std::vector<std::string>& cells) {
        for(size_t i = 0; i < cells.size(); i++) f << (i ? "," : "") << csv_field(cells[i]);
        f << "\r\n";
    };
    f << "\xEF\xBB\xBF";                // BOM: Excel reads the file as UTF-8
    line(header);
    for(const auto& r : rows) line(r);
    f.flush();
    if(!f) { HML_ERROR("cannot write '%s'", path.c_str()); return -1; }
    return static_cast<int64_t>(rows.size());
}

} // namespace hml
