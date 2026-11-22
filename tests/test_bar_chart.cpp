// "bar_chart" (bar_chart_view.hpp): a value of the data log in time slices.
// Grouped by ISO/IEC 25010 characteristic.
#include <doctest.h>
#include <sqlite3.h>
#include "bar_chart_view.hpp"
#include "data_log.hpp"
#include "lvgl_fixture.hpp"
#include "ui.hpp"

#include <atomic>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <string>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_bar_test_" + std::to_string(counter++));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        bar_chart_set_now_for_tests(0);
        data_log().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

int64_t local_ms(int y, int mo, int d, int h, int mi)
{
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&tm)) * 1000;
}

// A counter sampled every 10 minutes from midnight: in hour h each sample adds h + 1; it is set
// back to 0 at 05:00; the last sample is at 07:20.
void fill(const std::string& db, int64_t midnight)
{
    sqlite3* h = nullptr;
    REQUIRE(sqlite3_open(db.c_str(), &h) == SQLITE_OK);
    sqlite3_exec(h, "CREATE TABLE IF NOT EXISTS sample(id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL, grp TEXT NOT NULL, data TEXT NOT NULL)", nullptr, nullptr, nullptr);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(h, "INSERT INTO sample(ts, grp, data) VALUES(?1, 'prod', ?2)", -1, &st, nullptr);
    double c = 0;
    for(int k = 0; k <= 44; k++) {
        const int hour = k / 6;
        if(k == 30) c = 0;                                   // the counter is set back
        c += hour + 1;
        const std::string data = json{ { "count", c }, { "temp", 20 + hour } }.dump();
        sqlite3_bind_int64(st, 1, midnight + static_cast<int64_t>(k) * 600000);
        sqlite3_bind_text(st, 2, data.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    sqlite3_close(h);
}

lv_obj_t* chart(const char* spec)
{
    json scr = { { "name", "s" }, { "children", json::array({ json::parse(spec) }) } };
    lv_obj_t* s = ui().build_screen(scr, "");
    REQUIRE(s != nullptr);
    REQUIRE(ui().show_screen("s", "none"));
    return lv_obj_get_child(s, 0);
}

std::string text(lv_obj_t* o) { return lv_label_get_text(o); }

} // namespace

TEST_SUITE("functional suitability") {
TEST_CASE("bar_chart: what a counter made in each hour of the day, also across a reset")
{
    Runtime rt;
    TempDir t;
    rt.tag("count", TagType::Float);
    rt.tag("temp", TagType::Float);
    const int64_t m = local_ms(2026, 3, 10, 0, 0);
    fill((t.dir / "data.db").generic_string(), m);
    json cfg = json::parse(R"({ "path": "data.db", "retention_days": 0, "max_rows": 0, "max_size_mb": 0, "groups": [ { "name": "prod", "trigger": "manual", "tags": ["count", "temp"] } ] })");
    REQUIRE(data_log().load(&cfg, t.dir.generic_string()) == 1);
    bar_chart_set_now_for_tests(m + 7 * 3600000 + 30 * 60000);          // 07:30

    lv_obj_t* c = chart(R"({ "type": "bar_chart", "w": 600, "h": 240, "group": "prod", "tag": "count", "bucket_s": 3600, "bars": 10, "start": "day", "target": 30 })");
    const auto v = bar_chart_values(c);
    REQUIRE(v.size() == 10);
    CHECK(v[0] == doctest::Approx(5));          // (nothing before midnight: from the first sample of the hour)
    CHECK(v[1] == doctest::Approx(12));
    CHECK(v[2] == doctest::Approx(18));
    CHECK(v[4] == doctest::Approx(30));
    CHECK(v[5] == doctest::Approx(36));         // set back at 05:00: what it counted since
    CHECK(v[6] == doctest::Approx(42));
    CHECK(v[7] == doctest::Approx(24));         // the hour of now, so far
    CHECK(std::isnan(v[8]));                    // the future
    CHECK(std::isnan(v[9]));

    // the numbers above, the hours below, the target across
    lv_obj_t* col0 = lv_obj_get_child(c, 0);
    CHECK(text(lv_obj_get_child(col0, 0)) == "5");
    CHECK(text(lv_obj_get_child(col0, 2)) == "00");
    CHECK(text(lv_obj_get_child(lv_obj_get_child(c, 9), 2)) == "09");
    CHECK(text(lv_obj_get_child(lv_obj_get_child(c, 9), 0)) == "");
    lv_obj_t* area = lv_obj_get_child(col0, 1);
    CHECK_FALSE(lv_obj_is_hidden(lv_obj_get_child(area, 1)));          // the target mark
    CHECK(lv_obj_is_hidden(lv_obj_get_child(lv_obj_get_child(lv_obj_get_child(c, 9), 1), 0)));   // no bar in the future
}

TEST_CASE("bar_chart: the last value of each slice; the slices up to now")
{
    Runtime rt;
    TempDir t;
    rt.tag("count", TagType::Float);
    rt.tag("temp", TagType::Float);
    const int64_t m = local_ms(2026, 3, 10, 0, 0);
    fill((t.dir / "data.db").generic_string(), m);
    json cfg = json::parse(R"({ "path": "data.db", "retention_days": 0, "max_rows": 0, "max_size_mb": 0, "groups": [ { "name": "prod", "trigger": "manual", "tags": ["count", "temp"] } ] })");
    REQUIRE(data_log().load(&cfg, t.dir.generic_string()) == 1);
    bar_chart_set_now_for_tests(m + 7 * 3600000 + 30 * 60000);

    lv_obj_t* c = chart(R"({ "type": "bar_chart", "group": "prod", "tag": "temp", "mode": "value", "bars": 3, "decimals": 1, "label": "%H:%M" })");
    const auto v = bar_chart_values(c);
    REQUIRE(v.size() == 3);                      // 05, 06, 07 (07 is the hour of now)
    CHECK(v[0] == doctest::Approx(25));
    CHECK(v[1] == doctest::Approx(26));
    CHECK(v[2] == doctest::Approx(27));
    CHECK(text(lv_obj_get_child(lv_obj_get_child(c, 0), 0)) == "25.0");
    CHECK(text(lv_obj_get_child(lv_obj_get_child(c, 2), 2)) == "07:00");
}
}

TEST_SUITE("reliability") {
TEST_CASE("bar_chart: no data log, an unknown group: empty bars, no crash")
{
    Runtime rt;
    rt.tag("count", TagType::Float);
    lv_obj_t* c = chart(R"({ "type": "bar_chart", "group": "nothing", "tag": "count", "bars": 100, "bucket_s": 1 })");
    const auto v = bar_chart_values(c);
    CHECK(v.size() == 48);                       // at most 48 bars
    for(double x : v) CHECK(std::isnan(x));
    bar_chart_refresh(c);
}
}
