// Remote view (vnc.hpp): the pieces of the protocol byte by byte, and the server end to end
// against a small RFB client written here. The password check is compared with values made
// by OpenSSL (openssl enc -des-ecb), not by our own code.
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kBadSock = INVALID_SOCKET;
static void close_sock(sock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using sock_t = int;
static const sock_t kBadSock = -1;
static void close_sock(sock_t s) { close(s); }
#endif

#include <doctest.h>
#include <random>
#include "lvgl_fixture.hpp"
#include "util.hpp"
#include "vnc.hpp"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------- a small RFB client

struct Client {
    sock_t s = kBadSock;
    bool   failed = false;
    ~Client() { if(s != kBadSock) close_sock(s); }

    bool connect_to(int port)
    {
        s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(static_cast<uint16_t>(port));
        return connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
    }
    bool rd(uint8_t* b, int n, int timeout_ms = 5000)
    {
        while(n > 0) {
            fd_set set; FD_ZERO(&set); FD_SET(s, &set);
            timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
            if(select(static_cast<int>(s + 1), &set, nullptr, nullptr, &tv) <= 0) { failed = true; return false; }
            const int r = static_cast<int>(recv(s, reinterpret_cast<char*>(b), n, 0));
            if(r <= 0) { failed = true; return false; }
            b += r; n -= r;
        }
        return true;
    }
    Bytes rd(int n, int timeout_ms = 5000) { Bytes b(static_cast<size_t>(n)); if(!rd(b.data(), n, timeout_ms)) b.clear(); return b; }
    void wr(const Bytes& b) { if(send(s, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0) != static_cast<int>(b.size())) failed = true; }
    static uint32_t u32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]; }
    static int u16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

    // version and security. Returns the SecurityResult (0 = let in), -1 = something else went wrong.
    int login(const std::string& password, const char* version = "RFB 003.008\n", std::string* reason = nullptr, uint8_t* offered = nullptr)
    {
        Bytes v = rd(12);
        if(v.empty() || std::memcmp(v.data(), "RFB 003.008\n", 12) != 0) return -1;
        wr(Bytes(version, version + 12));
        uint8_t type = 0;
        if(std::strncmp(version, "RFB 003.003", 11) == 0) { Bytes t = rd(4); if(t.empty()) return -1; type = t[3]; }
        else {
            Bytes n = rd(1);
            if(n.empty() || n[0] != 1) return -1;
            Bytes t = rd(1);
            if(t.empty()) return -1;
            type = t[0];
            wr({ type });
        }
        if(offered) *offered = type;
        if(type == 2) {
            Bytes ch = rd(16);
            if(ch.empty()) return -1;
            wr(vnc_auth_response(ch.data(), password));
        }
        if(type == 2 || std::strncmp(version, "RFB 003.008", 11) == 0) {
            Bytes r = rd(4, 8000);
            if(r.empty()) return -1;
            const int result = static_cast<int>(u32(r.data()));
            if(result != 0 && reason && std::strncmp(version, "RFB 003.008", 11) == 0) {
                Bytes l = rd(4);
                if(!l.empty()) { Bytes t = rd(static_cast<int>(u32(l.data()))); reason->assign(t.begin(), t.end()); }
            }
            return result;
        }
        return 0;
    }
    struct Init { int w = 0, h = 0; VncPixelFormat pf; std::string name; };
    Init init()
    {
        Init i;
        wr({ 1 });
        Bytes b = rd(24);
        if(b.empty()) return i;
        i.w = u16(b.data()); i.h = u16(b.data() + 2);
        i.pf = VncPixelFormat::read(b.data() + 4);
        Bytes n = rd(static_cast<int>(u32(b.data() + 20)));
        i.name.assign(n.begin(), n.end());
        return i;
    }
    void request(bool incremental, int w, int h) { wr({ 3, static_cast<uint8_t>(incremental), 0, 0, 0, 0, static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w), static_cast<uint8_t>(h >> 8), static_cast<uint8_t>(h) }); }
    void pointer(bool down, int x, int y) { wr({ 5, static_cast<uint8_t>(down ? 1 : 0), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x), static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y) }); }
    struct Rect { int x, y, w, h; Bytes pixels; bool cursor = false; Bytes mask; };
    // one FramebufferUpdate (bytes per pixel as agreed)
    std::vector<Rect> update(int bytes_per_pixel = 4, int timeout_ms = 5000)
    {
        std::vector<Rect> out;
        Bytes h = rd(4, timeout_ms);
        if(h.empty() || h[0] != 0) { failed = true; return out; }
        for(int n = u16(h.data() + 2); n > 0; n--) {
            Bytes r = rd(12);
            const uint32_t enc = r.empty() ? 1 : u32(r.data() + 8);
            if(enc != 0 && enc != 0xFFFFFF11u) { failed = true; return out; }
            Rect rc{ u16(r.data()), u16(r.data() + 2), u16(r.data() + 4), u16(r.data() + 6), {} };
            rc.pixels = rd(rc.w * rc.h * bytes_per_pixel);
            if(enc != 0) { rc.cursor = true; rc.mask = rd(((rc.w + 7) / 8) * rc.h); }
            out.push_back(std::move(rc));
        }
        return out;
    }
};

// Run the client on a thread of its own while the UI thread keeps LVGL going.
void with_client(Runtime& rt, const std::function<void()>& client)
{
    std::atomic<bool> done{ false };
    std::thread th([&] { client(); done = true; });
    const uint32_t t0 = millis();
    while(!done && millis() - t0 < 30000) { rt.advance(20); rt.pump(); sleep_ms(2); }
    th.join();
    REQUIRE(done);
}

lv_obj_t* show_test_screen()
{
    // a red button at the top left that counts its clicks, a label that shows the count
    lv_obj_t* s = ui().build_screen(json::parse(R"~({ "name": "v", "style": { "bg_color": "#000000", "pad": 0 }, "children": [
        { "type": "button", "x": 0, "y": 0, "w": 96, "h": 64, "text": "", "style": { "bg_color": "#ff0000", "radius": 0, "shadow_width": 0, "border_width": 0 },
          "on_click": { "action": "inc", "tag": "clicks", "by": 1 } },
        { "type": "label", "x": 160, "y": 100, "bind": { "text": { "tag": "clicks", "format": "%d" } }, "style": { "text_color": "#ffffff", "font": 48 } } ] })~"), "");
    REQUIRE(s);
    REQUIRE(ui().show_screen("v", "none"));
    return s;
}

// "remote" of project.json
json remote(const char* extra = "")
{
    return json::parse(std::string("{ \"enabled\": true, \"port\": 0") + extra + " }");
}

// "remote" of project.local.json: what the device itself has set
const json kDevice = json::parse(R"({ "password": "secret12" })");

// 0.0.0.0 or 127.0.0.1: where the server listens, seen from outside (a socket bound to this
// machine's own address reaches it only when it listens for other machines too)
bool reachable_from_network(int port)
{
    // the address this machine uses towards others (no packet is sent: UDP connect only)
    sock_t u = socket(AF_INET, SOCK_DGRAM, 0);
    if(u == kBadSock) return false;
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &out.sin_addr);
    sockaddr_in mine{};
    socklen_t len = sizeof(mine);
    const bool has = connect(u, reinterpret_cast<sockaddr*>(&out), sizeof(out)) == 0 && getsockname(u, reinterpret_cast<sockaddr*>(&mine), &len) == 0;
    close_sock(u);
    if(!has || (ntohl(mine.sin_addr.s_addr) >> 24) == 127 || mine.sin_addr.s_addr == 0) return false;   // (no network: cannot be told)
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == kBadSock) return false;
    mine.sin_port = htons(static_cast<uint16_t>(port));
    const bool ok = connect(s, reinterpret_cast<sockaddr*>(&mine), sizeof(mine)) == 0;
    close_sock(s);
    return ok;
}
bool has_network()
{
    sock_t u = socket(AF_INET, SOCK_DGRAM, 0);
    if(u == kBadSock) return false;
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &out.sin_addr);
    sockaddr_in mine{};
    socklen_t len = sizeof(mine);
    const bool has = connect(u, reinterpret_cast<sockaddr*>(&out), sizeof(out)) == 0 && getsockname(u, reinterpret_cast<sockaddr*>(&mine), &len) == 0;
    close_sock(u);
    return has && (ntohl(mine.sin_addr.s_addr) >> 24) != 127 && mine.sin_addr.s_addr != 0;
}

} // namespace

// ================================================================ the pieces

TEST_CASE("vnc: the answer to a challenge is what OpenSSL computes")
{
    // printf '\x00..\x0f' | openssl enc -des-ecb -K cea6c64ea62e8c4c -nopad     (the key: "secret12", bits mirrored)
    uint8_t c1[16];
    for(int i = 0; i < 16; i++) c1[i] = static_cast<uint8_t>(i);
    CHECK(vnc_auth_response(c1, "secret12") == Bytes{ 0xad, 0xcd, 0x99, 0x7f, 0x8e, 0x16, 0xfe, 0xe5, 0x75, 0xe9, 0x73, 0xf9, 0x3c, 0x2b, 0x62, 0xb4 });
    CHECK(vnc_auth_response(c1, "secret12-and-more") == vnc_auth_response(c1, "secret12"));      // 8 characters count
    // a short password is padded with zeros: key 8646000000000000 for "ab"
    const uint8_t c2[16] = { 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
    CHECK(vnc_auth_response(c2, "ab") == Bytes{ 0x85, 0x7d, 0x0c, 0x0a, 0x27, 0xd6, 0x7b, 0x20, 0x9d, 0xaa, 0x03, 0x95, 0x2f, 0x48, 0xbf, 0x11 });
    CHECK(vnc_auth_response(c1, "secret13") != vnc_auth_response(c1, "secret12"));
}

TEST_CASE("vnc: pixel formats and the differences between two frames")
{
    uint8_t raw[16];
    VncPixelFormat().write(raw);
    CHECK(Bytes(raw, raw + 16) == Bytes{ 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0 });
    const VncPixelFormat back = VncPixelFormat::read(raw);
    CHECK(back.usable());
    CHECK(back.r_shift == 16);

    const uint8_t px[8] = { 0x10, 0x80, 0xFF, 0, /* B G R X */ 0xFF, 0x00, 0x00, 0 };
    Bytes out;
    vnc_convert(px, 2, VncPixelFormat(), out);                     // our own layout: copied
    CHECK(out == Bytes(px, px + 8));
    VncPixelFormat rgb565;                                        // 16 bits, 5-6-5, little-endian
    rgb565.bpp = 16; rgb565.depth = 16; rgb565.r_max = 31; rgb565.g_max = 63; rgb565.b_max = 31; rgb565.r_shift = 11; rgb565.g_shift = 5; rgb565.b_shift = 0;
    out.clear();
    vnc_convert(px, 2, rgb565, out);
    // red 255 -> 31, green 128 -> 32, blue 16 -> 2: 0xFC02; blue only: 0x001F
    CHECK(out == Bytes{ 0x02, 0xFC, 0x1F, 0x00 });
    rgb565.big_endian = 1;
    out.clear();
    vnc_convert(px, 1, rgb565, out);
    CHECK(out == Bytes{ 0xFC, 0x02 });
    VncPixelFormat bgr233;                                        // 8 bits: BBGGGRRR
    bgr233.bpp = 8; bgr233.depth = 8; bgr233.r_max = 7; bgr233.g_max = 7; bgr233.b_max = 3; bgr233.r_shift = 0; bgr233.g_shift = 3; bgr233.b_shift = 6;
    out.clear();
    vnc_convert(px, 2, bgr233, out);
    CHECK(out == Bytes{ static_cast<uint8_t>(7 | (4 << 3) | (0 << 6)), static_cast<uint8_t>(3 << 6) });
    VncPixelFormat bgrx;                                          // 32 bits, big-endian, red in the low byte
    bgrx.big_endian = 1; bgrx.r_shift = 0; bgrx.g_shift = 8; bgrx.b_shift = 16;
    out.clear();
    vnc_convert(px, 1, bgrx, out);
    CHECK(out == Bytes{ 0x00, 0x10, 0x80, 0xFF });
    VncPixelFormat map; map.true_colour = 0;
    CHECK_FALSE(map.usable());
    VncPixelFormat odd; odd.bpp = 24;
    CHECK_FALSE(odd.usable());
    VncPixelFormat zero; zero.g_max = 0;
    CHECK_FALSE(zero.usable());

    // 100 x 70, tiles of 32: a change at (40, 5) and one at (99, 69)
    Bytes a(100 * 70 * 4, 0), b = a;
    CHECK(vnc_diff(a.data(), b.data(), 100, 70).empty());
    b[(5 * 100 + 40) * 4] = 1;
    b[(69 * 100 + 99) * 4 + 2] = 1;
    auto d = vnc_diff(a.data(), b.data(), 100, 70);
    REQUIRE(d.size() == 2);
    CHECK(d[0].x == 32); CHECK(d[0].y == 0); CHECK(d[0].w == 32); CHECK(d[0].h == 32);
    CHECK(d[1].x == 96); CHECK(d[1].y == 64); CHECK(d[1].w == 4); CHECK(d[1].h == 6);
    // neighbours in a row are one rectangle
    b[(5 * 100 + 70) * 4] = 1;
    d = vnc_diff(a.data(), b.data(), 100, 70);
    REQUIRE(d.size() == 2);
    CHECK(d[0].x == 32); CHECK(d[0].w == 64);
}

// ================================================================ the server

TEST_CASE("vnc: a viewer logs in, sees the screen, touches a button and sees the change")
{
    Runtime rt;
    rt.tag("clicks", TagType::Int);
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    const int W = lv_display_get_horizontal_resolution(disp), H = lv_display_get_vertical_resolution(disp);
    VncServer srv;
    const json cfg1 = remote();
    REQUIRE(srv.start(&cfg1, &kDevice, disp, "Test HMI"));
    REQUIRE(srv.port() > 0);

    Client c;
    Client::Init init;
    std::vector<Client::Rect> first, second;
    int result = -1;
    with_client(rt, [&] {
        if(!c.connect_to(srv.port())) { c.failed = true; return; }
        result = c.login("secret12");
        if(result != 0) return;
        init = c.init();
        c.request(false, init.w, init.h);
        first = c.update();
        c.pointer(true, 20, 20);
        sleep_ms(250);
        c.pointer(false, 20, 20);
        sleep_ms(250);
        c.request(true, init.w, init.h);
        second = c.update(4, 8000);
    });
    CHECK_FALSE(c.failed);
    CHECK(result == 0);
    CHECK(init.w == W);
    CHECK(init.h == H);
    CHECK(init.name == "Test HMI");
    CHECK(init.pf.bpp == 32);
    // the whole screen first: red where the button is, black beside it
    REQUIRE(first.size() == 1);
    CHECK(first[0].w == W);
    CHECK(first[0].h == H);
    REQUIRE(first[0].pixels.size() == static_cast<size_t>(W) * H * 4);
    auto pixel = [&](int x, int y) { const uint8_t* p = first[0].pixels.data() + (static_cast<size_t>(y) * W + x) * 4; return std::vector<int>{ p[2], p[1], p[0] }; };
    CHECK(pixel(20, 20) == std::vector<int>{ 255, 0, 0 });
    CHECK(pixel(300, 200) == std::vector<int>{ 0, 0, 0 });
    // the touch arrived, and only what changed is sent afterwards (the label with the count)
    CHECK(tags().find("clicks")->get_number() == 1);
    REQUIRE(!second.empty());
    size_t sent = 0;
    bool label_area = false;
    for(const auto& r : second) {
        sent += r.pixels.size();
        if(r.x <= 170 && r.x + r.w > 170 && r.y <= 120 && r.y + r.h > 120) label_area = true;
    }
    CHECK(label_area);
    CHECK(sent < static_cast<size_t>(W) * H * 4 / 2);
    CHECK(srv.client_connected());
    srv.stop();
    CHECK_FALSE(srv.running());
}

TEST_CASE("vnc: a viewer that draws the pointer itself gets an arrow, once")
{
    // the rectangle itself: 12 x 18, hot spot at the tip, black edge, white inside
    std::vector<uint8_t> r;
    vnc_cursor(VncPixelFormat(), r);
    REQUIRE(r.size() == 12u + 12 * 18 * 4 + 2 * 18);
    CHECK(std::vector<int>(r.begin(), r.begin() + 12) == std::vector<int>{ 0, 0, 0, 0, 0, 12, 0, 18, 0xFF, 0xFF, 0xFF, 0x11 });
    const uint8_t* px = r.data() + 12;
    const uint8_t* mask = px + 12 * 18 * 4;
    CHECK(mask[0] == 0x80);                                      // row 0: the tip only
    CHECK(mask[1] == 0x00);
    CHECK(mask[2 * 4] == 0xF8);                                  // row 4: X...X
    CHECK(px[0] == 0);                                           // the tip is black
    CHECK(px[(4 * 12 + 2) * 4] == 255);                          // inside is white
    std::vector<uint8_t> r16;
    VncPixelFormat f16;
    f16.bpp = 16; f16.depth = 16; f16.r_max = 31; f16.g_max = 63; f16.b_max = 31; f16.r_shift = 11; f16.g_shift = 5; f16.b_shift = 0;
    vnc_cursor(f16, r16);
    CHECK(r16.size() == 12u + 12 * 18 * 2 + 2 * 18);

    Runtime rt;
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    VncServer srv;
    const json cfg = remote();
    REQUIRE(srv.start(&cfg, &kDevice, disp, "Test HMI"));
    Client c;
    std::vector<Client::Rect> first, second;
    with_client(rt, [&] {
        if(!c.connect_to(srv.port()) || c.login("secret12") != 0) { c.failed = true; return; }
        Client::Init init = c.init();
        c.wr({ 2, 0, 0, 2, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x11 });   // SetEncodings: raw, Cursor
        c.request(false, init.w, init.h);
        first = c.update();
        c.request(false, init.w, init.h);
        second = c.update();
    });
    CHECK_FALSE(c.failed);
    REQUIRE(first.size() == 2);
    CHECK_FALSE(first[0].cursor);
    CHECK(first[1].cursor);
    CHECK(first[1].w == 12);
    CHECK(first[1].h == 18);
    CHECK(first[1].mask.size() == 36);
    REQUIRE(second.size() == 1);                                 // not again
    CHECK_FALSE(second[0].cursor);
}

TEST_CASE("vnc: a wrong password is not let in; older protocol versions; other pixel formats")
{
    Runtime rt;
    rt.tag("clicks", TagType::Int);
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    const int W = lv_display_get_horizontal_resolution(disp), H = lv_display_get_vertical_resolution(disp);
    VncServer srv;
    const json cfg2 = remote();
    REQUIRE(srv.start(&cfg2, &kDevice, disp, "T"));

    int wrong = -1, v33 = -1, v37 = -1;
    std::string reason;
    uint8_t offered = 0;
    std::vector<Client::Rect> u16bit;
    bool closed_on_map = false;
    with_client(rt, [&] {
        { Client c; if(c.connect_to(srv.port())) wrong = c.login("nope", "RFB 003.008\n", &reason, &offered); }
        { Client c; if(c.connect_to(srv.port())) { v33 = c.login("secret12", "RFB 003.003\n"); if(v33 == 0) c.init(); } }
        {   // 3.7, then 16 bits per pixel
            Client c;
            if(!c.connect_to(srv.port())) return;
            v37 = c.login("secret12", "RFB 003.007\n");
            if(v37 != 0) return;
            Client::Init init = c.init();
            VncPixelFormat f; f.bpp = 16; f.depth = 16; f.r_max = 31; f.g_max = 63; f.b_max = 31; f.r_shift = 11; f.g_shift = 5; f.b_shift = 0;
            Bytes m{ 0, 0, 0, 0 };
            uint8_t raw[16]; f.write(raw);
            m.insert(m.end(), raw, raw + 16);
            c.wr(m);
            c.wr({ 2, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1 });                 // SetEncodings: raw, copyrect
            c.wr({ 4, 1, 0, 0, 0, 0, 0, 0x41 });                         // a key (ignored)
            c.wr({ 6, 0, 0, 0, 0, 0, 0, 3, 'a', 'b', 'c' });             // cut text (ignored)
            c.request(false, init.w, init.h);
            u16bit = c.update(2);
        }
        {   // a colour map is not offered: the connection ends
            Client c;
            if(!c.connect_to(srv.port()) || c.login("secret12") != 0) return;
            c.init();
            VncPixelFormat f; f.bpp = 8; f.depth = 8; f.true_colour = 0;
            Bytes m{ 0, 0, 0, 0 };
            uint8_t raw[16]; f.write(raw);
            m.insert(m.end(), raw, raw + 16);
            c.wr(m);
            uint8_t b;
            closed_on_map = !c.rd(&b, 1, 3000);
        }
    });
    CHECK(wrong == 1);
    CHECK(offered == 2);                                          // VNC authentication, nothing else
    CHECK(reason.find("Wrong password") != std::string::npos);
    CHECK(v33 == 0);
    CHECK(v37 == 0);
    REQUIRE(u16bit.size() == 1);
    REQUIRE(u16bit[0].pixels.size() == static_cast<size_t>(W) * H * 2);
    const uint8_t* p = u16bit[0].pixels.data() + (static_cast<size_t>(20) * W + 20) * 2;
    CHECK((p[0] | (p[1] << 8)) == 0xF800);                        // the red button in 5-6-5
    CHECK(closed_on_map);
    CHECK(tags().find("clicks")->get_number() == 0);
}

TEST_CASE("vnc: view only; without a password only this machine; off by default")
{
    Runtime rt;
    rt.tag("clicks", TagType::Int);
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    {
        VncServer off;
        CHECK_FALSE(off.start(nullptr, nullptr, disp, "T"));
        json no = json::parse(R"({ "port": 0 })");
        CHECK_FALSE(off.start(&no, nullptr, disp, "T"));
        json disabled = json::parse(R"({ "enabled": false, "port": 0 })");
        CHECK_FALSE(off.start(&disabled, &kDevice, disp, "T"));      // the device cannot turn on what the project has off
        CHECK(off.port() == 0);
    }
    VncServer srv;
    const json cfg3 = remote(", \"view_only\": true");
    REQUIRE(srv.start(&cfg3, nullptr, disp, "T"));
    uint8_t offered = 0;
    int result = -1;
    std::vector<Client::Rect> first;
    with_client(rt, [&] {
        Client c;
        if(!c.connect_to(srv.port())) return;
        result = c.login("", "RFB 003.008\n", nullptr, &offered);
        if(result != 0) return;
        Client::Init init = c.init();
        c.request(false, init.w, init.h);
        first = c.update();
        c.pointer(true, 20, 20);
        sleep_ms(250);
        c.pointer(false, 20, 20);
        sleep_ms(250);
    });
    CHECK(offered == 1);                                          // no password: "None" (and only from 127.0.0.1)
    CHECK(result == 0);
    CHECK(first.size() == 1);
    CHECK(tags().find("clicks")->get_number() == 0);              // the pointer of the viewer does nothing
}

// ================================================================ security: a project somebody else made

TEST_CASE("vnc: a password in the project opens nothing - who may connect is set on the device")
{
    // the pieces: what the project says, what the device says
    {
        const json proj = json::parse(R"({ "enabled": true, "port": 5901, "password": "attacker", "view_only": false })");
        VncSettings s = vnc_settings(&proj, nullptr);
        CHECK(s.enabled);
        CHECK(s.port == 5901);
        CHECK(s.password.empty());                                // the project's password is not used ...
        CHECK(s.project_password);                                // ... and that is said
        const json dev = json::parse(R"({ "password": "device-pw", "view_only": true })");
        s = vnc_settings(&proj, &dev);
        CHECK(s.password == "device-pw");
        CHECK(s.view_only);                                       // the device can narrow it
        const json off = json::parse(R"({ "enabled": false, "password": "device-pw" })");
        CHECK_FALSE(vnc_settings(&proj, &off).enabled);           // or turn it off
        const json ro = json::parse(R"({ "enabled": true, "view_only": true })"), wide = json::parse(R"({ "view_only": false, "enabled": true })");
        CHECK(vnc_settings(&ro, &wide).view_only);                // it cannot be widened from the device's file
        const json none = json::parse(R"({ "port": 5900 })");
        CHECK_FALSE(vnc_settings(&none, &wide).enabled);          // nor turned on when the project does not ask for it
        CHECK_FALSE(vnc_settings(nullptr, &dev).enabled);
    }

    Runtime rt;
    rt.tag("clicks", TagType::Int);
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    VncServer srv;
    const json evil = remote(", \"password\": \"attacker\"");
    REQUIRE(srv.start(&evil, nullptr, disp, "T"));
    CHECK_FALSE(srv.open_to_network());
    if(has_network()) CHECK_FALSE(reachable_from_network(srv.port()));   // it listens on 127.0.0.1 only
    uint8_t offered = 0;
    int result = -1;
    with_client(rt, [&] {
        Client c;
        if(!c.connect_to(srv.port())) return;
        result = c.login("attacker", "RFB 003.008\n", nullptr, &offered);
    });
    CHECK(offered == 1);                                          // "None": no password is asked for, from this machine only
    CHECK(result == 0);

    // with the device's password: other machines can connect, and only that password works
    REQUIRE(srv.start(&evil, &kDevice, disp, "T"));
    CHECK(srv.open_to_network());
    if(has_network()) CHECK(reachable_from_network(srv.port()));
    int with_project_pw = -1, with_device_pw = -1;
    with_client(rt, [&] {
        { Client c; if(c.connect_to(srv.port())) with_project_pw = c.login("attacker"); }
        { Client c; if(c.connect_to(srv.port())) with_device_pw = c.login("secret12"); }
    });
    CHECK(with_project_pw != 0);
    CHECK(with_device_pw == 0);
}

TEST_CASE("vnc: the settings made on the device are kept in project.local.json and take effect at once")
{
    namespace fs = std::filesystem;
    Runtime rt;
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    const fs::path dir = fs::temp_directory_path() / ("hml_vnc_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path file = dir / "project.local.json";
    const std::string store = file.generic_string();
    auto stored = [&] {
        std::ifstream f(file, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return json::parse(ss.str(), nullptr, false, true);
    };
    { std::ofstream f(file, std::ios::binary); f << R"({ "sound": { "volume": 40 } })"; }

    VncServer srv;
    const json cfg = remote();
    REQUIRE(srv.start(&cfg, nullptr, disp, "T", store));
    CHECK_FALSE(srv.open_to_network());

    CHECK(srv.set_local(true, "abc", false).find("at least 6") != std::string::npos);
    CHECK_FALSE(srv.set_local(true, "with space", false).empty());
    CHECK_FALSE(srv.open_to_network());
    CHECK_FALSE(stored().contains("remote"));

    CHECK(srv.set_local(true, "device12", false) == "");
    CHECK(srv.running());
    CHECK(srv.open_to_network());
    CHECK(stored()["remote"]["password"] == "device12");
    CHECK(stored()["sound"]["volume"] == 40);                     // what else is in the file stays
    int ok = -1, bad = -1;
    with_client(rt, [&] {
        { Client c; if(c.connect_to(srv.port())) ok = c.login("device12"); }
        { Client c; if(c.connect_to(srv.port())) bad = c.login("secret12"); }
    });
    CHECK(ok == 0);
    CHECK(bad != 0);

    // an empty field leaves the password; view only and off
    CHECK(srv.set_local(true, "", true) == "");
    CHECK(srv.settings().password == "device12");
    CHECK(srv.settings().view_only);
    CHECK(stored()["remote"]["view_only"] == true);
    CHECK(srv.set_local(false, "", false) == "");
    CHECK_FALSE(srv.running());
    CHECK(srv.port() == 0);
    CHECK(stored()["remote"]["enabled"] == false);
    CHECK(stored()["remote"]["password"] == "device12");
    CHECK_FALSE(stored()["remote"].contains("view_only"));

    // back on, the password removed: this machine only, nothing of it left in the file
    CHECK(srv.set_local(true, "", false, true) == "");
    CHECK(srv.running());
    CHECK_FALSE(srv.open_to_network());
    CHECK_FALSE(stored().contains("remote"));
    CHECK(stored()["sound"]["volume"] == 40);

    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ================================================================ reliability / security: a hostile viewer

TEST_CASE("vnc: garbage from a viewer, before and after the login, never takes the server down")
{
    Runtime rt;
    rt.tag("clicks", TagType::Int);
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    VncServer srv;
    const json cfg = remote();
    REQUIRE(srv.start(&cfg, &kDevice, disp, "Test HMI"));

    // a well-behaved viewer gets in and gets the picture (the server may still be letting the last one go)
    auto alive = [&] {
        bool ok = false;
        with_client(rt, [&] {
            for(int i = 0; i < 50 && !ok; i++) {
                Client c;
                if(c.connect_to(srv.port()) && c.login("secret12") == 0) {
                    Client::Init init = c.init();
                    c.request(false, init.w, init.h);
                    ok = c.update().size() == 1 && !c.failed;
                }
                if(!ok) sleep_ms(100);
            }
        });
        return ok;
    };
    // how much the server sends to a viewer that says this (it must never be a picture)
    auto bytes_back = [&](const std::function<void(Client&)>& say) {
        size_t n = 0;
        with_client(rt, [&] {
            Client c;
            if(!c.connect_to(srv.port())) return;
            say(c);
            uint8_t buf[4096];
            for(;;) {
                fd_set set; FD_ZERO(&set); FD_SET(c.s, &set);
                timeval tv{ 0, 300 * 1000 };
                if(select(static_cast<int>(c.s + 1), &set, nullptr, nullptr, &tv) <= 0) break;
                const int r = static_cast<int>(recv(c.s, reinterpret_cast<char*>(buf), sizeof(buf), 0));
                if(r <= 0) break;
                n += static_cast<size_t>(r);
            }
        });
        return n;
    };
    std::mt19937 rng(20261121);
    auto noise = [&](size_t n) { Bytes b(n); for(uint8_t& x : b) x = static_cast<uint8_t>(rng()); return b; };
    REQUIRE(alive());

    // ---- before the login: nothing of the screen is given away
    CHECK(bytes_back([&](Client& c) { c.wr(noise(12)); }) < 64);
    CHECK(bytes_back([&](Client& c) { c.wr(noise(3000)); }) < 64);
    CHECK(bytes_back([](Client&) {}) < 64);                                             // says nothing
    CHECK(bytes_back([](Client& c) { c.wr(Bytes{ 'R', 'F', 'B' }); }) < 64);              // half a version
    CHECK(bytes_back([](Client& c) {                                                     // "no authentication, please"
        c.rd(12);
        const char* v = "RFB 003.008\n";
        c.wr(Bytes(v, v + 12));
        c.rd(2);
        c.wr({ 1 });
        c.wr({ 1 });                                                                     // ClientInit
        c.wr({ 3, 0, 0, 0, 0, 0, 0x03, 0x20, 0x01, 0xE0 });                              // "send me the screen"
    }) < 64);
    CHECK(alive());

    // ---- after the login: broken messages end that connection, nothing else
    auto after_login = [&](const Bytes& msg) {
        with_client(rt, [&] {
            for(int i = 0; i < 50; i++) {
                Client c;
                if(c.connect_to(srv.port()) && c.login("secret12") == 0) {
                    c.init();
                    c.wr(msg);
                    sleep_ms(150);
                    return;
                }
                sleep_ms(100);
            }
        });
    };
    after_login({ 2, 0, 0xFF, 0xFF });                                        // SetEncodings: 65535 entries, none follow
    CHECK(alive());
    after_login({ 6, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF });                      // ClientCutText: 4 GB
    CHECK(alive());
    after_login({ 200 });                                                     // no such message
    CHECK(alive());
    after_login({ 0, 0, 0, 0, 24, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0 });   // 24 bits per pixel: not offered
    CHECK(alive());
    after_login({ 0, 0, 0, 0, 32, 24, 0, 1, 0, 0, 0, 0, 0, 0, 200, 200, 200, 0, 0, 0 });    // colours of no bits, shifted out of the pixel
    CHECK(alive());
    after_login({ 3 });                                                       // a request cut short
    CHECK(alive());
    after_login({ 5, 1, 0xFF, 0xFF, 0xFF, 0xFF });                            // a touch far outside the screen
    CHECK(alive());
    for(int i = 0; i < 6; i++) { after_login(noise(4000)); }
    CHECK(alive());
    CHECK(tags().find("clicks")->get_number() >= 0);
    srv.stop();
}

// ================================================================ performance efficiency

TEST_CASE("vnc: taking the picture and finding what changed is quick")
{
    Runtime rt;
    rt.tag("clicks", TagType::Int);
    show_test_screen();
    rt.advance(50);
    lv_display_t* disp = lv_display_get_default();
    std::vector<uint8_t> a, b, out;
    int w = 0, h = 0;
    const int rounds = 20;
    uint32_t t0 = millis();
    for(int i = 0; i < rounds; i++) REQUIRE(capture_display(disp, a, w, h));
    const double capture_ms = static_cast<double>(millis() - t0) / rounds;
    b = a;
    for(uint8_t& x : b) x = static_cast<uint8_t>(~x);                           // every pixel another one
    t0 = millis();
    size_t rects = 0;
    for(int i = 0; i < rounds; i++) rects += vnc_diff(a.data(), b.data(), w, h).size() + vnc_diff(a.data(), a.data(), w, h).size();
    const double diff_ms = static_cast<double>(millis() - t0) / (rounds * 2);
    VncPixelFormat f16;
    f16.bpp = 16; f16.depth = 16; f16.r_max = 31; f16.g_max = 63; f16.b_max = 31; f16.r_shift = 11; f16.g_shift = 5; f16.b_shift = 0;
    t0 = millis();
    for(int i = 0; i < rounds; i++) { out.clear(); vnc_convert(a.data(), static_cast<size_t>(w) * h, VncPixelFormat(), out); out.clear(); vnc_convert(a.data(), static_cast<size_t>(w) * h, f16, out); }
    const double convert_ms = static_cast<double>(millis() - t0) / (rounds * 2);
    MESSAGE(w << " x " << h << ": capture " << capture_ms << " ms, diff " << diff_ms << " ms, convert a whole frame " << convert_ms << " ms");
    CHECK(rects == static_cast<size_t>(rounds) * static_cast<size_t>((h + 31) / 32));   // every row of tiles, nothing for the same picture
    // (loose: a debug build on a busy CI machine. The numbers of a release build are in the README.)
    CHECK(capture_ms < 200);
    CHECK(diff_ms < 50);
    CHECK(convert_ms < 100);
}
