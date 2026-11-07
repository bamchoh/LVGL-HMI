/* Display / input backend abstraction.
 *
 * Exactly one backend is compiled in, selected by CMake (-DHML_BACKEND=...):
 *   sdl    - SDL2 window (Windows / Linux desktop / macOS), F5 = reload, Esc = quit
 *   fbdev  - Linux framebuffer /dev/fbN + evdev input (Raspberry Pi console)
 *   drm    - Linux DRM/KMS /dev/dri/cardN + evdev input (Raspberry Pi, needs libdrm)
 */
#ifndef HML_PLATFORM_H
#define HML_PLATFORM_H

#include "lvgl.h"
#include "hml_project.h"

typedef struct {
    void (*reload)(void);
    void (*quit)(void);
} hml_platform_cb_t;

/* Create the display and the input devices. *kb_out receives the keyboard
 * indev when the backend has one at init time (may be NULL). Returns NULL on failure. */
lv_display_t *hml_platform_init(const hml_project_info_t *info, const hml_platform_cb_t *cb, lv_indev_t **kb_out);

/* Called once per main-loop iteration (signal polling etc.). */
void hml_platform_poll(void);

/* Save the current frame as a BMP file (test automation). */
void hml_platform_screenshot(lv_display_t *disp, const char *path);

void hml_platform_deinit(void);

const char *hml_platform_name(void);
/* Human readable hint on how to reload / quit on this backend. */
const char *hml_platform_hint(void);

#endif
