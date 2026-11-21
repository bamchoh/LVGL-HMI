#include "upload.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#include "alarm.hpp"
#include "data_log.hpp"
#include "logdb.hpp"
#include "operation_log.hpp"
#include "system_log.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace fs = std::filesystem;
namespace {

constexpr const char* kWaitingTag = "sys.upload.waiting";
constexpr const char* kErrorTag = "sys.upload.error";

std::string one_line(const std::string& s, size_t max = 200)
{
    std::string out;
    for(char c : s) out += (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) ? ' ' : c;
    if(out.size() > max) out = out.substr(0, max) + "...";
    return out;
}

bool name_ok(const std::string& n)
{
    return !n.empty() && n.size() <= 32 && std::all_of(n.begin(), n.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}

bool log_ok(const std::string& l)
{
    if(l == "alarms" || l == "operation" || l == "system") return true;
    return l.rfind("data:", 0) == 0 && l.size() > 5 && l.size() < 100;
}

// the real logs, written with the functions the "export" actions use
int64_t export_log(const std::string& log, const std::string& path)
{
    if(log == "alarms") return alarms().enabled() ? alarms().export_csv(path) : -1;
    if(log == "operation") return operation_log().export_csv(path);
    if(log == "system") return system_log().export_csv(path);
    if(log.rfind("data:", 0) == 0) return data_log().export_csv(log.substr(5), path);
    return -1;
}

} // namespace

bool upload_parse(const json& j, const json* local, UploadTarget& out, std::string* err)
{
    auto fail = [&](const std::string& why) { if(err) *err = why; return false; };
    if(!j.is_object()) return fail("an upload is { \"name\": ..., \"url\": ..., \"logs\": [...] }");
    UploadTarget t;
    t.name = jstr(j, "name", "");
    if(!name_ok(t.name)) return fail("\"name\": letters, digits, _ and - (32 at most)");
    std::string why;
    const json* url = jget(j, "url");
    if(!url || !url->is_string()) return fail("no \"url\"");
    if(!net_url_parse(url->get<std::string>(), t.url, &why)) return fail("\"url\": " + why);
    const std::string method = jstr(j, "method", "put");
    if(method == "post") t.post = true;
    else if(method != "put") return fail("\"method\" is \"put\" or \"post\"");
    if(t.url.scheme == "ftp" && jhas(j, "method")) return fail("\"method\" has no meaning for ftp://");
    t.username = jstr(j, "username", "");
    t.password = jstr(j, "password", "");
    t.enabled = jbool(j, "enabled", true);
    if(local && local->is_object()) {
        if(const json* v = jget(*local, "username"); v && v->is_string()) t.username = v->get<std::string>();
        if(const json* v = jget(*local, "password"); v && v->is_string()) t.password = v->get<std::string>();
        if(const json* v = jget(*local, "enabled"); v && v->is_boolean()) t.enabled = v->get<bool>();
    }
    const json* logs = jget(j, "logs");
    if(!logs || !logs->is_array() || logs->empty()) return fail("\"logs\" names what is sent: \"data:<group>\", \"alarms\", \"operation\", \"system\"");
    for(const json& l : *logs) {
        if(!l.is_string() || !log_ok(l.get<std::string>())) return fail("\"logs\": \"data:<group>\", \"alarms\", \"operation\" or \"system\"");
        if(std::find(t.logs.begin(), t.logs.end(), l.get<std::string>()) == t.logs.end()) t.logs.push_back(l.get<std::string>());
    }
    if(t.logs.size() > 40) return fail("more than 40 logs in one upload");
    t.tls.verify = jbool(j, "verify", true);
    t.tls.ca_file = jstr(j, "ca_file", "");
    t.timeout_ms = static_cast<uint32_t>(std::clamp(jnum(j, "timeout_ms", 30000), 1000.0, 600000.0));
    t.retry_s = static_cast<int>(std::clamp(jnum(j, "retry_s", 300), 5.0, 86400.0));
    t.retries = static_cast<int>(std::clamp(jnum(j, "retries", 3), 0.0, 100.0));
    t.keep = static_cast<int>(std::clamp(jnum(j, "keep", 20), 1.0, 500.0));
    out = std::move(t);
    return true;
}

// ================================================================ Uploads

struct Uploads::Impl {
    struct State {
        std::vector<std::string> files;         // waiting, oldest first (names in logs/upload/<name>/)
        int attempts = 0;
        int64_t next_try_ms = 0;
        bool paused = false;                    // the tries are used up: the next run (or start) goes on
        std::string status = "nothing sent yet";
    };
    struct Job {
        NetUpload up;
        NetTls tls;
        std::string target, file;
        std::atomic<bool> done{ false };
        NetResult res;
    };

    bool configured = false, threaded = false, read_only = false;
    std::vector<UploadTarget> targets;
    std::map<std::string, State> state;
    json section, local;
    std::string dir, store, title;
    std::shared_ptr<Job> job;
    Sender sender;
    Exporter exporter;
    std::function<int64_t()> clock;
    uint64_t sent = 0, dropped = 0;
    bool last_failed = false;

    int64_t now() const { return clock ? clock() : std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
    fs::path folder(const std::string& target) const { return fs::u8path(dir) / "logs" / "upload" / fs::u8path(target); }
    UploadTarget* find(const std::string& name) { for(UploadTarget& t : targets) if(t.name == name) return &t; return nullptr; }

    void tags_update()
    {
        size_t n = 0;
        for(const auto& [k, s] : state) n += s.files.size();
        if(Tag* t = tags().find(kWaitingTag)) t->set_number(static_cast<double>(n), this);
        if(Tag* t = tags().find(kErrorTag)) t->set_number(last_failed ? 1 : 0, this);
    }

    // what waits in the folder of a target (after a restart; names sort by time)
    void scan(const UploadTarget& t)
    {
        State& s = state[t.name];
        s.files.clear();
        std::error_code ec;
        for(const auto& e : fs::directory_iterator(folder(t.name), ec)) {
            const std::string n = e.path().filename().generic_u8string();
            if(e.is_regular_file(ec) && n.size() > 4 && n.compare(n.size() - 4, 4, ".csv") == 0 && net_file_name(n) == n) s.files.push_back(n);
        }
        std::sort(s.files.begin(), s.files.end(), [](const std::string& a, const std::string& b) {
            // ..._YYYYMMDD-HHMMSS.csv: by that time, then by name
            const std::string ta = a.size() > 19 ? a.substr(a.size() - 19) : a, tb = b.size() > 19 ? b.substr(b.size() - 19) : b;
            return ta != tb ? ta < tb : a < b;
        });
        trim(t);
    }

    // more than "keep" waiting: the oldest go
    void trim(const UploadTarget& t)
    {
        State& s = state[t.name];
        while(static_cast<int>(s.files.size()) > t.keep) {
            const size_t victim = job && job->target == t.name && !s.files.empty() && job->file == s.files.front() && s.files.size() > 1 ? 1 : 0;
            if(job && job->target == t.name && s.files.size() == 1) break;
            HML_WARN("upload '%s': more than %d files are waiting: the oldest (%s) is deleted", t.name.c_str(), t.keep, s.files[victim].c_str());
            std::error_code ec;
            fs::remove(folder(t.name) / fs::u8path(s.files[victim]), ec);
            s.files.erase(s.files.begin() + static_cast<std::ptrdiff_t>(victim));
            dropped++;
        }
    }

    void finished(const std::string& target, const std::string& file, const NetResult& r)
    {
        UploadTarget* t = find(target);
        auto it = state.find(target);
        if(!t || it == state.end()) return;
        State& s = it->second;
        const bool still_first = !s.files.empty() && s.files.front() == file;
        if(r.ok) {
            sent++;
            last_failed = false;
            s.attempts = 0;
            s.next_try_ms = 0;
            s.status = "sent " + LogDb::format_time(now()).substr(0, 19) + ": " + file;
            HML_INFO("upload '%s': %s sent to %s", target.c_str(), file.c_str(), t->url.shown().c_str());
            std::error_code ec;
            fs::remove(folder(target) / fs::u8path(file), ec);
            if(still_first) s.files.erase(s.files.begin());
        }
        else {
            last_failed = true;
            s.attempts++;
            // the file stays either way: the cause (a password, a full disk at the server) can be
            // put right, and the next run or the next start tries again
            if(r.permanent || s.attempts > t->retries) {
                s.paused = true;
                s.status = std::string(r.permanent ? "not sent" : "given up after " + std::to_string(s.attempts) + " tries") + ": " + one_line(r.error) + " (" + std::to_string(s.files.size()) + " file(s) wait for the next run)";
                HML_WARN("upload '%s': %s is not sent: %s. It is kept and tried again at the next run.", target.c_str(), file.c_str(), one_line(r.error, 300).c_str());
            }
            else {
                s.next_try_ms = now() + static_cast<int64_t>(t->retry_s) * 1000 * s.attempts;
                s.status = "waiting (try " + std::to_string(s.attempts) + " of " + std::to_string(t->retries + 1) + " failed): " + one_line(r.error);
                if(s.attempts == 1) HML_WARN("upload '%s': %s could not be sent, it is tried again: %s", target.c_str(), file.c_str(), one_line(r.error, 300).c_str());
            }
        }
        tags_update();
    }
};

Uploads::Uploads() : impl_(std::make_unique<Impl>())
{
    impl_->sender = net_upload;
    impl_->exporter = export_log;
}
Uploads::~Uploads() = default;

void Uploads::clear()
{
    Impl& m = *impl_;
    // (a file that is on its way goes on in its thread; it stays on disk until the server is
    // known to have it, so the next start sends it again if we never hear)
    const bool threaded = m.threaded;
    Sender sender = m.sender;
    Exporter exporter = m.exporter;
    auto clock = m.clock;
    *impl_ = Impl();
    impl_->threaded = threaded;
    impl_->sender = sender;
    impl_->exporter = exporter;
    impl_->clock = clock;
}

void Uploads::configure(const json* section, const json* local, const std::string& project_dir, const std::string& store, const std::string& title)
{
    clear();
    Impl& m = *impl_;
    if(!section) return;
    if(!section->is_array()) { HML_WARN("uploads: a list is needed"); return; }
    m.configured = true;
    m.section = *section;
    m.local = local && local->is_object() ? *local : json::object();
    m.dir = project_dir;
    m.store = store;
    m.title = title;
    std::set<std::string> names;
    for(const json& j : *section) {
        if(m.targets.size() >= kUploadTargetsMax) { HML_WARN("uploads: more than %zu, the rest is left out", kUploadTargetsMax); break; }
        UploadTarget t;
        std::string err;
        const std::string n = j.is_object() ? jstr(j, "name", "") : "";
        if(!upload_parse(j, jget(m.local, n.c_str()), t, &err)) { HML_WARN("uploads: an entry is left out: %s", err.c_str()); continue; }
        if(!names.insert(t.name).second) { HML_WARN("uploads: '%s' is defined twice: the second is left out", t.name.c_str()); continue; }
        if(!t.url.secure())
            HML_WARN("upload '%s': %s is not secured: the files%s can be read on the network", t.name.c_str(), t.url.shown().c_str(), t.username.empty() ? "" : ", the user name and the password");
        else if(!t.tls.verify)
            HML_WARN("upload '%s': the certificate of the server is not checked (\"verify\": false): anybody can pose as the server", t.name.c_str());
        m.targets.push_back(std::move(t));
    }
    for(const UploadTarget& t : m.targets) m.scan(t);
    for(const char* name : { kWaitingTag, kErrorTag }) {
        if(tags().find(name)) continue;
        Value v = Value::number(0, name == std::string(kErrorTag) ? TagType::Bool : TagType::Int);
        tags().create(name, v.type, &v, false, 0, 0);
    }
    size_t waiting = 0;
    for(const auto& [k, s] : m.state) waiting += s.files.size();
    if(!m.targets.empty()) HML_INFO("uploads: %zu defined%s", m.targets.size(), waiting ? vformat(", %zu file(s) from before the restart are waiting", waiting).c_str() : "");
    m.tags_update();
}

void Uploads::start(bool threaded) { impl_->threaded = threaded; }
void Uploads::set_read_only(bool on) { impl_->read_only = on; }
bool Uploads::configured() const { return impl_->configured; }
const std::vector<UploadTarget>& Uploads::targets() const { return impl_->targets; }
uint64_t Uploads::sent() const { return impl_->sent; }
uint64_t Uploads::dropped() const { return impl_->dropped; }
void Uploads::set_sender(Sender s) { impl_->sender = s ? std::move(s) : Sender(net_upload); }
void Uploads::set_exporter(Exporter e) { impl_->exporter = e ? std::move(e) : Exporter(export_log); }
void Uploads::set_clock(std::function<int64_t()> now_ms) { impl_->clock = std::move(now_ms); }

size_t Uploads::waiting(const std::string& name) const
{
    size_t n = 0;
    for(const auto& [k, s] : impl_->state) if(name.empty() || k == name) n += s.files.size();
    return n;
}

std::string Uploads::status(const std::string& name) const
{
    auto it = impl_->state.find(name);
    return it == impl_->state.end() ? "" : it->second.status;
}

int Uploads::run(const std::string& name, std::string* why)
{
    Impl& m = *impl_;
    auto no = [&](const std::string& w) { if(why) *why = w; return -1; };
    if(!m.configured) return no("the project has no \"uploads\"");
    if(m.read_only) return no("nothing is sent while the project is only checked");
    if(!name.empty() && !m.find(name)) return no("no upload '" + one_line(name, 40) + "'");
    int written = 0, off = 0;
    // one time for all files of this run (and never the same name twice)
    std::string stamp = LogDb::format_time(m.now()).substr(0, 19);       // "YYYY-MM-DD HH:MM:SS"
    stamp.erase(std::remove_if(stamp.begin(), stamp.end(), [](char c) { return c == '-' || c == ':'; }), stamp.end());
    std::replace(stamp.begin(), stamp.end(), ' ', '-');                  // "YYYYMMDD-HHMMSS"
    for(UploadTarget& t : m.targets) {
        if(!name.empty() && t.name != name) continue;
        if(!t.enabled) { off++; continue; }
        Impl::State& s = m.state[t.name];
        std::error_code ec;
        fs::create_directories(m.folder(t.name), ec);
        for(const std::string& log : t.logs) {
            std::string what = log;
            std::replace(what.begin(), what.end(), ':', '-');
            const std::string file = net_file_name((m.title.empty() ? std::string("hml") : m.title) + "_" + what + "_" + stamp + ".csv");
            if(std::find(s.files.begin(), s.files.end(), file) != s.files.end()) continue;       // (a second run within the same second)
            const std::string path = (m.folder(t.name) / fs::u8path(file)).generic_u8string();
            const int64_t rows = m.exporter(log, path);
            if(rows < 0 || !fs::is_regular_file(fs::u8path(path), ec)) { HML_WARN("upload '%s': the log \"%s\" could not be written to a file (is it switched on?)", t.name.c_str(), log.c_str()); continue; }
            s.files.push_back(file);
            written++;
        }
        // a run is a new start for a target whose tries were used up
        s.paused = false;
        s.attempts = 0;
        s.next_try_ms = 0;
        m.trim(t);
    }
    m.tags_update();
    if(written == 0 && off > 0 && !name.empty()) return no("the upload '" + name + "' is switched off");
    return written;
}

void Uploads::poll()
{
    Impl& m = *impl_;
    if(!m.configured) return;
    if(m.job) {
        if(!m.job->done) return;
        const std::shared_ptr<Impl::Job> j = m.job;
        m.job.reset();
        m.finished(j->target, j->file, j->res);
    }
    if(m.read_only) return;
    const int64_t now = m.now();
    for(UploadTarget& t : m.targets) {
        Impl::State& s = m.state[t.name];
        if(!t.enabled || s.files.empty() || s.paused || now < s.next_try_ms) continue;
        auto job = std::make_shared<Impl::Job>();
        job->target = t.name;
        job->file = s.files.front();
        job->tls = t.tls;
        job->up.url = t.url;
        job->up.post = t.post;
        job->up.username = t.username;
        job->up.password = t.password;
        job->up.file = (m.folder(t.name) / fs::u8path(job->file)).generic_u8string();
        job->up.name = job->file;
        job->up.timeout_ms = t.timeout_ms;
        Sender sender = m.sender;
        if(m.threaded) {
            m.job = job;
            std::thread([job, sender] { job->res = sender(job->up, job->tls); job->done = true; }).detach();
        }
        else m.finished(job->target, job->file, sender(job->up, job->tls));
        return;                                                         // one file at a time
    }
}

std::string Uploads::set_local(const std::string& name, bool enabled, const std::string& username, const std::string& password)
{
    Impl& m = *impl_;
    UploadTarget* t = m.find(name);
    if(!t) return "No such upload.";
    auto bad = [](const std::string& s) { return s.size() > 200 || std::any_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; }); };
    if(bad(username) || bad(password) || username.find(':') != std::string::npos) return "The user name or the password cannot be used.";
    // what differs from the project is kept
    UploadTarget base;
    for(const json& j : m.section) if(j.is_object() && jstr(j, "name", "") == name) { upload_parse(j, nullptr, base); break; }
    json mine = json::object();
    if(enabled != base.enabled) mine["enabled"] = enabled;
    if(username != base.username) mine["username"] = username;
    const std::string old_pw = jstr(m.local.contains(name) && m.local[name].is_object() ? m.local[name] : json::object(), "password", "");
    if(!password.empty()) { if(password != base.password) mine["password"] = password; }
    else if(!old_pw.empty()) mine["password"] = old_pw;
    json all = m.local.is_object() ? m.local : json::object();
    if(mine.empty()) all.erase(name);
    else all[name] = mine;
    if(!m.store.empty()) {
        json overlay = json::object();
        {
            std::ifstream f(fs::u8path(m.store), std::ios::binary);
            if(f) {
                std::stringstream ss;
                ss << f.rdbuf();
                overlay = json::parse(ss.str(), nullptr, false, true);
                if(!overlay.is_object()) overlay = json::object();
            }
        }
        if(all.empty()) overlay.erase("uploads");
        else overlay["uploads"] = all;
        std::string err;
        if(overlay.empty()) { std::error_code ec; fs::remove(fs::u8path(m.store), ec); }
        else if(!write_file_atomic(m.store, overlay.dump(2) + "\n", &err)) return err;
    }
    if(enabled != t->enabled) operation_log().record("upload", name + " enabled", t->enabled ? "on" : "off", enabled ? "on" : "off");
    if(username != t->username) operation_log().record("upload", name + " username", t->username, username);
    if(!password.empty() && password != t->password) operation_log().record("upload", name + " password", "", "(changed)");
    m.local = all;
    t->enabled = enabled;
    t->username = username;
    if(!password.empty()) t->password = password;
    // (a new password is a reason to try what waits again)
    Impl::State& s = m.state[name];
    s.paused = false;
    s.attempts = 0;
    s.next_try_ms = 0;
    return "";
}

Uploads& uploads()
{
    static Uploads instance;
    return instance;
}

void register_upload_actions()
{
    ui().register_action("upload.run", [](const std::string& arg) {
        std::string why;
        const int n = uploads().run(arg, &why);
        if(n < 0) HML_WARN("upload.run: %s", why.c_str());
        else HML_INFO("upload.run%s%s: %d file(s) to send", arg.empty() ? "" : " ", arg.c_str(), n);
    });
}

// ================================================================ System > Upload

namespace {

class UploadPage final : public SettingsPage {
public:
    std::string id() const override { return "upload"; }
    std::string title() const override { return "Upload"; }

    const UploadTarget* current() const
    {
        const auto& list = uploads().targets();
        for(const UploadTarget& t : list) if(t.name == selected_) return &t;
        return list.empty() ? nullptr : &list.front();
    }
    // the upload picked in the list (the tag of a list holds the number of the entry)
    void take_selection()
    {
        Tag* t = tags().find(SystemSettings::tag_name(id(), "target"));
        if(!t) return;
        const std::string v = t->get().format();
        const auto& list = uploads().targets();
        if(v.empty()) return;
        if(v.find_first_not_of("0123456789") == std::string::npos) { const size_t i = static_cast<size_t>(std::atoi(v.c_str())); if(i < list.size()) selected_ = list[i].name; }
        else selected_ = v;
    }
    std::string shown_state() const
    {
        const UploadTarget* t = current();
        return t ? uploads().status(t->name) + "|" + std::to_string(uploads().waiting(t->name)) : "";
    }

    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        // (the target picked in the list is the one the other fields are about)
        take_selection();
        const UploadTarget* t = current();
        selected_ = t ? t->name : "";
        auto add = [&](const char* key, const char* label, SettingField::Type type, const std::string& value, bool password = false) {
            SettingField f; f.key = key; f.label = label; f.type = type; f.value = value; f.password = password; out.push_back(f);
        };
        SettingField sel;
        sel.key = "target"; sel.label = "Upload"; sel.type = SettingField::Type::Select; sel.value = selected_;
        for(const UploadTarget& x : uploads().targets()) sel.options.push_back(x.name);
        out.push_back(sel);
        add("enabled", "Send", SettingField::Type::Bool, t && t->enabled ? "1" : "0");
        add("url", "To", SettingField::Type::Info, t ? t->url.shown() : "-");
        add("username", "User name", SettingField::Type::Text, t ? t->username : "");
        add("password", "Password (empty: unchanged)", SettingField::Type::Text, "", true);
        add("state", "State", SettingField::Type::Info, t ? uploads().status(t->name) : "");
        add("waiting", "Files waiting", SettingField::Type::Info, t ? std::to_string(uploads().waiting(t->name)) : "0");
        shown_ = shown_state();
        shown_target_ = selected_;
        return out;
    }
    std::string apply(const std::map<std::string, std::string>& v) override
    {
        auto get = [&](const char* k) { auto it = v.find(k); return it == v.end() ? std::string() : it->second; };
        const std::string err = uploads().set_local(get("target"), get("enabled") != "0", get("username"), get("password"));
        return err.empty() ? "Saved." : err;
    }
    std::vector<std::pair<std::string, std::string>> buttons() const override { return { { "Send the logs now", "run" } }; }
    void command(const std::string& cmd, const std::string&) override
    {
        if(cmd != "run") { HML_WARN("system: upload: unknown command '%s'", cmd.c_str()); return; }
        take_selection();
        const UploadTarget* t = current();
        std::string why;
        const int n = t ? uploads().run(t->name, &why) : -1;
        if(Tag* st = tags().find(SystemSettings::tag_name(id(), "status"))) st->set_string(n < 0 ? (t ? why : "No upload.") : vformat("%d file(s) are on their way: see \"State\".", n), nullptr);
    }
    bool poll() override
    {
        // another upload was picked in the list: the other fields follow
        take_selection();
        if(selected_ != shown_target_) return true;
        const std::string now = shown_state();
        if(now == shown_) return false;
        shown_ = now;
        const UploadTarget* t = current();
        if(Tag* st = tags().find(SystemSettings::tag_name(id(), "state"))) st->set_string(t ? uploads().status(t->name) : "", nullptr);
        if(Tag* w = tags().find(SystemSettings::tag_name(id(), "waiting"))) w->set_string(t ? std::to_string(uploads().waiting(t->name)) : "0", nullptr);
        return false;
    }

private:
    std::string selected_, shown_, shown_target_;
};

} // namespace

std::unique_ptr<SettingsPage> make_upload_page() { return std::make_unique<UploadPage>(); }

} // namespace hml
