#include "system_settings.hpp"
#include "screen_saver.hpp"
#include "project.hpp"
#include "serial_ports.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"
#include "lvgl.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    // the menu: which group of pages is shown, and one line about each page under its name
    if(!tags().find("sys.menu.tab")) set_tag("sys.menu.tab", TagType::Int, Value::number(0, TagType::Int));
    update_summaries();
}

void SystemSettings::update_summaries()
{
    for(const auto& p : pages_) set_tag(tag_name(p->id(), "summary"), TagType::String, Value::string(p->summary()));
}

void SystemSettings::refresh(const std::string& page_id)
{
    for(const auto& p : pages_) {
        if(!page_id.empty() && p->id() != page_id) continue;
        std::vector<SettingField> fields = p->fields();
        for(const SettingField& f : fields) {
            if(f.type == SettingField::Type::Select && f.live_options) {       // first the options, then the selection
                std::string joined;
                for(const std::string& o : f.options) joined += (joined.empty() ? "" : "\n") + o;
                set_tag(tag_name(p->id(), f.key + "__options"), TagType::String, Value::string(joined));
            }
            set_tag(tag_name(p->id(), f.key), field_tag_type(f.type), field_value(f));
        }
        if(!tags().find(tag_name(p->id(), "status"))) set_tag(tag_name(p->id(), "status"), TagType::String, Value::string(""));
        fields_[p->id()] = std::move(fields);
    }
}

// The screens of the system settings ask for "users.system_level". Their actions ask again: a
// button of a project screen (or a script) that calls "system.do" must not get around it.
static bool system_allowed(const char* what, const std::string& page_id)
{
    const int need = users().system_level();
    if(need <= 0 || users().level() >= need) return true;
    HML_WARN("system: %s '%s' refused: level %d is needed, the user has %d", what, page_id.c_str(), need, users().level());
    return false;
}

std::string SystemSettings::apply(const std::string& page_id)
{
    SettingsPage* p = page(page_id);
    if(!p) { HML_WARN("system: unknown page '%s'", page_id.c_str()); return "unknown page"; }
    if(!system_allowed("apply", page_id)) return "not allowed";
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
    ui().register_action("system.do", [this](const std::string& arg) {
        size_t a = arg.find(':'), b = a == std::string::npos ? a : arg.find(':', a + 1);
        if(a == std::string::npos) { HML_WARN("system.do: expected \"<page>:<command>\", got '%s'", arg.c_str()); return; }
        command(arg.substr(0, a), arg.substr(a + 1, b == std::string::npos ? b : b - a - 1), b == std::string::npos ? "" : arg.substr(b + 1));
    });
}

void SystemSettings::command(const std::string& page_id, const std::string& cmd, const std::string& arg)
{
    SettingsPage* p = page(page_id);
    if(!p) { HML_WARN("system: unknown page '%s'", page_id.c_str()); return; }
    if(!system_allowed("command for", page_id)) return;
    p->command(cmd, arg);
    refresh(page_id);
}

void SystemSettings::poll()
{
    for(const auto& p : pages_) if(p->poll()) refresh(p->id());
    // the lines under the names in the menu follow what happens (about once a second, while it is shown)
    if(++summary_ticks_ % 4 == 0 && ui().current_screen().value_or("") == menu_screen()) update_summaries();
}

// ---------------------------------------------------------------- how the screens look
//
// The menu: the pages in a few groups, one group at a time as tiles in two columns. A group
// fits the screen (also 480x320), so that the menu does not scroll: a finger that wants to
// scroll cannot open a page by accident. Where a group is longer (many drivers) it scrolls,
// and the strip at the right edge has nothing in it to press.
// A page: a bar at the top with "back", the title and Apply - what acts is not in the list
// that scrolls. The list has the same empty strip at its right edge; the other buttons of a
// page are in a bar at the bottom.
namespace {

struct Group { const char* title; };
const Group kGroups[] = { { "Device" }, { "Connection" }, { "Notify" }, { "Service" } };
constexpr int kGroupCount = 4;

int group_of(const std::string& id)
{
    if(id.rfind("driver_", 0) == 0 || id == "device" || id == "monitor" || id == "transfers") return 1;
    if(id == "mail" || id == "upload" || id == "schedule") return 2;
    if(id == "deploy" || id == "storage" || id == "keys" || id == "remote") return 3;
    return 0;                                        // about, network, display, sound, users and what a project adds
}

const char* icon_of(const std::string& id)
{
    if(id == "about") return LV_SYMBOL_HOME;
    if(id == "network") return LV_SYMBOL_WIFI;
    if(id == "display") return LV_SYMBOL_IMAGE;
    if(id == "sound") return LV_SYMBOL_VOLUME_MAX;
    if(id == "users") return LV_SYMBOL_EYE_OPEN;
    if(id.rfind("driver_", 0) == 0) return LV_SYMBOL_SHUFFLE;
    if(id == "device") return LV_SYMBOL_EDIT;
    if(id == "monitor") return LV_SYMBOL_LIST;
    if(id == "transfers") return LV_SYMBOL_LOOP;
    if(id == "mail") return LV_SYMBOL_ENVELOPE;
    if(id == "upload") return LV_SYMBOL_UPLOAD;
    if(id == "schedule") return LV_SYMBOL_BELL;
    if(id == "deploy") return LV_SYMBOL_DOWNLOAD;
    if(id == "storage") return LV_SYMBOL_USB;
    if(id == "keys") return LV_SYMBOL_KEYBOARD;
    if(id == "remote") return LV_SYMBOL_VIDEO;
    return LV_SYMBOL_SETTINGS;
}

// The pages that build their rows themselves (Deploy, Keys, Storage, Users, Tags, Device ...)
// get the same look as the others here, in one place: their rows become tiles, their buttons
// and texts take the colours of the system screens. What a row contains and in which order is
// the page's and is not touched.
void restyle(json& w, bool top, bool compact, const std::string& accent)
{
    if(w.is_array()) { for(json& c : w) restyle(c, top, compact, accent); return; }
    if(!w.is_object()) return;
    static const char* kTile = "#222d3b";
    static const char* kNeutral = "#3a4a5e";
    const std::string type = jstr(w, "type", "");
    if(!w.contains("style") || !w["style"].is_object()) w["style"] = json::object();
    json& st = w["style"];
    const std::string bgc = jstr(st, "bg_color", ""), tc = jstr(st, "text_color", "");
    if(tc == "#9fb3c8") st["text_color"] = "#8fa3b8";
    else if(tc == "#ffffff") st["text_color"] = "#eef3f8";
    if(type == "button") {
        st["radius"] = 8;
        st["shadow_width"] = 0;
        if(bgc == "#2c3e50" || bgc == "#455a64" || bgc == "#37474f") st["bg_color"] = kNeutral;
        else if(bgc == "#1565c0") st["bg_color"] = accent;
    }
    else if(type == "panel") {
        const bool plain = st.contains("bg_opa") && st["bg_opa"].is_number() && st["bg_opa"].get<double>() == 0;
        if(plain && top) {                           // a row of the page: a tile
            st.erase("bg_opa");
            st["bg_color"] = kTile;
            st["radius"] = 8;
            st["pad"] = compact ? 6 : 10;
        }
        else if(bgc == "#22303f" || bgc == "#2c3e50") { st["bg_color"] = kTile; st["radius"] = 8; }
    }
    if(st.empty()) w.erase("style");
    if(w.contains("children")) restyle(w["children"], false, compact, accent);
}

} // namespace

std::vector<json> SystemSettings::screens(int width, int height) const
{
    const bool compact = width < 600;
    const int header_h = compact ? 40 : 56;
    const int font = compact ? 12 : 14;
    const int title_font = compact ? 16 : 20;
    const int kb_h = height * (compact ? 60 : 45) / 100;
    const int margin = compact ? 8 : 12;
    const int gutter = compact ? 22 : 30;            // the strip at the right edge of a list: for scrolling, nothing to press
    const int gap = compact ? 6 : 8;
    const char* bg = "#10151c";
    const char* bar = "#19212c";
    const char* tile = "#222d3b";
    const char* text = "#eef3f8";
    const char* muted = "#8fa3b8";
    const std::string accent = accent_;

    // "back", the title and - for a form - Apply: always there, never in what scrolls
    auto header = [&](const std::string& title, const std::string& apply_page) {
        json children = json::array({
            json{ { "type", "button" }, { "align", "left_mid" }, { "text", LV_SYMBOL_LEFT }, { "w", header_h + 4 }, { "h", header_h - 8 },
                  { "style", { { "bg_color", tile }, { "radius", 8 }, { "shadow_width", 0 } } }, { "style_label", { { "font", font + 2 } } },
                  { "on_click", { { "action", "back" } } } },
            json{ { "type", "label" }, { "align", "left_mid" }, { "x", header_h + 16 }, { "text", title }, { "style", { { "font", title_font }, { "text_color", text } } } },
        });
        if(!apply_page.empty())
            children.push_back(json{ { "type", "button" }, { "align", "right_mid" }, { "text", "Apply" }, { "w", compact ? 84 : 120 }, { "h", header_h - 8 },
                                     { "style", { { "bg_color", "#2e7d32" }, { "radius", 8 }, { "shadow_width", 0 } } }, { "style_label", { { "font", font + 2 } } },
                                     { "on_click", { { "action", "call" }, { "fn", "system.apply" }, { "arg", apply_page } } } });
        return json{
            { "type", "panel" }, { "x", 0 }, { "y", 0 }, { "w", "100%" }, { "h", header_h }, { "scrollable", false },
            { "style", { { "bg_color", bar }, { "radius", 0 }, { "border_width", 0 }, { "pad_left", margin }, { "pad_right", margin }, { "pad_top", 0 }, { "pad_bottom", 0 } } },
            { "children", children },
        };
    };

    std::vector<json> out;
    // with user management the whole of the system settings asks for a level ("users.system_level")
    const int level = users().system_level();

    // ---- menu
    {
        std::vector<json> items[kGroupCount];
        const int list_w = width - 2 * margin;
        const int tile_w = (list_w - gutter - gap) / 2;
        const int tile_h = compact ? 50 : 66;
        for(const auto& p : pages_) {
            const std::string id = p->id();
            items[group_of(id)].push_back(json{
                { "type", "panel" }, { "w", tile_w }, { "h", tile_h }, { "scrollable", false }, { "flex", "column" }, { "flex_main", "center" },
                { "style", { { "bg_color", tile }, { "radius", 10 }, { "border_width", 0 }, { "pad_left", compact ? 10 : 14 }, { "pad_right", 6 }, { "pad_top", 4 }, { "pad_bottom", 4 }, { "pad_row", 2 } } },
                { "style_pressed", { { "bg_color", accent } } },
                { "on_click", json::array({
                    // what the page shows is read when it is opened (ports plugged in since startup ...)
                    { { "action", "call" }, { "fn", "system.refresh" }, { "arg", id } },
                    { { "action", "goto" }, { "screen", page_screen(id) }, { "anim", "slide_left" } } }) },
                { "children", json::array({
                    json{ { "type", "label" }, { "w", "100%" }, { "h", font + 8 }, { "long_mode", "dots" }, { "text", std::string(icon_of(id)) + "  " + p->title() },
                          { "style", { { "font", font + 2 }, { "text_color", text } } } },
                    json{ { "type", "label" }, { "w", "100%" }, { "h", font + 4 }, { "long_mode", "dots" }, { "bind", { { "text", tag_name(id, "summary") } } },
                          { "style", { { "font", font }, { "text_color", muted } } } },
                }) },
            });
        }
        // The groups that have pages, as tabs: a strip under the header that belongs to it, the
        // names as text, and a line under the one that is chosen. Pressing a name writes the
        // number of its group into sys.menu.tab.
        const int tabs_h = compact ? 34 : 44, line_h = 3;
        json tabs = json::array(), marks = json::array();
        std::vector<int> used;
        for(int g = 0; g < kGroupCount; g++) if(!items[g].empty()) used.push_back(g);
        for(int g : used) {
            tabs.push_back(json{ { "type", "button" }, { "text", kGroups[g].title }, { "flex_grow", 1 }, { "h", tabs_h }, { "checkable", true },
                                 { "bind", { { "checked", { { "tag", "sys.menu.tab" }, { "eq", g } } } } },
                                 { "style", { { "bg_color", bar }, { "radius", 0 }, { "shadow_width", 0 }, { "text_color", muted } } },
                                 { "style_checked", { { "bg_color", bar }, { "text_color", "#ffffff" } } },
                                 { "style_label", { { "font", font + 2 } } } });
            marks.push_back(json{ { "type", "panel" }, { "flex_grow", 1 }, { "h", line_h }, { "scrollable", false }, { "clickable", false },
                                  { "bind", { { "bg_color", { { "tag", "sys.menu.tab" }, { "map", { { std::to_string(g), accent }, { "default", "#2b3848" } } } } } } },
                                  { "style", { { "radius", 0 }, { "border_width", 0 }, { "pad", 0 } } } });
        }
        auto strip = [&](int y, int h, json row) {
            return json{ { "type", "panel" }, { "x", 0 }, { "y", y }, { "w", "100%" }, { "h", h }, { "scrollable", false }, { "flex", "row" },
                         { "style", { { "bg_color", bar }, { "radius", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 0 } } }, { "children", std::move(row) } };
        };
        json children = json::array({ header("System", ""), strip(header_h, tabs_h, tabs), strip(header_h + tabs_h, line_h, marks) });
        const int top = header_h + tabs_h + line_h + gap;
        for(int g : used)
            children.push_back(json{ { "type", "panel" }, { "x", margin }, { "y", top }, { "w", list_w }, { "h", height - top - margin },
                                     { "flex", "row_wrap" }, { "scrollbar", "auto" },
                                     { "bind", { { "visible", { { "tag", "sys.menu.tab" }, { "eq", g } } } } },
                                     { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "radius", 0 }, { "pad", 0 }, { "pad_right", gutter }, { "pad_row", gap }, { "pad_column", gap } } },
                                     { "children", items[g] } });
        out.push_back(json{ { "name", menu_screen() }, { "level", level }, { "scrollable", false }, { "style", { { "bg_color", bg }, { "pad", 0 } } }, { "children", children } });
    }

    // ---- one screen per page
    for(const auto& p : pages_) {
        const std::string id = p->id();
        json rows = json::array();
        auto it = fields_.find(id);
        std::vector<SettingField> fields = it != fields_.end() ? it->second : p->fields();
        json custom = p->custom_rows(compact, font);
        if(custom.is_array()) { rows = std::move(custom); fields.clear(); restyle(rows, true, compact, accent); }
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
                    if(f.live_options) widget["bind"] = { { "value", tag }, { "options", tag + "__options" } };
                    break;
                default:   // Info
                    widget = json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "wrap" }, { "bind", { { "text", tag } } },
                                   { "style", { { "font", font }, { "text_color", text } } } };
                    break;
            }
            // one setting, one tile: the name at the left, what it is at the right
            rows.push_back(json{
                { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                { "style", { { "bg_color", tile }, { "radius", 8 }, { "border_width", 0 }, { "pad", compact ? 6 : 10 }, { "pad_left", compact ? 10 : 14 }, { "pad_column", 8 } } },
                { "children", json::array({
                    json{ { "type", "label" }, { "text", f.label }, { "w", compact ? 110 : 160 }, { "long_mode", "wrap" }, { "style", { { "font", font }, { "text_color", muted } } } },
                    widget,
                }) },
            });
        }
        rows.push_back(json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", tag_name(id, "status") } } },
                             { "style", { { "font", font }, { "text_color", "#ffc107" } } } });

        // the other buttons of the page: in a bar of their own at the bottom, not in what scrolls
        const auto extra = p->buttons();
        const int footer_h = extra.empty() ? 0 : (compact ? 42 : 54);
        const int body_h = height - header_h - margin - (footer_h ? footer_h : margin);
        json children = json::array({
            header(p->title(), p->has_apply() ? id : ""),             // "back" pops the history: menu, then the caller
            json{ { "type", "panel" }, { "x", margin }, { "y", header_h + margin }, { "w", width - 2 * margin }, { "h", body_h - (footer_h ? gap : 0) },
                  { "flex", "column" }, { "flex_cross", "start" }, { "scrollbar", "auto" },
                  { "style", { { "bg_opa", 0 }, { "radius", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_right", gutter }, { "pad_row", gap } } },
                  { "children", rows } },
        });
        if(footer_h) {
            json buttons = json::array();
            for(const auto& [label, cmd] : extra)
                buttons.push_back(json{ { "type", "button" }, { "text", label }, { "h", footer_h - 10 },
                                        { "style", { { "bg_color", "#3a4a5e" }, { "radius", 8 }, { "shadow_width", 0 }, { "pad_left", 14 }, { "pad_right", 14 } } },
                                        { "style_label", { { "font", font + 2 } } },
                                        { "on_click", { { "action", "call" }, { "fn", "system.do" }, { "arg", id + ":" + cmd } } } });
            children.push_back(json{ { "type", "panel" }, { "x", 0 }, { "y", height - footer_h }, { "w", "100%" }, { "h", footer_h }, { "scrollable", false },
                                     { "flex", "row" }, { "flex_cross", "center" },
                                     { "style", { { "bg_color", bar }, { "radius", 0 }, { "border_width", 0 }, { "pad_left", margin }, { "pad_right", margin }, { "pad_top", 0 }, { "pad_bottom", 0 }, { "pad_column", gap } } },
                                     { "children", buttons } });
        }
        children.push_back(json{ { "type", "keyboard" }, { "align", "bottom_mid" }, { "w", "100%" }, { "h", kb_h },
                                 { "style", { { "pad", compact ? 4 : 6 }, { "pad_gap", compact ? 4 : 6 } } }, { "style_items", { { "font", 18 } } } });
        out.push_back(json{ { "name", page_screen(id) }, { "level", level }, { "scrollable", false }, { "style", { { "bg_color", bg }, { "pad", 0 } } }, { "children", children } });
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
    std::string summary() override { return proj_.info().title; }
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
    std::string summary() override { return local_ip_addresses(); }
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

// Settings a driver type takes that are better entered on the device than kept in project.json
// (credentials): shown even when the project does not have them.
std::vector<const char*> device_only_keys(const json& cfg)
{
    const std::string type = jstr(cfg, "type", "");
    if(iequals(type, "mqtt") || iequals(type, "opcua_client") || iequals(type, "opcua_server") || iequals(type, "opcua")) return { "username", "password" };
    return {};
}

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
    std::string summary() override
    {
        const json* cfg = proj_.driver_config(key_);
        if(!cfg) return "";
        if(!jbool(*cfg, "enabled", true)) return "Off";
        const std::string host = jstr(*cfg, "host", jstr(*cfg, "device", ""));
        return host.empty() ? std::string("On") : host;
    }

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
            if(k == "device" && v.is_string()) {
                // a serial port: nobody should have to type /dev/serial/by-id/usb-... on a touch keyboard
                f.type = SettingField::Type::Select;
                f.live_options = true;
                f.value = v.get<std::string>();
                std::string found;
                for(const SerialPortInfo& p : list_serial_ports()) {
                    f.options.push_back(p.device);
                    if(!p.stable.empty()) f.options.push_back(p.stable);
                    // (the description may be in the language of the OS; the built-in fonts only have Latin letters)
                    std::string desc;
                    for(unsigned char c : p.description) if(c >= 0x20 && c < 0x7f && !(c == ' ' && !desc.empty() && desc.back() == ' ')) desc += static_cast<char>(c);
                    found += (found.empty() ? "" : "\n") + p.device + (desc.empty() ? "" : "  " + desc);
                }
                if(std::find(f.options.begin(), f.options.end(), f.value) == f.options.end()) f.options.insert(f.options.begin(), f.value);   // what is set now, even if not found
                out.push_back(f);
                SettingField info;
                info.key = "device_ports"; info.label = "ports found"; info.type = SettingField::Type::Info;
                info.value = found.empty() ? "none" : found;
                out.push_back(info);
                continue;
            }
            if(v.is_boolean()) { f.type = SettingField::Type::Bool; f.value = v.get<bool>() ? "1" : "0"; }
            else if(v.is_number()) {
                f.type = SettingField::Type::Number;
                double d = v.get<double>();
                f.decimals = (d == static_cast<double>(static_cast<long long>(d))) ? 0 : 3;
                f.value = vformat("%g", d);
            }
            else if(v.is_string()) { f.type = SettingField::Type::Text; f.value = v.get<std::string>(); f.password = k == "password"; }
            else continue;                                     // arrays / objects (map, items) are not editable here
            out.push_back(f);
        }
        for(const char* k : device_only_keys(*cfg)) {
            if(cfg->contains(k)) continue;
            SettingField f;
            f.key = f.label = k;
            f.password = std::string(k) == "password";
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
        // Only what differs from project.json is kept: a setting that was not changed here must
        // keep following the project (a later deploy of a new project.json takes effect).
        const json* base = proj_.driver_base_config(key_);
        if(!overlay["drivers"].is_object()) overlay["drivers"] = json::object();
        json& d = overlay["drivers"][key_];
        if(!d.is_object()) d = json::object();
        for(const auto& [k, text] : values) {
            json v;
            bool same = false;
            if(k == "enabled") {
                v = (text == "1");
                same = base && jbool(*base, "enabled", true) == v.get<bool>();
            }
            else {
                const json* orig = jget(*cfg, k.c_str());
                const json empty_text = "";
                if(!orig) {
                    const auto extra = device_only_keys(*cfg);
                    if(std::find_if(extra.begin(), extra.end(), [&](const char* e) { return k == e; }) == extra.end()) continue;
                    orig = &empty_text;
                }
                const json* b = base ? jget(*base, k.c_str()) : nullptr;
                if(orig->is_boolean()) { v = (text == "1"); same = b && b->is_boolean() && *b == v; }
                else if(orig->is_number()) {
                    v = std::atof(text.c_str());
                    // the field shows the number with %g: the same text means "not changed"
                    same = b && b->is_number() && (*b == v || vformat("%g", b->get<double>()) == text);
                }
                else { v = text; same = (b && b->is_string() && *b == v) || (!b && text.empty()); }
            }
            if(same) d.erase(k);
            else d[k] = v;
        }
        if(d.empty()) overlay["drivers"].erase(key_);
        if(overlay["drivers"].empty()) overlay.erase("drivers");
        if(overlay.empty()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::u8path(overlay_), ec);
            if(ec) return "cannot remove " + overlay_;
        }
        else {
            std::ofstream f(overlay_, std::ios::binary | std::ios::trunc);
            if(!f) return "cannot write " + overlay_;
            f << overlay.dump(2) << "\n";
            f.close();
        }

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
    {   // what is chosen or pressed has the colour of the project
        const lv_color_t c = proj.info().primary;
        s.set_accent(vformat("#%02x%02x%02x", c.red, c.green, c.blue));
    }
    s.add_page(make_about_page(proj));
    s.add_page(make_network_page());
    s.add_page(make_display_page());
    if(auto up = make_users_page()) s.add_page(std::move(up));
    if(auto keys = make_keys_page(proj)) s.add_page(std::move(keys));
    add_driver_pages(s, proj);
    if(const json* so = proj.section("sound"); so && so->is_object()) s.add_page(make_sound_page());
    if(const json* tr = proj.section("transfers"); tr && tr->is_array() && !tr->empty()) s.add_page(make_transfers_page());
    if(const json* ml = proj.section("mail"); ml && ml->is_object()) s.add_page(make_mail_page());
    if(const json* rm = proj.section("remote"); rm && rm->is_object()) s.add_page(make_remote_page());
    if(const json* up = proj.section("uploads"); up && up->is_array() && !up->empty()) s.add_page(make_upload_page());
    if(const json* sc = proj.section("schedules"); sc && sc->is_array() && !sc->empty()) s.add_page(make_schedule_page());
    s.add_page(make_monitor_page());
    if(has_device_driver(proj.drivers_config())) s.add_page(make_device_page());
    s.add_page(make_deploy_page(proj));
    s.add_page(make_storage_page(proj));
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
