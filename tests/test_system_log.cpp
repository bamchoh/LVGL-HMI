#include <doctest.h>
#include "system_log.hpp"
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "script.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace hml;
namespace fs = std::filesystem;

namespace {

// A scratch directory for one database; the store is closed and the directory removed at the end.
struct TempLog {
    fs::path dir;
    SystemLog store;

    TempLog()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_log_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
    }
    ~TempLog()
    {
        store.close();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    SystemLogConfig config() const
    {
        SystemLogConfig c;
        c.enabled = true;
        c.path = (dir / "logs" / "hml.db").generic_string();
        return c;
    }
};

std::string read_all(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int total(const std::vector<SystemLogEntry>& rows)
{
    int n = 0;
    for(const SystemLogEntry& e : rows) n += e.count;
    return n;
}

} // namespace

TEST_CASE("system_log: records rows and filters them")
{
    TempLog t;
    REQUIRE(t.store.open(t.config()));          // creates the directory and the database
    CHECK(t.store.is_open());
    CHECK(fs::exists(t.dir / "logs" / "hml.db"));

    t.store.add(LogLevel::Info, "project 'x' loaded");
    t.store.add(LogLevel::Error, "script 'a': a:1: boom");
    t.store.add(LogLevel::Warn, "modbus 10.0.0.1:502 disconnected");
    t.store.add(LogLevel::Info, "custom", "hello");

    auto rows = t.store.query();
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].message == "project 'x' loaded");
    CHECK(rows[0].source == "system");
    CHECK(rows[0].level == LogLevel::Info);
    CHECK(rows[0].count == 1);
    CHECK(rows[0].ts_ms > 1600000000000LL);
    CHECK(rows[1].source == "script");
    CHECK(rows[1].level == LogLevel::Error);
    CHECK(rows[2].source == "driver");
    CHECK(rows[3].source == "custom");

    SystemLogQuery q;
    q.min_level = LogLevel::Warn;
    CHECK(t.store.query(q).size() == 2);
    q = {};
    q.source = "script";
    CHECK(t.store.query(q).size() == 1);
    q = {};
    q.text = "disconnected";
    CHECK(t.store.query(q).size() == 1);
    q = {};
    q.newest_first = true;
    q.limit = 2;
    rows = t.store.query(q);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].message == "hello");
    CHECK(rows[1].source == "driver");
    q = {};
    q.since_ms = rows[0].ts_ms + 60000;
    CHECK(t.store.query(q).empty());
}

TEST_CASE("system_log: source_of")
{
    CHECK(SystemLog::source_of("script 'x': x:1: err") == "script");
    CHECK(SystemLog::source_of("[script] hello") == "script");
    CHECK(SystemLog::source_of("opcua: listening") == "driver");
    CHECK(SystemLog::source_of("driver 'sim' (sim) started") == "driver");
    CHECK(SystemLog::source_of("2 screens built") == "system");
}

TEST_CASE("system_log: consecutive repeats are folded into one row")
{
    TempLog t;
    REQUIRE(t.store.open(t.config()));
    for(int i = 0; i < 50; i++) t.store.add(LogLevel::Error, "script 'tick': tick:1: boom");
    t.store.flush();                            // repeats are folded across flushes too
    for(int i = 0; i < 50; i++) t.store.add(LogLevel::Error, "script 'tick': tick:1: boom");
    t.store.add(LogLevel::Info, "other");
    t.store.add(LogLevel::Error, "script 'tick': tick:1: boom");

    SystemLogQuery q;
    q.source = "script";
    auto rows = t.store.query(q);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].count == 100);
    CHECK(rows[0].last_ts_ms >= rows[0].ts_ms);
    CHECK(rows[1].count == 1);
}

TEST_CASE("system_log: minimum level")
{
    TempLog t;
    SystemLogConfig c = t.config();
    c.level = LogLevel::Warn;
    REQUIRE(t.store.open(c));
    t.store.add(LogLevel::Info, "info");
    t.store.add(LogLevel::Warn, "warn");
    t.store.add(LogLevel::Error, "error");
    auto rows = t.store.query();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].message == "warn");
    CHECK(rows[1].message == "error");
}

TEST_CASE("system_log: rows survive a reopen, max_rows drops the oldest")
{
    TempLog t;
    REQUIRE(t.store.open(t.config()));
    for(int i = 0; i < 20; i++) t.store.add(LogLevel::Info, "row " + std::to_string(i));
    t.store.close();
    CHECK_FALSE(t.store.is_open());

    REQUIRE(t.store.open(t.config()));
    CHECK(t.store.query().size() == 20);
    t.store.close();

    SystemLogConfig c = t.config();
    c.max_rows = 5;
    REQUIRE(t.store.open(c));
    auto rows = t.store.query();
    REQUIRE(rows.size() == 5);
    CHECK(rows[0].message == "row 15");
    CHECK(rows[4].message == "row 19");
}

TEST_CASE("system_log: disabled / unusable paths")
{
    TempLog t;
    SystemLogConfig c = t.config();
    c.enabled = false;
    CHECK_FALSE(t.store.open(c));
    CHECK_FALSE(fs::exists(t.dir));             // nothing is created when system_log is off
    t.store.add(LogLevel::Error, "dropped");
    CHECK(t.store.query().empty());
    CHECK(t.store.export_csv((t.dir / "x.csv").generic_string()) == -1);

    // a reader does not create the database
    CHECK_FALSE(t.store.open(t.config(), true));
    CHECK_FALSE(fs::exists(t.dir / "logs" / "hml.db"));
}

TEST_CASE("system_log: captures hml::log(), including lines logged before open()")
{
    TempLog t;
    // the global sink feeds the singleton, so use it here (t only provides the directory)
    system_log().attach();
    HML_WARN("early %d", 1);
    REQUIRE(system_log().open(t.config()));
    HML_ERROR("script 'late': late:2: oops");
    auto rows = system_log().query();
    system_log().close();
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].message == "early 1");
    CHECK(rows[0].level == LogLevel::Warn);
    CHECK(rows[2].source == "script");

    HML_INFO("after close");                    // no sink any more: must not crash or be recorded
    CHECK(system_log().query().empty());
}

TEST_CASE("system_log: CSV export")
{
    TempLog t;
    SystemLogConfig c = t.config();
    c.level = LogLevel::Warn;
    REQUIRE(t.store.open(c));
    t.store.add(LogLevel::Warn, "plain");
    t.store.add(LogLevel::Error, "script", "a \"quoted\", comma\nand a new line");
    t.store.add(LogLevel::Error, "script", "twice");
    t.store.add(LogLevel::Error, "script", "twice");

    std::string csv = (t.dir / "out" / "log.csv").generic_string();
    CHECK(t.store.export_csv(csv) == 3);
    std::string text = read_all(csv);
    REQUIRE(text.size() > 3);
    CHECK(text.substr(0, 3) == "\xEF\xBB\xBF");
    CHECK(text.find("time,level,source,message,count,last_time\r\n") == 3);
    CHECK(text.find(",warn,system,plain,1,\r\n") != std::string::npos);
    CHECK(text.find(",error,script,\"a \"\"quoted\"\", comma\nand a new line\",1,\r\n") != std::string::npos);
    CHECK(text.find(",error,script,twice,2,20") != std::string::npos);    // last_time filled in

    SystemLogQuery q;
    q.min_level = LogLevel::Error;
    CHECK(t.store.export_csv(csv, q) == 2);
    CHECK(read_all(csv).find("plain") == std::string::npos);

    std::string def = t.store.default_export_path();
    CHECK(def.find("/logs/system-log-") != std::string::npos);
    CHECK(def.substr(def.size() - 4) == ".csv");
}

TEST_CASE("system_log: a second reader sees the rows while the writer is open")
{
    TempLog t;
    REQUIRE(t.store.open(t.config()));
    t.store.add(LogLevel::Error, "visible");
    t.store.flush();

    SystemLog reader;
    REQUIRE(reader.open(t.config(), true));
    SystemLogQuery q;
    q.min_level = LogLevel::Error;
    auto rows = reader.query(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].message == "visible");
    reader.add(LogLevel::Error, "reader must not write");
    reader.close();
    CHECK(t.store.query(q).size() == 1);
}

TEST_CASE("system_log: add() from several threads")
{
    TempLog t;
    SystemLogConfig c = t.config();
    c.level = LogLevel::Warn;
    REQUIRE(t.store.open(c));
    std::vector<std::thread> threads;
    for(int n = 0; n < 4; n++) {
        threads.emplace_back([&t, n] {
            for(int i = 0; i < 500; i++) t.store.add(LogLevel::Warn, "thread " + std::to_string(n) + " row " + std::to_string(i % 7));
        });
    }
    for(std::thread& th : threads) th.join();
    CHECK(total(t.store.query()) == 2000);
}

TEST_CASE("system_log: \"system_log\" in project.json")
{
    hmltest::Runtime rt;
    TempLog t;
    fs::create_directories(t.dir);
    auto write = [&t](const std::string& text) {
        std::string p = (t.dir / "project.json").generic_string();
        std::ofstream(p, std::ios::binary) << text;
        return p;
    };

    REQUIRE(project().load(write(R"~({ "tags": [] })~")));
    CHECK_FALSE(project().info().system_log.enabled);              // off unless the section exists

    REQUIRE(project().load(write(R"~({ "tags": [], "system_log": {} })~")));
    const SystemLogConfig& d = project().info().system_log;
    CHECK(d.enabled);
    CHECK(d.path == t.dir.generic_string() + "/logs/system.db");
    CHECK(d.level == LogLevel::Info);
    CHECK(d.retention_days == 30);
    CHECK(d.max_rows == 100000);

    REQUIRE(project().load(write(R"~({ "tags": [], "system_log": { "enabled": false, "path": "data/sys.db", "level": "WARN", "retention_days": 7, "max_rows": 500 } })~")));
    const SystemLogConfig& c = project().info().system_log;
    CHECK_FALSE(c.enabled);
    CHECK(c.path == t.dir.generic_string() + "/data/sys.db");
    CHECK(c.level == LogLevel::Warn);
    CHECK(c.retention_days == 7);
    CHECK(c.max_rows == 500);
    CHECK_FALSE(fs::exists(t.dir / "logs"));                // load() never opens the database
    project().unload();
}

TEST_CASE("system_log: script errors, log() and warn() end up in the log")
{
    hmltest::Runtime rt;
    TempLog t;
    REQUIRE(system_log().open(t.config()));
    json defs = json::parse(R"~([ { "name": "bad", "code": "local x = nil\nx.y = 1" } ])~");
    scripts().load(&defs, ".");
    CHECK_FALSE(scripts().run("bad"));
    CHECK(scripts().exec("log('hello', 1) warn('careful')", "inline"));

    SystemLogQuery q;
    q.source = "script";
    auto rows = system_log().query(q);
    system_log().close();
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].level == LogLevel::Error);
    CHECK(rows[0].message.rfind("script 'bad': bad:2:", 0) == 0);
    CHECK(rows[1].level == LogLevel::Info);
    CHECK(rows[1].message.find("hello") != std::string::npos);
    CHECK(rows[2].level == LogLevel::Warn);
    CHECK(rows[2].message == "[script] careful");
}
