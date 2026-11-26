// Lua scripting: project scripts ("scripts" in project.json) and { "action": "script" } actions.
//
//  - One Lua 5.4 state for the whole project: globals set by one script are seen by the others.
//  - Everything runs on the UI thread (LVGL timers, TagStore::dispatch, widget events).
//  - The sandbox has base / string / table / math / utf8 / coroutine and a small os (time, date,
//    clock); no file or process access. A script that runs longer than the timeout is aborted.
//    The memory of the Lua state has a limit: a script that goes over it fails, nothing else.
//
// See README.md ("スクリプト") for the JSON format and the Lua API.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "jsonutil.hpp"

namespace hml {

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    // Drop every script and start over with a fresh Lua state, then compile the "scripts" array
    // of project.json (may be null). Tag subscriptions and interval timers are created here, so
    // LVGL must be initialised. Returns the number of scripts loaded.
    int  load(const json* scripts, const std::string& base_dir);
    // Run the "startup" scripts in file order.
    void run_startup();
    // Remove all scripts, timers and tag subscriptions and close the Lua state.
    void clear();

    // Run a project script by name (any trigger type). False if unknown or it raised an error.
    bool run(const std::string& name);
    bool has(const std::string& name) const;
    // Run a piece of code (compiled once, then cached). label is used in error messages.
    bool exec(const std::string& code, const std::string& label);
    // Compile only: reports syntax errors at load time instead of at the first click.
    bool check(const std::string& code, const std::string& label);

    // Called by the Ui when a screen becomes the current one / stops being it.
    void on_screen(const std::string& name, bool shown);

    size_t count() const;
    int    error_count() const;                 // compile + runtime errors since the last load() / clear()
    const std::string& last_error() const;
    void   set_timeout_ms(uint32_t ms);         // per top-level run, default 500
    // What the Lua state may hold, in bytes (default 32 MB, 0 = no limit). A script that asks for
    // more fails with "not enough memory"; the runtime and the other scripts go on.
    void   set_memory_limit(size_t bytes);
    size_t memory_limit() const;
    size_t memory_used() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

ScriptEngine& scripts();

} // namespace hml
