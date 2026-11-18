// HML runtime - declarative LVGL HMI runtime.
// The backend (SDL window / Linux fbdev / Linux DRM) is selected at build time, see platform.hpp.
//
//   hml_runtime [project.json] [--screen <name>] [--exit-after <ms>] [--screenshot <file.bmp>]
//               [--reload-after <ms>] [--click <x>,<y>,<ms>]... [--shot-at <ms>,<file.bmp>]...
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
//
// SDL: F5 = reload screens, Esc = quit.  Linux fbdev/drm: SIGHUP/SIGUSR1 = reload, SIGUSR2 = restart,
// SIGINT/SIGTERM = quit.
#include <cstdio>
#include <cstdlib>
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
#include "tag.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"

namespace {

volatile bool s_quit = false;
volatile bool s_reload = false;
volatile int  s_restart = -1;      // -1 none, otherwise a hml::RestartTarget

// ---- scripted pointer clicks (--click) ----
struct Click { int32_t x, y; uint32_t at_ms; };
struct Shot { uint32_t at_ms; std::string path; bool done = false; };
std::vector<Click> s_clicks;
std::vector<Shot>  s_shots;
uint32_t s_t_start = 0;

void script_pointer_read(lv_indev_t*, lv_indev_data_t* data)
{
    uint32_t now = hml::millis() - s_t_start;
    data->state = LV_INDEV_STATE_RELEASED;
    for(const Click& c : s_clicks) {
        if(now >= c.at_ms && now < c.at_ms + 80) {
            data->point.x = c.x;
            data->point.y = c.y;
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }
}

bool file_exists(const std::string& p)
{
    return std::ifstream(p, std::ios::binary).good();
}

int usage(const char* argv0)
{
    std::fprintf(stderr, "usage: %s <project.json> [--screen <name>] [--exit-after <ms>] [--screenshot <file.bmp>]\n"
                         "       [--reload-after <ms>] [--click <x>,<y>,<ms>]... [--shot-at <ms>,<file.bmp>]...\n"
                         "       %s --splash <project.json> [--screenshot <file.bmp>] [--exit-after <ms>]\n"
                         "       %s --export-system-log <file.csv> <project.json> [--log-level info|warn|error]\n"
                         "       %s --export-data-log <file.csv> <project.json> [--group <name>]\n"
                         "       %s --export-alarm-log | --export-operation-log <file.csv> <project.json>\n", argv0, argv0, argv0, argv0, argv0);
    return 1;
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
    std::string project_path, screenshot, screen, export_log, export_level = "info";
    std::string export_kind, export_file, export_group;      // --export-data-log / -alarm-log / -operation-log
    uint32_t exit_after = 0, reload_after = 0;
    bool splash = false;

    for(int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if(a == "--exit-after") { const char* v = next(); if(!v) return usage(argv[0]); exit_after = static_cast<uint32_t>(std::atoi(v)); }
        else if(a == "--reload-after") { const char* v = next(); if(!v) return usage(argv[0]); reload_after = static_cast<uint32_t>(std::atoi(v)); }
        else if(a == "--screenshot") { const char* v = next(); if(!v) return usage(argv[0]); screenshot = v; }
        else if(a == "--splash") splash = true;
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
        else if(a == "--click") {
            const char* v = next();
            Click c{};
            if(!v || std::sscanf(v, "%d,%d,%u", &c.x, &c.y, &c.at_ms) != 3) { std::fprintf(stderr, "bad --click (want x,y,ms): %s\n", v ? v : ""); return 1; }
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

    // Capture the log from the first line on; it is written once the database is open.
    if(!splash && export_log.empty() && export_kind.empty()) hml::system_log().attach();
    hml::drivers().register_builtins();
    if(!hml::project().load(project_path)) return 2;
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
    // pages that follow outside events; auto logout
    lv_timer_create([](lv_timer_t*) { hml::system_settings().poll(); hml::users().poll(); }, 250, nullptr);
    // { "action": "call", "fn": "system_log.export", "arg": "<file.csv>" } / Lua: call("system_log.export", path)
    hml::ui().register_action("system_log.export", [](const std::string& arg) {
        std::string path = arg.empty() ? hml::system_log().default_export_path() : arg;
        int64_t n = hml::system_log().export_csv(path);
        if(n >= 0) HML_INFO("system log: %lld rows exported to '%s'", static_cast<long long>(n), path.c_str());
    });

    hml::project().start_drivers();
    hml::project().start_logs();
    hml::project().build_screens();
    if(!screen.empty()) hml::ui().show_screen(screen, "none");
    HML_INFO("running (%s backend). %s", hml::platform::name(), hml::platform::hint());

    const uint32_t t_start = hml::millis();
    s_t_start = t_start;
    bool shot_done = false;
    while(!s_quit) {
        hml::platform::poll();
        hml::tags().dispatch(false);
        uint32_t wait = lv_timer_handler();
        uint32_t elapsed = hml::millis() - t_start;

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
