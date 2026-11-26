// HML runtime - declarative LVGL HMI runtime.
// The backend (SDL window / Linux fbdev / Linux DRM) is selected at build time, see platform.hpp.
//
//   hml_runtime [project.json] [--screen <name>] [--exit-after <ms>] [--screenshot <file.bmp>]
//               [--reload-after <ms>] [--click <x>,<y>,<ms>[,<hold ms>]]... [--shot-at <ms>,<file.bmp>]...
//   hml_runtime --splash [project.json] [--screenshot <file.bmp>] [--exit-after <ms>]
//   hml_runtime --export-system-log <file.csv> [project.json] [--log-level info|warn|error]
//   hml_runtime --export-data-log <file.csv> [project.json] [--group <name>]
//   hml_runtime --export-alarm-log <file.csv> [project.json]
//   hml_runtime --export-operation-log <file.csv> [project.json]
//
// --export-system-log writes the system log ("system_log" in project.json) as CSV and exits; it can be
// used while the HMI is running.
// --splash draws the boot splash (display.splash image, or the title) and exits at once, leaving
// the frame on the framebuffer and the console VT in graphics mode until the HMI starts
// (tools/rpi/hml-splash.service). On the SDL backend the window stays open for --exit-after ms.
// --click injects a pointer click at (x,y) <ms> after start (UI test automation);
// --shot-at saves a screenshot at the given time. Both may be repeated.
// --stats logs every <seconds> how busy the main loop is (passes per second, longest pass).
// --ignore-active: load the project from the command line even if an active_project file next
// to the executable names another one (see active_project.hpp).
//
// SDL: F5 = reload screens, Esc = quit.  Linux fbdev/drm: SIGHUP/SIGUSR1 = reload, SIGUSR2 = restart,
// SIGINT/SIGTERM = quit.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "lvgl.h"
#include "alarm.hpp"
#include "data_log.hpp"
#include "driver.hpp"
#include "operation_log.hpp"
#include "system_log.hpp"
#include "platform.hpp"
#include "project.hpp"
#include "script.hpp"
#include "splash.hpp"
#include "system_settings.hpp"
#include "recipe.hpp"
#include "clock_tags.hpp"
#include "screen_saver.hpp"
#include "tag.hpp"
#include "active_project.hpp"
#include "check.hpp"
#include "screenshot.hpp"
#include "texts.hpp"
#include "project_archive.hpp"
#include "transfer.hpp"
#include "data_transfer.hpp"
#include "mail.hpp"
#include "upload.hpp"
#include "schedule.hpp"
#include "sound.hpp"
#include "vnc.hpp"
#include <csignal>
#include "deploy_service.hpp"
#include "system_access.hpp"
#include "capabilities.hpp"
#include "serial_ports.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"

namespace {

volatile bool s_quit = false;
volatile bool s_reload = false;
volatile int  s_restart = -1;      // -1 none, otherwise a hml::RestartTarget

// ---- scripted pointer clicks (--click) ----
struct Click { int32_t x, y; uint32_t at_ms; uint32_t hold_ms = 80; int reads = 0; };   // reads: how often LVGL saw it pressed
struct Shot { uint32_t at_ms; std::string path; bool done = false; };
std::vector<Click> s_clicks;
std::vector<Shot>  s_shots;
uint32_t s_t_start = 0;

void script_pointer_read(lv_indev_t*, lv_indev_data_t* data)
{
    uint32_t now = hml::millis() - s_t_start;
    data->state = LV_INDEV_STATE_RELEASED;
    // A click is pressed from its time on until LVGL has read it pressed at least twice and for
    // 80 ms: a busy main loop (screenshots of a large animated screen) must not lose it.
    for(Click& c : s_clicks) {
        // (--click x,y,ms,hold_ms: held that long, e.g. the corner that opens the system settings)
        if(now < c.at_ms || (c.reads >= 2 && now >= c.at_ms + c.hold_ms)) continue;
        data->point.x = c.x;
        data->point.y = c.y;
        data->state = LV_INDEV_STATE_PRESSED;
        c.reads++;
        return;
    }
}

bool file_exists(const std::string& p)
{
    return std::ifstream(p, std::ios::binary).good();
}

int usage(const char* argv0)
{
    std::fprintf(stderr, "usage: %s <project.json> [--screen <name>] [--exit-after <ms>] [--screenshot <file.png|bmp>]\n"
                         "       [--reload-after <ms>] [--click <x>,<y>,<ms>[,<hold ms>]]... [--shot-at <ms>,<file.png|bmp>]...\n"
                         "       [--stats <seconds>] [--ignore-active] [--lang <code>] [--set <tag>=<value>]...\n"
                         "       [--data-dir <folder>]   (where this device keeps its keys for System > Deploy; default: keys/ next to the executable)\n"
                         "       %s <project.json> --check [--render <screen>[+<popup>],<file.png>]... [--at <ms>] [--lang <code>] [--set <tag>=<value>]...\n"
                         "           (no window, no drivers, nothing recorded: lists what is wrong; exit code 1 if anything is)\n"
                         "       %s <project.json> --capabilities | --capabilities-json\n"
                         "           (what the project can do beyond drawing screens: devices it writes to, where it sends data,\n"
                         "            ports it opens, what runs by itself. Read from its files, nothing is started)\n"
                         "       %s --list-serial-ports\n"
                         "       %s --splash <project.json> [--screenshot <file.bmp>] [--exit-after <ms>]\n"
                         "       %s --export-system-log <file.csv> <project.json> [--log-level info|warn|error]\n"
                         "       %s --export-data-log <file.csv> <project.json> [--group <name>]\n"
                         "       %s --export-alarm-log | --export-operation-log <file.csv> <project.json>\n", argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
    return 1;
}

// ---- --check / --render: look at a project without running it ----
// The files are checked (check.hpp), then the project is loaded and every screen is built on a
// display that exists in memory only, so that what the runtime itself complains about is seen
// too. No window, no drivers (no device is contacted), no logs, nothing written but the pictures.
struct Render { std::string screen, popup, path; };     // popup: opened over the screen ("screen+popup")
std::vector<std::string> s_check_log;
void check_log_sink(hml::LogLevel level, const std::string& msg)
{
    if(level == hml::LogLevel::Info) return;
    // (what check_project already reports, with the place)
    if(msg.rfind("bind: unknown tag", 0) == 0 || msg.rfind("unknown widget type", 0) == 0 || msg.rfind("action '", 0) == 0) return;
    if(msg.find("are for PNG only") != std::string::npos || msg.find("only files inside the project folder") != std::string::npos) return;
    for(const std::string& m : s_check_log) if(m == msg) return;
    s_check_log.push_back(msg);
}

int run_check(const std::string& project_path, const std::vector<Render>& renders, const std::string& lang,
              const std::vector<std::pair<std::string, std::string>>& sets, uint32_t render_at_ms)
{
    std::vector<std::string> screen_names;
    std::vector<hml::SysTagUse> sys_uses;
    std::vector<hml::CheckProblem> problems = hml::check_project(project_path, &screen_names, &sys_uses);
    hml::set_log_quiet(true);
    hml::set_log_sink(check_log_sink);
    bool loaded = hml::project().load(project_path);
    int rendered = 0;
    std::vector<std::string> notes, glyphs;
    std::vector<bool> written(renders.size(), false);
    if(loaded) {
        const hml::ProjectInfo& info = hml::project().info();
        lv_init();
        lv_tick_set_cb([]() -> uint32_t { return hml::millis(); });
        static std::vector<uint32_t> buf;
        buf.resize(static_cast<size_t>(info.width) * 40);
        lv_display_t* disp = lv_display_create(info.width, info.height);
        lv_display_set_buffers(disp, buf.data(), nullptr, static_cast<uint32_t>(buf.size() * 4), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(disp, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
        hml::project().apply_theme(disp);
        hml::ui().init(nullptr, hml::AppCallbacks{ [] {}, [] {}, [](hml::RestartTarget) {} });
        hml::system_settings().register_actions();
        hml::register_user_actions();
        for(const auto& [name, value] : sets) {
            hml::Tag* t = hml::tags().find(name);
            auto v = t ? hml::Value::parse(t->type(), value) : std::nullopt;
            if(!t) notes.push_back("--set: unknown tag \"" + name + "\"");
            else if(!v) notes.push_back("--set: \"" + value + "\" is not a value for " + name);
            else t->set(*v, nullptr);
        }
        if(!lang.empty() && !hml::texts().set_language(lang)) notes.push_back("--lang: \"" + lang + "\" is not a language of the project");
        // alarms, trends and recipes as on the device, recorded into a folder that is thrown away
        std::error_code ec;
        const std::filesystem::path scratch = std::filesystem::temp_directory_path(ec) / ("hml-check-" + std::to_string(hml::LogDb::now_ms()));
        std::filesystem::create_directories(scratch, ec);
        hml::project().start_logs(false, scratch.generic_u8string());
        {   // the settings of the drivers, read by the drivers themselves (addresses ...); nothing connects
            hml::DriverRegistry reg;
            reg.register_builtins();
            reg.check(hml::project().section("drivers"));
        }
        hml::project().build_screens();
        hml::users().preview_as_highest();
        // the runtime's tags of this project exist now: a "sys." name that is not one of them is a mistake
        for(hml::CheckProblem& p : hml::unknown_sys_tags(sys_uses, [](const std::string& t) { return hml::tags().find(t) != nullptr; }))
            problems.push_back(std::move(p));
        auto settle = [&](int passes) {
            for(int i = 0; i < passes; i++) { hml::tags().dispatch(false); lv_timer_handler(); hml::sleep_ms(5); }
        };
        // every screen once: screens are built when they are first shown
        // characters the font has no glyph for (drawn as empty boxes): every text of "texts" in every
        // language, then what each screen shows now (texts written into the screens, in their fonts)
        const lv_font_t* theme_font = lv_obj_get_style_text_font(lv_layer_top(), LV_PART_MAIN);
        std::vector<std::string> text_misses;
        if(const hml::json* tx = hml::project().section("texts"); tx && tx->is_object()) {
            for(auto id = tx->begin(); id != tx->end(); ++id) {
                if(!id.value().is_object()) continue;
                for(auto l = id.value().begin(); l != id.value().end(); ++l) {
                    if(!l.value().is_string()) continue;
                    const std::string miss = hml::missing_glyphs(theme_font, l.value().get<std::string>());
                    if(miss.empty()) continue;
                    glyphs.push_back("project.json: texts." + id.key() + "." + l.key() + ": no glyph in the font for: " + miss);
                    text_misses.push_back(l.value().get<std::string>());
                }
            }
        }
        for(const std::string& name : screen_names) {
            if(!hml::ui().show_screen(name, "none")) s_check_log.push_back("screen '" + name + "' cannot be shown");
            settle(3);
            for(lv_obj_t* root : { lv_screen_active(), lv_layer_top() }) {
                for(const std::string& m : hml::missing_glyphs_on(root)) {
                    // (a text of "texts" is reported there, once)
                    bool known = false;
                    for(const std::string& t : text_misses) if(m.rfind("\"" + t + "\"", 0) == 0) known = true;
                    const std::string line = "screen \"" + name + "\": text " + m;
                    if(!known && std::find(glyphs.begin(), glyphs.end(), line) == glyphs.end()) glyphs.push_back(line);
                }
            }
        }
        for(size_t ri = 0; ri < renders.size(); ri++) {
            const Render& r = renders[ri];
            while(hml::ui().close_popup()) {}
            if(!hml::project().info().start_screen.empty()) hml::ui().show_screen(hml::project().info().start_screen, "none");
            if(!hml::ui().show_screen(r.screen, "none")) { notes.push_back("--render: unknown screen \"" + r.screen + "\""); continue; }
            if(!r.popup.empty() && !hml::ui().open_popup(r.popup)) { notes.push_back("--render: \"" + r.popup + "\" is not a popup"); continue; }
            settle(std::max(1, static_cast<int>(render_at_ms / 5)));     // (animations: the picture after that time)
            lv_refr_now(disp);
            if(hml::save_screenshot(disp, r.path)) { rendered++; written[ri] = true; }
            else notes.push_back("--render: cannot write " + r.path + " (a Windows path, D:/..., not /d/...)");
        }
    }
    hml::set_log_sink(nullptr);
    if(loaded) {
        hml::project().stop_logs();
        std::error_code ec;
        const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
        for(auto it = std::filesystem::directory_iterator(tmp, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            const std::string n = it->path().filename().generic_u8string();
            if(n.rfind("hml-check-", 0) == 0) { std::error_code rec; std::filesystem::remove_all(it->path(), rec); }
        }
    }
    for(const hml::CheckProblem& p : problems) std::printf("%s\n", p.line().c_str());
    if(!loaded && problems.empty()) std::printf("project.json: cannot be loaded\n");
    for(const std::string& m : s_check_log) std::printf("runtime: %s\n", m.c_str());
    for(const std::string& m : glyphs) std::printf("%s (drawn as an empty box)\n", m.c_str());
    for(const std::string& m : notes) std::printf("%s\n", m.c_str());
    for(size_t ri = 0; ri < renders.size(); ri++) {           // (only the pictures that were written)
        const Render& r = renders[ri];
        if(written[ri]) std::printf("rendered: %s%s%s -> %s\n", r.screen.c_str(), r.popup.empty() ? "" : "+", r.popup.c_str(), r.path.c_str());
    }
    const size_t n = problems.size() + s_check_log.size() + notes.size() + glyphs.size() + ((!loaded && problems.empty()) ? 1 : 0);
    if(n) std::printf("%d problem(s)\n", static_cast<int>(n));
    else std::printf("no problems\n");
    (void)rendered;
    std::fflush(nullptr);
    std::_Exit(n ? 1 : 0);      // (nothing to save; LVGL and the stores are not torn down)
}

// --splash: draw one frame, then exit
int run_splash(lv_display_t* disp, const hml::ProjectInfo& info, const std::string& screenshot, uint32_t exit_after)
{
    hml::build_splash(disp, info);
    lv_refr_now(disp);
    if(!screenshot.empty()) hml::platform::screenshot(disp, screenshot);
    HML_INFO("splash shown (%s)", info.splash.empty() ? "title" : info.splash.c_str());
#if defined(HML_BACKEND_SDL)
    const uint32_t t0 = hml::millis(), until = exit_after ? exit_after : 2000;
    while(!s_quit && hml::millis() - t0 < until) { hml::platform::poll(); lv_timer_handler(); hml::sleep_ms(10); }
    hml::platform::deinit();
#else
    (void)exit_after;
    // No platform::deinit(): it would switch the VT back to text mode and fbcon would clear the
    // picture. The frame stays on /dev/fb0 until the HMI draws over it.
#endif
    std::fflush(nullptr);
    std::_Exit(0);
}

} // namespace

int main(int argc, char** argv)
{
    // Every line out at once, also into a pipe (the designer shows the log of the runtime it started).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifndef _WIN32
    // A viewer or a designer that goes away while something is being sent to it must not end
    // the runtime (the write fails instead).
    std::signal(SIGPIPE, SIG_IGN);
#endif
    // hml_runtime --list-serial-ports: the serial ports of this machine, one per line:
    //   <device> TAB <stable name (Linux USB adapters) or empty> TAB <description>
    if(argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) { usage(argv[0]); return 0; }
    if(argc >= 2 && std::string(argv[1]) == "--list-serial-ports") {
        for(const hml::SerialPortInfo& p : hml::list_serial_ports())
            std::printf("%s\t%s\t%s\n", p.device.c_str(), p.stable.c_str(), p.description.c_str());
        return 0;
    }
    std::string project_path, screenshot, screen, export_log, export_level = "info";
    std::string data_dir;             // --data-dir: where this device keeps its keys (deploy without SSH); "" = <exe>/keys
    std::string export_kind, export_file, export_group;      // --export-data-log / -alarm-log / -operation-log
    uint32_t exit_after = 0, reload_after = 0, stats_s = 0;
    bool ignore_active = false;
    bool splash = false;
    bool check = false;
    int  capabilities = 0;            // --capabilities (1: for a person) / --capabilities-json (2)
    std::vector<Render> renders;
    std::string check_lang;
    uint32_t render_at_ms = 200;      // --at: how long a screen has been shown when --render takes its picture
    std::vector<std::pair<std::string, std::string>> check_sets;

    for(int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if(a == "--exit-after") { const char* v = next(); if(!v) return usage(argv[0]); exit_after = static_cast<uint32_t>(std::atoi(v)); }
        else if(a == "--ignore-active") ignore_active = true;
        else if(a == "--stats") { const char* v = next(); if(!v) return usage(argv[0]); stats_s = static_cast<uint32_t>(std::atoi(v)); }
        else if(a == "--reload-after") { const char* v = next(); if(!v) return usage(argv[0]); reload_after = static_cast<uint32_t>(std::atoi(v)); }
        else if(a == "--screenshot") { const char* v = next(); if(!v) return usage(argv[0]); screenshot = v; }
        else if(a == "--splash") splash = true;
        else if(a == "--check") check = true;
        else if(a == "--capabilities") capabilities = 1;
        else if(a == "--capabilities-json") capabilities = 2;
        else if(a == "--render") {
            const char* v = next();
            const std::string s = v ? v : "";
            const size_t comma = s.find(',');
            if(comma == std::string::npos || comma == 0 || comma + 1 >= s.size()) return usage(argv[0]);
            const std::string what = s.substr(0, comma);
            const size_t plus = what.find('+');
            renders.push_back({ what.substr(0, plus), plus == std::string::npos ? "" : what.substr(plus + 1), s.substr(comma + 1) });
            check = true;
        }
        else if(a == "--at") { const char* v = next(); if(!v) return usage(argv[0]); render_at_ms = static_cast<uint32_t>(std::clamp(std::atoi(v), 5, 60000)); }
        else if(a == "--lang") { const char* v = next(); if(!v) return usage(argv[0]); check_lang = v; }
        else if(a == "--set") {
            const char* v = next();
            const std::string s = v ? v : "";
            const size_t eq = s.find('=');
            if(eq == std::string::npos || eq == 0) return usage(argv[0]);
            check_sets.emplace_back(s.substr(0, eq), s.substr(eq + 1));
        }
        else if(a == "--export-system-log") { const char* v = next(); if(!v) return usage(argv[0]); export_log = v; }
        else if(a == "--export-data-log" || a == "--export-alarm-log" || a == "--export-operation-log") {
            const char* v = next();
            if(!v) return usage(argv[0]);
            export_kind = a.substr(9);
            export_file = v;
        }
        else if(a == "--group") { const char* v = next(); if(!v) return usage(argv[0]); export_group = v; }
        else if(a == "--log-level") { const char* v = next(); if(!v) return usage(argv[0]); export_level = v; }
        else if(a == "--screen") { const char* v = next(); if(!v) return usage(argv[0]); screen = v; }
        else if(a == "--data-dir") { const char* v = next(); if(!v) return usage(argv[0]); data_dir = v; }
        else if(a == "--click") {
            const char* v = next();
            Click c{};
            if(!v || std::sscanf(v, "%d,%d,%u,%u", &c.x, &c.y, &c.at_ms, &c.hold_ms) < 3) { std::fprintf(stderr, "bad --click (want x,y,ms or x,y,ms,hold_ms): %s\n", v ? v : ""); return 1; }
            s_clicks.push_back(c);
        }
        else if(a == "--shot-at") {
            const char* v = next();
            std::string s = v ? v : "";
            size_t comma = s.find(',');
            if(comma == std::string::npos) { std::fprintf(stderr, "bad --shot-at (want ms,file): %s\n", s.c_str()); return 1; }
            s_shots.push_back({ static_cast<uint32_t>(std::atoi(s.c_str())), s.substr(comma + 1) });
        }
        else if(!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return usage(argv[0]); }
        else project_path = a;
    }
    if(project_path.empty()) {
        for(const char* c : { "project.json", "projects/demo/project.json", "../projects/demo/project.json", "../../projects/demo/project.json" }) {
            if(file_exists(c)) { project_path = c; break; }
        }
    }
    if(project_path.empty()) return usage(argv[0]);
    // A project file (<name>.hmlproj) is unpacked into its working folder and run from there
    // (project_archive.hpp). Logs and this machine's settings live in that folder.
    std::string archive_note;
    if(hml::is_project_archive(project_path)) {
        const std::filesystem::path archive = std::filesystem::u8path(project_path), work = hml::archive_work_dir(archive);
        std::string err;
        if(!hml::unpack_project(archive, work, &err)) { std::fprintf(stderr, "hml_runtime: %s\n", err.c_str()); return 2; }
        archive_note = "project file '" + project_path + "' unpacked into " + work.generic_u8string();
        hml::transfer_set_archive(archive.generic_u8string());
        project_path = (work / "project.json").generic_u8string();
    }
    // What the project can do beyond drawing screens (capabilities.hpp): read from its files,
    // nothing is started. For a project somebody else made, before it is run.
    if(capabilities) {
        hml::ProjectCapabilities caps;
        std::string err;
        if(!hml::project_capabilities_of(project_path, caps, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
        std::fputs(capabilities == 2 ? (caps.to_json().dump(2) + "\n").c_str() : caps.text().c_str(), stdout);
        return 0;
    }
    if(check) {
        if(!screen.empty() || !screenshot.empty() || !s_clicks.empty() || !s_shots.empty())
            std::fprintf(stderr, "hml_runtime: --screen, --screenshot, --click and --shot-at do nothing with --check (use --render <screen>,<file>)\n");
        hml::drivers().register_builtins();
        return run_check(project_path, renders, check_lang, check_sets, render_at_ms);
    }
    // A deployed runtime runs the project named by active_project next to it, whatever the
    // service's command line says (active_project.hpp). Not for the exports: they are asked
    // about one project by name.
    const std::string home = hml::install_dir();
    std::string active_note;
    if(!ignore_active && export_log.empty() && export_kind.empty()) project_path = hml::resolve_active_project(home, project_path, &active_note);

    // Capture the log from the first line on; it is written once the database is open.
    if(!splash && export_log.empty() && export_kind.empty()) hml::system_log().attach();
    hml::drivers().register_builtins();
    if(!archive_note.empty()) HML_INFO("%s", archive_note.c_str());
    if(!active_note.empty()) HML_INFO("%s", active_note.c_str());
    if(!hml::project().load(project_path)) return 2;
    if(!splash && export_log.empty() && export_kind.empty()) hml::note_running_project(home, project_path);
    const hml::ProjectInfo& info = hml::project().info();

    if(!export_log.empty()) {
        hml::SystemLogQuery q;
        q.min_level = hml::log_level_from_name(export_level, hml::LogLevel::Info);
        if(!hml::system_log().open(info.system_log, true)) { std::fprintf(stderr, "no system log: \"system_log\" is not enabled in %s, or nothing was recorded yet\n", project_path.c_str()); return 5; }
        int64_t n = hml::system_log().export_csv(export_log, q);
        hml::system_log().close();
        if(n < 0) return 5;
        std::printf("%lld log rows exported to %s\n", static_cast<long long>(n), export_log.c_str());
        return 0;
    }
    if(!export_kind.empty()) {
        hml::project().start_logs(true);
        int64_t n = -1;
        if(export_kind == "data-log") {
            std::vector<std::string> groups = hml::data_log().groups();
            if(export_group.empty() && groups.size() == 1) export_group = groups[0];
            if(export_group.empty()) {
                std::fprintf(stderr, "--group <name> is needed; groups:");
                for(const std::string& g : groups) std::fprintf(stderr, " %s", g.c_str());
                std::fprintf(stderr, "\n");
            }
            else n = hml::data_log().export_csv(export_group, export_file);
        }
        else if(export_kind == "alarm-log") n = hml::alarms().export_csv(export_file);
        else n = hml::operation_log().export_csv(export_file);
        hml::project().stop_logs();
        if(n < 0) return 5;
        std::printf("%lld rows exported to %s\n", static_cast<long long>(n), export_file.c_str());
        return 0;
    }
    // (not for --splash: it runs as root before the HMI and must not create the database)
    if(!splash) hml::system_log().open(info.system_log);

    lv_init();
    hml::platform::Callbacks pcb{ [] { s_reload = true; }, [] { s_quit = true; },
                                  [] {
                                      HML_INFO("restart requested (SIGUSR2)");
                                      s_restart = static_cast<int>(hml::RestartTarget::App);
                                      s_quit = true;
                                  } };
    lv_indev_t* kb = nullptr;
    lv_display_t* disp = hml::platform::init(info, pcb, &kb);
    if(!disp) { HML_ERROR("cannot initialise the '%s' display backend", hml::platform::name()); return 3; }
    if(splash) return run_splash(disp, info, screenshot, exit_after);
    if(!s_clicks.empty()) {
        lv_indev_t* script = lv_indev_create();
        lv_indev_set_type(script, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(script, script_pointer_read);
    }

    hml::project().apply_theme(disp);
    hml::ui().init(kb, hml::AppCallbacks{ [] { s_reload = true; }, [] { s_quit = true; },
                                          [](hml::RestartTarget t) { s_restart = static_cast<int>(t); s_quit = true; } });
    hml::system_settings().register_actions();
    hml::register_user_actions();
    // "remote": the screen in a VNC viewer
    // (who may connect is the device's: "remote" of project.local.json, System > Remote)
    hml::vnc_server().start(hml::project().section("remote"), hml::project().local_remote(), disp, info.title, hml::project().local_overlay_path());
    // projects from the designer without SSH: only when it was switched on at this device
    // (System > Deploy). Its keys and settings are the device's: --data-dir, else <exe>/keys
    hml::deploy_service().open(hml::device_data_dir(data_dir));
    {   // the program file a runtime sent by a designer would replace (System > Deploy > Runtime)
        std::string exe = std::filesystem::u8path(argv[0]).filename().u8string();
#ifdef _WIN32
        if(exe.size() < 4 || exe.substr(exe.size() - 4) != ".exe") exe += ".exe";
#endif
        const std::string dir = hml::install_dir();
        if(!dir.empty()) hml::deploy_service().set_runtime_path(dir + "/" + exe);
    }
    hml::deploy_service().start();
    hml::system_access().start();                    // a finger held in the corner opens the system settings
    hml::scheduler().start();
    hml::sound().start();
    hml::mail().start();
    hml::uploads().start();
    hml::transfers().start();
    // pages that follow outside events; auto logout
    lv_timer_create([](lv_timer_t*) { hml::system_settings().poll(); hml::users().poll(); hml::recipes().poll(); hml::screen_saver().poll(); hml::sound().poll(); hml::mail().poll(); hml::uploads().poll(); hml::poll_clock_tags(); }, 250, nullptr);
    // { "action": "call", "fn": "system_log.export", "arg": "<file.csv>" } / Lua: call("system_log.export", path)
    hml::ui().register_action("system_log.export", [](const std::string& arg) {
        std::string path = arg.empty() ? hml::system_log().default_export_path() : arg;
        int64_t n = hml::system_log().export_csv(path);
        if(n >= 0) HML_INFO("system log: %lld rows exported to '%s'", static_cast<long long>(n), path.c_str());
    });

    hml::project().start_drivers();
    hml::project().start_logs();
    for(const auto& [name, value] : check_sets) {              // --set tag=value
        hml::Tag* t = hml::tags().find(name);
        auto v = t ? hml::Value::parse(t->type(), value) : std::nullopt;
        if(t && v) t->set(*v, nullptr);
        else HML_WARN("--set %s=%s: %s", name.c_str(), value.c_str(), t ? "not a value for this tag" : "unknown tag");
    }
    if(!check_lang.empty()) hml::texts().set_language(check_lang);
    hml::project().build_screens();
    if(!screen.empty()) hml::ui().show_screen(screen, "none");
    HML_INFO("running (%s backend). %s", hml::platform::name(), hml::platform::hint());

    const uint32_t t_start = hml::millis();
    s_t_start = t_start;
    bool shot_done = false;
    // --stats <seconds>: how busy the main loop is, logged every <seconds>. One pass of the loop
    // = tag notifications + LVGL (timers, input, drawing); while a pass runs nothing else is
    // handled, so the longest pass is the worst delay a touch or a tag change can see.
    using clock = std::chrono::steady_clock;
    struct LoopStats {
        uint32_t loops = 0, slow = 0;                 // slow: passes of 50 ms and more
        double   dispatch_us = 0, lvgl_us = 0, max_dispatch_us = 0, max_lvgl_us = 0, max_pass_us = 0;
    } stats;
    clock::time_point stats_from = clock::now();
    auto us_since = [](clock::time_point t) { return std::chrono::duration<double, std::micro>(clock::now() - t).count(); };
    while(!s_quit) {
        const clock::time_point t0 = clock::now();
        hml::platform::poll();
        hml::tags().dispatch(false);
        const clock::time_point t1 = clock::now();
        uint32_t wait = lv_timer_handler();
        uint32_t elapsed = hml::millis() - t_start;
        if(stats_s) {
            const double d = std::chrono::duration<double, std::micro>(t1 - t0).count(), l = us_since(t1);
            stats.loops++;
            stats.dispatch_us += d;
            stats.lvgl_us += l;
            if(d > stats.max_dispatch_us) stats.max_dispatch_us = d;
            if(l > stats.max_lvgl_us) stats.max_lvgl_us = l;
            if(d + l > stats.max_pass_us) stats.max_pass_us = d + l;
            if(d + l >= 50000) stats.slow++;
            const double span = us_since(stats_from) / 1e6;
            if(span >= stats_s) {
                HML_INFO("stats: %.1f s, %u passes (%.0f/s), busy %.0f%%, longest pass %.1f ms, %u of 50 ms or more; "
                         "tags avg %.2f max %.1f ms, lvgl avg %.2f max %.1f ms",
                         span, stats.loops, stats.loops / span, (stats.dispatch_us + stats.lvgl_us) / (span * 1e4), stats.max_pass_us / 1000, stats.slow,
                         stats.dispatch_us / stats.loops / 1000, stats.max_dispatch_us / 1000, stats.lvgl_us / stats.loops / 1000, stats.max_lvgl_us / 1000);
                stats = LoopStats{};
                stats_from = clock::now();
            }
        }

        if(!screenshot.empty() && !shot_done && elapsed >= (exit_after ? exit_after / 2 : 1000)) {
            lv_refr_now(disp);
            hml::platform::screenshot(disp, screenshot);
            shot_done = true;
        }
        for(Shot& s : s_shots) {
            if(!s.done && elapsed >= s.at_ms) {
                s.done = true;
                lv_refr_now(disp);
                hml::platform::screenshot(disp, s.path);
            }
        }
        if(exit_after && elapsed >= exit_after) s_quit = true;
        if(reload_after && elapsed >= reload_after) { reload_after = 0; s_reload = true; }
        if(s_reload) {
            s_reload = false;
            HML_INFO("reloading screens...");
            hml::project().build_screens();
        }
        if(wait > 10) wait = 10;
        if(wait < 1) wait = 1;
        hml::sleep_ms(wait);
    }

    HML_INFO("shutting down");
    hml::vnc_server().stop();
    hml::deploy_service().stop();
    hml::transfers().clear();
    hml::drivers().stop_all();
    hml::scripts().clear();
    hml::project().stop_logs();
    hml::ui().unload_all();
    hml::project().unload();
    lv_deinit();
    hml::tags().clear();
    hml::platform::deinit();
    hml::system_log().close();
    if(s_restart >= 0) {
        if(!hml::platform::restart(static_cast<hml::RestartTarget>(s_restart), argc, argv)) return 4;
    }
    return 0;
}
