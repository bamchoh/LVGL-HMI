/* Linux framebuffer / DRM backend with evdev input (Raspberry Pi and other embedded Linux).
 *
 * Display : /dev/fb0 (fbdev) or /dev/dri/card0 (drm); overridable with
 *           "display": { "fbdev": ..., "drm": ... } in project.json or HML_FBDEV / HML_DRM env.
 * Input   : "display": { "input": "/dev/input/event0" } or HML_INPUT env; when empty every
 *           /dev/input/event* device is auto-discovered (touch, mouse, keyboard).
 * Control : SIGHUP / SIGUSR1 = reload screens, SIGINT / SIGTERM = quit.
 */
#if defined(HML_BACKEND_FBDEV) || defined(HML_BACKEND_DRM)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "hml_platform.h"
#include "hml_os.h"

static hml_platform_cb_t s_cb;
static volatile sig_atomic_t s_sig_reload;
static volatile sig_atomic_t s_sig_quit;
static const hml_project_info_t *s_info;

static void on_signal(int sig)
{
    if(sig == SIGINT || sig == SIGTERM) s_sig_quit = 1;
    else s_sig_reload = 1;
}

static void apply_touch_settings(lv_indev_t *indev)
{
    if(!indev || !s_info) return;
    if(s_info->touch_swap_axes) lv_evdev_set_swap_axes(indev, true);
    if(s_info->touch_has_calib) {
        lv_evdev_set_calibration(indev, s_info->touch_calib[0], s_info->touch_calib[1],
                                 s_info->touch_calib[2], s_info->touch_calib[3]);
    }
}

static void on_evdev_discovered(lv_indev_t *indev, lv_evdev_type_t type, void *user_data)
{
    (void)user_data;
    const char *kind = "pointer";
    if(type == LV_EVDEV_TYPE_KEY) {
        kind = "keyboard";
        lv_group_t *g = lv_group_get_default();
        if(g) lv_indev_set_group(indev, g);
    }
    else if(type == LV_EVDEV_TYPE_ABS) {
        kind = "touch";
        apply_touch_settings(indev);
    }
    HML_LOG_INFO("evdev: %s device added", kind);
}

static bool device_usable(const char *path)
{
    int fd = open(path, O_RDWR);
    if(fd < 0) {
        HML_LOG_ERROR("cannot open %s (%s). Run as root or add the user to the 'video'/'input' groups.",
                      path, strerror(errno));
        return false;
    }
    close(fd);
    return true;
}

lv_display_t *hml_platform_init(const hml_project_info_t *info, const hml_platform_cb_t *cb, lv_indev_t **kb_out)
{
    if(cb) s_cb = *cb;
    s_info = info;
    if(kb_out) *kb_out = NULL;

    lv_display_t *disp = NULL;
#if defined(HML_BACKEND_FBDEV)
    const char *dev = getenv("HML_FBDEV");
    if(!dev || !*dev) dev = info->fbdev[0] ? info->fbdev : "/dev/fb0";
    if(!device_usable(dev)) return NULL;
    disp = lv_linux_fbdev_create();
    if(!disp) return NULL;
    lv_linux_fbdev_set_file(disp, dev);
#else
    const char *dev = getenv("HML_DRM");
    if(!dev || !*dev) dev = info->drm[0] ? info->drm : "/dev/dri/card0";
    if(!device_usable(dev)) return NULL;
    disp = lv_linux_drm_create();
    if(!disp) return NULL;
    lv_linux_drm_set_file(disp, dev, -1);
#endif
    HML_LOG_INFO("%s display %s: %dx%d (project says %dx%d)", hml_platform_name(), dev,
                 (int)lv_display_get_horizontal_resolution(disp), (int)lv_display_get_vertical_resolution(disp),
                 info->width, info->height);
    if(info->rotation) lv_display_set_rotation(disp, (lv_display_rotation_t)(info->rotation / 90));

    /* Input: explicit device or auto-discovery */
    const char *in = getenv("HML_INPUT");
    if(!in || !*in) in = info->input;
    if(in && *in) {
        lv_indev_t *ptr = lv_evdev_create(LV_INDEV_TYPE_POINTER, in);
        if(!ptr) HML_LOG_WARN("cannot open input device %s", in);
        else { apply_touch_settings(ptr); HML_LOG_INFO("input: %s", in); }
    }
    else {
        if(lv_evdev_discovery_start(on_evdev_discovered, NULL) != LV_RESULT_OK)
            HML_LOG_WARN("evdev auto-discovery failed; set \"display\": { \"input\": \"/dev/input/eventN\" }");
        else HML_LOG_INFO("input: evdev auto-discovery (/dev/input/event*)");
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGUSR1, on_signal);
    return disp;
}

void hml_platform_poll(void)
{
    if(s_sig_quit) { s_sig_quit = 0; if(s_cb.quit) s_cb.quit(); }
    if(s_sig_reload) { s_sig_reload = 0; if(s_cb.reload) s_cb.reload(); }
}

/* Snapshot of the active screen written as a 24-bit BMP. */
void hml_platform_screenshot(lv_display_t *disp, const char *path)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_draw_buf_t *buf = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if(!buf) { HML_LOG_WARN("screenshot: snapshot failed"); return; }
    uint32_t w = buf->header.w, h = buf->header.h;
    uint32_t row = (w * 3 + 3) & ~3u;
    uint32_t size = 54 + row * h;
    FILE *f = fopen(path, "wb");
    if(!f) { HML_LOG_WARN("screenshot: cannot write %s", path); lv_draw_buf_destroy(buf); return; }
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t v;
    v = size;  memcpy(hdr + 2, &v, 4);
    v = 54;    memcpy(hdr + 10, &v, 4);
    v = 40;    memcpy(hdr + 14, &v, 4);
    v = w;     memcpy(hdr + 18, &v, 4);
    v = h;     memcpy(hdr + 22, &v, 4);
    hdr[26] = 1; hdr[28] = 24;
    v = row * h; memcpy(hdr + 34, &v, 4);
    fwrite(hdr, 1, 54, f);
    uint8_t *line = calloc(row, 1);
    for(uint32_t y = h; y-- > 0;) {
        const uint8_t *src = (const uint8_t *)buf->data + y * buf->header.stride;
        for(uint32_t x = 0; x < w; x++) { line[x * 3] = src[x * 4]; line[x * 3 + 1] = src[x * 4 + 1]; line[x * 3 + 2] = src[x * 4 + 2]; }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    lv_draw_buf_destroy(buf);
    HML_LOG_INFO("screenshot saved to %s", path);
}

void hml_platform_deinit(void)
{
    lv_evdev_discovery_stop();
}

const char *hml_platform_name(void)
{
#if defined(HML_BACKEND_FBDEV)
    return "fbdev";
#else
    return "drm";
#endif
}

const char *hml_platform_hint(void) { return "SIGHUP/SIGUSR1 = reload screens, SIGINT/SIGTERM (Ctrl-C) = quit"; }

#endif /* HML_BACKEND_FBDEV || HML_BACKEND_DRM */
