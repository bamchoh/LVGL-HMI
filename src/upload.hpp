// Uploads: the logs as CSV files, handed to a server (net_upload.hpp) - for the people who want
// the data of the device somewhere else every night.
//
//   "uploads": [
//     { "name": "nightly", "url": "https://files.example.com/hml/", "method": "put",
//       "username": "hmi", "password": "...",      // better in project.local.json (System > Upload)
//       "logs": ["data:line1", "alarms", "operation", "system"],
//       "verify": true, "ca_file": "", "timeout_ms": 30000, "retry_s": 300, "retries": 3, "keep": 20 }
//   ]
//
// Nothing is sent by itself: { "action": "call", "fn": "upload.run", "arg": "nightly" } does it -
// from a schedule ("schedules": every night), or from a button. A run writes each log to a file
// in logs/upload/<name>/ and the files are then sent one by one from a thread of their own. A
// file is deleted when the server has it. One that could not be sent stays: it is tried again
// ("retries" times, "retry_s" apart, then twice, three times that long), and again at the next
// run and after a restart. More than "keep" files waiting: the oldest is deleted and that is
// written to the log.
//
// Each file holds the whole log as it is at that moment (what the log keeps is bounded by its
// "retention_days" / "max_rows"); its name is <project>_<log>_<YYYYMMDD-HHMMSS>.csv.
//
// Tags: sys.upload.waiting (files waiting), sys.upload.error (1 while the last file failed).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"
#include "net_upload.hpp"

namespace hml {

struct UploadTarget {
    std::string name;
    NetUrl      url;
    bool        post = false;
    std::string username, password;
    std::vector<std::string> logs;      // "data:<group>" | "alarms" | "operation" | "system"
    NetTls      tls;
    uint32_t    timeout_ms = 30000;
    int         retry_s = 300, retries = 3, keep = 20;
    bool        enabled = true;
};

constexpr size_t kUploadTargetsMax = 10;

// One entry of "uploads" with what project.local.json has for it on top (username, password,
// enabled). False with a reason for what is none.
bool upload_parse(const json& j, const json* local, UploadTarget& out, std::string* err = nullptr);

class Uploads {
public:
    using Sender = std::function<NetResult(const NetUpload&, const NetTls&)>;
    // writes the log (as named in "logs") to the file; rows written, -1 = it could not
    using Exporter = std::function<int64_t(const std::string& log, const std::string& path)>;

    Uploads();
    ~Uploads();
    void configure(const json* section, const json* local, const std::string& project_dir, const std::string& store, const std::string& title);
    void clear();
    void start(bool threaded = true);
    void set_read_only(bool on);                // --check: nothing is written, nothing is sent
    bool configured() const;
    const std::vector<UploadTarget>& targets() const;

    // Write the logs of a target ("" = of every target) to files and put them in line. Returns
    // how many files were written, -1 and *why when there is no such target (or uploads are off).
    int  run(const std::string& name, std::string* why = nullptr);
    void poll();
    size_t waiting(const std::string& name = "") const;
    std::string status(const std::string& name) const;
    uint64_t sent() const;
    uint64_t dropped() const;

    // Changed on the device: kept in project.local.json. An empty password leaves the one there is.
    std::string set_local(const std::string& name, bool enabled, const std::string& username, const std::string& password);

    void set_sender(Sender s);                  // (tests)
    void set_exporter(Exporter e);
    void set_clock(std::function<int64_t()> now_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Uploads& uploads();
void register_upload_actions();

class SettingsPage;
std::unique_ptr<SettingsPage> make_upload_page();   // System > Upload

} // namespace hml
