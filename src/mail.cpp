#include "mail.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "alarm.hpp"
#include "logdb.hpp"
#include "operation_log.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr const char* kWaitingTag = "sys.mail.waiting";
constexpr const char* kErrorTag = "sys.mail.error";

std::string one_line(const std::string& s, size_t max = 80)
{
    std::string out = smtp_header_text(s, max);
    if(s.size() > max) out += "...";
    return out;
}

const char* security_name(SmtpSecurity s) { return s == SmtpSecurity::Tls ? "tls" : s == SmtpSecurity::None ? "none" : "starttls"; }

} // namespace

bool mail_parse_recipients(const json& v, std::vector<std::string>& out)
{
    std::vector<std::string> list;
    if(v.is_string()) {
        std::string word;
        for(char c : v.get<std::string>() + ",") {
            if(c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\n' || c == '\r') { if(!word.empty()) list.push_back(word); word.clear(); }
            else word += c;
        }
    }
    else if(v.is_array()) { for(const json& a : v) { if(!a.is_string()) return false; list.push_back(a.get<std::string>()); } }
    else return false;
    if(list.size() > kMailRecipientsMax) return false;
    for(const std::string& a : list) if(!smtp_address_ok(a)) return false;
    out = std::move(list);
    return true;
}

std::vector<std::string> mail_parse(const json& section, const json* local, MailConfig& c)
{
    std::vector<std::string> bad;
    c = MailConfig();
    if(!section.is_object()) { bad.push_back("\"mail\" is an object"); return bad; }
    // what was set on the device goes on top of the project (a few keys only)
    json s = section;
    if(local && local->is_object())
        for(const char* k : { "enabled", "to", "username", "password" }) if(local->contains(k)) s[k] = (*local)[k];

    c.enabled = jbool(s, "enabled", true);
    c.account.host = jstr(s, "host", "");
    if(c.account.host.empty()) bad.push_back("no \"host\" (the mail server)");
    else if(c.account.host.size() > 253 || c.account.host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]") != std::string::npos) {
        bad.push_back("\"host\" is not the name of a server");
        c.account.host.clear();
    }
    const std::string sec = jstr(s, "security", "starttls");
    if(sec == "starttls") c.account.security = SmtpSecurity::StartTls;
    else if(sec == "tls") c.account.security = SmtpSecurity::Tls;
    else if(sec == "none") c.account.security = SmtpSecurity::None;
    else bad.push_back("\"security\" is \"starttls\", \"tls\" or \"none\"");
    const double port = jnum(s, "port", c.account.security == SmtpSecurity::Tls ? 465 : c.account.security == SmtpSecurity::None ? 25 : 587);
    if(port >= 1 && port <= 65535 && port == static_cast<int>(port)) c.account.port = static_cast<uint16_t>(port);
    else bad.push_back("\"port\" is 1 to 65535");
    c.account.username = jstr(s, "username", "");
    c.account.password = jstr(s, "password", "");
    c.account.timeout_ms = static_cast<uint32_t>(std::clamp(jnum(s, "timeout_ms", 10000), 1000.0, 120000.0));
    c.tls.verify = jbool(s, "verify", true);
    c.tls.ca_file = jstr(s, "ca_file", "");
    c.from = jstr(s, "from", "");
    if(!smtp_address_ok(c.from)) { bad.push_back("\"from\" is the mail address of the sender"); c.from.clear(); }
    if(const json* to = jget(s, "to")) { if(!mail_parse_recipients(*to, c.to)) bad.push_back("\"to\" is a mail address, or a list of them (20 at most)"); }
    if(c.to.empty() && std::find(bad.begin(), bad.end(), "\"to\" is a mail address, or a list of them (20 at most)") == bad.end()) bad.push_back("no recipient (\"to\")");
    c.retry_s = static_cast<int>(std::clamp(jnum(s, "retry_s", 60), 5.0, 86400.0));
    c.retries = static_cast<int>(std::clamp(jnum(s, "retries", 5), 0.0, 100.0));
    c.queue_max = static_cast<int>(std::clamp(jnum(s, "queue", 50), 1.0, 500.0));
    if(const json* a = jget(s, "alarms")) {
        if(a->is_boolean()) { if(!a->get<bool>()) c.on_raised = false; }
        else if(a->is_object()) {
            if(const json* ev = jget(*a, "events")) {
                c.on_raised = c.on_cleared = c.on_acked = false;
                if(ev->is_array()) for(const json& e : *ev) {
                    const std::string n = e.is_string() ? e.get<std::string>() : "";
                    if(n == "raised") c.on_raised = true;
                    else if(n == "cleared") c.on_cleared = true;
                    else if(n == "acked") c.on_acked = true;
                    else bad.push_back("alarms.events: \"raised\", \"cleared\" or \"acked\"");
                }
                else bad.push_back("alarms.events is a list");
            }
            if(const json* sv = jget(*a, "severities"); sv && sv->is_array()) for(const json& e : *sv) if(e.is_string()) c.severities.push_back(e.get<std::string>());
            c.min_interval_s = static_cast<int>(std::clamp(jnum(*a, "min_interval_s", 60), 0.0, 86400.0));
            if(auto t = jstr_opt(*a, "subject"); t && !t->empty()) c.subject = *t;
            if(auto t = jstr_opt(*a, "body"); t && !t->empty()) c.body = *t;
        }
        else bad.push_back("\"alarms\" is an object, or false");
    }
    return bad;
}

std::string mail_expand(const std::string& text, const std::map<std::string, std::string>& vars)
{
    std::string out;
    for(size_t i = 0; i < text.size();) {
        if(text[i] != '{') { out += text[i++]; continue; }
        size_t j = i + 1;
        while(j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_' || text[j] == '.')) j++;
        if(j >= text.size() || text[j] != '}' || j == i + 1 || j - i > 100) { out += text[i++]; continue; }
        const std::string name = text.substr(i + 1, j - i - 1);
        auto v = vars.find(name);
        if(v != vars.end()) out += v->second;
        else if(Tag* t = tags().find(name)) out += t->get().format();
        else out += text.substr(i, j - i + 1);                  // (what names nothing stays as it is written)
        i = j + 1;
    }
    return out;
}

// ================================================================ Mail

struct Mail::Impl {
    struct Item {
        std::string subject, body;
        int64_t time_s = 0, next_try_ms = 0;
        int attempts = 0;
    };
    struct Job {
        SmtpAccount acc;
        SmtpTls tls;
        SmtpMessage msg;
        std::atomic<bool> done{ false };
        SmtpResult res;
    };

    bool configured = false, threaded = false, read_only = false;
    MailConfig cfg;
    std::vector<std::string> problems;
    json section, local;
    std::string dir, store, title, status = "nothing sent yet";
    std::deque<Item> queue;
    std::shared_ptr<Job> job;
    Sender sender;
    std::function<int64_t()> clock;
    uint64_t sent = 0, dropped = 0;
    bool last_failed = false;
    std::map<std::string, int64_t> last_alarm;          // "name/event" -> when it was mailed last

    int64_t now() const { return clock ? clock() : std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
    // "ca_file" is a file of the project unless it says where else it is
    void resolve_ca()
    {
        if(cfg.tls.ca_file.empty() || dir.empty() || std::filesystem::u8path(cfg.tls.ca_file).is_absolute()) return;
        cfg.tls.ca_file = (std::filesystem::u8path(dir) / std::filesystem::u8path(cfg.tls.ca_file)).generic_u8string();
    }
    std::string queue_file() const { return dir.empty() ? "" : dir + "/logs/mail_queue.json"; }

    void tags_update()
    {
        if(Tag* t = tags().find(kWaitingTag)) t->set_number(static_cast<double>(queue.size()), this);
        if(Tag* t = tags().find(kErrorTag)) t->set_number(last_failed ? 1 : 0, this);
    }

    void save_queue()
    {
        const std::string f = queue_file();
        if(f.empty()) return;
        std::error_code ec;
        if(queue.empty()) { std::filesystem::remove(std::filesystem::u8path(f), ec); return; }
        std::filesystem::create_directories(std::filesystem::u8path(dir + "/logs"), ec);
        json list = json::array();
        for(const Item& it : queue) list.push_back({ { "subject", it.subject }, { "body", it.body }, { "time", it.time_s }, { "attempts", it.attempts } });
        std::string err;
        if(!write_file_atomic(f, json{ { "mails", list } }.dump(1, ' ', false, json::error_handler_t::replace) + "\n", &err)) HML_WARN("mail: the queue could not be saved: %s", err.c_str());
    }

    void load_queue()
    {
        const std::string f = queue_file();
        if(f.empty()) return;
        const auto text = read_file(f, 8u * 1024 * 1024);
        if(!text) return;
        const json j = json::parse(*text, nullptr, false, true);
        const json* list = j.is_object() ? jget(j, "mails") : nullptr;
        if(!list || !list->is_array()) return;
        for(const json& m : *list) {
            if(!m.is_object() || static_cast<int>(queue.size()) >= cfg.queue_max) continue;
            Item it;
            it.subject = smtp_header_text(jstr(m, "subject", ""));
            it.body = jstr(m, "body", "");
            if(it.body.size() > kMailBodyMax) it.body.resize(kMailBodyMax);
            it.time_s = static_cast<int64_t>(jnum(m, "time", 0));
            it.attempts = static_cast<int>(std::clamp(jnum(m, "attempts", 0), 0.0, 1000.0));
            if(it.subject.empty() && it.body.empty()) continue;
            queue.push_back(std::move(it));
        }
        if(!queue.empty()) HML_INFO("mail: %zu mail(s) from before the restart are waiting", queue.size());
    }

    // what came of the mail at the head of the queue
    void finished(const SmtpResult& r)
    {
        if(queue.empty()) return;
        Item& it = queue.front();
        const std::string what = "\"" + one_line(it.subject, 60) + "\"";
        if(r.ok) {
            sent++;
            last_failed = false;
            status = "sent " + LogDb::format_time(now()).substr(0, 19) + ": " + one_line(it.subject, 60);
            HML_INFO("mail: sent %s to %zu recipient(s)", what.c_str(), cfg.to.size() - r.refused.size());
            for(const std::string& x : r.refused) HML_WARN("mail: the server did not take the recipient %s", one_line(x, 160).c_str());
            if(!r.refused.empty()) status += " (" + std::to_string(r.refused.size()) + " recipient(s) refused)";
            queue.pop_front();
        }
        else {
            last_failed = true;
            it.attempts++;
            if(r.permanent) {
                dropped++;
                status = "not sent: " + one_line(r.error, 200);
                HML_WARN("mail: %s is not sent: %s", what.c_str(), one_line(r.error, 300).c_str());
                queue.pop_front();
            }
            else if(it.attempts > cfg.retries) {
                dropped++;
                status = "given up after " + std::to_string(it.attempts) + " tries: " + one_line(r.error, 200);
                HML_WARN("mail: %s is given up after %d tries: %s", what.c_str(), it.attempts, one_line(r.error, 300).c_str());
                queue.pop_front();
            }
            else {
                // a little longer each time: 1, 2, 3 ... times retry_s
                it.next_try_ms = now() + static_cast<int64_t>(cfg.retry_s) * 1000 * it.attempts;
                status = "waiting (try " + std::to_string(it.attempts) + " of " + std::to_string(cfg.retries + 1) + " failed): " + one_line(r.error, 200);
                if(it.attempts == 1) HML_WARN("mail: %s could not be sent, it is tried again: %s", what.c_str(), one_line(r.error, 300).c_str());
            }
        }
        save_queue();
        tags_update();
    }
};

Mail::Mail() : impl_(std::make_unique<Impl>()) { impl_->sender = smtp_deliver; }
Mail::~Mail() = default;

void Mail::clear()
{
    Impl& m = *impl_;
    // (a mail that is on its way goes on in its thread; it holds what it needs itself. Whether it
    // arrived is not known here any more: it stays in the queue file and is sent again after the
    // next start - twice is better than never for an alarm)
    const bool threaded = m.threaded;
    Sender sender = m.sender;
    auto clock = m.clock;
    *impl_ = Impl();
    impl_->threaded = threaded;
    impl_->sender = sender;
    impl_->clock = clock;
}

void Mail::configure(const json* section, const json* local, const std::string& project_dir, const std::string& store, const std::string& title)
{
    clear();
    Impl& m = *impl_;
    if(!section) return;
    m.configured = true;
    m.section = *section;
    m.local = local && local->is_object() ? *local : json::object();
    m.dir = project_dir;
    m.store = store;
    m.title = title;
    m.problems = mail_parse(m.section, &m.local, m.cfg);
    m.resolve_ca();
    for(const std::string& p : m.problems) HML_WARN("mail: %s", p.c_str());
    if(!m.problems.empty()) m.status = "not set up: " + m.problems.front();
    else {
        if(m.cfg.account.security == SmtpSecurity::None)
            HML_WARN("mail %s: not secured (\"security\": \"none\"): the mails%s can be read on the network", m.cfg.account.host.c_str(), m.cfg.account.username.empty() ? "" : ", the user name and the password");
        if(!m.cfg.tls.verify && m.cfg.account.security != SmtpSecurity::None)
            HML_WARN("mail %s: the certificate of the server is not checked (\"verify\": false): anybody can pose as the server", m.cfg.account.host.c_str());
        HML_INFO("mail: %s:%u (%s), %zu recipient(s)%s", m.cfg.account.host.c_str(), m.cfg.account.port, security_name(m.cfg.account.security), m.cfg.to.size(), m.cfg.enabled ? "" : ", switched off");
    }
    for(const char* name : { kWaitingTag, kErrorTag }) {
        if(tags().find(name)) continue;
        Value v = Value::number(0, name == std::string(kErrorTag) ? TagType::Bool : TagType::Int);
        tags().create(name, v.type, &v, false, 0, 0);
    }
    m.load_queue();
    m.tags_update();
}

void Mail::start(bool threaded) { impl_->threaded = threaded; }
void Mail::set_read_only(bool on) { impl_->read_only = on; }
bool Mail::configured() const { return impl_->configured; }
const MailConfig& Mail::config() const { return impl_->cfg; }
size_t Mail::waiting() const { return impl_->queue.size(); }
std::string Mail::status() const { return impl_->status; }
uint64_t Mail::sent() const { return impl_->sent; }
uint64_t Mail::dropped() const { return impl_->dropped; }
void Mail::set_sender(Sender s) { impl_->sender = s ? std::move(s) : Sender(smtp_deliver); }
void Mail::set_clock(std::function<int64_t()> now_ms) { impl_->clock = std::move(now_ms); }

bool Mail::queue(const std::string& subject, const std::string& body, std::string* why)
{
    Impl& m = *impl_;
    auto no = [&](const std::string& w) { if(why) *why = w; return false; };
    if(!m.configured) return no("the project has no \"mail\"");
    if(m.read_only) return no("mails are not sent while the project is only checked");
    if(!m.problems.empty()) return no("mail is not set up: " + m.problems.front());
    if(!m.cfg.enabled) return no("mail is switched off");
    Impl::Item it;
    it.subject = smtp_header_text(subject);
    it.body = body.size() > kMailBodyMax ? body.substr(0, kMailBodyMax) : body;
    if(it.subject.empty() && it.body.empty()) return no("an empty mail");
    if(it.subject.empty()) it.subject = m.title.empty() ? "HML" : m.title;
    it.time_s = m.now() / 1000;
    // full: the oldest gives way - but not the one that is being sent right now
    while(static_cast<int>(m.queue.size()) >= m.cfg.queue_max) {
        const size_t victim = m.job && m.queue.size() > 1 ? 1 : 0;
        if(m.job && m.queue.size() == 1) break;
        HML_WARN("mail: the queue is full (%d): the oldest mail \"%s\" is dropped", m.cfg.queue_max, one_line(m.queue[victim].subject, 60).c_str());
        m.queue.erase(m.queue.begin() + static_cast<std::ptrdiff_t>(victim));
        m.dropped++;
    }
    m.queue.push_back(std::move(it));
    m.save_queue();
    m.tags_update();
    return true;
}

void Mail::on_alarm(const AlarmEvent& e)
{
    Impl& m = *impl_;
    if(!m.configured || m.read_only || !m.problems.empty() || !m.cfg.enabled) return;
    const MailConfig& c = m.cfg;
    if(!((e.event == "raised" && c.on_raised) || (e.event == "cleared" && c.on_cleared) || (e.event == "acked" && c.on_acked))) return;
    if(!c.severities.empty() && std::find(c.severities.begin(), c.severities.end(), e.severity) == c.severities.end()) return;
    // an alarm that flickers does not fill mailboxes
    const std::string key = e.name + "/" + e.event;
    const int64_t now = m.now();
    auto last = m.last_alarm.find(key);
    if(last != m.last_alarm.end() && c.min_interval_s > 0 && now - last->second < static_cast<int64_t>(c.min_interval_s) * 1000 && now >= last->second) return;
    m.last_alarm[key] = now;
    const std::map<std::string, std::string> vars{
        { "project", m.title }, { "time", LogDb::format_time(now).substr(0, 19) },
        { "alarm.name", e.name }, { "alarm.message", e.message }, { "alarm.severity", e.severity }, { "alarm.value", e.value },
        { "alarm.event", e.event }, { "alarm.time", LogDb::format_time(e.ts_ms).substr(0, 19) }, { "alarm.user", e.user } };
    std::string why;
    if(!queue(mail_expand(c.subject, vars), mail_expand(c.body, vars), &why)) HML_WARN("mail: alarm '%s': %s", e.name.c_str(), why.c_str());
}

void Mail::poll()
{
    Impl& m = *impl_;
    if(!m.configured) return;
    if(m.job) {
        if(!m.job->done) return;
        const SmtpResult r = m.job->res;
        m.job.reset();
        m.finished(r);
    }
    if(m.queue.empty() || !m.problems.empty() || !m.cfg.enabled) return;
    Impl::Item& it = m.queue.front();
    if(m.now() < it.next_try_ms) return;
    auto job = std::make_shared<Impl::Job>();
    job->acc = m.cfg.account;
    job->tls = m.cfg.tls;
    job->msg.from = m.cfg.from;
    job->msg.to = m.cfg.to;
    job->msg.subject = it.subject;
    job->msg.body = it.body;
    job->msg.time_s = it.time_s;
    Sender sender = m.sender;
    if(m.threaded) {
        m.job = job;
        std::thread([job, sender] { job->res = sender(job->acc, job->tls, job->msg); job->done = true; }).detach();
        return;
    }
    m.finished(sender(job->acc, job->tls, job->msg));
}

std::string Mail::set_local(bool enabled, const std::string& to, const std::string& username, const std::string& password)
{
    Impl& m = *impl_;
    if(!m.configured) return "the project has no \"mail\"";
    std::vector<std::string> list;
    if(!mail_parse_recipients(json(to), list)) return "\"" + one_line(to, 60) + "\": not a list of mail addresses (a@b.example, c@d.example)";
    if(list.empty()) return "No recipient.";
    if(username.find_first_of(std::string("\r\n\0", 3)) != std::string::npos || password.find_first_of(std::string("\r\n\0", 3)) != std::string::npos || username.size() > 200 || password.size() > 200)
        return "The user name or the password cannot be used.";
    // what differs from the project is kept (a later deploy of the project still counts for the rest)
    MailConfig base;
    mail_parse(m.section, nullptr, base);
    json mine = json::object();
    if(enabled != base.enabled) mine["enabled"] = enabled;
    if(list != base.to) mine["to"] = list;
    if(username != base.account.username) mine["username"] = username;
    const std::string old_pw = jstr(m.local, "password", "");
    if(!password.empty()) { if(password != base.account.password) mine["password"] = password; }
    else if(!old_pw.empty()) mine["password"] = old_pw;                          // (an empty field: the password stays)
    if(!m.store.empty()) {
        json overlay = json::object();
        {
            std::ifstream f(std::filesystem::u8path(m.store), std::ios::binary);
            if(f) {
                std::stringstream ss;
                ss << f.rdbuf();
                overlay = json::parse(ss.str(), nullptr, false, true);
                if(!overlay.is_object()) overlay = json::object();
            }
        }
        if(mine.empty()) overlay.erase("mail");
        else overlay["mail"] = mine;
        std::string err;
        if(overlay.empty()) { std::error_code ec; std::filesystem::remove(std::filesystem::u8path(m.store), ec); }
        else if(!write_file_atomic(m.store, overlay.dump(2) + "\n", &err)) return err;
    }
    // who changed what (never the password itself)
    auto join = [](const std::vector<std::string>& v) { std::string s; for(const auto& a : v) s += (s.empty() ? "" : ", ") + a; return s; };
    if(list != m.cfg.to) operation_log().record("mail", "to", join(m.cfg.to), join(list));
    if(enabled != m.cfg.enabled) operation_log().record("mail", "enabled", m.cfg.enabled ? "on" : "off", enabled ? "on" : "off");
    if(username != m.cfg.account.username) operation_log().record("mail", "username", m.cfg.account.username, username);
    if(!password.empty() && password != m.cfg.account.password) operation_log().record("mail", "password", "", "(changed)");
    m.local = mine;
    m.problems = mail_parse(m.section, &m.local, m.cfg);
    m.resolve_ca();
    if(!m.problems.empty()) m.status = "not set up: " + m.problems.front();
    return "";
}

std::string Mail::expand(const std::string& text) const
{
    return mail_expand(text, { { "project", impl_->title }, { "time", LogDb::format_time(impl_->now()).substr(0, 19) } });
}

Mail& mail()
{
    static Mail instance;
    return instance;
}

void register_mail_actions()
{
    // "Subject\nBody ...": the first line is the subject; {tag} is the value of a tag
    ui().register_action("mail.send", [](const std::string& arg) {
        const size_t nl = arg.find('\n');
        std::string why;
        if(!mail().queue(mail().expand(arg.substr(0, nl)), nl == std::string::npos ? "" : mail().expand(arg.substr(nl + 1)), &why)) HML_WARN("mail.send: %s", why.c_str());
    });
    ui().register_action("mail.test", [](const std::string&) {
        std::string why;
        if(!mail().queue("Test mail", "This is a test mail from the HMI.\n", &why)) HML_WARN("mail.test: %s", why.c_str());
    });
}

// ================================================================ System > Mail

namespace {

class MailPage final : public SettingsPage {
public:
    std::string id() const override { return "mail"; }
    std::string title() const override { return "Mail"; }

    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        const MailConfig& c = mail().config();
        auto add = [&](const char* key, const char* label, SettingField::Type type, const std::string& value, bool password = false) {
            SettingField f; f.key = key; f.label = label; f.type = type; f.value = value; f.password = password; out.push_back(f);
        };
        std::string to;
        for(const std::string& a : c.to) to += (to.empty() ? "" : ", ") + a;
        add("enabled", "Send mails", SettingField::Type::Bool, c.enabled ? "1" : "0");
        add("to", "To", SettingField::Type::Text, to);
        add("username", "User name", SettingField::Type::Text, c.account.username);
        // (never shown, not even as dots: empty means "leave it as it is")
        add("password", "Password (empty: unchanged)", SettingField::Type::Text, "", true);
        add("server", "Server", SettingField::Type::Info, c.account.host.empty() ? "-" : c.account.host + ":" + std::to_string(c.account.port) + " (" + security_name(c.account.security) + ")");
        shown_ = mail().status() + "|" + std::to_string(mail().waiting());
        add("state", "State", SettingField::Type::Info, mail().status());
        add("waiting", "Waiting", SettingField::Type::Info, std::to_string(mail().waiting()));
        return out;
    }
    std::string apply(const std::map<std::string, std::string>& v) override
    {
        auto get = [&](const char* k) { auto it = v.find(k); return it == v.end() ? std::string() : it->second; };
        const std::string err = mail().set_local(get("enabled") != "0", get("to"), get("username"), get("password"));
        return err.empty() ? "Saved." : err;
    }
    std::vector<std::pair<std::string, std::string>> buttons() const override { return { { "Send a test mail", "test" } }; }
    void command(const std::string& cmd, const std::string&) override
    {
        if(cmd != "test") { HML_WARN("system: mail: unknown command '%s'", cmd.c_str()); return; }
        std::string why;
        const bool ok = mail().queue("Test mail", "This is a test mail from the HMI.\n", &why);
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string(ok ? "A test mail is on its way: see \"State\"." : why, nullptr);
    }
    // The state moves while a mail is on its way. Only the two lines that show it are written:
    // what the operator is typing into the other fields is not touched.
    bool poll() override
    {
        const std::string now = mail().status() + "|" + std::to_string(mail().waiting());
        if(now == shown_) return false;
        shown_ = now;
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "state"))) t->set_string(mail().status(), nullptr);
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "waiting"))) t->set_string(std::to_string(mail().waiting()), nullptr);
        return false;
    }

private:
    std::string shown_;
};

} // namespace

std::unique_ptr<SettingsPage> make_mail_page() { return std::make_unique<MailPage>(); }

} // namespace hml
