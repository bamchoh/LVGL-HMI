// One page of a log as text, for the log_view widget (log_view.hpp): the system log, the
// operation log or a group of the data log, newest first, filtered and paged by the database.
// No LVGL here, so that it can be asked from a worker thread and tested without a screen.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hml {

struct LogViewQuery {
    std::string source = "system";      // "system" | "operation" | "data"
    std::string group;                  // data: the group
    std::vector<std::string> columns;   // empty = the usual ones of the source (log_view_columns)
    std::string text;                   // system: part of the message; operation: part of any column
    int         min_level = 0;          // system: 0 = info, 1 = warnings and errors, 2 = errors
    int         days = 0;               // only the last N days (0 = everything)
    int         offset = 0;             // rows to skip (paging)
    int         limit = 20;             // rows of a page
};

struct LogViewRow {
    std::vector<std::string> cells;     // one per column, ready to show
    int level = 0;                      // system: 0 info, 1 warning, 2 error (for the colour); else 0
};

struct LogViewPage {
    std::vector<LogViewRow> rows;       // newest first
    int64_t     total = 0;              // rows that match (all pages)
    std::string problem;                // why there is nothing to show ("" = fine)
};

// The columns a source has:
//   system:    time level source message count
//   operation: time user screen kind target old new
//   data:      time, then the tags of the group
// The usual ones (when the widget names none): system without count, operation without screen.
std::vector<std::string> log_view_known_columns(const std::string& source, const std::string& group);
std::vector<std::string> log_view_columns(const LogViewQuery& q);

// Thread safe (the logs lock their databases). now_ms: the time "days" counts back from (0 = now).
LogViewPage log_view_fetch(const LogViewQuery& q, int64_t now_ms = 0);

// A text from a log as a table cell shows it: one line, control characters and bytes that are
// not UTF-8 replaced, at most max_chars characters ("..." when cut).
std::string log_view_text(const std::string& s, size_t max_chars = 200);

} // namespace hml
