#include "deploy_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "active_project.hpp"
#include "jsonutil.hpp"
#include "mbedtls/sha256.h"
#include "operation_log.hpp"
#include "project_archive.hpp"
#include "util.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kInvalidSock = INVALID_SOCKET;
static void sock_close(sock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <unistd.h>
using sock_t = int;
static const sock_t kInvalidSock = -1;
static void sock_close(sock_t s) { ::close(s); }
#endif

namespace fs = std::filesystem;

namespace hml {
namespace {

using Clock = std::chrono::steady_clock;

int wait_readable(sock_t s, int ms)
{
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    timeval tv{ ms / 1000, (ms % 1000) * 1000 };
    return select(static_cast<int>(s) + 1, &r, nullptr, nullptr, &tv);
}

std::string now_text()
{
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char buf[32] = "";
    if(tm) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return buf;
}

// what a designer calls itself, for the screen: printable ASCII, short
std::string clean_name(const std::string& in)
{
    std::string out;
    for(char c : in) if(c > 0x20 && c < 0x7f && out.size() < 40) out += c;
    return out.empty() ? "designer" : out;
}

bool is_hex64(const std::string& s)
{
    if(s.size() != 64) return false;
    for(char c : s) if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

} // namespace

std::string deploy_staged_runtime(const std::string& exe)
{
    const fs::path e = fs::u8path(exe);
    return (e.parent_path() / (e.stem().u8string() + ".new" + e.extension().u8string())).u8string();
}

std::string deploy_install_runtime(const std::string& staged, const std::string& exe)
{
    std::error_code ec;
    const fs::path s = fs::u8path(staged), e = fs::u8path(exe);
    auto refuse = [&](const std::string& why) { fs::remove(s, ec); return why; };
    if(!fs::is_regular_file(s, ec)) return "the runtime that was sent is not there any more";
    if(!fs::is_regular_file(e, ec)) return refuse("the runtime of this device is not where it is expected (" + exe + ")");
    // (the path goes into a command line: it is this program's own folder, but nothing that a shell reads)
    for(char c : staged) if(c == '"' || c == '$' || c == '`' || c == '%' || c == '&' || c == '^' || c == '!' || c == '\n') return refuse("the folder of the runtime has a name that cannot be used here");
#ifdef _WIN32
    const std::string cmd = "\"\"" + s.string() + "\" --help\" >NUL 2>&1";
#else
    ::chmod(s.c_str(), 0755);
    const std::string cmd = "\"" + s.string() + "\" --help >/dev/null 2>&1";
#endif
    if(std::system(cmd.c_str()) != 0) return refuse("the runtime that was sent does not start on this device (built for another kind of device?)");
    fs::path prev = e;
    prev += ".prev";
    fs::remove(prev, ec);
    fs::rename(e, prev, ec);
    if(ec) return refuse("the runtime in use cannot be moved aside: " + ec.message());
    fs::rename(s, e, ec);
    if(ec) {
        const std::string why = ec.message();
        fs::rename(prev, e, ec);
        return refuse("the new runtime cannot be put in place: " + why);
    }
    return "";
}

std::string device_data_dir(const std::string& given)
{
    if(!given.empty()) return given;
    const std::string exe = install_dir();
    return exe.empty() ? std::string() : exe + "/keys";
}

DeployService::DeployService() = default;
DeployService::~DeployService() { stop(); }

void DeployService::open(const std::string& data_dir)
{
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    dir_ = data_dir;
    enabled_ = false;
    ask_ = true;
    approved_.clear();
    port_cfg_ = kDeployDefaultPort;
    designers_.clear();
    has_identity_ = false;
    if(dir_.empty()) return;
    std::ifstream f(fs::u8path(dir_) / "deploy.json", std::ios::binary);
    if(!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    const json j = json::parse(ss.str(), nullptr, false, true);
    if(!j.is_object()) { HML_WARN("deploy: %s/deploy.json cannot be read: receiving projects is off", dir_.c_str()); return; }
    enabled_ = jbool(j, "enabled", false);
    ask_ = jbool(j, "ask", true);
    approved_ = jstr(j, "approved", "");
    const int port = static_cast<int>(jnum(j, "port", kDeployDefaultPort));
    if(port >= 1 && port <= 65535) port_cfg_ = port;
    if(const json* list = jget(j, "designers"); list && list->is_array())
        for(const json& d : *list) {
            const std::string fp = jstr(d, "fingerprint", "");
            if(fp.rfind("SHA256:", 0) == 0) designers_.push_back({ fp, clean_name(jstr(d, "name", "")), jstr(d, "added", ""), jbool(d, "runtime", false) });
        }
}

std::string DeployService::save()
{
    json list = json::array();
    for(const DeployDesigner& d : designers_) {
        json one = { { "fingerprint", d.fingerprint }, { "name", d.name }, { "added", d.added } };
        if(d.runtime) one["runtime"] = true;
        list.push_back(one);
    }
    json j = { { "enabled", enabled_ }, { "port", port_cfg_ }, { "ask", ask_ }, { "designers", list } };
    if(!approved_.empty()) j["approved"] = approved_;
    std::string err;
    if(!write_file_atomic((fs::u8path(dir_) / "deploy.json").u8string(), j.dump(2) + "\n", &err)) return err;
    return "";
}

std::string DeployService::fingerprint()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(dir_.empty()) return "";
    if(!has_identity_) {
        std::string err;
        if(!deploy_identity(fs::u8path(dir_), "device", "hml-device", identity_, &err)) { HML_ERROR("deploy: %s", err.c_str()); return ""; }
        has_identity_ = true;
    }
    return identity_.fingerprint;
}

std::string DeployService::set_enabled(bool on)
{
    if(dir_.empty()) return "No place for the keys of this device (start the runtime with --data-dir <folder>).";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(on == enabled_ && on == listening_.load()) return "";
        enabled_ = on;
        const std::string err = save();
        if(!err.empty()) { enabled_ = !on; return err; }
    }
    operation_log().record("deploy", "receive projects", on ? "off" : "on", on ? "on" : "off");
    if(!on) { stop(); return ""; }
    std::string err;
    if(!start(&err)) return err;
    return "";
}

std::string DeployService::set_port(int port)
{
    if(port < 1 || port > 65535) return "The port is 1..65535.";
    bool again = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(port == port_cfg_) return "";
        const int old = port_cfg_;
        port_cfg_ = port;
        const std::string err = dir_.empty() ? std::string("No place for the settings.") : save();
        if(!err.empty()) { port_cfg_ = old; return err; }
        again = listening_;
    }
    if(again) {
        stop();
        std::string err;
        if(!start(&err)) return err;
    }
    return "";
}

bool DeployService::start(std::string* error)
{
    auto fail = [&](const std::string& m) { if(error) *error = m; HML_ERROR("deploy: %s", m.c_str()); return false; };
    if(listening_) return true;
    if(!enabled_ || dir_.empty()) return false;
    if(thread_.joinable()) thread_.join();
    if(fingerprint().empty()) return fail("the certificate of this device cannot be read or made in " + dir_);
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == kInvalidSock) return fail("cannot create a socket");
#ifndef _WIN32
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_cfg_));
    if(bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 4) != 0) {
        sock_close(s);
        return fail("port " + std::to_string(port_cfg_) + " is in use");
    }
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    listen_sock_ = static_cast<std::intptr_t>(s);
    port_ = ntohs(addr.sin_port);
    stop_ = false;
    listening_ = true;
    thread_ = std::thread([this] { run(); });
    HML_INFO("deploy: receiving projects on port %d (TLS), %d designer(s) accepted, this device: %s", port_.load(), static_cast<int>(designers().size()), identity_.fingerprint.c_str());
    return true;
}

void DeployService::stop()
{
    const bool was = listening_;
    stop_ = true;
    cv_.notify_all();
    if(thread_.joinable()) thread_.join();
    if(listen_sock_ != -1) { sock_close(static_cast<sock_t>(listen_sock_)); listen_sock_ = -1; }
    listening_ = false;
    port_ = 0;
    adding_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        has_offer_ = false;
        decision_ = Decision::None;
    }
    if(was) HML_INFO("deploy: receiving projects is off");
}

std::vector<DeployDesigner> DeployService::designers() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return designers_;
}

bool DeployService::known(const std::string& fingerprint) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(const DeployDesigner& d : designers_) if(d.fingerprint == fingerprint) return true;
    return false;
}

std::string DeployService::remove(const std::string& fingerprint)
{
    std::string name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DeployDesigner> kept;
        for(const DeployDesigner& d : designers_) { if(d.fingerprint == fingerprint) name = d.name; else kept.push_back(d); }
        if(kept.size() == designers_.size()) return "No such designer.";
        const std::vector<DeployDesigner> old = designers_;
        designers_ = kept;
        const std::string err = save();
        if(!err.empty()) { designers_ = old; return err; }
    }
    HML_INFO("deploy: designer removed: %s %s", name.c_str(), fingerprint.c_str());
    operation_log().record("deploy", "designer removed", name + " " + fingerprint, "");
    return "";
}

std::string DeployService::set_runtime_allowed(const std::string& fingerprint, bool on)
{
    std::string name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DeployDesigner* d = nullptr;
        for(DeployDesigner& x : designers_) if(x.fingerprint == fingerprint) d = &x;
        if(!d) return "No such designer.";
        if(d->runtime == on) return "";
        d->runtime = on;
        const std::string err = save();
        if(!err.empty()) { d->runtime = !on; return err; }
        name = d->name;
    }
    HML_INFO("deploy: designer %s %s %s replace the runtime", name.c_str(), fingerprint.c_str(), on ? "may now" : "may no longer");
    operation_log().record("deploy", "runtime from designer", on ? "not allowed" : "allowed", (on ? "allowed: " : "not allowed: ") + name + " " + fingerprint);
    return "";
}

std::string DeployService::set_ask(bool on)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(on == ask_) return "";
        if(dir_.empty()) return "No place for the settings.";
        ask_ = on;
        const std::string err = save();
        if(!err.empty()) { ask_ = !on; return err; }
    }
    operation_log().record("deploy", "ask before a project that can do more", on ? "off" : "on", on ? "on" : "off");
    return "";
}

std::string DeployService::approved() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return approved_;
}

std::string DeployService::set_approved(const std::string& fingerprint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(dir_.empty() || fingerprint == approved_) return "";
    const std::string old = approved_;
    approved_ = fingerprint;
    const std::string err = save();
    if(!err.empty()) approved_ = old;
    return err;
}

void DeployService::set_runtime_path(const std::string& exe)
{
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_path_ = exe;
}

std::string DeployService::runtime_path() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return runtime_path_;
}

bool DeployService::pending(Offer& out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!has_offer_ || decision_ != Decision::None) return false;
    out = offer_;
    return true;
}

std::string DeployService::accept()
{
    Offer o;
    std::string err;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!has_offer_ || decision_ != Decision::None) return "No designer is waiting.";
        o = offer_;
        bool have = false;
        for(const DeployDesigner& d : designers_) have = have || d.fingerprint == o.fingerprint;
        if(!have) {
            designers_.push_back({ o.fingerprint, o.name, now_text(), false });
            err = save();
            if(!err.empty()) designers_.pop_back();
        }
        accept_error_ = err;
        decision_ = Decision::Accept;
    }
    cv_.notify_all();
    if(err.empty()) {
        HML_INFO("deploy: designer accepted: %s %s from %s", o.name.c_str(), o.fingerprint.c_str(), o.from.c_str());
        operation_log().record("deploy", "designer accepted", "", o.name + " " + o.fingerprint + " (" + o.from + ")");
    }
    return err;
}

void DeployService::reject()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!has_offer_ || decision_ != Decision::None) return;
        decision_ = Decision::Reject;
        HML_INFO("deploy: designer rejected: %s %s from %s", offer_.name.c_str(), offer_.fingerprint.c_str(), offer_.from.c_str());
    }
    cv_.notify_all();
}

bool DeployService::received(Received& out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!has_received_ || finished_) return false;
    out = received_;
    return true;
}

void DeployService::finish(const std::string& error)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!has_received_) return;
        finish_error_ = error;
        finished_ = true;
        std::error_code ec;
        fs::remove(fs::u8path(received_.file), ec);          // (a runtime that was installed has been moved into place: nothing is left here)
    }
    cv_.notify_all();
}

void DeployService::run()
{
    const sock_t ls = static_cast<sock_t>(listen_sock_);
    while(!stop_) {
        if(wait_readable(ls, 200) <= 0) continue;
        sockaddr_in peer{};
#ifdef _WIN32
        int len = sizeof(peer);
#else
        socklen_t len = sizeof(peer);
#endif
        const sock_t c = ::accept(ls, reinterpret_cast<sockaddr*>(&peer), &len);
        if(c == kInvalidSock) continue;
        char ip[64] = "?";
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        serve(static_cast<std::intptr_t>(c), ip);
        sock_close(c);
    }
}

// One designer at a time: others wait in the listen queue.
void DeployService::serve(std::intptr_t client, const std::string& from)
{
    struct Serving { std::atomic<bool>& f; explicit Serving(std::atomic<bool>& x) : f(x) { f = true; } ~Serving() { f = false; } } guard(serving_);
    DeployIdentity me;
    { std::lock_guard<std::mutex> lock(mutex_); me = identity_; }
    DeployConn c;
    c.set_cancel(&stop_);
    std::string err;
    if(!c.accept(client, me, 10000, &err)) return;                  // (not TLS, no certificate, too slow: nothing is said, nothing is logged per try)
    const std::string fp = c.peer_fingerprint();

    std::string hello;
    if(!c.read_line(hello, 200, 5000) || hello.rfind("HML-DEPLOY 1", 0) != 0) { c.write_line("ERROR unknown request"); return; }
    const std::string name = clean_name(hello.size() > 13 ? hello.substr(13) : "");

    if(!known(fp)) {
        if(!adding_) {
            HML_WARN("deploy: a designer that is not accepted here tried to connect from %s (%s %s)", from.c_str(), name.c_str(), fp.c_str());
            c.write_line("REJECTED this designer is not accepted at the device yet. Switch on \"Add a designer\" there (System > Deploy) and deploy again");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            offer_ = Offer{ fp, name, from };
            decision_ = Decision::None;
            accept_error_.clear();
            has_offer_ = true;
        }
        HML_INFO("deploy: a new designer asks to be accepted: %s %s from %s", name.c_str(), fp.c_str(), from.c_str());
        c.write_line("PENDING " + fp);
        const auto until = Clock::now() + std::chrono::seconds(offer_timeout_s_.load());
        std::string answer = "REJECTED";
        bool accepted = false;
        for(;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(150), [&] { return decision_ != Decision::None || stop_.load(); });
                if(decision_ == Decision::Accept) { accepted = accept_error_.empty(); answer = accepted ? "" : "ERROR " + accept_error_; break; }
                if(decision_ == Decision::Reject || stop_) break;
            }
            if(Clock::now() > until) { answer = "TIMEOUT"; HML_INFO("deploy: nobody decided about the designer from %s in time", from.c_str()); break; }
            char buf[16];
            if(c.read_some(buf, sizeof(buf), 0) != 0) { answer.clear(); break; }   // the sender gave up (or talks out of turn)
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            has_offer_ = false;
            decision_ = Decision::None;
        }
        if(!accepted) { if(!answer.empty()) c.write_line(answer); return; }
    }
    if(!c.write_line("READY")) return;

    // ---- what it sends: the runtime ("RUNTIME ..."), a project ("PUT ..."), or one after the other
    for(int n = 0; n < 4; n++) {
        std::string req;
        if(!c.read_line(req, 600, n == 0 ? 30000 : 10000)) return;
        unsigned long long size = 0;
        char verb[16] = "", hash[80] = "", fname[300] = "";
        if(std::sscanf(req.c_str(), "%15s %llu %64s %255s", verb, &size, hash, fname) != 4 || !is_hex64(hash)) { c.write_line("ERROR unknown request"); return; }
        const bool runtime = std::strcmp(verb, "RUNTIME") == 0;
        if(!runtime && std::strcmp(verb, "PUT") != 0) { c.write_line("ERROR unknown request"); return; }
        const std::string base = fs::u8path(std::string(fname)).filename().u8string();
        fs::path file;
        if(runtime) {
            bool allowed = false;
            std::string exe;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for(const DeployDesigner& d : designers_) if(d.fingerprint == fp) allowed = d.runtime;
                exe = runtime_path_;
            }
            if(!allowed) {
                HML_WARN("deploy: the designer %s (%s) from %s tried to replace the runtime and is not allowed to", name.c_str(), fp.c_str(), from.c_str());
                c.write_line("ERROR this designer may not replace the runtime of the device. Allow it there first (System > Deploy > Runtime)");
                return;
            }
            if(exe.empty()) { c.write_line("ERROR this device cannot replace its runtime"); return; }
            if(size == 0 || size > kMaxRuntimeBytes) { c.write_line("ERROR the file is too large for this device"); return; }
            file = fs::u8path(deploy_staged_runtime(exe));
        }
        else {
            if(size == 0 || size > kMaxBytes) { c.write_line("ERROR the file is too large for this device"); return; }
            if(base.size() < 9 || base.substr(base.size() - 8) != kProjectArchiveExt) { c.write_line("ERROR not a project file (.hmlproj)"); return; }
            file = fs::u8path(dir_) / "incoming.hmlproj";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(has_received_) { c.write_line("ERROR the device is busy installing"); return; }
        }
        fs::path part = file;
        part += ".part";
        std::error_code ec;
        bool ok = false;
        if(!c.write_line("SEND")) return;
        {
            std::ofstream f(part, std::ios::binary | std::ios::trunc);
            mbedtls_sha256_context sha;
            mbedtls_sha256_init(&sha);
            mbedtls_sha256_starts(&sha, 0);
            std::vector<char> buf(64 * 1024);
            unsigned long long got = 0;
            const auto until = Clock::now() + std::chrono::minutes(15);
            while(f && got < size && Clock::now() < until) {
                const int r = c.read_some(buf.data(), static_cast<size_t>(std::min<unsigned long long>(buf.size(), size - got)), 30000);
                if(r <= 0) break;
                f.write(buf.data(), r);
                mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char*>(buf.data()), static_cast<size_t>(r));
                got += static_cast<unsigned long long>(r);
            }
            unsigned char h[32];
            mbedtls_sha256_finish(&sha, h);
            mbedtls_sha256_free(&sha);
            f.flush();
            static const char* hex = "0123456789abcdef";
            std::string mine;
            for(unsigned char b : h) { mine += hex[b >> 4]; mine += hex[b & 15]; }
            if(!f.good()) c.write_line("ERROR the device cannot store the file");
            else if(got != size) HML_WARN("deploy: what %s sent did not arrive completely", from.c_str());
            else if(mine != hash) c.write_line("ERROR the file arrived damaged");
            else ok = true;
        }
        if(ok) {
            fs::remove(file, ec);
            fs::rename(part, file, ec);
            if(ec) { ok = false; c.write_line("ERROR the device cannot store the file"); }
        }
        if(!ok) { fs::remove(part, ec); return; }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            received_ = Received{ file.u8string(), base, from, name + " " + fp, runtime };
            finish_error_.clear();
            finished_ = false;
            has_received_ = true;
        }
        HML_INFO("deploy: %s '%s' (%llu bytes) received from %s (%s)", runtime ? "runtime" : "project", base.c_str(), size, from.c_str(), name.c_str());
        c.write_line("RECEIVED");

        // ---- the UI thread checks and installs it
        std::string answer;
        {
            const auto until = Clock::now() + std::chrono::seconds(install_timeout_s_.load());
            bool told = false;
            for(;;) {
                bool tell = false;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait_for(lock, std::chrono::milliseconds(200), [&] { return finished_ || stop_.load(); });
                    if(finished_ || stop_ || Clock::now() > until) break;
                    tell = asking_ && !told;
                }
                // (the operator of the device is being asked: the designer hears that it waits for a person)
                if(tell) { told = true; if(!c.write_line("ASKING")) break; }
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if(finished_) answer = finish_error_.empty() ? "INSTALLED" : "ERROR " + finish_error_;
            else answer = "ERROR the device did not install it in time";
            if(!finished_) fs::remove(file, ec);
            has_received_ = false;
            finished_ = false;
        }
        asking_ = false;
        for(char& ch : answer) if(ch == '\n' || ch == '\r') ch = ' ';
        if(!c.write_line(answer) || answer != "INSTALLED") return;
    }
}

DeployService& deploy_service()
{
    static DeployService inst;
    return inst;
}

} // namespace hml
