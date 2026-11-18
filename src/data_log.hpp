// Data log (変数ロギング): tag values recorded over time, in groups.
//
//   "data_log": { "path": "logs/data.db", "retention_days": 30, "max_rows": 1000000,
//     "groups": [ { "name": "process", "trigger": "interval", "interval_ms": 1000, "tags": ["temp", "pressure"] },
//                 { "name": "state",   "trigger": "change", "tags": ["run", "mode"] },
//                 { "name": "batch",   "trigger": "tag", "trigger_tag": "batch_done", "tags": ["total"] },
//                 { "name": "report",  "trigger": "manual", "tags": ["total"] } ] }
//
// A sample is one row holding the values of every tag of the group (table "sample": ts, grp,
// data = JSON object). Everything runs on the UI thread; the disk is written in the background.
// See README.md ("ロギング").
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "logdb.hpp"

namespace hml {

struct DataSample {
    int64_t ts_ms = 0;
    json    values;                     // { "temp": 21.5, "run": true }
};

class DataLog {
public:
    DataLog();
    ~DataLog();

    // Open the database and start the groups (LVGL timers, tag subscriptions). With read_only
    // only the group list is read (export from a second process). Returns the number of groups.
    int  load(const json* section, const std::string& base_dir, bool read_only = false);
    void clear();

    bool enabled() const;
    std::vector<std::string> groups() const;
    // Record the group now (trigger "manual", the call action "data_log.sample" and tests).
    bool sample(const std::string& group);

    std::vector<DataSample> query(const std::string& group, int limit = 0, bool newest_first = false);
    // CSV: time, then one column per tag. Empty path = next to the database. Returns rows, -1 on failure.
    int64_t export_csv(const std::string& group, std::string path = "");
    LogDb& db();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

DataLog& data_log();

} // namespace hml
