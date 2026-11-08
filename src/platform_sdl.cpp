// SDL2 backend (PC simulator).
#if defined(HML_BACKEND_SDL)

#include <SDL2/SDL.h>
#include "platform.hpp"
#include "screenshot.hpp"
#include "util.hpp"
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace hml::platform {
namespace {

Callbacks s_cb;

// Runs when SDL queues an event, before LVGL's driver polls it. Returning 0 drops the event.
int SDLCALL event_filter(void*, SDL_Event* ev)
{
    auto quit = [] { if(s_cb.quit) s_cb.quit(); return 0; };
    if(ev->type == SDL_QUIT) return quit();
    if(ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_CLOSE) return quit();
    if(ev->type == SDL_KEYDOWN) {
        if(ev->key.keysym.sym == SDLK_F5) { if(s_cb.reload) s_cb.reload(); return 0; }
        if(ev->key.keysym.sym == SDLK_ESCAPE) return quit();
    }
    return 1;
}

} // namespace

lv_display_t* init(const ProjectInfo& info, Callbacks cb, lv_indev_t** kb_out)
{
    s_cb = std::move(cb);
    lv_display_t* disp = lv_sdl_window_create(info.width, info.height);
    if(!disp) { HML_ERROR("cannot create SDL window"); return nullptr; }
    lv_sdl_window_set_title(disp, info.title.c_str());
    lv_display_set_dpi(disp, info.dpi);
    if(info.zoom > 0 && info.zoom != 1.0) lv_sdl_window_set_zoom(disp, static_cast<float>(info.zoom));
    if(info.rotation) lv_display_set_rotation(disp, static_cast<lv_display_rotation_t>(info.rotation / 90));
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_indev_t* kb = lv_sdl_keyboard_create();
    if(kb_out) *kb_out = kb;
    SDL_SetEventFilter(event_filter, nullptr);
    return disp;
}

void poll() {}

// Rendered with lv_snapshot rather than read back from the SDL renderer, so that it also works
// with SDL_VIDEODRIVER=dummy (headless previews for the designer).
void screenshot(lv_display_t* disp, const std::string& path) { save_screenshot_bmp(disp, path); }

void deinit() { SDL_Quit(); }

// PC: a reboot is never what the developer wants, so both targets restart the application.
bool restart(RestartTarget what, int argc, char** argv)
{
    if(what == RestartTarget::System) HML_WARN("restart: system reboot is not supported on the SDL backend, restarting the app");
    (void)argc;
#ifdef _WIN32
    (void)argv;
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string cmd = GetCommandLineA();                     // same executable, same arguments
    if(!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        HML_ERROR("restart: CreateProcess failed (%lu)", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    HML_INFO("restart: new instance started");
    return true;                                             // caller exits
#else
    execv("/proc/self/exe", argv);
    execv(argv[0], argv);
    HML_ERROR("restart: execv failed: %s", std::strerror(errno));
    return false;
#endif
}

const char* name() { return "sdl"; }
const char* hint() { return "F5 = reload screens, Esc = quit"; }

} // namespace hml::platform

#endif // HML_BACKEND_SDL
