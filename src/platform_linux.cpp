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
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "platform.hpp"
#include "screenshot.hpp"
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

// ---- console (fbcon) suppression -------------------------------------------------------------
// The kernel console shares /dev/fb0 with us: boot messages, the login prompt and kernel
// warnings get drawn over the HMI. Switching the VT to KD_GRAPHICS stops fbcon from drawing
// anything (and hides its cursor). /dev/tty1 is root-only, so either the systemd unit hands us
// the tty as stdin (TTYPath=/dev/tty1 + StandardInput=tty, see tools/rpi/hml.service) or the
// path must be writable by the user.
int  s_console_fd = -1;
bool s_console_owned = false;
long s_console_prev_mode = KD_TEXT;

void console_graphics_mode(const std::string& path)
{
    if(path.empty() || path == "none") return;
    int fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    const int open_errno = errno;
    bool owned = fd >= 0;
    if(!owned) {
        // fall back to stdin when systemd attached the VT to it
        long mode;
        if(isatty(STDIN_FILENO) && ioctl(STDIN_FILENO, KDGETMODE, &mode) == 0) fd = STDIN_FILENO;
        else {
            HML_WARN("console: cannot open %s (%s): kernel text may be drawn over the HMI. Run from the hml.service unit "
                     "(it passes the VT as stdin) or set \"display\": { \"console\": \"none\" } to silence this.",
                     path.c_str(), std::strerror(open_errno));
            return;
        }
    }
    long mode = KD_TEXT;
    ioctl(fd, KDGETMODE, &mode);
    if(ioctl(fd, KDSETMODE, KD_GRAPHICS) != 0) {
        HML_WARN("console: KDSETMODE on %s failed (%s)", owned ? path.c_str() : "stdin", std::strerror(errno));
        if(owned) close(fd);
        return;
    }
    s_console_fd = fd;
    s_console_owned = owned;
    s_console_prev_mode = mode;
    HML_INFO("console: %s switched to graphics mode (text output suppressed)", owned ? path.c_str() : "stdin VT");
}

void console_restore()
{
    if(s_console_fd < 0) return;
    ioctl(s_console_fd, KDSETMODE, s_console_prev_mode);
    if(s_console_owned) close(s_console_fd);
    s_console_fd = -1;
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
    // The driver computes the DPI from the physical size reported by the kernel; HDMI often
    // reports nonsense (a Pi 4 says 70 mm for 1280 px -> 464 dpi) which blows up every theme
    // padding and radius. Use the project's value instead (130 = same as the SDL backend).
    int driver_dpi = static_cast<int>(lv_display_get_dpi(disp));
    lv_display_set_dpi(disp, info.dpi);
    HML_INFO("%s display %s: %dx%d (project says %dx%d), dpi %d (driver reported %d)", name(), dev.c_str(),
             static_cast<int>(lv_display_get_horizontal_resolution(disp)), static_cast<int>(lv_display_get_vertical_resolution(disp)),
             info.width, info.height, info.dpi, driver_dpi);
    if(info.rotation) lv_display_set_rotation(disp, static_cast<lv_display_rotation_t>(info.rotation / 90));

    console_graphics_mode(env_or("HML_CONSOLE", info.console, "/dev/tty1"));

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
void screenshot(lv_display_t* disp, const std::string& path) { save_screenshot_bmp(disp, path); }

void deinit()
{
    lv_evdev_discovery_stop();
    console_restore();
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

// Console backends: "restart" reboots the machine. The runtime normally runs as a plain user
// (systemd unit), so the reboot goes through `sudo -n` (see tools/rpi/hml.sudoers) with
// plain systemctl as a fallback. When neither is allowed the application restarts itself.
bool restart(RestartTarget what, int argc, char** argv)
{
    (void)argc;
    if(what != RestartTarget::App) {
        HML_INFO("restart: rebooting the system");
        std::fflush(stdout);
        int rc = std::system("sudo -n systemctl reboot >/dev/null 2>&1 || systemctl reboot >/dev/null 2>&1");
        if(rc == 0) return true;                            // the system is going down, caller exits
        HML_ERROR("restart: reboot refused (install tools/rpi/hml.sudoers); restarting the app instead");
        if(what == RestartTarget::System) return false;
    }
    HML_INFO("restart: re-executing %s", argv[0]);
    std::fflush(stdout);
    execv("/proc/self/exe", argv);
    execv(argv[0], argv);
    HML_ERROR("restart: execv failed: %s", std::strerror(errno));
    return false;
}

} // namespace hml::platform

#endif // HML_BACKEND_FBDEV || HML_BACKEND_DRM
