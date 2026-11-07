/* LVGL configuration for the HML runtime.
 * The display/input driver is chosen by HML_BACKEND_* (set by CMake, -DHML_BACKEND=sdl|fbdev|drm).
 * Anything not set here takes the LVGL default from lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE

#define LV_DEF_REFR_PERIOD 16

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* ---- display / input backend (exactly one) ---- */
#if !defined(HML_BACKEND_SDL) && !defined(HML_BACKEND_FBDEV) && !defined(HML_BACKEND_DRM)
#  define HML_BACKEND_SDL 1   /* default: PC simulator */
#endif

#if defined(HML_BACKEND_SDL)
/* SDL window driver (Windows / Linux desktop / macOS) */
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT 1
#define LV_SDL_FULLSCREEN 0
#define LV_SDL_DIRECT_EXIT 0
#define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER
#endif

#if defined(HML_BACKEND_FBDEV)
/* Linux framebuffer /dev/fbN (Raspberry Pi console, no X11/Wayland needed) */
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_BSD 0
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT 0
#define LV_LINUX_FBDEV_BUFFER_SIZE 60
#define LV_LINUX_FBDEV_MMAP 1
#endif

#if defined(HML_BACKEND_DRM)
/* Linux DRM/KMS /dev/dri/cardN (Raspberry Pi with vc4-kms-v3d, needs libdrm) */
#define LV_USE_LINUX_DRM 1
#define LV_USE_LINUX_DRM_GBM_BUFFERS 0
#define LV_LINUX_DRM_USE_EGL 0
#endif

#if defined(HML_BACKEND_FBDEV) || defined(HML_BACKEND_DRM)
/* evdev input: touch screens, mice, keyboards via /dev/input/eventN */
#define LV_USE_EVDEV 1
#endif

/* Screen snapshot, used for --screenshot on the non-SDL backends */
#define LV_USE_SNAPSHOT 1

/* Fonts available to screens via "font": <size> */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Widgets used by the loader */
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMAGE 1
#define LV_USE_LABEL 1
#define LV_USE_LED 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1
#define LV_USE_CHART 1
#define LV_USE_SCALE 1
#define LV_USE_SPINNER 1
#define LV_USE_KEYBOARD 1
#define LV_USE_MSGBOX 1

#define LV_USE_THEME_DEFAULT 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_STDIO_PATH ""
#define LV_USE_LODEPNG 1

#define LV_USE_DEMO_WIDGETS 0

#endif /* LV_CONF_H */
