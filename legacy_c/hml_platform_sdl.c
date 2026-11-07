/* SDL2 backend (PC simulator). */
#if defined(HML_BACKEND_SDL)

#include <SDL2/SDL.h>
#include "hml_platform.h"
#include "hml_os.h"

static hml_platform_cb_t s_cb;

/* Runs when SDL queues an event, before LVGL's driver polls it. Returning 0 drops the event. */
static int SDLCALL event_filter(void *userdata, SDL_Event *ev)
{
    (void)userdata;
    if(ev->type == SDL_QUIT) { if(s_cb.quit) s_cb.quit(); return 0; }
    if(ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_CLOSE) { if(s_cb.quit) s_cb.quit(); return 0; }
    if(ev->type == SDL_KEYDOWN) {
        if(ev->key.keysym.sym == SDLK_F5) { if(s_cb.reload) s_cb.reload(); return 0; }
        if(ev->key.keysym.sym == SDLK_ESCAPE) { if(s_cb.quit) s_cb.quit(); return 0; }
    }
    return 1;
}

lv_display_t *hml_platform_init(const hml_project_info_t *info, const hml_platform_cb_t *cb, lv_indev_t **kb_out)
{
    if(cb) s_cb = *cb;
    lv_display_t *disp = lv_sdl_window_create(info->width, info->height);
    if(!disp) { HML_LOG_ERROR("cannot create SDL window"); return NULL; }
    lv_sdl_window_set_title(disp, info->title);
    if(info->zoom > 0 && info->zoom != 1.0) lv_sdl_window_set_zoom(disp, (float)info->zoom);
    if(info->rotation) lv_display_set_rotation(disp, (lv_display_rotation_t)(info->rotation / 90));
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_indev_t *kb = lv_sdl_keyboard_create();
    if(kb_out) *kb_out = kb;
    SDL_SetEventFilter(event_filter, NULL);
    return disp;
}

void hml_platform_poll(void) {}

void hml_platform_screenshot(lv_display_t *disp, const char *path)
{
    SDL_Renderer *r = lv_sdl_window_get_renderer(disp);
    if(!r) { HML_LOG_WARN("screenshot: no renderer"); return; }
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if(!s) return;
    if(SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
        if(SDL_SaveBMP(s, path) == 0) HML_LOG_INFO("screenshot saved to %s", path);
        else HML_LOG_WARN("screenshot: %s", SDL_GetError());
    }
    else HML_LOG_WARN("screenshot: %s", SDL_GetError());
    SDL_FreeSurface(s);
}

void hml_platform_deinit(void) { SDL_Quit(); }

const char *hml_platform_name(void) { return "sdl"; }
const char *hml_platform_hint(void) { return "F5 = reload screens, Esc = quit"; }

#endif /* HML_BACKEND_SDL */
