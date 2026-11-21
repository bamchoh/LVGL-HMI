// Mail: alarms (and what an action asks for) sent by e-mail through an SMTP server (smtp.hpp).
//
//   "mail": {
//     "host": "smtp.example.com", "port": 587, "security": "starttls",     // "starttls" | "tls" | "none"
//     "username": "hmi", "password": "...",       // better in project.local.json (System > Mail): not deployed, not in the project file
//     "from": "hmi@example.com", "to": ["shift@example.com"],
//     "verify": true, "ca_file": "",              // the server's certificate: against ca_file, else against what the system trusts
//     "timeout_ms": 10000, "retry_s": 60, "retries": 5, "queue": 50,
//     "alarms": { "events": ["raised"], "severities": [], "min_interval_s": 60,
//                 "subject": "[{project}] {alarm.severity}: {alarm.message}", "body": "..." }
//   }
//
// A mail waits in a queue and is sent from a thread of its own: the screen and the drivers never
// wait for a mail server. A mail the server cannot be reached for is tried again ("retries" times,
// "retry_s" apart, then twice, three times ... that long); one the server refuses for good is
// dropped at once. The queue is kept in logs/mail_queue.json, so what waits survives a restart;
// when it is full the oldest mail gives way, and that is written to the log.
//
// In the texts {name} is the value of the tag "name", {project} the title of the project,
// {time} the time, and for an alarm {alarm.name} {alarm.message} {alarm.severity} {alarm.value}
// {alarm.event} {alarm.time}. Line ends that a value brings into a subject are taken out.
//
// Actions: { "action": "call", "fn": "mail.send", "arg": "Subject\nBody ..." } and "mail.test".
// Tags: sys.mail.waiting (mails in the queue), sys.mail.error (1 while the last mail failed).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"
#include "smtp.hpp"

namespace hml {

struct AlarmEvent;

struct MailConfig {
    SmtpAccount account;
    SmtpTls     tls;
    std::string from;
    std::vector<std::string> to;
    bool        enabled = true;
    int         retry_s = 60, retries = 5, queue_max = 50;
    // alarms
    bool        on_raised = true, on_cleared = false, on_acked = false;
    std::vector<std::string> severities;        // empty = all
    int         min_interval_s = 60;            // the same alarm is not mailed more often than this
    std::string subject = "[{project}] {alarm.severity}: {alarm.message}";
    std::string body = "{alarm.time}\n{alarm.message}\n\nalarm: {alarm.name}\nstate: {alarm.event}\nvalue: {alarm.value}\n";
};

// "mail" of the project with what project.local.json has on top. Returns what is wrong with it
// (empty = usable); the configuration is filled as far as it goes either way.
std::vector<std::string> mail_parse(const json& section, const json* local, MailConfig& out);
// "a@b, c@d" / ["a@b", "c@d"] -> the addresses; false when one of them is none
bool mail_parse_recipients(const json& v, std::vector<std::string>& out);
// {name} in a text: from vars, else the value of the tag of that name, else left as it is
std::string mail_expand(const std::string& text, const std::map<std::string, std::string>& vars);

class Mail {
public:
    using Sender = std::function<SmtpResult(const SmtpAccount&, const SmtpTls&, const SmtpMessage&)>;

    Mail();
    ~Mail();
    void configure(const json* section, const json* local, const std::string& project_dir, const std::string& store, const std::string& title);
    void clear();
    // From here on mails are sent from a thread (the runtime; not in --check, not in tests: there
    // poll() sends with the sender that was set, at once).
    void start(bool threaded = true);
    // --check: the project folder is only read - nothing is queued, nothing is written.
    void set_read_only(bool on);
    bool configured() const;
    const MailConfig& config() const;

    // Put a mail into the queue. false and *why when mail is off or the mail is nothing to send.
    bool queue(const std::string& subject, const std::string& body, std::string* why = nullptr);
    void on_alarm(const AlarmEvent& e);
    // {project}, {time} and {tag} in a text of an action
    std::string expand(const std::string& text) const;
    // Call regularly (main loop): hands the next mail to the sender, takes what came of the last.
    void poll();
    size_t waiting() const;
    std::string status() const;                 // what came of the last mail, in a line
    uint64_t sent() const;
    uint64_t dropped() const;

    // Changed on the device: kept in project.local.json. An empty password leaves the one there is.
    std::string set_local(bool enabled, const std::string& to, const std::string& username, const std::string& password);

    void set_sender(Sender s);                  // (tests) instead of the network
    void set_clock(std::function<int64_t()> now_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Mail& mail();
void register_mail_actions();

class SettingsPage;
std::unique_ptr<SettingsPage> make_mail_page();     // System > Mail

} // namespace hml
