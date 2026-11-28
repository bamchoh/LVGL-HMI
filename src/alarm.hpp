// Alarms (アラームロギング): conditions on tags; every raise / clear / acknowledge is recorded.
//
//   "alarms": { "path": "logs/alarm.db", "retention_days": 90, "max_rows": 100000, "ack_level": 1,
//     "items": [ { "name": "temp_high", "tag": "temp", "condition": "gt", "value_tag": "temp_hi",
//                  "message": "Temperature high", "severity": "warning", "equipment": "Oven 1" },
//                { "name": "estop", "tag": "estop", "message": "Emergency stop", "severity": "critical" },
//                { "name": "info", "tag": "door", "ack": false } ] }
//
// condition: "true" (default: the tag is non-zero / non-empty), "false", "eq", "ne", "gt", "lt",
// "ge", "le" (also "==", "!=", ">", "<", ">=", "<="), compared with "value" or the live value of
// "value_tag". Table "alarm": ts, name, event ("raised" | "cleared" | "acked"), severity, message,
// value, user (who acknowledged). Evaluated on the UI thread when the tag changes.
//
// Acknowledge (確認): an alarm with "ack" (the default) stays in the list of current alarms until
// it is cleared AND acknowledged; "ack": false alarms leave it when they clear. "ack_level" is the
// user level needed to acknowledge (users; 0 = anybody). The state survives a restart: it is
// read back from the database.
//
// Tags kept up to date: sys.alarm.active (number of active alarms), sys.alarm.unacked (number not
// acknowledged), sys.alarm.latest (message of the newest unacknowledged alarm, else of the newest
// active one, else ""), sys.alarm.latest_severity. See README.md ("アラーム").
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include "logdb.hpp"

namespace hml {

struct AlarmEvent {
    int64_t     ts_ms = 0;
    std::string name, event, severity, message, value, user;
};

// An alarm as the lists show it.
struct AlarmStatus {
    std::string name, message, severity, value, acked_by;
    std::string equipment;               // "equipment" of the item (texts resolved), "" when not given
    bool        active = false;
    bool        acked = true;            // nothing to acknowledge
    bool        need_ack = true;
    int64_t     raised_ms = 0, cleared_ms = 0, acked_ms = 0;
    uint64_t    seq = 0;                 // order of the raises within this run

    bool current() const { return active || !acked; }     // shown in the list of current alarms
};

class Alarms {
public:
    Alarms();
    ~Alarms();

    // Open the database, subscribe to the tags and evaluate every alarm once. The state from the
    // last run (active, acknowledged) is read back first, so an alarm that was active and stays
    // active is not recorded again. Returns the number of alarms.
    int  load(const json* section, const std::string& base_dir, bool read_only = false);
    void clear();

    bool   enabled() const;
    size_t count() const;
    bool   active(const std::string& name) const;
    std::vector<std::string> active_names() const;

    // The alarms that are active or not acknowledged, newest raise first.
    std::vector<AlarmStatus> current() const;
    std::optional<AlarmStatus> status(const std::string& name) const;
    size_t active_count() const;
    size_t unacked_count() const;
    // Changes whenever the state of an alarm changes (lists redraw when it differs).
    uint64_t generation() const;
    int ack_level() const;
    std::vector<std::string> severities() const;           // used by the alarms, in definition order
    // "equipment" of an alarm as the project defines it now (texts resolved; "" = none or unknown).
    // The history has no column for it: its rows show what the alarm is set to now.
    std::string equipment_of(const std::string& name) const;

    // Acknowledge (records "acked" with the user). false: unknown, or nothing to acknowledge.
    bool ack(const std::string& name, const std::string& user);
    int  ack_all(const std::string& user);                 // returns how many were acknowledged

    std::vector<AlarmEvent> query(int limit = 0, bool newest_first = false);
    // The history, newest first: events since since_ms (0 = all), of one severity ("" = all).
    std::vector<AlarmEvent> history(int limit, int offset, const std::string& severity = "", int64_t since_ms = 0);
    int64_t history_count(const std::string& severity = "", int64_t since_ms = 0);
    // CSV: time,name,event,severity,message,value,user. Empty path = next to the database.
    int64_t export_csv(std::string path = "");
    LogDb& db();
    // Told about every event as it is recorded ("raised", "cleared", "acked"), on the UI thread.
    void set_listener(std::function<void(const AlarmEvent&)> fn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Alarms& alarms();

// A message for one line of a list: control characters become spaces, at most max_bytes
// (cut at a UTF-8 character boundary, "…" appended).
std::string alarm_text_line(const std::string& s, size_t max_bytes = 200);

} // namespace hml
