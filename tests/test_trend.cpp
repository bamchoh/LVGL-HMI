// Trend history: DataLog::trend (slicing in SQLite) and the "trend" widget. Grouped by ISO/IEC
// 25010 characteristic.
#include <doctest.h>
#include <sqlite3.h>
#include "data_log.hpp"
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "trend_view.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_trend_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        trend_wait_idle();
        data_log().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string base() const { return dir.generic_string(); }
};

// Write samples straight into the data log's database (much faster than sampling the tags):
// rows of (ts, data json) for group "g". The data log must be closed meanwhile.
void fill(const std::string& db, const std::function<void(const std::function<void(int64_t, const std::string&)>&)>& gen, const char* table = "sample")
{
    sqlite3* h = nullptr;
    REQUIRE(sqlite3_open(db.c_str(), &h) == SQLITE_OK);
    sqlite3_exec(h, (std::string("CREATE TABLE IF NOT EXISTS ") + table + "(id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL, grp TEXT NOT NULL, data TEXT NOT NULL)").c_str(), nullptr, nullptr, nullptr);
    sqlite3_exec(h, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(h, (std::string("INSERT INTO ") + table + "(ts, grp, data) VALUES(?1, 'g', ?2)").c_str(), -1, &st, nullptr);
    gen([&](int64_t ts, const std::string& data) {
        sqlite3_bind_int64(st, 1, ts);
        sqlite3_bind_text(st, 2, data.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_reset(st);
    });
    sqlite3_finalize(st);
    sqlite3_exec(h, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_close(h);
}

int open_log(Runtime& rt, const TempDir& t)
{
    rt.tag("temp", TagType::Float);
    rt.tag("run", TagType::Bool);
    rt.tag("name", std::string(""));
    rt.tag("a.b", TagType::Float);
    json cfg = json::parse(R"~({ "path": "data.db", "max_rows": 0, "max_size_mb": 0, "retention_days": 0,
        "groups": [ { "name": "g", "trigger": "manual", "tags": ["temp", "run", "name", "a.b"] } ] })~");
    return data_log().load(&cfg, t.base());
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("trend: slices keep the smallest and largest value, and the last one")
{
    Runtime rt;
    TempDir t;
    fill(t.base() + "/data.db", [](auto add) {
        // 0..999 ms: temp rises 0..99.9, with one short peak of 500 at 503 ms; run on from 500 ms
        for(int i = 0; i < 1000; i++) {
            const double temp = i == 503 ? 500 : i / 10.0;
            add(1000000 + i, json{ { "temp", temp }, { "run", i >= 500 }, { "name", "x" }, { "a.b", -i } }.dump());
        }
    });
    REQUIRE(open_log(rt, t) == 1);
    auto r = data_log().trend("g", { "temp", "run", "name", "a.b", "missing" }, 1000000, 1001000, 10);
    REQUIRE(r.size() == 5);
    REQUIRE(r[0].size() == 10);
    CHECK(r[0][0].has);
    CHECK(r[0][0].min == 0);
    CHECK(r[0][0].max == doctest::Approx(9.9));
    CHECK(r[0][0].last == doctest::Approx(9.9));
    CHECK(r[0][5].max == 500);                                 // the peak survives the slicing
    CHECK(r[0][5].min == doctest::Approx(50));
    CHECK(r[1][4].max == 0);                                   // bools: 0 / 1
    CHECK(r[1][5].max == 1);
    CHECK_FALSE(r[2][0].has);                                  // text is left out
    CHECK(r[3][9].min == -999);                                // a tag with a dot in its name
    CHECK_FALSE(r[4][0].has);                                  // not in the data
    // a window that only partly has data
    auto p = data_log().trend("g", { "temp" }, 999000, 1001000, 2);
    CHECK_FALSE(p[0][0].has);
    CHECK(p[0][1].has);
    CHECK(data_log().time_range("g") == std::make_pair(int64_t(1000000), int64_t(1000999)));
    CHECK(data_log().time_range("other") == std::make_pair(int64_t(0), int64_t(0)));
    CHECK(data_log().group_tags("g") == std::vector<std::string>{ "temp", "run", "name", "a.b" });
    // nonsense windows
    CHECK(data_log().trend("g", { "temp" }, 5, 5, 10)[0].size() == 10);
    CHECK(data_log().trend("g", { "temp" }, 1000000, 1001000, 0)[0].empty());
}

// ================================================================ security

TEST_CASE("trend: names are parameters, never SQL")
{
    Runtime rt;
    TempDir t;
    fill(t.base() + "/data.db", [](auto add) { for(int i = 0; i < 10; i++) add(1000 + i, json{ { "temp", i } }.dump()); });
    REQUIRE(open_log(rt, t) == 1);
    for(const char* evil : { "x') OR 1=1 --", "temp\"]', 1) FROM sample; DROP TABLE sample; --", "'; DELETE FROM sample; --" }) {
        auto r = data_log().trend("g", { evil }, 0, 100000, 4);
        CHECK_FALSE(r[0][0].has);
        auto r2 = data_log().trend(evil, { "temp" }, 0, 100000, 4);
        for(const auto& b : r2[0]) CHECK_FALSE(b.has);
    }
    CHECK(data_log().trend("g", { "temp" }, 0, 100000, 1)[0][0].max == 9);   // still all there
}

// ================================================================ reliability: the per-minute summary

TEST_CASE("trend: the data log writes a summary per minute, flushed when it stops")
{
    Runtime rt;
    TempDir t;
    REQUIRE(open_log(rt, t) == 1);
    Tag* temp = tags().find("temp");
    for(double v : { 5.0, -3.0, 12.0, 7.0 }) { temp->set_number(v, nullptr); CHECK(data_log().sample("g")); }
    data_log().clear();                                        // flushes the minute so far
    LogDbConfig cfg;
    cfg.enabled = true;
    cfg.path = t.base() + "/data.db";
    LogDb sum("t", "minute", { { "grp", "TEXT NOT NULL" }, { "data", "TEXT NOT NULL" } });
    REQUIRE(sum.open(cfg, true));
    auto rows = sum.query();
    REQUIRE(rows.size() >= 1);                                 // (2 if the samples crossed a minute)
    json d = json::parse(rows.back()[3].s);
    CHECK(d["temp"][2] == 7.0);                                // the last value
    CHECK_FALSE(d.contains("name"));                           // text is not summarised
    if(rows.size() == 1) { CHECK(d["temp"][0] == -3.0); CHECK(d["temp"][1] == 12.0); }
    CHECK(rows.back()[1].i % 60000 == 0);                      // the start of the minute
    sum.close();
}

TEST_CASE("trend: summary and samples together give the same answer as the samples alone")
{
    Runtime rt;
    TempDir t;
    const int64_t t0 = 1700000040000LL / 60000 * 60000;       // a whole minute
    // 3 hours of samples every 10 s with peaks; the summary covers only the first 2 hours
    auto value = [](int i) { return (i % 97 == 0) ? 1000.0 - i : (i % 50) * 1.5; };
    fill(t.base() + "/data.db", [&](auto add) { for(int i = 0; i < 1080; i++) add(t0 + int64_t(i) * 10000, json{ { "temp", value(i) }, { "run", i % 3 == 0 } }.dump()); });
    fill(t.base() + "/data.db", [&](auto add) {
        for(int m = 0; m < 120; m++) {
            double mn = 1e9, mx = -1e9, last = 0, rmn = 1, rmx = 0, rl = 0;
            for(int i = m * 6; i < m * 6 + 6; i++) { mn = std::min(mn, value(i)); mx = std::max(mx, value(i)); last = value(i); double r = i % 3 == 0; rmn = std::min(rmn, r); rmx = std::max(rmx, r); rl = r; }
            add(t0 + int64_t(m) * 60000, json{ { "temp", { mn, mx, last } }, { "run", { rmn, rmx, rl } } }.dump());
        }
    }, "minute");
    REQUIRE(open_log(rt, t) == 1);
    const int64_t from = t0 - 600000, to = t0 + 3 * 3600000LL + 600000;   // also before and after the data
    auto both = data_log().trend("g", { "temp", "run" }, from, to, 40);    // slices of 5 min: the summary is used
    // the same from the samples only: slices below a minute
    auto fine = data_log().trend("g", { "temp", "run" }, from, to, 400);
    for(size_t tag = 0; tag < 2; tag++) {
        for(int b = 0; b < 40; b++) {
            TrendBucket ref;
            for(int k = b * 10; k < b * 10 + 10; k++) {
                const TrendBucket& f = fine[tag][static_cast<size_t>(k)];
                if(!f.has) continue;
                if(!ref.has) { ref = f; continue; }
                ref.min = std::min(ref.min, f.min); ref.max = std::max(ref.max, f.max); ref.last = f.last;
            }
            const TrendBucket& x = both[tag][static_cast<size_t>(b)];
            CHECK_MESSAGE(x.has == ref.has, "tag " << tag << " slice " << b);
            if(!x.has || !ref.has) continue;
            CHECK_MESSAGE(x.min == ref.min, "tag " << tag << " slice " << b);
            CHECK_MESSAGE(x.max == ref.max, "tag " << tag << " slice " << b);
            CHECK_MESSAGE(x.last == ref.last, "tag " << tag << " slice " << b);
        }
    }
}

// ================================================================ performance efficiency

TEST_CASE("trend: one million samples are sliced quickly")
{
    Runtime rt;
    TempDir t;
    const int64_t t0 = 1700000040000LL / 60000 * 60000;
    fill(t.base() + "/data.db", [&](auto add) {
        for(int i = 0; i < 1000000; i++) add(t0 + int64_t(i) * 1000, "{\"temp\":" + std::to_string(20 + (i % 600) / 10.0) + ",\"run\":" + (i % 2 ? "true" : "false") + "}");
    });
    REQUIRE(open_log(rt, t) == 1);
    // without a summary (a database from before this version): every sample is read
    auto start = std::chrono::steady_clock::now();
    auto raw = data_log().trend("g", { "temp", "run" }, t0, t0 + 1000000LL * 1000, 200);
    auto raw_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    data_log().clear();
    // the summary as the data log writes it: one row per minute
    fill(t.base() + "/data.db", [&](auto add) {
        for(int m = 0; m < 1000000 / 60 + 1; m++) {
            double mn = 1e9, mx = -1e9, last = 0;
            for(int i = m * 60; i < m * 60 + 60 && i < 1000000; i++) { double v = 20 + (i % 600) / 10.0; mn = std::min(mn, v); mx = std::max(mx, v); last = v; }
            add(t0 + int64_t(m) * 60000, json{ { "temp", { mn, mx, last } }, { "run", { 0, 1, 1 } } }.dump());
        }
    }, "minute");
    REQUIRE(open_log(rt, t) == 1);
    start = std::chrono::steady_clock::now();
    auto r = data_log().trend("g", { "temp", "run" }, t0, t0 + 1000000LL * 1000, 200);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("1,000,000 samples, 2 tags, 200 slices: " << ms << " ms with the summary, " << raw_ms << " ms from the samples alone");
    CHECK(r[0][0].min == doctest::Approx(20));
    CHECK(r[0][0].max == doctest::Approx(79.9));
    CHECK(r[1][199].max == 1);
    CHECK(raw[0][0].max == doctest::Approx(79.9));
    CHECK(ms < 300);
    // an hour of it (the usual live window) is much quicker
    start = std::chrono::steady_clock::now();
    data_log().trend("g", { "temp" }, t0 + 500000LL * 1000, t0 + 503600LL * 1000, 200);
    auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("1 hour window: " << ms2 << " ms");
    CHECK(ms2 < 200);
}

TEST_CASE("trend: slices between two samples are filled the way the group records")
{
    auto series = [](std::initializer_list<double> v) {        // NAN = no sample in the slice
        std::vector<std::vector<TrendBucket>> d(1);
        for(double x : v) d[0].push_back(std::isnan(x) ? TrendBucket{} : TrendBucket{ true, x, x, x });
        return d;
    };
    const double N = NAN;
    // 12 slices of 500 ms; recorded every second: every other slice is empty
    auto d = series({ 0, N, 10, N, 20, N, N, N, N, N, N, 70 });
    trend_fill_gaps(d, 0, 6000, { TrendFill::Line, 2500 }, 6000);
    CHECK(d[0][1].has);
    CHECK(d[0][1].last == doctest::Approx(5));                 // on the line between its neighbours
    CHECK(d[0][3].min == doctest::Approx(15));
    CHECK_FALSE(d[0][5].has);                                  // 3 s without a sample: a real gap
    CHECK_FALSE(d[0][10].has);
    // nothing before the first and after the last sample
    d = series({ N, 1, N, 3, N });
    trend_fill_gaps(d, 0, 2500, { TrendFill::Line, 2500 }, 2500);
    CHECK_FALSE(d[0][0].has);
    CHECK(d[0][2].last == doctest::Approx(2));
    CHECK_FALSE(d[0][4].has);
    // recorded on change: the value stays until the next sample, and the newest one up to now
    d = series({ N, 1, N, N, 0, N, N, N });
    trend_fill_gaps(d, 0, 8000, { TrendFill::Hold, 0 }, 6500);
    CHECK_FALSE(d[0][0].has);
    CHECK(d[0][3].last == 1);
    CHECK(d[0][6].last == 0);
    CHECK_FALSE(d[0][7].has);                                  // (that slice starts after now)
    // other groups (manual, tag): as recorded
    d = series({ 1, N, 3 });
    trend_fill_gaps(d, 0, 3000, {}, 3000);
    CHECK_FALSE(d[0][1].has);

    Runtime rt;
    TempDir t;
    rt.tag("temp", TagType::Float);
    json cfg = json::parse(R"~({ "path": "data.db", "groups": [
        { "name": "fast", "interval_ms": 200, "tags": ["temp"] },
        { "name": "chg", "trigger": "change", "tags": ["temp"] },
        { "name": "man", "trigger": "manual", "tags": ["temp"] } ] })~");
    REQUIRE(data_log().load(&cfg, t.base()) == 3);
    CHECK(data_log().trend_fill("fast").mode == TrendFill::Line);
    CHECK(data_log().trend_fill("fast").max_gap_ms == 500);
    CHECK(data_log().trend_fill("chg").mode == TrendFill::Hold);
    CHECK(data_log().trend_fill("man").mode == TrendFill::None);
    CHECK(data_log().trend_fill("nope").mode == TrendFill::None);
}

// ================================================================ the widget

TEST_CASE("trend widget: draws, moves, zooms, shows values at a touch")
{
    Runtime rt;
    TempDir t;
    const int64_t now = LogDb::now_ms();
    fill(t.base() + "/data.db", [&](auto add) {
        for(int i = 0; i < 3600; i++) add(now - 3600000 + int64_t(i) * 1000, json{ { "temp", i % 100 }, { "run", i % 2 } }.dump());
    });
    REQUIRE(open_log(rt, t) == 1);
    json scr = { { "name", "tr" }, { "children", json::parse(R"~([
        { "type": "trend", "group": "g", "w": 600, "h": 300, "tags": ["temp", { "tag": "run", "color": "#ff0000" }], "buckets": 60 },
        { "type": "trend", "group": "nope" } ])~") } };
    lv_obj_t* s = ui().build_screen(scr, "/base");
    REQUIRE(s);
    REQUIRE(ui().show_screen("tr", "none"));
    rt.advance(150);
    trend_wait_idle();
    rt.advance(150);
    lv_obj_t* w = lv_obj_get_child(s, 0);
    lv_obj_t* range = lv_obj_get_child(w, 0);
    CHECK(std::string(lv_label_get_text(range)).find("(live)") != std::string::npos);
    lv_obj_t* chart = lv_obj_get_child(lv_obj_get_child(w, 2), 1);
    REQUIRE(lv_obj_check_type(chart, &lv_chart_class));
    CHECK(lv_chart_get_point_count(chart) == 120);            // two points per slice: its smallest and largest value
    CHECK(lv_chart_get_series_next(chart, lv_chart_get_series_next(chart, lv_chart_get_series_next(chart, nullptr))) == nullptr);   // one line per tag
    lv_chart_series_t* s0 = lv_chart_get_series_next(chart, nullptr);
    int drawn = 0;
    for(uint32_t i = 0; i < 60; i++) drawn += lv_chart_get_series_y_array(chart, s0)[i * 2] != LV_CHART_POINT_NONE;
    {   // temp rises 0..99 and falls back: within a slice the points are in the order the value moved
        const int32_t* ya = lv_chart_get_series_y_array(chart, s0);
        int up = 0, down = 0;
        for(uint32_t i = 0; i < 58; i++) { up += ya[i * 2] < ya[i * 2 + 1]; down += ya[i * 2] > ya[i * 2 + 1]; }
        CHECK(up >= 20);                                   // (the slices without the fall in them)
        CHECK(down > 0);
    }
    CHECK(drawn >= 58);                                        // (the newest slice may still be empty)
    lv_obj_t* axis = lv_obj_get_child(lv_obj_get_child(w, 2), 0);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(axis, 0))) == "103.95");   // auto range 0..99 with 5 %
    // (the readout at a touch needs a pointer device: checked on the runtime with --click)

    // move back half a window: no longer live
    lv_obj_t* foot = lv_obj_get_child(w, 3);
    lv_obj_send_event(lv_obj_get_child(foot, 0), LV_EVENT_CLICKED, nullptr);
    rt.advance(150); trend_wait_idle(); rt.advance(150);
    CHECK(std::string(lv_label_get_text(range)).find("(live)") == std::string::npos);
    int drawn2 = 0;
    for(uint32_t i = 0; i < 60; i++) drawn2 += lv_chart_get_series_y_array(chart, s0)[i * 2] != LV_CHART_POINT_NONE;
    CHECK(drawn2 >= 29);
    CHECK(drawn2 <= 31);                                       // half of the window is before the data starts
    // zoom out, forward past now: live again
    lv_obj_send_event(lv_obj_get_child(foot, 2), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(foot, 1), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(foot, 1), LV_EVENT_CLICKED, nullptr);
    rt.advance(150); trend_wait_idle(); rt.advance(150);
    CHECK(std::string(lv_label_get_text(range)).find("(live)") != std::string::npos);
    // the legend has the tag names
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(foot, 5))) == "temp");
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(foot, 6))) == "run");
    // a group that does not exist says so
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(s, 1), 0))).find("no data log group 'nope'") != std::string::npos);
    ui().unload_all();
    rt.advance(300);
    trend_wait_idle();
}
