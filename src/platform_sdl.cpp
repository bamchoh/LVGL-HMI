// SDL2 backend (PC simulator).
#if defined(HML_BACKEND_SDL)

#include <SDL2/SDL.h>
#include "platform.hpp"
#include "util.hpp"

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

void screenshot(lv_display_t* disp, const std::string& path)
{
    SDL_Renderer* r = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(disp));
    if(!r) { HML_WARN("screenshot: no renderer"); return; }
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if(!s) return;
    if(SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 && SDL_SaveBMP(s, path.c_str()) == 0) {
        HML_INFO("screenshot saved to %s", path.c_str());
    }
    else HML_WARN("screenshot: %s", SDL_GetError());
    SDL_FreeSurface(s);
}

void deinit() { SDL_Quit(); }

const char* name() { return "sdl"; }
const char* hint() { return "F5 = reload screens, Esc = quit"; }

} // namespace hml::platform

#endif // HML_BACKEND_SDL
