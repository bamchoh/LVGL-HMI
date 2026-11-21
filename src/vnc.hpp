// Remote view and operation: a small VNC server (RFB 3.3 / 3.7 / 3.8, raw encoding), so that the
// screen of the device can be seen and touched from a PC or a phone with any VNC viewer.
//
// project.json:
//   "remote": {
//     "enabled": true,
//     "port": 5900,
//     "password": "up to 8",       // VNC authentication. Without one only this machine itself
//                                  // (127.0.0.1) may connect, for example through an SSH tunnel
//     "view_only": false           // true: the pointer of the viewer does nothing
//   }
//
// The protocol is not encrypted and its password check is weak (DES, 8 characters): use it on
// a closed network or through an SSH tunnel. One viewer at a time; a second one is refused.
//
// The screen is captured on the UI thread (the same picture a screenshot gives) while a viewer
// waits for an update; a thread of its own talks to the viewer. The pieces of the protocol that
// do no I/O are declared here so that every byte can be tested.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"
#include "lvgl.h"

namespace hml {

// The 16 bytes a viewer answers to a challenge with: DES of the challenge, the key being the
// password (8 bytes, zero padded) with the bits of every byte mirrored.
std::vector<uint8_t> vnc_auth_response(const uint8_t challenge[16], const std::string& password);

// How a viewer wants its pixels (the 16 bytes of ServerInit / SetPixelFormat).
struct VncPixelFormat {
    uint8_t  bpp = 32, depth = 24, big_endian = 0, true_colour = 1;
    uint16_t r_max = 255, g_max = 255, b_max = 255;
    uint8_t  r_shift = 16, g_shift = 8, b_shift = 0;

    void write(uint8_t out[16]) const;
    static VncPixelFormat read(const uint8_t in[16]);
    bool usable() const;                 // true colour, 8 / 16 / 32 bits per pixel
    int  bytes() const { return bpp / 8; }
};

// Pixels of our frame (XRGB8888: B, G, R, X in memory) in the format of the viewer, appended to out.
void vnc_convert(const uint8_t* xrgb, size_t pixels, const VncPixelFormat& f, std::vector<uint8_t>& out);

struct VncRect { int x, y, w, h; };
// Where two frames of the same size differ, as rectangles of whole tiles (runs of tiles in a
// row are one rectangle). Empty = the same picture.
std::vector<VncRect> vnc_diff(const uint8_t* a, const uint8_t* b, int width, int height, int tile = 32);

// The pointer a viewer draws by itself (the "Cursor" pseudo encoding, -239): one rectangle of a
// FramebufferUpdate (header, pixels in the format of the viewer, mask), appended to out. Without
// it some viewers (TigerVNC) show no pointer at all over the picture.
constexpr int kVncCursorW = 12, kVncCursorH = 18;
void vnc_cursor(const VncPixelFormat& f, std::vector<uint8_t>& out);

class VncServer {
public:
    VncServer();
    ~VncServer();

    // Start listening (UI thread, LVGL and the display exist). section = "remote" of project.json
    // (nullptr or "enabled": false = nothing is started). Returns true when it listens.
    bool start(const json* section, lv_display_t* disp, const std::string& title);
    void stop();
    bool running() const;
    int  port() const;                   // the port it listens on (0 = not running)
    bool client_connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

VncServer& vnc_server();

// Fill xrgb (width * height * 4 bytes: B, G, R, X) with what the display shows now: the active
// screen with the top and system layers over it (screenshot.cpp). UI thread.
bool capture_display(lv_display_t* disp, std::vector<uint8_t>& xrgb, int& width, int& height);

} // namespace hml
