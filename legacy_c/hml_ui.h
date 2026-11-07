/* UI loader: builds LVGL screens from JSON and wires widgets to the tag store.
 * See README.md for the screen file format. */
#ifndef HML_UI_H
#define HML_UI_H

#include <stdbool.h>
#include "lvgl.h"
#include "cJSON.h"

typedef struct {
    void (*request_reload)(void);
    void (*request_exit)(void);
} hml_ui_app_cb_t;

void hml_ui_init(lv_indev_t *keyboard_indev, const hml_ui_app_cb_t *cb);

/* Build a screen from its JSON description. base_dir resolves relative asset paths. */
lv_obj_t *hml_ui_build_screen(const cJSON *screen_json, const char *base_dir);

/* Delete every screen built so far, free bindings, drop UI tag subscriptions. */
void hml_ui_unload_all(void);

bool        hml_ui_show_screen(const char *name, const char *anim);
bool        hml_ui_back(void);
const char *hml_ui_current_screen(void);
int         hml_ui_screen_count(void);

/* Helpers reused by the project loader */
lv_color_t  hml_ui_parse_color(const char *s, lv_color_t dflt);
const lv_font_t *hml_ui_font(const cJSON *v);

#endif
