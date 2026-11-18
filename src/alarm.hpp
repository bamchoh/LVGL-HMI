// Alarms (アラームロギング): conditions on tags; every raise / clear is recorded.
//
//   "alarms": { "path": "logs/alarm.db", "retention_days": 90, "max_rows": 100000,
//     "items": [ { "name": "temp_high", "tag": "temp", "condition": "gt", "value_tag": "temp_hi",
//                  "message": "Temperature high", "severity": "warning" },
//                { "name": "estop", "tag": "estop", "message": "Emergency stop", "severity": "critical" } ] }
//
// condition: "true" (default: the tag is non-zero / non-empty), "false", "eq", "ne", "gt", "lt",
// "ge", "le" (also "==", "!=", ">", "<", ">=", "<="), compared with "value" or the live value of
// "value_tag". Table "alarm": ts, name, event ("raised" | "cleared"), severity, message, value.
// Evaluated on the UI thread when the tag changes. See README.md ("ロギング").
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "logdb.hpp"

namespace hml {

struct AlarmEvent {
    int64_t     ts_ms = 0;
    std::string name, event, severity, message, value;
};

class Alarms {
public:
    Alarms();
    ~Alarms();

    // Open the database, subscribe to the tags and evaluate every alarm once (an alarm that is
    // already active is recorded as raised). Returns the number of alarms.
    int  load(const json* section, const std::string& base_dir, bool read_only = false);
    void clear();

    bool   enabled() const;
    size_t count() const;
    bool   active(const std::string& name) const;
    std::vector<std::string> active_names() const;

    std::vector<AlarmEvent> query(int limit = 0, bool newest_first = false);
    // CSV: time,name,event,severity,message,value. Empty path = next to the database.
    int64_t export_csv(std::string path = "");
    LogDb& db();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Alarms& alarms();

} // namespace hml
