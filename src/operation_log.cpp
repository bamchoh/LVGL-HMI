#include "operation_log.hpp"
#include "lvgl.h"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {
constexpr int64_t kMergeMs = 1000;      // writes to the same tag closer than this are one operation
}

struct OperationLog::Impl {
    LogDb db{ "operation log", "operation", { { "user", "TEXT NOT NULL" }, { "screen", "TEXT NOT NULL" }, { "kind", "TEXT NOT NULL" },
                                               { "target", "TEXT NOT NULL" }, { "old_value", "TEXT" }, { "new_value", "TEXT" } } };
    Tag*        user_tag = nullptr;
    lv_timer_t* timer = nullptr;
    // a "write" waits here for a moment so that a burst on the same tag is stored as one row
    bool        has_pending = false;
    Operation   pending;
    int64_t     pending_last = 0;

    void commit()
    {
        if(!has_pending) return;
        has_pending = false;
        db.add(pending.ts_ms, { pending.user, pending.screen, pending.kind, pending.target, pending.old_value, pending.new_value });
    }
};

OperationLog::OperationLog() : impl_(std::make_unique<Impl>()) {}
OperationLog::~OperationLog() = default;

void OperationLog::clear()
{
    Impl& m = *impl_;
    m.commit();
    if(m.timer && lv_is_initialized()) lv_timer_delete(m.timer);
    m.timer = nullptr;
    m.user_tag = nullptr;
    m.db.close();
}

bool OperationLog::load(const json* section, const std::string& base_dir, bool read_only)
{
    Impl& m = *impl_;
    clear();
    LogDbConfig cfg = parse_log_config(section, base_dir, "logs/operation.db", 100000);
    if(!section || !section->is_object()) return false;
    if(!m.db.open(cfg, read_only)) return false;
    if(read_only) return true;

    std::string ut = jstr(*section, "user_tag", "");
    if(!ut.empty()) {
        m.user_tag = tags().find(ut);
        if(!m.user_tag) HML_WARN("operation log: unknown user_tag '%s'", ut.c_str());
    }
    if(lv_is_initialized()) {
        m.timer = lv_timer_create([](lv_timer_t*) {
            Impl& i = *operation_log().impl_;
            if(i.has_pending && LogDb::now_ms() - i.pending_last >= kMergeMs) i.commit();
        }, 250, nullptr);
    }
    HML_INFO("operation log: recording to '%s'", cfg.path.c_str());
    return true;
}

bool OperationLog::enabled() const { return impl_->db.is_open() && !impl_->db.read_only(); }
LogDb& OperationLog::db() { return impl_->db; }

void OperationLog::record(const std::string& kind, const std::string& target, const std::string& old_value, const std::string& new_value)
{
    Impl& m = *impl_;
    if(!enabled()) return;
    const int64_t now = LogDb::now_ms();
    const bool write = kind == "write";
    if(write && m.has_pending && m.pending.target == target && now - m.pending_last < kMergeMs) {
        m.pending.new_value = new_value;
        m.pending_last = now;
        return;
    }
    m.commit();
    Operation op;
    op.ts_ms = now;
    op.user = m.user_tag ? m.user_tag->get().format() : std::string();
    op.screen = ui().current_screen().value_or("");
    op.kind = kind;
    op.target = target;
    op.old_value = old_value;
    op.new_value = new_value;
    m.pending = std::move(op);
    m.pending_last = now;
    m.has_pending = true;
    if(!write) m.commit();              // only writes are merged
}

std::vector<Operation> OperationLog::query(int limit, bool newest_first)
{
    impl_->commit();
    std::vector<Operation> out;
    for(const LogRow& r : impl_->db.query("", {}, limit, newest_first)) {
        out.push_back({ r[1].i, r[2].s, r[3].s, r[4].s, r[5].s, r[6].str(), r[7].str() });
    }
    return out;
}

int64_t OperationLog::export_csv(std::string path)
{
    if(!impl_->db.is_open()) { HML_ERROR("operation log: export failed: the operation log is not enabled"); return -1; }
    std::vector<std::vector<std::string>> rows;
    for(const Operation& o : query()) rows.push_back({ LogDb::format_time(o.ts_ms), o.user, o.screen, o.kind, o.target, o.old_value, o.new_value });
    if(path.empty()) path = impl_->db.export_path("operation-log");
    return LogDb::write_csv(path, { "time", "user", "screen", "kind", "target", "old_value", "new_value" }, rows);
}

OperationLog& operation_log()
{
    static OperationLog instance;
    return instance;
}

} // namespace hml
