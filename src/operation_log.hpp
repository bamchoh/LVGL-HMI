// Operation log (操作ロギング): what the operator did on the screens.
//
//   "operation_log": { "path": "logs/operation.db", "retention_days": 90, "max_rows": 100000,
//                      "user_tag": "operator" }
//
// Recorded by the Ui: values written from widgets (switch, slider, input field ...) and the
// actions of widget events (set / toggle / inc / dec, goto / back, script, call, restart ...).
// Table "operation": ts, user, screen, kind, target, old_value, new_value.
//   kind "write"  : target = tag, old_value / new_value
//   kind "screen" : target = screen shown ("(back)" for the back action)
//   kind "script" / "call" / "system" : target = script name / action name / reload, exit, restart
// Writes to the same tag within a second (dragging a slider, holding +) become one row with the
// first old value and the last new value. See README.md ("ロギング").
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "logdb.hpp"

namespace hml {

struct Operation {
    int64_t     ts_ms = 0;
    std::string user, screen, kind, target, old_value, new_value;
};

class OperationLog {
public:
    OperationLog();
    ~OperationLog();

    bool load(const json* section, const std::string& base_dir, bool read_only = false);
    void clear();
    bool enabled() const;

    // UI thread. Does nothing while the log is not enabled.
    void record(const std::string& kind, const std::string& target, const std::string& old_value = "", const std::string& new_value = "");

    std::vector<Operation> query(int limit = 0, bool newest_first = false);
    // CSV: time,user,screen,kind,target,old_value,new_value. Empty path = next to the database.
    int64_t export_csv(std::string path = "");
    LogDb& db();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

OperationLog& operation_log();

} // namespace hml
