/* Project file: tags, drivers, display settings and the list of screens. */
#ifndef HML_PROJECT_H
#define HML_PROJECT_H

#include <stdbool.h>
#include "lvgl.h"

typedef struct {
    char   path[512];
    char   dir[512];
    char   title[128];
    int    width, height;
    double zoom;
    int    rotation;          /* 0, 90, 180, 270 (degrees) */
    /* Linux backends (fbdev / drm): device paths, empty = default */
    char   fbdev[64];         /* "/dev/fb0" */
    char   drm[64];           /* "/dev/dri/card0" */
    char   input[64];         /* "/dev/input/eventN"; empty = auto-discover all */
    bool   touch_swap_axes;
    bool   touch_has_calib;
    int    touch_calib[4];    /* min_x, min_y, max_x, max_y */
    bool   dark;
    lv_color_t primary, secondary;
    char   start_screen[64];
} hml_project_info_t;

/* Parse project.json, create tags. LVGL does not need to be initialised yet. */
int  hml_project_load(const char *path);
const hml_project_info_t *hml_project_info(void);

/* After LVGL + display exist: apply theme, start drivers. */
void hml_project_apply_theme(lv_display_t *disp);
int  hml_project_start_drivers(void);

/* Build (or rebuild) all screens from their JSON files and show the start screen
 * (or the previously shown one on reload). */
int  hml_project_build_screens(void);

void hml_project_unload(void);

#endif
