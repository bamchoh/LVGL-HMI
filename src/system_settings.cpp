#include "system_settings.hpp"
#include "project.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"
#include "lvgl.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define HML_POPEN _popen
#  define HML_PCLOSE _pclose
#else
#  include <arpa/inet.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <netdb.h>
#  include <unistd.h>
#  define HML_POPEN popen
#  define HML_PCLOSE pclose
#endif

namespace hml {

// ================================================================ helpers

std::string run_command(const std::string& cmd, int* exit_code)
{
    std::string full = cmd + " 2>&1";
    FILE* p = HML_POPEN(full.c_str(), "r");
    if(!p) { if(exit_code) *exit_code = -1; return ""; }
    std::string out;
    char buf[512];
    size_t n;
    while((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    int rc = HML_PCLOSE(p);
#ifndef _WIN32
    if(rc != -1 && WIFEXITED(rc)) rc = WEXITSTATUS(rc);
#endif
    if(exit_code) *exit_code = rc;
    return out;
}

std::string local_ip_addresses()
{
    std::vector<std::string> ips;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    char host[256] = "";
    if(gethostname(host, sizeof(host)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if(getaddrinfo(host, nullptr, &hints, &res) == 0) {
            for(addrinfo* ai = res; ai; ai = ai->ai_next) {
                char txt[INET_ADDRSTRLEN] = "";
                auto* sin = reinterpret_cast<sockaddr_in*>(ai->ai_addr);
                if(inet_ntop(AF_INET, &sin->sin_addr, txt, sizeof(txt))) ips.push_back(txt);
            }
            freeaddrinfo(res);
        }
    }
#else
    ifaddrs* list = nullptr;
    if(getifaddrs(&list) == 0) {
        for(ifaddrs* a = list; a; a = a->ifa_next) {
            if(!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if(a->ifa_flags & IFF_LOOPBACK) continue;
            char txt[INET_ADDRSTRLEN] = "";
            auto* sin = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
            if(inet_ntop(AF_INET, &sin->sin_addr, txt, sizeof(txt))) ips.push_back(std::string(txt) + " (" + a->ifa_name + ")");
        }
        freeifaddrs(list);
    }
#endif
    std::string out;
    for(const std::string& ip : ips) { if(!out.empty()) out += ", "; out += ip; }
    return out.empty() ? "-" : out;
}

namespace {

std::string trim(std::string s)
{
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while(i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r')) i++;
    return s.substr(i);
}

std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string line;
    while(std::getline(ss, line)) { line = trim(line); if(!line.empty()) out.push_back(line); }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for(char c : s) { if(c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}

std::string hostname()
{
    char host[256] = "";
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    if(gethostname(host, sizeof(host)) != 0) return "-";
    return host;
}

const char* backend_name()
{
#if defined(HML_BACKEND_FBDEV)
    return "fbdev";
#elif defined(HML_BACKEND_DRM)
    return "drm";
#else
    return "sdl";
#endif
}

} // namespace

// ================================================================ SystemSettings

void SystemSettings::add_page(std::unique_ptr<SettingsPage> page)
{
    if(!page) return;
    for(auto& p : pages_) {
        if(p->id() == page->id()) { HML_WARN("system: page '%s' registered twice, replacing", page->id().c_str()); p = std::move(page); return; }
    }
    pages_.push_back(std::move(page));
}

void SystemSettings::clear()
{
    pages_.clear();
    fields_.clear();
}

SettingsPage* SystemSettings::page(const std::string& id) const
{
    for(const auto& p : pages_) if(p->id() == id) return p.get();
    return nullptr;
}

std::vector<std::string> SystemSettings::page_ids() const
{
    std::vector<std::string> out;
    for(const auto& p : pages_) out.push_back(p->id());
    return out;
}

namespace {

TagType field_tag_type(SettingField::Type t)
{
    switch(t) {
        case SettingField::Type::Number: return TagType::Float;
        case SettingField::Type::Bool: return TagType::Bool;
        case SettingField::Type::Select: return TagType::Int;
        default: return TagType::String;
    }
}

Value field_value(const SettingField& f)
{
    switch(f.type) {
        case SettingField::Type::Number: return Value::number(std::atof(f.value.c_str()));
        case SettingField::Type::Bool: return Value::number((f.value == "1" || iequals(f.value, "true")) ? 1 : 0, TagType::Bool);
        case SettingField::Type::Select: {
            int idx = 0;
            for(size_t i = 0; i < f.options.size(); i++) if(f.options[i] == f.value) idx = static_cast<int>(i);
            return Value::number(idx, TagType::Int);
        }
        default: return Value::string(f.value);
    }
}

// Create the tag if needed, otherwise just update it.
void set_tag(const std::string& name, TagType type, const Value& v)
{
    Tag* t = tags().find(name);
    if(!t) t = tags().create(name, type, &v, false, 0, 0);
    else t->set(v, nullptr);
}

} // namespace

void SystemSettings::create_tags()
{
    for(const auto& p : pages_) refresh(p->id());
}

void SystemSettings::refresh(const std::string& page_id)
{
    for(const auto& p : pages_) {
        if(!page_id.empty() && p->id() != page_id) continue;
        std::vector<SettingField> fields = p->fields();
        for(const SettingField& f : fields) set_tag(tag_name(p->id(), f.key), field_tag_type(f.type), field_value(f));
        if(!tags().find(tag_name(p->id(), "status"))) set_tag(tag_name(p->id(), "status"), TagType::String, Value::string(""));
        fields_[p->id()] = std::move(fields);
    }
}

std::string SystemSettings::apply(const std::string& page_id)
{
    SettingsPage* p = page(page_id);
    if(!p) { HML_WARN("system: unknown page '%s'", page_id.c_str()); return "unknown page"; }
    if(!fields_.count(page_id)) refresh(page_id);

    std::map<std::string, std::string> values;
    for(const SettingField& f : fields_[page_id]) {
        Tag* t = tags().find(tag_name(page_id, f.key));
        if(!t) continue;
        Value v = t->get();
        switch(f.type) {
            case SettingField::Type::Number: values[f.key] = vformat("%g", v.as_number()); break;
            case SettingField::Type::Bool: values[f.key] = v.truthy() ? "1" : "0"; break;
            case SettingField::Type::Select: {
                int idx = static_cast<int>(v.as_number());
                values[f.key] = (idx >= 0 && static_cast<size_t>(idx) < f.options.size()) ? f.options[static_cast<size_t>(idx)] : "";
                break;
            }
            default: values[f.key] = v.is_string() ? v.str : v.format(); break;
        }
    }
    std::string msg = p->apply(values);
    refresh(page_id);
    set_tag(tag_name(page_id, "status"), TagType::String, Value::string(msg.empty() ? "Applied" : msg));
    if(msg.empty()) HML_INFO("system: page '%s' applied", page_id.c_str());
    else HML_WARN("system: page '%s': %s", page_id.c_str(), msg.c_str());
    return msg;
}

void SystemSettings::register_actions()
{
    ui().register_action("system.apply", [this](const std::string& arg) { apply(arg); });
    ui().register_action("system.refresh", [this](const std::string& arg) { refresh(arg); });
}

std::vector<json> SystemSettings::screens(int width, int height) const
{
    const bool compact = width < 600;
    const int header_h = compact ? 40 : 56;
    const int font = compact ? 12 : 14;
    const int title_font = compact ? 16 : 20;
    const int kb_h = height * (compact ? 45 : 40) / 100;
    const char* bg = "#141a22";
    const char* card = "#1b2430";
    const char* muted = "#9fb3c8";

    auto header = [&](const std::string& title, const json& back_action) {
        return json{
            { "type", "panel" }, { "x", 0 }, { "y", 0 }, { "w", "100%" }, { "h", header_h }, { "scrollable", false },
            { "style", { { "bg_color", "#1f2a38" }, { "radius", 0 }, { "border_width", 0 }, { "pad_left", 8 }, { "pad_right", 8 }, { "pad_top", 0 }, { "pad_bottom", 0 } } },
            { "children", json::array({
                json{ { "type", "button" }, { "align", "left_mid" }, { "text", "< Back" }, { "h", header_h - 10 }, { "style_label", { { "font", font } } }, { "on_click", back_action } },
                json{ { "type", "label" }, { "align", "center" }, { "text", title }, { "style", { { "font", title_font }, { "text_color", "#ffffff" } } } },
            }) },
        };
    };

    std::vector<json> out;

    // ---- menu
    json menu_items = json::array();
    for(const auto& p : pages_) {
        menu_items.push_back(json{ { "type", "button" }, { "text", p->title() }, { "w", "100%" }, { "h", compact ? 34 : 44 },
                                   { "style_label", { { "font", font + 2 } } },
                                   { "on_click", { { "action", "goto" }, { "screen", page_screen(p->id()) }, { "anim", "slide_left" } } } });
    }
    out.push_back(json{
        { "name", menu_screen() }, { "scrollable", false }, { "style", { { "bg_color", bg }, { "pad", 0 } } },
        { "children", json::array({
            header("System", json{ { "action", "back" } }),
            json{ { "type", "panel" }, { "x", 8 }, { "y", header_h + 8 }, { "w", width - 16 }, { "h", height - header_h - 16 },
                  { "flex", "column" }, { "style", { { "bg_color", card }, { "radius", 8 }, { "border_width", 0 }, { "pad", 8 }, { "pad_row", 6 } } },
                  { "children", menu_items } },
        }) },
    });

    // ---- one screen per page
    for(const auto& p : pages_) {
        const std::string id = p->id();
        json rows = json::array();
        auto it = fields_.find(id);
        std::vector<SettingField> fields = it != fields_.end() ? it->second : p->fields();
        for(const SettingField& f : fields) {
            const std::string tag = tag_name(id, f.key);
            json widget;
            switch(f.type) {
                case SettingField::Type::Text:
                    widget = json{ { "type", f.password ? "password_display" : "text_display" }, { "input", true }, { "flex_grow", 1 }, { "bind", tag },
                                   { "style", { { "font", font }, { "pad", 6 } } } };
                    break;
                case SettingField::Type::Number:
                    widget = json{ { "type", "numeric_display" }, { "input", true }, { "w", compact ? 110 : 150 }, { "decimals", f.decimals }, { "unit", f.unit },
                                   { "bind", tag }, { "style", { { "font", font }, { "pad", 6 } } } };
                    break;
                case SettingField::Type::Bool:
                    widget = json{ { "type", "switch" }, { "bind", tag } };
                    break;
                case SettingField::Type::Select:
                    widget = json{ { "type", "dropdown" }, { "options", f.options }, { "flex_grow", 1 }, { "bind", tag }, { "style", { { "font", font } } } };
                    break;
                default:   // Info
                    widget = json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "wrap" }, { "bind", { { "text", tag } } },
                                   { "style", { { "font", font }, { "text_color", "#ffffff" } } } };
                    break;
            }
            rows.push_back(json{
                { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 8 } } },
                { "children", json::array({
                    json{ { "type", "label" }, { "text", f.label }, { "w", compact ? 110 : 160 }, { "long_mode", "wrap" }, { "style", { { "font", font }, { "text_color", muted } } } },
                    widget,
                }) },
            });
        }
        rows.push_back(json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", tag_name(id, "status") } } },
                             { "style", { { "font", font }, { "text_color", "#ffc107" } } } });
        if(p->has_apply()) {
            rows.push_back(json{ { "type", "button" }, { "text", "Apply" }, { "w", compact ? 100 : 140 }, { "h", compact ? 34 : 44 },
                                 { "style", { { "bg_color", "#2e7d32" } } }, { "style_label", { { "font", font + 2 } } },
                                 { "on_click", { { "action", "call" }, { "fn", "system.apply" }, { "arg", id } } } });
        }
        out.push_back(json{
            { "name", page_screen(id) }, { "scrollable", false }, { "style", { { "bg_color", bg }, { "pad", 0 } } },
            { "children", json::array({
                header(p->title(), json{ { "action", "back" } }),   // pop the history: menu, then the caller
                json{ { "type", "panel" }, { "x", 8 }, { "y", header_h + 8 }, { "w", width - 16 }, { "h", height - header_h - 16 },
                      { "flex", "column" }, { "flex_cross", "start" }, { "scrollbar", "auto" },
                      { "style", { { "bg_color", card }, { "radius", 8 }, { "border_width", 0 }, { "pad", 8 }, { "pad_row", 6 } } },
                      { "children", rows } },
                json{ { "type", "keyboard" }, { "align", "bottom_mid" }, { "w", "100%" }, { "h", kb_h } },
            }) },
        });
    }
    return out;
}

SystemSettings& system_settings()
{
    static SystemSettings inst;
    return inst;
}

// ================================================================ built-in pages

namespace {

class AboutPage final : public SettingsPage {
public:
    explicit AboutPage(Project& proj) : proj_(proj) {}
    std::string id() const override { return "about"; }
    std::string title() const override { return "About"; }
    bool has_apply() const override { return false; }
    std::vector<SettingField> fields() override
    {
        const ProjectInfo& info = proj_.info();
        auto f = [](const char* key, const char* label, std::string value) {
            SettingField s; s.key = key; s.label = label; s.type = SettingField::Type::Info; s.value = std::move(value); return s;
        };
        return {
            f("host", "Host name", hostname()),
            f("ip", "IP address", local_ip_addresses()),
            f("project", "Project", info.title),
            f("file", "Project file", info.path),
            f("display", "Display", vformat("%s %dx%d", backend_name(), info.width, info.height)),
            f("version", "Runtime", vformat("HML built %s, LVGL %d.%d.%d", __DATE__, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH)),
        };
    }
    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

private:
    Project& proj_;
};

// ---------------------------------------------------------------- network (NetworkManager / nmcli)

bool safe_arg(const std::string& s)
{
    for(char c : s) {
        if(!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == ',' || c == ':' || c == '/' || c == '-' || c == '_' || c == ' ')) return false;
    }
    return true;
}

class NetworkPage final : public SettingsPage {
public:
    std::string id() const override { return "network"; }
    std::string title() const override { return "Network"; }
#ifdef _WIN32
    bool has_apply() const override { return false; }          // information only on the PC
#endif

    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        auto info = [&](const char* key, const char* label, std::string value) {
            SettingField s; s.key = key; s.label = label; s.type = SettingField::Type::Info; s.value = std::move(value); out.push_back(s);
        };
#ifdef _WIN32
        info("host", "Host name", hostname());
        info("ip", "IP address", local_ip_addresses());
        info("note", "Note", "IP settings can be changed on Linux (NetworkManager) only");
        return out;
#else
        int rc = 0;
        std::string list = run_command("nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status", &rc);
        if(rc != 0) {
            info("host", "Host name", hostname());
            info("ip", "IP address", local_ip_addresses());
            info("note", "Note", "nmcli (NetworkManager) is not available: " + trim(list).substr(0, 120));
            return out;
        }
        devices_.clear();
        connections_.clear();
        std::string first_connected;
        for(const std::string& line : split_lines(list)) {
            std::vector<std::string> f = split(line, ':');
            if(f.size() < 4) continue;
            if(f[1] != "ethernet" && f[1] != "wifi") continue;
            devices_.push_back(f[0]);
            connections_[f[0]] = f[3];
            if(first_connected.empty() && f[2].rfind("connected", 0) == 0) first_connected = f[0];
        }
        if(devices_.empty()) { info("note", "Note", "no ethernet / wifi device found"); return out; }
        if(std::find(devices_.begin(), devices_.end(), dev_) == devices_.end()) dev_ = first_connected.empty() ? devices_[0] : first_connected;

        SettingField dev; dev.key = "interface"; dev.label = "Interface"; dev.type = SettingField::Type::Select; dev.options = devices_; dev.value = dev_;
        out.push_back(dev);

        std::string con = connections_[dev_];
        std::string method = "dhcp", address, gateway, dns;
        if(!con.empty() && con != "--") {
            std::string cfg = run_command("nmcli -t -f ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns connection show \"" + con + "\"");
            for(const std::string& line : split_lines(cfg)) {
                size_t colon = line.find(':');
                if(colon == std::string::npos) continue;
                std::string k = line.substr(0, colon), v = line.substr(colon + 1);
                if(k == "ipv4.method") method = (v == "manual") ? "static" : "dhcp";
                else if(k == "ipv4.addresses") address = v;
                else if(k == "ipv4.gateway") gateway = v;
                else if(k == "ipv4.dns") dns = v;
            }
        }
        SettingField m; m.key = "method"; m.label = "IPv4 method"; m.type = SettingField::Type::Select; m.options = { "dhcp", "static" }; m.value = method;
        out.push_back(m);
        auto text = [&](const char* key, const char* label, std::string value) {
            SettingField s; s.key = key; s.label = label; s.type = SettingField::Type::Text; s.value = std::move(value); out.push_back(s);
        };
        text("address", "Address (CIDR)", address);
        text("gateway", "Gateway", gateway);
        text("dns", "DNS", dns);
        std::string now = trim(run_command("nmcli -g IP4.ADDRESS device show \"" + dev_ + "\""));
        std::replace(now.begin(), now.end(), '\n', ' ');
        info("current", "Current IP", now.empty() ? "-" : now);
        info("connection", "Connection", con);
        return out;
#endif
    }

    std::string apply(const std::map<std::string, std::string>& values) override
    {
#ifdef _WIN32
        (void)values;
        return "Changing the IP address is only supported on Linux (NetworkManager)";
#else
        auto get = [&](const char* k) { auto it = values.find(k); return it == values.end() ? std::string() : it->second; };
        std::string dev = get("interface");
        if(dev.empty()) return "no network device";
        if(dev != dev_) { dev_ = dev; return "interface changed: values reloaded, press Apply again"; }
        std::string con = connections_[dev];
        if(con.empty() || con == "--") return "device " + dev + " has no NetworkManager connection";
        std::string method = get("method"), address = get("address"), gateway = get("gateway"), dns = get("dns");
        for(const std::string& s : { con, address, gateway, dns }) if(!safe_arg(s)) return "invalid characters in a value";

        std::string cmd = "nmcli connection modify \"" + con + "\"";
        if(method == "static") {
            if(address.empty()) return "address is required for a static IP (e.g. 192.168.0.8/24)";
            if(address.find('/') == std::string::npos) address += "/24";
            cmd += " ipv4.method manual ipv4.addresses \"" + address + "\" ipv4.gateway \"" + gateway + "\" ipv4.dns \"" + dns + "\"";
        }
        else cmd += " ipv4.method auto ipv4.addresses \"\" ipv4.gateway \"\" ipv4.dns \"\"";
        auto run_privileged = [](const std::string& c, std::string& out) {
            int rc = 0;
            out = run_command(c, &rc);
            if(rc != 0) out = run_command("sudo -n " + c, &rc);   // polkit says "auth": no agent in a service
            return rc;
        };
        std::string out;
        if(run_privileged(cmd, out) != 0) return "nmcli: " + trim(out).substr(0, 160) + " (install tools/rpi/hml.sudoers)";
        if(run_privileged("nmcli connection up \"" + con + "\"", out) != 0) return "activation failed: " + trim(out).substr(0, 160);
        return "";
#endif
    }

private:
    std::string dev_;
    std::vector<std::string> devices_;
    std::map<std::string, std::string> connections_;
};

// ---------------------------------------------------------------- drivers

class DriverPage final : public SettingsPage {
public:
    DriverPage(Project& proj, std::string key, std::string title)
        : proj_(proj), key_(std::move(key)), title_(std::move(title)), overlay_(proj.local_overlay_path()) {}

    std::string id() const override
    {
        std::string s = "driver_";
        for(char c : key_) s += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
        return s;
    }
    std::string title() const override { return "Driver: " + title_; }

    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        const json* cfg = proj_.driver_config(key_);
        SettingField en; en.key = "enabled"; en.label = "Enabled"; en.type = SettingField::Type::Bool;
        en.value = (cfg && !jbool(*cfg, "enabled", true)) ? "0" : "1";
        out.push_back(en);
        if(!cfg) return out;
        for(auto it = cfg->begin(); it != cfg->end(); ++it) {
            const std::string& k = it.key();
            const json& v = it.value();
            if(k == "type" || k == "name" || k == "enabled") continue;
            SettingField f;
            f.key = k;
            f.label = k;
            if(v.is_boolean()) { f.type = SettingField::Type::Bool; f.value = v.get<bool>() ? "1" : "0"; }
            else if(v.is_number()) {
                f.type = SettingField::Type::Number;
                double d = v.get<double>();
                f.decimals = (d == static_cast<double>(static_cast<long long>(d))) ? 0 : 3;
                f.value = vformat("%g", d);
            }
            else if(v.is_string()) { f.type = SettingField::Type::Text; f.value = v.get<std::string>(); }
            else continue;                                     // arrays / objects (map, items) are not editable here
            out.push_back(f);
        }
        return out;
    }

    std::string apply(const std::map<std::string, std::string>& values) override
    {
        const json* cfg = proj_.driver_config(key_);
        if(!cfg) return "driver '" + key_ + "' is no longer in the project";

        json overlay = json::object();
        {
            std::ifstream f(overlay_, std::ios::binary);
            if(f) {
                std::stringstream ss;
                ss << f.rdbuf();
                try { overlay = json::parse(ss.str(), nullptr, true, true); } catch(const json::exception&) { overlay = json::object(); }
                if(!overlay.is_object()) overlay = json::object();
            }
        }
        json& d = overlay["drivers"][key_];
        if(!d.is_object()) d = json::object();
        for(const auto& [k, text] : values) {
            if(k == "enabled") { d[k] = (text == "1"); continue; }
            const json* orig = jget(*cfg, k.c_str());
            if(!orig) continue;
            if(orig->is_boolean()) d[k] = (text == "1");
            else if(orig->is_number()) d[k] = std::atof(text.c_str());
            else d[k] = text;
        }
        std::ofstream f(overlay_, std::ios::binary | std::ios::trunc);
        if(!f) return "cannot write " + overlay_;
        f << overlay.dump(2) << "\n";
        f.close();

        int running = proj_.restart_drivers();
        HML_INFO("system: driver settings saved to %s, %d driver(s) running", overlay_.c_str(), running);
        return "";
    }

private:
    Project&    proj_;
    std::string key_, title_, overlay_;
};

} // namespace

std::unique_ptr<SettingsPage> make_about_page(Project& proj) { return std::make_unique<AboutPage>(proj); }
std::unique_ptr<SettingsPage> make_network_page() { return std::make_unique<NetworkPage>(); }

void add_driver_pages(SystemSettings& s, Project& proj)
{
    const json* drivers = proj.drivers_config();
    std::vector<std::string> keys = driver_keys(drivers);
    for(size_t i = 0; i < keys.size(); i++) {
        const json& cfg = (*drivers)[i];
        std::string title = jstr(cfg, "name", jstr(cfg, "type", "driver"));
        s.add_page(std::make_unique<DriverPage>(proj, keys[i], title));
    }
}

void register_builtin_pages(SystemSettings& s, Project& proj)
{
    s.clear();
    s.add_page(make_about_page(proj));
    s.add_page(make_network_page());
    add_driver_pages(s, proj);
}

std::vector<std::string> driver_keys(const json* drivers)
{
    std::vector<std::string> keys;
    if(!drivers || !drivers->is_array()) return keys;
    std::map<std::string, int> seen;
    for(const json& cfg : *drivers) {
        std::string key = jstr(cfg, "name", jstr(cfg, "type", "driver"));
        int n = ++seen[key];
        if(n > 1) key += "#" + std::to_string(n);
        keys.push_back(key);
    }
    return keys;
}

} // namespace hml
