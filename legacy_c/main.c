/* HML runtime - declarative LVGL HMI runtime.
 * Backend (SDL window / Linux fbdev / Linux DRM) is selected at build time, see hml_platform.h.
 *
 *   hml_runtime [project.json] [--screen <name>] [--exit-after <ms>] [--screenshot <file.bmp>] [--reload-after <ms>]
 *               [--click <x>,<y>,<ms>]... [--shot-at <ms>,<file.bmp>]...
 *
 * --click injects a pointer click at (x,y) <ms> after start (UI test automation);
 * --shot-at saves a screenshot at the given time. Both may be repeated.
 *
 * SDL: F5 = reload screens, Esc = quit.  Linux fbdev/drm: SIGHUP/SIGUSR1 = reload, SIGINT/SIGTERM = quit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "hml_os.h"
#include "hml_tag.h"
#include "hml_driver.h"
#include "hml_ui.h"
#include "hml_project.h"
#include "hml_platform.h"

static volatile bool s_quit;
static volatile bool s_reload;

/* ---- scripted pointer clicks (--click) ---- */
#define MAX_CLICKS 32
typedef struct { int32_t x, y; uint32_t at_ms; } click_t;
static click_t  s_clicks[MAX_CLICKS];
static int      s_click_count;
static uint32_t s_t_start;

static void script_pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint32_t now = hml_millis() - s_t_start;
    data->state = LV_INDEV_STATE_RELEASED;
    for(int i = 0; i < s_click_count; i++) {
        if(now >= s_clicks[i].at_ms && now < s_clicks[i].at_ms + 80) {
            data->point.x = s_clicks[i].x;
            data->point.y = s_clicks[i].y;
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }
}

#define MAX_SHOTS 16
typedef struct { uint32_t at_ms; const char *path; bool done; } shot_t;
static shot_t s_shots[MAX_SHOTS];
static int    s_shot_count;

static void request_reload(void) { s_reload = true; }
static void request_exit(void)   { s_quit = true; }

static bool file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if(!f) return false;
    fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    const char *project = NULL;
    const char *screenshot = NULL;
    const char *screen = NULL;
    uint32_t exit_after = 0, reload_after = 0;
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--exit-after") == 0 && i + 1 < argc) exit_after = (uint32_t)atoi(argv[++i]);
        else if(strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else if(strcmp(argv[i], "--screen") == 0 && i + 1 < argc) screen = argv[++i];
        else if(strcmp(argv[i], "--reload-after") == 0 && i + 1 < argc) reload_after = (uint32_t)atoi(argv[++i]);
        else if(strcmp(argv[i], "--click") == 0 && i + 1 < argc && s_click_count < MAX_CLICKS) {
            click_t *c = &s_clicks[s_click_count];
            if(sscanf(argv[++i], "%d,%d,%u", &c->x, &c->y, &c->at_ms) == 3) s_click_count++;
            else { fprintf(stderr, "bad --click (want x,y,ms): %s\n", argv[i]); return 1; }
        }
        else if(strcmp(argv[i], "--shot-at") == 0 && i + 1 < argc && s_shot_count < MAX_SHOTS) {
            char *arg = argv[++i];
            char *comma = strchr(arg, ',');
            if(!comma) { fprintf(stderr, "bad --shot-at (want ms,file): %s\n", arg); return 1; }
            *comma = '\0';
            s_shots[s_shot_count].at_ms = (uint32_t)atoi(arg);
            s_shots[s_shot_count].path = comma + 1;
            s_shot_count++;
        }
        else if(argv[i][0] == '-') { fprintf(stderr, "unknown option %s\n", argv[i]); return 1; }
        else project = argv[i];
    }
    if(!project) {
        static const char *candidates[] = {
            "project.json", "projects/demo/project.json", "../projects/demo/project.json", "../../projects/demo/project.json"
        };
        for(size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            if(file_exists(candidates[i])) { project = candidates[i]; break; }
        }
    }
    if(!project) {
        fprintf(stderr, "usage: %s <project.json>\n", argv[0]);
        return 1;
    }

    hml_tags_init();
    hml_drivers_register_builtins();
    if(hml_project_load(project) != 0) return 2;
    const hml_project_info_t *info = hml_project_info();

    lv_init();
    hml_platform_cb_t pcb = { request_reload, request_exit };
    lv_indev_t *kb = NULL;
    lv_display_t *disp = hml_platform_init(info, &pcb, &kb);
    if(!disp) { HML_LOG_ERROR("cannot initialise the '%s' display backend", hml_platform_name()); return 3; }
    if(s_click_count) {
        lv_indev_t *script = lv_indev_create();
        lv_indev_set_type(script, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(script, script_pointer_read);
    }

    hml_project_apply_theme(disp);
    hml_ui_app_cb_t cb = { request_reload, request_exit };
    hml_ui_init(kb, &cb);

    hml_project_start_drivers();
    hml_project_build_screens();
    if(screen) hml_ui_show_screen(screen, "none");
    HML_LOG_INFO("running (%s backend). %s", hml_platform_name(), hml_platform_hint());

    uint32_t t_start = hml_millis();
    s_t_start = t_start;
    bool shot_done = false;
    while(!s_quit) {
        hml_platform_poll();
        hml_tags_dispatch(false);
        uint32_t wait = lv_timer_handler();
        uint32_t elapsed = hml_millis() - t_start;
        if(screenshot && !shot_done && elapsed >= (exit_after ? exit_after / 2 : 1000)) {
            lv_refr_now(disp);
            hml_platform_screenshot(disp, screenshot);
            shot_done = true;
        }
        for(int i = 0; i < s_shot_count; i++) {
            if(!s_shots[i].done && elapsed >= s_shots[i].at_ms) {
                s_shots[i].done = true;
                lv_refr_now(disp);
                hml_platform_screenshot(disp, s_shots[i].path);
            }
        }
        if(exit_after && elapsed >= exit_after) s_quit = true;
        if(reload_after && elapsed >= reload_after) { reload_after = 0; s_reload = true; }
        if(s_reload) {
            s_reload = false;
            HML_LOG_INFO("reloading screens...");
            hml_project_build_screens();
        }
        if(wait > 10) wait = 10;
        if(wait < 1) wait = 1;
        hml_sleep_ms(wait);
    }

    HML_LOG_INFO("shutting down");
    hml_drivers_stop_all();
    hml_ui_unload_all();
    hml_project_unload();
    lv_deinit();
    hml_tags_deinit();
    hml_platform_deinit();
    return 0;
}
