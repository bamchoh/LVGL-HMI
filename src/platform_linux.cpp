// Linux framebuffer / DRM backend with evdev input (Raspberry Pi and other embedded Linux).
//
// Display : /dev/fb0 (fbdev) or /dev/dri/card0 (drm); overridable with
//           "display": { "fbdev": ..., "drm": ... } in project.json or HML_FBDEV / HML_DRM env.
// Input   : "display": { "input": "/dev/input/event0" } or HML_INPUT env; when empty every
//           /dev/input/event* device is auto-discovered (touch, mouse, keyboard).
// Control : SIGHUP / SIGUSR1 = reload screens, SIGINT / SIGTERM = quit.
#if defined(HML_BACKEND_FBDEV) || defined(HML_BACKEND_DRM)

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include "platform.hpp"
#include "util.hpp"

namespace hml::platform {
namespace {

Callbacks s_cb;
volatile sig_atomic_t s_sig_reload = 0;
volatile sig_atomic_t s_sig_quit = 0;
const ProjectInfo* s_info = nullptr;

void on_signal(int sig)
{
    if(sig == SIGINT || sig == SIGTERM) s_sig_quit = 1;
    else s_sig_reload = 1;
}

void apply_touch_settings(lv_indev_t* indev)
{
    if(!indev || !s_info) return;
    if(s_info->touch_swap_axes) lv_evdev_set_swap_axes(indev, true);
    if(s_info->touch_has_calib) {
        lv_evdev_set_calibration(indev, s_info->touch_calib[0], s_info->touch_calib[1], s_info->touch_calib[2], s_info->touch_calib[3]);
    }
}

void on_evdev_discovered(lv_indev_t* indev, lv_evdev_type_t type, void*)
{
    const char* kind = "pointer";
    if(type == LV_EVDEV_TYPE_KEY) {
        kind = "keyboard";
        if(lv_group_t* g = lv_group_get_default()) lv_indev_set_group(indev, g);
    }
    else if(type == LV_EVDEV_TYPE_ABS) {
        kind = "touch";
        apply_touch_settings(indev);
    }
    HML_INFO("evdev: %s device added", kind);
}

bool device_usable(const std::string& path)
{
    int fd = open(path.c_str(), O_RDWR);
    if(fd < 0) {
        HML_ERROR("cannot open %s (%s). Run as root or add the user to the 'video'/'input' groups.", path.c_str(), std::strerror(errno));
        return false;
    }
    close(fd);
    return true;
}

std::string env_or(const char* env, const std::string& value, const std::string& dflt)
{
    const char* e = std::getenv(env);
    if(e && *e) return e;
    return value.empty() ? dflt : value;
}

} // namespace

lv_display_t* init(const ProjectInfo& info, Callbacks cb, lv_indev_t** kb_out)
{
    s_cb = std::move(cb);
    s_info = &info;
    if(kb_out) *kb_out = nullptr;

    lv_display_t* disp = nullptr;
#if defined(HML_BACKEND_FBDEV)
    std::string dev = env_or("HML_FBDEV", info.fbdev, "/dev/fb0");
    if(!device_usable(dev)) return nullptr;
    disp = lv_linux_fbdev_create();
    if(!disp) return nullptr;
    lv_linux_fbdev_set_file(disp, dev.c_str());
#else
    std::string dev = env_or("HML_DRM", info.drm, "/dev/dri/card0");
    if(!device_usable(dev)) return nullptr;
    disp = lv_linux_drm_create();
    if(!disp) return nullptr;
    lv_linux_drm_set_file(disp, dev.c_str(), -1);
#endif
    HML_INFO("%s display %s: %dx%d (project says %dx%d)", name(), dev.c_str(),
             static_cast<int>(lv_display_get_horizontal_resolution(disp)), static_cast<int>(lv_display_get_vertical_resolution(disp)),
             info.width, info.height);
    if(info.rotation) lv_display_set_rotation(disp, static_cast<lv_display_rotation_t>(info.rotation / 90));

    // Input: explicit device or auto-discovery
    std::string in = env_or("HML_INPUT", info.input, "");
    if(!in.empty()) {
        lv_indev_t* ptr = lv_evdev_create(LV_INDEV_TYPE_POINTER, in.c_str());
        if(!ptr) HML_WARN("cannot open input device %s", in.c_str());
        else { apply_touch_settings(ptr); HML_INFO("input: %s", in.c_str()); }
    }
    else if(lv_evdev_discovery_start(on_evdev_discovered, nullptr) != LV_RESULT_OK) {
        HML_WARN("evdev auto-discovery failed; set \"display\": { \"input\": \"/dev/input/eventN\" }");
    }
    else HML_INFO("input: evdev auto-discovery (/dev/input/event*)");

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);
    std::signal(SIGUSR1, on_signal);
    return disp;
}

void poll()
{
    if(s_sig_quit) { s_sig_quit = 0; if(s_cb.quit) s_cb.quit(); }
    if(s_sig_reload) { s_sig_reload = 0; if(s_cb.reload) s_cb.reload(); }
}

// Snapshot of the active screen written as a 24-bit BMP.
void screenshot(lv_display_t* disp, const std::string& path)
{
    lv_draw_buf_t* buf = lv_snapshot_take(lv_display_get_screen_active(disp), LV_COLOR_FORMAT_XRGB8888);
    if(!buf) { HML_WARN("screenshot: snapshot failed"); return; }
    const uint32_t w = buf->header.w, h = buf->header.h;
    const uint32_t row = (w * 3 + 3) & ~3u;
    FILE* f = std::fopen(path.c_str(), "wb");
    if(!f) { HML_WARN("screenshot: cannot write %s", path.c_str()); lv_draw_buf_destroy(buf); return; }

    uint8_t hdr[54] = { 'B', 'M' };
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(hdr + off, &v, 4); };
    put32(2, 54 + row * h); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    hdr[26] = 1; hdr[28] = 24;
    put32(34, row * h);
    std::fwrite(hdr, 1, 54, f);

    std::vector<uint8_t> line(row, 0);
    for(uint32_t y = h; y-- > 0;) {
        const uint8_t* src = static_cast<const uint8_t*>(buf->data) + y * buf->header.stride;
        for(uint32_t x = 0; x < w; x++) { line[x * 3] = src[x * 4]; line[x * 3 + 1] = src[x * 4 + 1]; line[x * 3 + 2] = src[x * 4 + 2]; }
        std::fwrite(line.data(), 1, row, f);
    }
    std::fclose(f);
    lv_draw_buf_destroy(buf);
    HML_INFO("screenshot saved to %s", path.c_str());
}

void deinit()
{
    lv_evdev_discovery_stop();
}

const char* name()
{
#if defined(HML_BACKEND_FBDEV)
    return "fbdev";
#else
    return "drm";
#endif
}

const char* hint() { return "SIGHUP/SIGUSR1 = reload screens, SIGINT/SIGTERM (Ctrl-C) = quit"; }

} // namespace hml::platform

#endif // HML_BACKEND_FBDEV || HML_BACKEND_DRM
