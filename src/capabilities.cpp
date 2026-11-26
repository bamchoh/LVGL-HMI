#include "capabilities.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include "mbedtls/sha256.h"
#include "project_archive.hpp"

namespace fs = std::filesystem;

namespace hml {
namespace {

bool read_text(const fs::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if(!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if(out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}

// "a, b, c ... (12)": the first few names and how many there are
std::string some(const std::vector<std::string>& names, size_t show = 4)
{
    std::string out;
    for(size_t i = 0; i < names.size() && i < show; i++) out += (i ? ", " : "") + names[i];
    if(names.size() > show) out += " ...";
    return out;
}

// where a driver connects to: "192.168.0.10:502", "COM3", "opc.tcp://..."
std::string endpoint(const json& d)
{
    for(const char* k : { "endpoint", "url" }) if(const std::string v = jstr(d, k, ""); !v.empty()) return v;
    std::string host = jstr(d, "host", "");
    if(host.empty()) host = jstr(d, "device", "");
    if(host.empty()) return "(no address)";
    if(const json* p = jget(d, "port"); p && p->is_number()) host += ":" + std::to_string(static_cast<int>(jnum(d, "port", 0)));
    return host;
}

// how many scripts the buttons and events of a screen carry ({ "action": "script", "code": ... })
int inline_scripts(const json& w)
{
    int n = 0;
    if(w.is_array()) { for(const json& c : w) n += inline_scripts(c); return n; }
    if(!w.is_object()) return 0;
    if(jstr(w, "action", "") == "script" && w.contains("code")) n++;
    for(auto it = w.begin(); it != w.end(); ++it) if(it.value().is_structured()) n += inline_scripts(it.value());
    return n;
}

std::string what_action_does(const json& a)
{
    if(a.is_array()) {
        std::string out;
        for(const json& one : a) { const std::string s = what_action_does(one); if(!s.empty()) out += (out.empty() ? "" : ", ") + s; }
        return out;
    }
    if(!a.is_object()) return "";
    const std::string kind = jstr(a, "action", "");
    if(kind == "set" || kind == "toggle" || kind == "inc") return "writes " + jstr(a, "tag", "?");
    if(kind == "script") return a.contains("name") ? "runs the script " + jstr(a, "name", "?") : "runs a script";
    if(kind == "call") return "calls " + jstr(a, "fn", "?");
    return kind;
}

} // namespace

ProjectCapabilities project_capabilities(const json& root, const std::string& dir)
{
    ProjectCapabilities c;
    if(!root.is_object()) return c;

    // ---- drivers: devices written to, brokers, servers
    if(const json* dl = jget(root, "drivers"); dl && dl->is_array()) {
        for(const json& d : *dl) {
            if(!d.is_object() || !jbool(d, "enabled", true)) continue;
            const std::string type = jstr(d, "type", "");
            if(type.empty() || type == "sim") continue;
            if(type == "opcua_server") {
                const int port = static_cast<int>(jnum(d, "port", 4840));
                const std::string sec = jstr(d, "security", "sign_encrypt");
                c.listens.push_back("OPC UA server on port " + std::to_string(port) + (sec == "none" ? ": anybody on the network can read and change the values (no security)"
                                                                                                      : ": the clients the device trusts can read and change the values"));
                continue;
            }
            if(type == "mqtt") {
                size_t pub = 0, sub = 0;
                if(const json* p = jget(d, "publish"); p && p->is_array()) pub = p->size();
                if(const json* s = jget(d, "subscribe"); s && s->is_array()) sub = s->size();
                if(pub) c.sends.push_back("MQTT broker " + endpoint(d) + ": publishes " + std::to_string(pub) + " topic(s) with values of tags");
                if(sub) c.devices.push_back("MQTT broker " + endpoint(d) + ": takes values for tags from " + std::to_string(sub) + " topic(s)");
                continue;
            }
            // a device driver: the rows that are not read-only can be written
            std::vector<std::string> writable;
            size_t rows = 0;
            if(const json* map = jget(d, "map"); map && map->is_array())
                for(const json& row : *map) {
                    if(!row.is_object()) continue;
                    rows++;
                    if(jstr(row, "access", "") != "ro") writable.push_back(jstr(row, "tag", "?"));
                }
            if(!writable.empty())
                c.devices.push_back(type + " " + endpoint(d) + ": can write " + std::to_string(writable.size()) + " of " + std::to_string(rows) + " value(s) (" + some(writable) + ")");
        }
    }

    // ---- sending on its own
    if(const json* ml = jget(root, "mail"); ml && ml->is_object() && jbool(*ml, "enabled", true) && !jstr(*ml, "host", "").empty()) {
        std::vector<std::string> to;
        if(const json* t = jget(*ml, "to")) {
            if(t->is_string()) to.push_back(t->get<std::string>());
            else if(t->is_array()) for(const json& a : *t) if(a.is_string()) to.push_back(a.get<std::string>());
        }
        c.sends.push_back("mail through " + jstr(*ml, "host", "") + " to " + (to.empty() ? std::string("(set on the device)") : some(to)) +
                          (jget(*ml, "alarms") ? ": alarms, with values of tags" : ""));
    }
    if(const json* up = jget(root, "uploads"); up && up->is_array())
        for(const json& u : *up) {
            if(!u.is_object() || !jbool(u, "enabled", true)) continue;
            std::vector<std::string> logs;
            if(const json* l = jget(u, "logs"); l && l->is_array()) for(const json& x : *l) if(x.is_string()) logs.push_back(x.get<std::string>());
            c.sends.push_back("uploads the recorded logs to " + jstr(u, "url", "(no address)") + (logs.empty() ? "" : " (" + some(logs) + ")"));
        }

    // ---- ports for others
    if(const json* rm = jget(root, "remote"); rm && rm->is_object() && jbool(*rm, "enabled", false))
        c.listens.push_back("VNC on port " + std::to_string(static_cast<int>(jnum(*rm, "port", 5900))) + (jbool(*rm, "view_only", false) ? " (view only)" : ": the screen can be operated") +
                            ", from other machines once a password is set on the device");

    // ---- what runs by itself
    if(const json* sc = jget(root, "scripts"); sc && sc->is_array()) {
        std::vector<std::string> names;
        for(const json& s : *sc) {
            if(!s.is_object() || !jbool(s, "enabled", true)) continue;
            const std::string trig = jstr(s, "trigger", "manual");
            if(trig == "manual" || trig == "none") continue;      // (run by a button: not by itself)
            names.push_back(jstr(s, "name", "?") + " (" + (trig == "interval" ? "every " + std::to_string(static_cast<int>(jnum(s, "interval_ms", 1000))) + " ms" : trig) + ")");
        }
        if(!names.empty()) c.automatic.push_back(std::to_string(names.size()) + " script(s) run by themselves and can write any tag: " + some(names));
    }
    if(const json* sl = jget(root, "schedules"); sl && sl->is_array())
        for(const json& s : *sl) {
            if(!s.is_object() || !jbool(s, "enabled", true)) continue;
            const std::string does = what_action_does(s.contains("do") ? s["do"] : json());
            c.automatic.push_back("schedule " + jstr(s, "name", "?") + " at " + jstr(s, "at", "?") + (does.empty() ? "" : ": " + does));
        }
    if(const json* tr = jget(root, "transfers"); tr && tr->is_array() && !tr->empty()) {
        std::vector<std::string> names;
        for(const json& t : *tr) if(t.is_object() && jbool(t, "enabled", true)) names.push_back(jstr(t, "from", "?") + " > " + jstr(t, "to", "?"));
        if(!names.empty()) c.automatic.push_back(std::to_string(names.size()) + " transfer(s) copy values from tag to tag: " + some(names));
    }

    // ---- the scripts buttons carry (they run when pressed; listed because they are code, too)
    if(!dir.empty()) {
        int n = 0;
        if(const json* screens = jget(root, "screens"); screens && screens->is_array())
            for(const json& s : *screens) {
                if(s.is_object()) { n += inline_scripts(s); continue; }
                if(!s.is_string() || !project_rel_path(s.get<std::string>())) continue;     // (nothing outside the project is read)
                std::string text;
                if(!read_text(fs::u8path(dir) / fs::u8path(s.get<std::string>()), text)) continue;
                const json def = json::parse(text, nullptr, false, true);
                if(!def.is_discarded()) n += inline_scripts(def);
            }
        if(n) c.automatic.push_back(std::to_string(n) + " script(s) on the screens run when a button is pressed and can write any tag");
    }
    return c;
}

bool project_capabilities_of(const std::string& project_json, ProjectCapabilities& out, std::string* error)
{
    std::string text;
    const fs::path p = fs::u8path(project_json);
    if(!read_text(p, text)) { if(error) *error = "cannot read " + project_json; return false; }
    const json root = json::parse(text, nullptr, false, true);
    if(root.is_discarded() || !root.is_object()) { if(error) *error = project_json + " is not valid JSON"; return false; }
    out = project_capabilities(root, p.parent_path().u8string());
    return true;
}

json ProjectCapabilities::to_json() const
{
    return json{ { "devices", devices }, { "sends", sends }, { "listens", listens }, { "automatic", automatic },
                 { "unattended_writes", unattended_writes() }, { "fingerprint", fingerprint() } };
}

std::string ProjectCapabilities::fingerprint() const
{
    const std::string s = json{ { "devices", devices }, { "sends", sends }, { "listens", listens }, { "automatic", automatic } }.dump();
    unsigned char h[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), h, 0);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for(unsigned char b : h) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

std::string ProjectCapabilities::text() const
{
    std::string out;
    auto section = [&](const char* title, const std::vector<std::string>& items) {
        out += std::string(title) + "\n";
        if(items.empty()) out += "  (nothing)\n";
        for(const std::string& i : items) out += "  - " + i + "\n";
    };
    section("Writes to devices", devices);
    section("Sends data out", sends);
    section("Opens ports for others", listens);
    section("Runs by itself", automatic);
    if(unattended_writes()) out += "! It writes to devices and runs things by itself: values can reach a device without anybody at the screen.\n";
    if(empty()) out += "This project only draws screens: it writes to no device, sends nothing, opens no port and runs nothing by itself.\n";
    out += "fingerprint " + fingerprint().substr(0, 16) + "\n";
    return out;
}

std::string ProjectCapabilities::brief() const
{
    if(empty()) return "It only draws screens.";
    std::string out;
    auto line = [&](const char* title, const std::vector<std::string>& items) {
        if(items.empty()) return;
        out += std::string(title) + ": " + items[0] + (items.size() > 1 ? " (+" + std::to_string(items.size() - 1) + " more)" : "") + "\n";
    };
    line("Writes to", devices);
    line("Sends", sends);
    line("Opens", listens);
    line("By itself", automatic);
    if(!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

} // namespace hml
