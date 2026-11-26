#include "vnc.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include "mbedtls/des.h"
#include "operation_log.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "util.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kBadSock = INVALID_SOCKET;
static void close_sock(sock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using sock_t = int;
static const sock_t kBadSock = -1;
static void close_sock(sock_t s) { close(s); }
#endif

namespace hml {

// ---------------------------------------------------------------- pieces without I/O

std::vector<uint8_t> vnc_auth_response(const uint8_t challenge[16], const std::string& password)
{
    uint8_t key[8] = {};
    for(size_t i = 0; i < 8 && i < password.size(); i++) {
        uint8_t b = static_cast<uint8_t>(password[i]), r = 0;
        for(int k = 0; k < 8; k++) if(b & (1u << k)) r = static_cast<uint8_t>(r | (0x80u >> k));
        key[i] = r;
    }
    std::vector<uint8_t> out(16);
    mbedtls_des_context ctx;
    mbedtls_des_init(&ctx);
    mbedtls_des_setkey_enc(&ctx, key);
    mbedtls_des_crypt_ecb(&ctx, challenge, out.data());
    mbedtls_des_crypt_ecb(&ctx, challenge + 8, out.data() + 8);
    mbedtls_des_free(&ctx);
    return out;
}

void VncPixelFormat::write(uint8_t o[16]) const
{
    o[0] = bpp; o[1] = depth; o[2] = big_endian; o[3] = true_colour;
    o[4] = static_cast<uint8_t>(r_max >> 8); o[5] = static_cast<uint8_t>(r_max);
    o[6] = static_cast<uint8_t>(g_max >> 8); o[7] = static_cast<uint8_t>(g_max);
    o[8] = static_cast<uint8_t>(b_max >> 8); o[9] = static_cast<uint8_t>(b_max);
    o[10] = r_shift; o[11] = g_shift; o[12] = b_shift; o[13] = o[14] = o[15] = 0;
}

VncPixelFormat VncPixelFormat::read(const uint8_t i[16])
{
    VncPixelFormat f;
    f.bpp = i[0]; f.depth = i[1]; f.big_endian = i[2]; f.true_colour = i[3];
    f.r_max = static_cast<uint16_t>((i[4] << 8) | i[5]);
    f.g_max = static_cast<uint16_t>((i[6] << 8) | i[7]);
    f.b_max = static_cast<uint16_t>((i[8] << 8) | i[9]);
    f.r_shift = i[10]; f.g_shift = i[11]; f.b_shift = i[12];
    return f;
}

bool VncPixelFormat::usable() const
{
    if(!true_colour || (bpp != 8 && bpp != 16 && bpp != 32)) return false;
    if(r_max == 0 || g_max == 0 || b_max == 0) return false;
    return r_shift < bpp && g_shift < bpp && b_shift < bpp;
}

void vnc_convert(const uint8_t* p, size_t pixels, const VncPixelFormat& f, std::vector<uint8_t>& out)
{
    const VncPixelFormat ours;
    const size_t at = out.size();
    out.resize(at + pixels * static_cast<size_t>(f.bytes()));
    uint8_t* o = out.data() + at;
    // (the usual case: the viewer takes our own layout, the bytes are copied as they are)
    if(f.bpp == 32 && !f.big_endian && f.r_max == 255 && f.g_max == 255 && f.b_max == 255 && f.r_shift == ours.r_shift && f.g_shift == ours.g_shift && f.b_shift == ours.b_shift) {
        std::memcpy(o, p, pixels * 4);
        return;
    }
    for(size_t i = 0; i < pixels; i++, p += 4) {
        const uint32_t v = ((p[2] * static_cast<uint32_t>(f.r_max) + 127) / 255) << f.r_shift |
                           ((p[1] * static_cast<uint32_t>(f.g_max) + 127) / 255) << f.g_shift |
                           ((p[0] * static_cast<uint32_t>(f.b_max) + 127) / 255) << f.b_shift;
        if(f.bpp == 8) *o++ = static_cast<uint8_t>(v);
        else if(f.bpp == 16) {
            if(f.big_endian) { *o++ = static_cast<uint8_t>(v >> 8); *o++ = static_cast<uint8_t>(v); }
            else { *o++ = static_cast<uint8_t>(v); *o++ = static_cast<uint8_t>(v >> 8); }
        }
        else if(f.big_endian) { *o++ = static_cast<uint8_t>(v >> 24); *o++ = static_cast<uint8_t>(v >> 16); *o++ = static_cast<uint8_t>(v >> 8); *o++ = static_cast<uint8_t>(v); }
        else { *o++ = static_cast<uint8_t>(v); *o++ = static_cast<uint8_t>(v >> 8); *o++ = static_cast<uint8_t>(v >> 16); *o++ = static_cast<uint8_t>(v >> 24); }
    }
}

void vnc_cursor(const VncPixelFormat& f, std::vector<uint8_t>& out)
{
    // X = black, . = white, the rest is not drawn; the tip (0, 0) is the hot spot
    static const char* const kArrow[kVncCursorH] = {
        "X           ",
        "XX          ",
        "X.X         ",
        "X..X        ",
        "X...X       ",
        "X....X      ",
        "X.....X     ",
        "X......X    ",
        "X.......X   ",
        "X........X  ",
        "X.........X ",
        "X......XXXXX",
        "X...X..X    ",
        "X..X X..X   ",
        "X.X  X..X   ",
        "XX    X..X  ",
        "X     X..X  ",
        "       XX   ",
    };
    auto put16 = [&](int v) { out.push_back(static_cast<uint8_t>(v >> 8)); out.push_back(static_cast<uint8_t>(v)); };
    put16(0); put16(0); put16(kVncCursorW); put16(kVncCursorH);
    out.push_back(0xFF); out.push_back(0xFF); out.push_back(0xFF); out.push_back(0x11);      // -239
    std::vector<uint8_t> px(static_cast<size_t>(kVncCursorW) * kVncCursorH * 4, 0);
    for(int y = 0; y < kVncCursorH; y++)
        for(int x = 0; x < kVncCursorW; x++)
            if(kArrow[y][x] == '.') std::fill_n(px.begin() + (static_cast<size_t>(y) * kVncCursorW + x) * 4, 3, static_cast<uint8_t>(255));
    vnc_convert(px.data(), px.size() / 4, f, out);
    for(int y = 0; y < kVncCursorH; y++)
        for(int x0 = 0; x0 < kVncCursorW; x0 += 8) {
            uint8_t m = 0;
            for(int x = x0; x < x0 + 8 && x < kVncCursorW; x++) if(kArrow[y][x] != ' ') m = static_cast<uint8_t>(m | (0x80 >> (x - x0)));
            out.push_back(m);
        }
}

std::vector<VncRect> vnc_diff(const uint8_t* a, const uint8_t* b, int width, int height, int tile)
{
    std::vector<VncRect> out;
    for(int ty = 0; ty < height; ty += tile) {
        const int th = std::min(tile, height - ty);
        int run_x = -1;
        for(int tx = 0; tx < width; tx += tile) {
            const int tw = std::min(tile, width - tx);
            bool changed = false;
            for(int y = ty; y < ty + th && !changed; y++) {
                const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(tx)) * 4;
                changed = std::memcmp(a + off, b + off, static_cast<size_t>(tw) * 4) != 0;
            }
            if(changed && run_x < 0) run_x = tx;
            if(!changed && run_x >= 0) { out.push_back({ run_x, ty, tx - run_x, th }); run_x = -1; }
        }
        if(run_x >= 0) out.push_back({ run_x, ty, width - run_x, th });      // (a run that reaches the right edge)
    }
    return out;
}

bool capture_display(lv_display_t* disp, std::vector<uint8_t>& xrgb, int& width, int& height)
{
    lv_draw_buf_t* buf = lv_snapshot_take(lv_display_get_screen_active(disp), LV_COLOR_FORMAT_XRGB8888);
    if(!buf) return false;
    const uint32_t w = buf->header.w, h = buf->header.h;
    xrgb.resize(static_cast<size_t>(w) * h * 4);
    for(uint32_t y = 0; y < h; y++) std::memcpy(xrgb.data() + static_cast<size_t>(y) * w * 4, static_cast<const uint8_t*>(buf->data) + y * buf->header.stride, static_cast<size_t>(w) * 4);
    lv_draw_buf_destroy(buf);
    // what is on the top layer (dialogs) and the system layer (screen saver) is over the screen
    for(lv_obj_t* top : { lv_display_get_layer_top(disp), lv_display_get_layer_sys(disp) }) {
        if(!top || lv_obj_get_child_count(top) == 0) continue;
        lv_draw_buf_t* ov = lv_snapshot_take(top, LV_COLOR_FORMAT_ARGB8888);
        if(!ov) continue;
        const uint32_t ow = std::min<uint32_t>(ov->header.w, w), oh = std::min<uint32_t>(ov->header.h, h);
        for(uint32_t y = 0; y < oh; y++) {
            uint8_t* d = xrgb.data() + static_cast<size_t>(y) * w * 4;
            const uint8_t* o = static_cast<const uint8_t*>(ov->data) + y * ov->header.stride;
            for(uint32_t x = 0; x < ow; x++) {
                const uint32_t a = o[x * 4 + 3];
                if(a == 0) continue;
                for(int c = 0; c < 3; c++) d[x * 4 + c] = static_cast<uint8_t>((o[x * 4 + c] * a + d[x * 4 + c] * (255 - a)) / 255);
            }
        }
        lv_draw_buf_destroy(ov);
    }
    for(size_t i = 3; i < xrgb.size(); i += 4) xrgb[i] = 0;      // (the unused byte: always the same, for the comparison of frames)
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

// ---------------------------------------------------------------- the server

namespace {

constexpr int kFailLimit = 5;            // wrong passwords before a pause
constexpr int kFailPauseMs = 30000;

bool wait_readable(sock_t s, int ms)
{
    fd_set set; FD_ZERO(&set); FD_SET(s, &set);
    timeval tv{ ms / 1000, (ms % 1000) * 1000 };
    return select(static_cast<int>(s + 1), &set, nullptr, nullptr, &tv) > 0;
}

void put16(std::vector<uint8_t>& b, int v) { b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
void put32(std::vector<uint8_t>& b, uint32_t v) { for(int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s)); }

} // namespace

VncSettings vnc_settings(const json* section, const json* local)
{
    VncSettings s;
    const bool has = section && section->is_object(), mine = local && local->is_object();
    s.wanted = has && jbool(*section, "enabled", false);
    s.allowed = !mine || jbool(*local, "enabled", true);
    s.enabled = s.wanted && s.allowed;
    if(has) s.port = static_cast<int>(std::clamp(jnum(*section, "port", 5900), 0.0, 65535.0));
    s.project_password = has && !jstr(*section, "password", "").empty();
    if(mine) s.password = jstr(*local, "password", "");
    s.view_only = (has && jbool(*section, "view_only", false)) || (mine && jbool(*local, "view_only", false));
    return s;
}

struct VncServer::Impl {
    // what start() was given (set_local() starts again with it)
    json        section, local;
    std::string store;
    VncSettings cfg;

    // configuration
    int         port = 5900;
    std::string password, title;
    bool        view_only = false;
    lv_display_t* disp = nullptr;
    int         width = 0, height = 0;

    sock_t      listen_sock = kBadSock;
    int         bound_port = 0;
    std::thread thread;
    std::atomic<bool> running{ false };

    // between the UI thread and the server thread
    std::mutex  mu;
    std::vector<uint8_t> frame;          // the newest capture
    uint64_t    seq = 0;                 // counts the captures that differ
    std::atomic<bool> want_frame{ false };
    std::atomic<bool> connected{ false };
    std::atomic<int>  px{ 0 }, py{ 0 };
    std::atomic<bool> pressed{ false };
    std::string peer;                    // guarded by mu
    int         logged = 0;              // UI thread: what was last put into the log (0 nobody, 1 a viewer)

    lv_timer_t* timer = nullptr;
    lv_indev_t* indev = nullptr;
    uint32_t    last_capture = 0;
    std::vector<uint8_t> scratch;

    int         failures = 0;
    std::chrono::steady_clock::time_point locked_until{};

    // ---- UI thread
    static void on_timer(lv_timer_t* t) { static_cast<Impl*>(lv_timer_get_user_data(t))->tick(); }
    static void on_read(lv_indev_t* indev, lv_indev_data_t* data)
    {
        Impl* m = static_cast<Impl*>(lv_indev_get_user_data(indev));
        data->point.x = m->px;
        data->point.y = m->py;
        data->state = m->pressed && m->connected ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    void tick()
    {
        const int now = connected ? 1 : 0;
        if(now != logged) {
            logged = now;
            std::string who;
            { std::lock_guard<std::mutex> l(mu); who = peer; }
            HML_INFO("remote: viewer %s %s", who.c_str(), now ? "connected" : "disconnected");
            operation_log().record("remote", now ? "connected" : "disconnected", "", who);
        }
        if(!connected || !want_frame) return;
        if(millis() - last_capture < 120) return;
        last_capture = millis();
        int w = 0, h = 0;
        if(!capture_display(disp, scratch, w, h) || w != width || h != height) return;
        std::lock_guard<std::mutex> l(mu);
        if(scratch != frame) { frame.swap(scratch); seq++; }
    }

    // ---- server thread
    bool read_exact(sock_t c, uint8_t* b, int n, int timeout_ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while(n > 0) {
            if(!running || std::chrono::steady_clock::now() > deadline) return false;
            if(!wait_readable(c, 100)) continue;
            const int r = static_cast<int>(recv(c, reinterpret_cast<char*>(b), n, 0));
            if(r <= 0) return false;
            b += r; n -= r;
        }
        return true;
    }
    bool send_all(sock_t c, const uint8_t* b, size_t n)
    {
        while(n > 0) {
            const int r = static_cast<int>(send(c, reinterpret_cast<const char*>(b), static_cast<int>(std::min<size_t>(n, 1 << 20)), 0));
            if(r <= 0) return false;
            b += r; n -= static_cast<size_t>(r);
        }
        return true;
    }
    bool send_all(sock_t c, const std::vector<uint8_t>& b) { return send_all(c, b.data(), b.size()); }

    void run()
    {
        while(running) {
            if(!wait_readable(listen_sock, 100)) continue;
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            sock_t c = accept(listen_sock, reinterpret_cast<sockaddr*>(&addr), &len);
            if(c == kBadSock) continue;
            char ip[64] = "?";
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            const bool loopback = (ntohl(addr.sin_addr.s_addr) >> 24) == 127;
            if(password.empty() && !loopback) {         // (the socket is bound to 127.0.0.1 then; this is the second lock)
                close_sock(c);
                continue;
            }
            int one = 1;
            setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
            { std::lock_guard<std::mutex> l(mu); peer = ip; }
            serve(c);
            connected = false;
            want_frame = false;
            pressed = false;
            close_sock(c);
        }
    }

    // RFB version, security, ClientInit / ServerInit. False = the viewer is not let in.
    bool handshake(sock_t c)
    {
        if(!send_all(c, reinterpret_cast<const uint8_t*>("RFB 003.008\n"), 12)) return false;
        uint8_t v[12];
        if(!read_exact(c, v, 12, 10000) || std::memcmp(v, "RFB 003.00", 10) != 0) return false;
        const int minor = v[10] >= '8' ? 8 : v[10] == '7' ? 7 : 3;
        const uint8_t type = password.empty() ? 1 : 2;
        if(minor == 3) { const uint8_t m[4] = { 0, 0, 0, type }; if(!send_all(c, m, 4)) return false; }
        else {
            const uint8_t m[2] = { 1, type };
            uint8_t chosen = 0;
            if(!send_all(c, m, 2) || !read_exact(c, &chosen, 1, 10000) || chosen != type) return false;
        }
        bool ok = true;
        std::string reason;
        if(type == 2) {
            if(std::chrono::steady_clock::now() < locked_until) { ok = false; reason = "Too many wrong passwords. Try again later."; }
            uint8_t challenge[16], answer[16];
            std::random_device rd;
            for(uint8_t& b : challenge) b = static_cast<uint8_t>(rd());
            if(!send_all(c, challenge, 16) || !read_exact(c, answer, 16, 30000)) return false;
            if(ok) {
                const std::vector<uint8_t> want = vnc_auth_response(challenge, password);
                uint8_t diff = 0;
                for(int i = 0; i < 16; i++) diff = static_cast<uint8_t>(diff | (want[static_cast<size_t>(i)] ^ answer[i]));
                ok = diff == 0;
                if(!ok) {
                    reason = "Wrong password.";
                    if(++failures >= kFailLimit) { failures = 0; locked_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFailPauseMs); }
                    HML_WARN("remote: wrong password from a viewer");
                    for(int i = 0; i < 20 && running; i++) sleep_ms(100);      // (guessing is slow)
                }
                else failures = 0;
            }
        }
        if(type == 2 || minor == 8) {
            std::vector<uint8_t> r;
            put32(r, ok ? 0 : 1);
            if(!ok && minor == 8) { put32(r, static_cast<uint32_t>(reason.size())); r.insert(r.end(), reason.begin(), reason.end()); }
            if(!send_all(c, r)) return false;
        }
        if(!ok) return false;
        uint8_t shared = 0;
        if(!read_exact(c, &shared, 1, 10000)) return false;
        std::vector<uint8_t> init;
        put16(init, width); put16(init, height);
        uint8_t pf[16];
        VncPixelFormat().write(pf);
        init.insert(init.end(), pf, pf + 16);
        put32(init, static_cast<uint32_t>(title.size()));
        init.insert(init.end(), title.begin(), title.end());
        return send_all(c, init);
    }

    void serve(sock_t c)
    {
        if(!handshake(c)) return;
        connected = true;
        VncPixelFormat pf;
        std::vector<uint8_t> sent;                   // what the viewer has
        uint64_t sent_seq = 0;
        bool pending = false, full = false;
        bool cursor_wanted = false, cursor_sent = false;   // the viewer draws the pointer we give it
        std::vector<uint8_t> msg, cur;
        while(running) {
            if(wait_readable(c, pending ? 30 : 200)) {
                uint8_t type = 0, b[24];
                if(!read_exact(c, &type, 1, 5000)) return;
                switch(type) {
                    case 0:                             // SetPixelFormat
                        if(!read_exact(c, b, 19, 5000)) return;
                        pf = VncPixelFormat::read(b + 3);
                        if(!pf.usable()) { HML_WARN("remote: the viewer asks for a pixel format that is not supported (%d bits, %s)", pf.bpp, pf.true_colour ? "true colour" : "colour map"); return; }
                        full = true;                    // (what it has is in the old format)
                        cursor_sent = false;
                        break;
                    case 2: {                           // SetEncodings: pixels go raw whatever it lists; "Cursor" is taken
                        if(!read_exact(c, b, 3, 5000)) return;
                        int n = (b[1] << 8) | b[2];
                        uint8_t e[4];
                        cursor_wanted = false;
                        while(n-- > 0) {
                            if(!read_exact(c, e, 4, 5000)) return;
                            if(e[0] == 0xFF && e[1] == 0xFF && e[2] == 0xFF && e[3] == 0x11) cursor_wanted = true;
                        }
                        cursor_sent = false;
                        break;
                    }
                    case 3:                             // FramebufferUpdateRequest
                        if(!read_exact(c, b, 9, 5000)) return;
                        pending = true;
                        if(b[0] == 0) full = true;
                        break;
                    case 4:                             // KeyEvent
                        if(!read_exact(c, b, 7, 5000)) return;
                        break;
                    case 5: {                           // PointerEvent
                        if(!read_exact(c, b, 5, 5000)) return;
                        if(!view_only) {
                            px = std::clamp((b[1] << 8) | b[2], 0, width - 1);
                            py = std::clamp((b[3] << 8) | b[4], 0, height - 1);
                            pressed = (b[0] & 1) != 0;
                        }
                        break;
                    }
                    case 6: {                           // ClientCutText
                        if(!read_exact(c, b, 7, 5000)) return;
                        uint32_t n = (static_cast<uint32_t>(b[3]) << 24) | (static_cast<uint32_t>(b[4]) << 16) | (static_cast<uint32_t>(b[5]) << 8) | b[6];
                        if(n > (1u << 20)) return;
                        uint8_t skip[256];
                        while(n > 0) { const int k = static_cast<int>(std::min<uint32_t>(n, sizeof(skip))); if(!read_exact(c, skip, k, 5000)) return; n -= static_cast<uint32_t>(k); }
                        break;
                    }
                    default:
                        HML_WARN("remote: unknown message %d from the viewer", type);
                        return;
                }
            }
            if(!pending) continue;
            want_frame = true;                           // the UI thread captures while somebody waits
            uint64_t seq_now = 0;
            {
                std::lock_guard<std::mutex> l(mu);
                if(seq == 0 || (seq == sent_seq && !full)) continue;
                cur = frame;
                seq_now = seq;
            }
            std::vector<VncRect> rects;
            if(full || sent.size() != cur.size()) rects.push_back({ 0, 0, width, height });
            else rects = vnc_diff(sent.data(), cur.data(), width, height);
            sent_seq = seq_now;
            const bool with_cursor = cursor_wanted && !cursor_sent;
            if(rects.empty() && !with_cursor) continue;
            msg.clear();
            msg.push_back(0); msg.push_back(0);
            put16(msg, static_cast<int>(rects.size()) + (with_cursor ? 1 : 0));
            for(const VncRect& r : rects) {
                put16(msg, r.x); put16(msg, r.y); put16(msg, r.w); put16(msg, r.h);
                put32(msg, 0);                           // raw
                for(int y = r.y; y < r.y + r.h; y++)
                    vnc_convert(cur.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(r.x)) * 4, static_cast<size_t>(r.w), pf, msg);
            }
            if(with_cursor) { vnc_cursor(pf, msg); cursor_sent = true; }
            if(!send_all(c, msg)) return;
            sent.swap(cur);
            pending = false;
            full = false;
        }
    }
};

VncServer::VncServer() : impl_(std::make_unique<Impl>()) {}
VncServer::~VncServer() { stop(); }

bool VncServer::start(const json* section, const json* local, lv_display_t* disp, const std::string& title, const std::string& store)
{
    stop();
    Impl& m = *impl_;
    m.section = section ? *section : json();
    m.local = local ? *local : json();
    m.store = store;
    m.cfg = vnc_settings(section, local);
    m.disp = disp;
    m.title = title.empty() ? "HML" : title;
    if(m.cfg.project_password)
        HML_WARN("remote: the \"password\" in project.json is not used: who may connect is set on the device (System > Remote)");
    if(m.cfg.wanted && !m.cfg.allowed) HML_INFO("remote: VNC is turned off on this device (System > Remote)");
    if(!m.cfg.enabled || !disp) return false;
    m.port = m.cfg.port;
    m.password = m.cfg.password;
    m.view_only = m.cfg.view_only;
    m.disp = disp;
    m.width = lv_display_get_horizontal_resolution(disp);
    m.height = lv_display_get_vertical_resolution(disp);
    if(m.password.size() > 8) HML_WARN("remote: VNC uses the first 8 characters of the password only");

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    m.listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(m.listen_sock == kBadSock) { HML_ERROR("remote: cannot create a socket"); return false; }
#ifndef _WIN32
    int one = 1;
    setsockopt(m.listen_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(m.password.empty() ? INADDR_LOOPBACK : INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(m.port));
    if(bind(m.listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(m.listen_sock, 2) != 0) {
        HML_ERROR("remote: cannot listen on port %d (in use?)", m.port);
        close_sock(m.listen_sock);
        m.listen_sock = kBadSock;
        return false;
    }
    socklen_t len = sizeof(addr);
    getsockname(m.listen_sock, reinterpret_cast<sockaddr*>(&addr), &len);
    m.bound_port = ntohs(addr.sin_port);

    m.indev = lv_indev_create();
    lv_indev_set_type(m.indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(m.indev, &Impl::on_read);
    lv_indev_set_user_data(m.indev, &m);
    lv_indev_set_display(m.indev, disp);
    m.timer = lv_timer_create(&Impl::on_timer, 40, &m);
    m.seq = 0;
    m.frame.clear();
    m.logged = 0;
    m.running = true;
    m.thread = std::thread([&m] { m.run(); });
    if(m.password.empty()) HML_WARN("remote: VNC on port %d, no password set on this device (System > Remote): only this machine itself (127.0.0.1) can connect", m.bound_port);
    else HML_INFO("remote: VNC on port %d%s, other machines can connect (not encrypted: for closed networks)", m.bound_port, m.view_only ? ", view only" : "");
    return true;
}

void VncServer::stop()
{
    Impl& m = *impl_;
    if(!m.running && m.listen_sock == kBadSock) return;
    m.running = false;
    if(m.thread.joinable()) m.thread.join();
    if(m.listen_sock != kBadSock) close_sock(m.listen_sock);
    m.listen_sock = kBadSock;
    m.bound_port = 0;
    if(m.timer) lv_timer_delete(m.timer);
    m.timer = nullptr;
    if(m.indev) lv_indev_delete(m.indev);
    m.indev = nullptr;
    m.connected = false;
}

bool VncServer::running() const { return impl_->running; }
int  VncServer::port() const { return impl_->bound_port; }
bool VncServer::client_connected() const { return impl_->connected; }
bool VncServer::open_to_network() const { return impl_->running && !impl_->password.empty(); }
const VncSettings& VncServer::settings() const { return impl_->cfg; }

std::string VncServer::set_local(bool allowed, const std::string& password, bool view_only, bool remove_password)
{
    Impl& m = *impl_;
    if(!m.section.is_object()) return "the project has no \"remote\"";
    if(!password.empty() && !remove_password) {
        if(password.size() < kVncMinPassword) return "The password needs at least " + std::to_string(kVncMinPassword) + " characters.";
        for(unsigned char c : password) if(c <= 0x20 || c > 0x7e) return "The password takes letters, digits and signs only (no spaces).";
    }
    const VncSettings before = m.cfg;
    const VncSettings base = vnc_settings(&m.section, nullptr);
    // only what the device adds is kept (a later deploy of the project still counts for the rest)
    json mine = json::object();
    if(!allowed) mine["enabled"] = false;
    if(view_only && !base.view_only) mine["view_only"] = true;
    const std::string pw = remove_password ? std::string() : !password.empty() ? password : before.password;
    if(!pw.empty()) mine["password"] = pw;
    if(!m.store.empty()) {
        json overlay = json::object();
        {
            std::ifstream f(std::filesystem::u8path(m.store), std::ios::binary);
            if(f) {
                std::stringstream ss;
                ss << f.rdbuf();
                overlay = json::parse(ss.str(), nullptr, false, true);
                if(!overlay.is_object()) overlay = json::object();
            }
        }
        if(mine.empty()) overlay.erase("remote");
        else overlay["remote"] = mine;
        std::string err;
        if(overlay.empty()) { std::error_code ec; std::filesystem::remove(std::filesystem::u8path(m.store), ec); }
        else if(!write_file_atomic(m.store, overlay.dump(2) + "\n", &err)) return err;
    }
    // who changed what (never the password itself)
    const VncSettings now = vnc_settings(&m.section, &mine);
    if(now.allowed != before.allowed) operation_log().record("remote", "enabled", before.allowed ? "on" : "off", now.allowed ? "on" : "off");
    if(now.view_only != before.view_only) operation_log().record("remote", "view_only", before.view_only ? "on" : "off", now.view_only ? "on" : "off");
    if(now.password != before.password) operation_log().record("remote", "password", before.password.empty() ? "none" : "set", now.password.empty() ? "none" : "changed");

    // (copies: start() assigns the members it is given)
    const json section = m.section, local = mine;
    const std::string title = m.title, store = m.store;
    lv_display_t* disp = m.disp;
    const bool listening = start(&section, mine.empty() ? nullptr : &local, disp, title, store);
    if(now.enabled && disp && !listening) return "Saved, but the server could not be started (see the log).";
    return "";
}

// ---------------------------------------------------------------- System > Remote

namespace {

class RemotePage final : public SettingsPage {
public:
    std::string id() const override { return "remote"; }
    std::string title() const override { return "Remote"; }

    static std::string state()
    {
        const VncServer& s = vnc_server();
        const VncSettings& c = s.settings();
        if(!c.wanted) return "Off (the project does not use it)";
        if(!c.allowed) return "Off";
        if(!s.running()) return "Not running";
        std::string out = "Port " + std::to_string(s.port()) + (s.open_to_network() ? ": other machines can connect" : ": this device only (no password set)");
        if(c.view_only) out += ", view only";
        if(s.client_connected()) out += " - a viewer is connected";
        return out;
    }
    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        const VncSettings& c = vnc_server().settings();
        auto add = [&](const char* key, const char* label, SettingField::Type type, const std::string& value, bool password = false) {
            SettingField f; f.key = key; f.label = label; f.type = type; f.value = value; f.password = password; out.push_back(f);
        };
        add("enabled", "Remote access (VNC)", SettingField::Type::Bool, c.allowed ? "1" : "0");
        // (never shown, not even as dots: empty means "leave it as it is")
        add("password", "Password (empty: unchanged)", SettingField::Type::Text, "", true);
        add("view_only", "View only", SettingField::Type::Bool, c.view_only ? "1" : "0");
        add("has_password", "Password", SettingField::Type::Info, c.password.empty() ? "Not set: only this device itself can connect" : "Set (the first 8 characters count)");
        shown_ = state();
        add("state", "State", SettingField::Type::Info, shown_);
        return out;
    }
    std::string apply(const std::map<std::string, std::string>& v) override
    {
        auto get = [&](const char* k) { auto it = v.find(k); return it == v.end() ? std::string() : it->second; };
        const std::string err = vnc_server().set_local(get("enabled") != "0", get("password"), get("view_only") == "1");
        return err.empty() ? "Saved." : err;
    }
    std::vector<std::pair<std::string, std::string>> buttons() const override { return { { "Remove the password", "remove_password" } }; }
    void command(const std::string& cmd, const std::string&) override
    {
        if(cmd != "remove_password") { HML_WARN("system: remote: unknown command '%s'", cmd.c_str()); return; }
        const VncSettings c = vnc_server().settings();
        const std::string err = vnc_server().set_local(c.allowed, "", c.view_only, true);
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string(err.empty() ? "The password was removed: only this device itself can connect." : err, nullptr);
    }
    // A viewer comes and goes: only the line that shows it is written.
    bool poll() override
    {
        const std::string now = state();
        if(now == shown_) return false;
        shown_ = now;
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "state"))) t->set_string(now, nullptr);
        return false;
    }

private:
    std::string shown_;
};

} // namespace

std::unique_ptr<SettingsPage> make_remote_page() { return std::make_unique<RemotePage>(); }

VncServer& vnc_server()
{
    static VncServer s;
    return s;
}

} // namespace hml
