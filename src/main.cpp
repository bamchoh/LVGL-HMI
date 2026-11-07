// HML runtime - declarative LVGL HMI runtime.
// The backend (SDL window / Linux fbdev / Linux DRM) is selected at build time, see platform.hpp.
//
//   hml_runtime [project.json] [--screen <name>] [--exit-after <ms>] [--screenshot <file.bmp>]
//               [--reload-after <ms>] [--click <x>,<y>,<ms>]... [--shot-at <ms>,<file.bmp>]...
//
// --click injects a pointer click at (x,y) <ms> after start (UI test automation);
// --shot-at saves a screenshot at the given time. Both may be repeated.
//
// SDL: F5 = reload screens, Esc = quit.  Linux fbdev/drm: SIGHUP/SIGUSR1 = reload, SIGINT/SIGTERM = quit.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "lvgl.h"
#include "driver.hpp"
#include "platform.hpp"
#include "project.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace {

volatile bool s_quit = false;
volatile bool s_reload = false;

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
                         "       [--reload-after <ms>] [--click <x>,<y>,<ms>]... [--shot-at <ms>,<file.bmp>]...\n", argv0);
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    std::string project_path, screenshot, screen;
    uint32_t exit_after = 0, reload_after = 0;

    for(int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if(a == "--exit-after") { const char* v = next(); if(!v) return usage(argv[0]); exit_after = static_cast<uint32_t>(std::atoi(v)); }
        else if(a == "--reload-after") { const char* v = next(); if(!v) return usage(argv[0]); reload_after = static_cast<uint32_t>(std::atoi(v)); }
        else if(a == "--screenshot") { const char* v = next(); if(!v) return usage(argv[0]); screenshot = v; }
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

    hml::drivers().register_builtins();
    if(!hml::project().load(project_path)) return 2;
    const hml::ProjectInfo& info = hml::project().info();

    lv_init();
    hml::platform::Callbacks pcb{ [] { s_reload = true; }, [] { s_quit = true; } };
    lv_indev_t* kb = nullptr;
    lv_display_t* disp = hml::platform::init(info, pcb, &kb);
    if(!disp) { HML_ERROR("cannot initialise the '%s' display backend", hml::platform::name()); return 3; }
    if(!s_clicks.empty()) {
        lv_indev_t* script = lv_indev_create();
        lv_indev_set_type(script, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(script, script_pointer_read);
    }

    hml::project().apply_theme(disp);
    hml::ui().init(kb, hml::AppCallbacks{ [] { s_reload = true; }, [] { s_quit = true; } });

    hml::project().start_drivers();
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
    hml::ui().unload_all();
    hml::project().unload();
    lv_deinit();
    hml::tags().clear();
    hml::platform::deinit();
    return 0;
}
