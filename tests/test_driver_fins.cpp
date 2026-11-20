// FINS (OMRON) client: frames and addresses byte by byte (fins.hpp), and the driver end to end
// against an in-process fake FINS/TCP server. Grouped by ISO/IEC 25010 characteristic.
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
#include "fins.hpp"
#include "lvgl_fixture.hpp"
#include "util.hpp"

#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------- fake FINS/TCP server

class FakeFinsServer {
public:
    FakeFinsServer()
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listen_ = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_ != kBadSock);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(listen(listen_, 1) == 0);
        socklen_t len = sizeof(addr);
        REQUIRE(getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }
    ~FakeFinsServer()
    {
        running_ = false;
        if(thread_.joinable()) thread_.join();
        close_sock(listen_);
    }
    uint16_t port() const { return port_; }

    // memory: (word area code, word number) -> word. A bit area code maps onto its word area.
    std::map<std::pair<uint8_t, uint16_t>, uint16_t> mem;
    std::mutex mutex;
    uint16_t& word(uint8_t code, uint16_t n) { return mem[{ code, n }]; }

    std::atomic<int> reads{ 0 }, writes{ 0 }, connections{ 0 };
    std::atomic<uint16_t> end_code{ 0 };            // answer every command with this end code (main << 8 | sub)
    std::atomic<uint32_t> refuse{ 0 };              // answer the node address request with this error
    std::atomic<uint8_t> flags_main{ 0 }, flags_sub{ 0 };   // or-ed into the end code bytes (relay / CPU error flags)
    uint8_t client_node = 0x21, server_node = 0x0A;
    // replace the whole FINS frame of an answer (to send something broken); empty = normal answer
    std::function<Bytes(const Bytes& frame)> override_answer;
    std::vector<Bytes> frames;                      // every command frame seen (guarded by mutex)

private:
    static bool recv_all(sock_t s, uint8_t* b, int n)
    {
        while(n > 0) { int r = static_cast<int>(recv(s, reinterpret_cast<char*>(b), n, 0)); if(r <= 0) return false; b += r; n -= r; }
        return true;
    }
    static bool wait_readable(sock_t s, int ms)
    {
        fd_set set; FD_ZERO(&set); FD_SET(s, &set);
        timeval tv{ 0, ms * 1000 };
        return select(static_cast<int>(s + 1), &set, nullptr, nullptr, &tv) > 0;
    }
    static void put32(Bytes& b, uint32_t v) { for(int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s)); }
    static uint32_t get32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]; }
    bool send_tcp(sock_t c, uint32_t command, uint32_t error, const Bytes& data)
    {
        Bytes b{ 'F', 'I', 'N', 'S' };
        put32(b, static_cast<uint32_t>(8 + data.size())); put32(b, command); put32(b, error);
        b.insert(b.end(), data.begin(), data.end());
        return send(c, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0) == static_cast<int>(b.size());
    }
    void run()
    {
        while(running_) {
            if(!wait_readable(listen_, 20)) continue;
            sock_t c = accept(listen_, nullptr, nullptr);
            if(c == kBadSock) continue;
            connections++;
            serve(c);
            close_sock(c);
        }
    }
    void serve(sock_t c)
    {
        while(running_) {
            if(!wait_readable(c, 20)) continue;
            uint8_t h[16];
            if(!recv_all(c, h, 16)) return;
            if(std::memcmp(h, "FINS", 4) != 0) return;
            const uint32_t len = get32(h + 4), command = get32(h + 8);
            if(len < 8 || len > 4000) return;
            Bytes body(len - 8);
            if(!body.empty() && !recv_all(c, body.data(), static_cast<int>(body.size()))) return;
            if(command == 0) {                                  // the node address request
                if(refuse) { send_tcp(c, 3, refuse, {}); return; }
                Bytes d;
                put32(d, client_node); put32(d, server_node);
                if(!send_tcp(c, 1, 0, d)) return;
                continue;
            }
            if(command != 2 || body.size() < 12) return;
            Bytes ans;
            {
                std::lock_guard<std::mutex> g(mutex);
                frames.push_back(body);
                if(override_answer) ans = override_answer(body);
                if(ans.empty()) ans = answer(body);
            }
            if(ans.size() == 1 && ans[0] == 0xEE) return;      // "close the connection"
            if(ans.size() == 1 && ans[0] == 0xAA) continue;     // "say nothing"
            if(!send_tcp(c, 2, 0, ans)) return;
        }
    }
    static uint8_t word_area(uint8_t code)                     // a bit area code -> its word area
    {
        if(code >= 0x30 && code <= 0x33) return static_cast<uint8_t>(code + 0x80);
        if(code == 0x02) return 0x82;
        if(code >= 0x20 && code <= 0x2C) return static_cast<uint8_t>(code + 0x80);
        return code;
    }
    Bytes answer(const Bytes& f)
    {
        // ICF RSV GCT DNA DA1 DA2 SNA SA1 SA2 SID MRC SRC | area addr(2) bit count(2) data
        Bytes r{ 0xC0, 0x00, 0x02, f[6], f[7], f[8], f[3], f[4], f[5], f[9], f[10], f[11] };
        uint16_t end = end_code;
        Bytes data;
        const bool area_cmd = f[10] == 0x01 && (f[11] == 0x01 || f[11] == 0x02) && f.size() >= 18;
        if(end == 0 && !area_cmd) end = 0x0401;
        if(end == 0) {
            const uint8_t code = f[12];
            const uint16_t num = static_cast<uint16_t>((f[13] << 8) | f[14]);
            const uint8_t bit = f[15];
            const uint16_t count = static_cast<uint16_t>((f[16] << 8) | f[17]);
            const bool bits = word_area(code) != code;
            if(f[11] == 0x01) {
                reads++;
                for(uint16_t i = 0; i < count; i++) {
                    if(bits) data.push_back(static_cast<uint8_t>((word(word_area(code), static_cast<uint16_t>(num + (bit + i) / 16)) >> ((bit + i) % 16)) & 1));
                    else { const uint16_t w = word(code, static_cast<uint16_t>(num + i)); data.push_back(static_cast<uint8_t>(w >> 8)); data.push_back(static_cast<uint8_t>(w)); }
                }
            }
            else {
                writes++;
                if(f.size() != 18u + (bits ? count : count * 2u)) end = 0x1003;
                else for(uint16_t i = 0; i < count; i++) {
                    if(bits) {
                        uint16_t& w = word(word_area(code), static_cast<uint16_t>(num + (bit + i) / 16));
                        const uint16_t m = static_cast<uint16_t>(1u << ((bit + i) % 16));
                        w = f[18 + i] ? static_cast<uint16_t>(w | m) : static_cast<uint16_t>(w & ~m);
                    }
                    else word(code, static_cast<uint16_t>(num + i)) = static_cast<uint16_t>((f[18 + i * 2] << 8) | f[19 + i * 2]);
                }
            }
        }
        r.push_back(static_cast<uint8_t>((end >> 8) | flags_main));
        r.push_back(static_cast<uint8_t>((end & 0xFF) | flags_sub));
        r.insert(r.end(), data.begin(), data.end());
        return r;
    }

    sock_t listen_ = kBadSock;
    uint16_t port_ = 0;
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

template <class Pred>
bool wait_for(Pred pred, uint32_t timeout_ms = 3000)
{
    uint32_t t0 = millis();
    for(;;) {
        tags().dispatch(false);
        if(pred()) return true;
        if(millis() - t0 > timeout_ms) return false;
        sleep_ms(5);
    }
}
double tagv(const char* n) { return tags().find(n)->get_number(); }
std::string tags_str(const char* n) { return tags().find(n)->get().format(); }

// the distinct memory area reads (area code, number, count) the server saw
std::set<std::tuple<uint8_t, uint16_t, uint16_t>> read_targets(FakeFinsServer& srv)
{
    std::lock_guard<std::mutex> g(srv.mutex);
    std::set<std::tuple<uint8_t, uint16_t, uint16_t>> out;
    for(const Bytes& f : srv.frames) {
        if(f.size() < 18 || f[10] != 0x01 || f[11] != 0x01) continue;
        out.insert({ f[12], static_cast<uint16_t>((f[13] << 8) | f[14]), static_cast<uint16_t>((f[16] << 8) | f[17]) });
    }
    return out;
}

json fins_cfg(const FakeFinsServer& srv, const json& map)
{
    return json{ { "type", "fins" }, { "host", "127.0.0.1" }, { "port", srv.port() }, { "poll_ms", 20 }, { "timeout_ms", 400 },
                 { "status_tag", "online" }, { "map", map } };
}
int start(Runtime& rt, const json& cfg) { json arr = json::array({ cfg }); return rt.drivers.load(&arr); }

constexpr uint8_t kCIO = 0xB0, kW = 0xB1, kH = 0xB2, kA = 0xB3, kD = 0x82, kTC = 0x89;

} // namespace

// ================================================================ functional suitability: frames

TEST_CASE("fins: addresses as CX-Programmer writes them")
{
    auto a = fins_parse_address("D100");
    REQUIRE(a);
    CHECK(a->area == "D");
    CHECK(a->word_code == 0x82);
    CHECK(a->bit_code == 0x02);
    CHECK(a->number == 100);
    CHECK(a->bit == -1);
    CHECK(fins_parse_address("dm 100")->word_code == 0x82);           // DM = D, any case, blanks
    CHECK(fins_format_address(*fins_parse_address("DM0100")) == "D100");
    CHECK(fins_parse_address("CIO10")->word_code == 0xB0);
    CHECK(fins_parse_address("10")->word_code == 0xB0);               // a bare number is CIO
    CHECK(fins_format_address(*fins_parse_address("10.5")) == "CIO10.05");
    CHECK(fins_parse_address("W5")->word_code == 0xB1);
    CHECK(fins_parse_address("H7")->word_code == 0xB2);
    auto aux = fins_parse_address("A440");
    CHECK(aux->word_code == 0xB3);
    CHECK(aux->system);
    auto b = fins_parse_address("W5.15");
    REQUIRE(b);
    CHECK(b->bit == 15);
    CHECK(b->bit_code == 0x31);
    CHECK(fins_format_address(*b) == "W5.15");
    // EM banks: 0..C have one run of codes, D..18 another
    CHECK(fins_parse_address("E0_100")->word_code == 0xA0);
    CHECK(fins_parse_address("EC_0")->word_code == 0xAC);
    CHECK(fins_parse_address("ED_0")->word_code == 0x60);
    CHECK(fins_parse_address("E18_0")->word_code == 0x6B);
    CHECK(fins_parse_address("E1_5.03")->bit_code == 0x21);
    CHECK(fins_parse_address("EM2_7")->word_code == 0xA2);
    CHECK(fins_format_address(*fins_parse_address("e1_050")) == "E1_50");
    // timers and counters share an area: the counters start at 0x8000
    CHECK(fins_parse_address("T3")->number == 3);
    CHECK(fins_parse_address("C4")->number == 0x8004);
    CHECK(fins_parse_address("C4")->word_code == 0x89);
    CHECK(fins_format_address(*fins_parse_address("C4")) == "C4");

    std::string err;
    CHECK_FALSE(fins_parse_address("", &err));
    CHECK_FALSE(fins_parse_address("Q10", &err));
    CHECK(err.find("unknown area") != std::string::npos);
    CHECK_FALSE(fins_parse_address("D1A", &err));
    CHECK(err.find("decimal") != std::string::npos);
    CHECK_FALSE(fins_parse_address("D65536", &err));
    CHECK(fins_parse_address("D65535"));
    CHECK_FALSE(fins_parse_address("T32768", &err));
    CHECK_FALSE(fins_parse_address("D10.16", &err));                  // bits are 00..15
    CHECK_FALSE(fins_parse_address("D10.", &err));
    CHECK_FALSE(fins_parse_address("T1.00", &err));                   // a timer value has no bits
    CHECK_FALSE(fins_parse_address("E19_0", &err));
    CHECK_FALSE(fins_parse_address("E_0", &err));
    CHECK_FALSE(fins_parse_address("D", &err));
    CHECK_FALSE(fins_parse_address("D-1", &err));
    CHECK_FALSE(fins_parse_address(std::string(40, '1'), &err));
}

TEST_CASE("fins: FINS/TCP and command frames byte by byte")
{
    CHECK(fins_tcp_handshake(0) == Bytes{ 'F', 'I', 'N', 'S', 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    CHECK(fins_tcp_handshake(0x21).back() == 0x21);

    // memory area read of D100, 2 words, from node 0x21 to node 0x0A, service id 7
    const Bytes frame = fins_frame({ 0, 0x0A, 0 }, { 0, 0x21, 0 }, 7, 0x01, 0x01, fins_area_data(0x82, 100, 0, 2));
    CHECK(frame == Bytes{ 0x80, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00, 0x21, 0x00, 0x07, 0x01, 0x01, 0x82, 0x00, 0x64, 0x00, 0x00, 0x02 });
    const Bytes tcp = fins_tcp_wrap(frame);
    REQUIRE(tcp.size() == 16 + frame.size());
    CHECK(Bytes(tcp.begin(), tcp.begin() + 16) == Bytes{ 'F', 'I', 'N', 'S', 0, 0, 0, 26, 0, 0, 0, 2, 0, 0, 0, 0 });
    uint32_t command = 9, error = 9;
    CHECK(fins_tcp_body_len(tcp.data(), 100, &command, &error) == static_cast<int>(frame.size()));
    CHECK(command == 2);
    CHECK(error == 0);
    // a bit: the bit area code and the bit number
    CHECK(fins_area_data(0x31, 5, 15, 1) == Bytes{ 0x31, 0x00, 0x05, 0x0F, 0x00, 0x01 });

    // the answer to the node address request
    const uint8_t hs[] = { 0, 0, 0, 0x21, 0, 0, 0, 0x0A };
    uint8_t c = 0, s = 0;
    CHECK(fins_tcp_parse_handshake(hs, 8, &c, &s));
    CHECK(c == 0x21);
    CHECK(s == 0x0A);
    CHECK_FALSE(fins_tcp_parse_handshake(hs, 7, &c, &s));
    const uint8_t hs0[] = { 0, 0, 0, 0, 0, 0, 0, 0x0A };              // no node address for us
    CHECK_FALSE(fins_tcp_parse_handshake(hs0, 8, &c, &s));

    // a response: the flags in the end code are not part of the code
    const Bytes resp{ 0xC0, 0x00, 0x02, 0x00, 0x21, 0x00, 0x00, 0x0A, 0x00, 0x07, 0x01, 0x01, 0x80 | 0x11, 0x40 | 0x03, 0x12, 0x34 };
    FinsResponse r;
    REQUIRE(fins_parse_response(resp.data(), static_cast<int>(resp.size()), r));
    CHECK(r.sid == 7);
    CHECK(r.end_code == 0x1103);
    CHECK(r.data == Bytes{ 0x12, 0x34 });
    CHECK(fins_end_code_text(0x1103).find("1103") == 0);
    CHECK(fins_end_code_text(0x1103).find("out of range") != std::string::npos);
    CHECK(fins_end_code_text(0x7777).find("7777") == 0);
    CHECK(fins_tcp_error_text(0x21).find("already connected") != std::string::npos);

    CHECK(fins_from_bcd(0x1234) == 1234);
    CHECK(fins_from_bcd(0x99999999) == 99999999);
    bool ok = true;
    fins_from_bcd(0x12A4, &ok);
    CHECK_FALSE(ok);
    CHECK(fins_to_bcd(987) == 0x0987);
    CHECK(fins_to_bcd(0) == 0);
}

TEST_CASE("fins: broken, foreign and oversized answers are refused")
{
    uint8_t h[16] = { 'F', 'I', 'N', 'S', 0, 0, 0, 8, 0, 0, 0, 2, 0, 0, 0, 0 };
    CHECK(fins_tcp_body_len(h, 100, nullptr, nullptr) == 0);
    h[0] = 'G';
    CHECK(fins_tcp_body_len(h, 100, nullptr, nullptr) == -1);        // not FINS/TCP
    h[0] = 'F'; h[7] = 7;
    CHECK(fins_tcp_body_len(h, 100, nullptr, nullptr) == -1);        // shorter than the header itself
    h[7] = 110;
    CHECK(fins_tcp_body_len(h, 100, nullptr, nullptr) == -1);        // more than we will take
    h[4] = 0xFF;
    CHECK(fins_tcp_body_len(h, 100, nullptr, nullptr) == -1);

    FinsResponse r;
    std::string err;
    const Bytes shortf{ 0xC0, 0, 2, 0, 1, 0, 0, 2, 0, 1, 1, 1, 0 };
    CHECK_FALSE(fins_parse_response(shortf.data(), static_cast<int>(shortf.size()), r, &err));
    const Bytes cmd{ 0x80, 0, 2, 0, 1, 0, 0, 2, 0, 1, 1, 1, 0, 0 };  // a command, not a response
    CHECK_FALSE(fins_parse_response(cmd.data(), static_cast<int>(cmd.size()), r, &err));
}

// ================================================================ functional suitability: the driver

TEST_CASE("fins: reads every area and data type, in few requests")
{
    Runtime rt;
    FakeFinsServer srv;
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        srv.word(kD, 100) = static_cast<uint16_t>(-1234);
        srv.word(kD, 102) = 0x86A0; srv.word(kD, 103) = 0x0001;                     // 100000, the low word first
        srv.word(kD, 104) = 0x0001; srv.word(kD, 105) = 0x86A0;                     // the same, swapped
        srv.word(kD, 110) = 0x0000; srv.word(kD, 111) = 0x4148;                     // 12.5
        srv.word(kD, 112) = 0x1234;                                                // BCD
        srv.word(kD, 113) = 0x5678; srv.word(kD, 114) = 0x1234;                     // BCD 12345678
        srv.word(kD, 115) = 250;                                                   // scaled
        srv.word(kD, 120) = 0x0008;
        srv.word(kD, 200) = 0x4845; srv.word(kD, 201) = 0x4C4C; srv.word(kD, 202) = 0x4F00;   // "HELLO", the high byte first
        srv.word(kCIO, 10) = 0x8001;
        srv.word(kW, 5) = 0x0002;
        srv.word(kH, 7) = 40000;
        srv.word(kA, 440) = 77;
        srv.word(0xA1, 50) = 321;                                                  // EM bank 1
        srv.word(kTC, 3) = 55;
        srv.word(kTC, 0x8004) = 66;
    }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "speed", "total", "swapped", "bcd", "bcd32", "h", "aux", "e", "t", "c" }) rt.tag(n, TagType::Int);
    rt.tag("temp", TagType::Float);
    rt.tag("scaled", TagType::Float);
    for(const char* n : { "ready", "c0", "c15", "c1", "w1" }) rt.tag(n, TagType::Bool);
    rt.tag("name", std::string(""));
    json map = json::parse(R"([
        { "tag": "speed", "addr": "D100" },
        { "tag": "total", "addr": "D102", "dtype": "int32" },
        { "tag": "swapped", "addr": "D104", "dtype": "int32", "swap_words": true },
        { "tag": "temp", "addr": "D110", "dtype": "float" },
        { "tag": "bcd", "addr": "D112", "dtype": "bcd16" },
        { "tag": "bcd32", "addr": "D113", "dtype": "bcd32" },
        { "tag": "scaled", "addr": "D115", "scale": 0.1, "offset": -5 },
        { "tag": "ready", "addr": "D120.03" },
        { "tag": "name", "addr": "D200", "dtype": "string", "length": 8 },
        { "tag": "c0", "addr": "CIO10.00" }, { "tag": "c15", "addr": "10.15" }, { "tag": "c1", "addr": "CIO10.01" },
        { "tag": "w1", "addr": "W5.01" },
        { "tag": "h", "addr": "H7", "dtype": "uint16" },
        { "tag": "aux", "addr": "A440" },
        { "tag": "e", "addr": "E1_50" },
        { "tag": "t", "addr": "T3" }, { "tag": "c", "addr": "C4" }
    ])");
    REQUIRE(start(rt, fins_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("c") == 66 && tags_str("name") == "HELLO"; }));
    CHECK(tagv("speed") == -1234);
    CHECK(tagv("total") == 100000);
    CHECK(tagv("swapped") == 100000);
    CHECK(tagv("temp") == doctest::Approx(12.5));
    CHECK(tagv("bcd") == 1234);
    CHECK(tagv("bcd32") == 12345678);
    CHECK(tagv("scaled") == doctest::Approx(20));
    CHECK(tagv("ready") == 1);
    CHECK(tagv("c0") == 1);
    CHECK(tagv("c15") == 1);
    CHECK(tagv("c1") == 0);
    CHECK(tagv("w1") == 1);
    CHECK(tagv("h") == 40000);
    CHECK(tagv("aux") == 77);
    CHECK(tagv("e") == 321);
    CHECK(tagv("t") == 55);

    // D100..D120 in one read (gaps read through), D200.. in another; the bits of CIO10 in one word;
    // the timer and the counter in two (0x8000 words apart)
    const auto reads = read_targets(srv);
    CHECK(reads.count({ kD, 100, 21 }) == 1);
    CHECK(reads.count({ kD, 200, 4 }) == 1);
    CHECK(reads.count({ kCIO, 10, 1 }) == 1);
    CHECK(reads.count({ kTC, 3, 1 }) == 1);
    CHECK(reads.count({ kTC, 0x8004, 1 }) == 1);
    CHECK(reads.size() == 9);
    {   // the node addresses the server gave are used
        std::lock_guard<std::mutex> g(srv.mutex);
        REQUIRE(!srv.frames.empty());
        CHECK(srv.frames[0][4] == 0x0A);       // DA1: the server's node
        CHECK(srv.frames[0][7] == 0x21);       // SA1: ours
    }

    // a value that is not BCD leaves the tag as it is
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 112) = 0x12A4; srv.word(kD, 100) = 5; }
    REQUIRE(wait_for([] { return tagv("speed") == 5; }));
    CHECK(tagv("bcd") == 1234);
}

TEST_CASE("fins: writes words and single bits, write protection")
{
    Runtime rt;
    FakeFinsServer srv;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kCIO, 10) = 0x8001; srv.word(kA, 440) = 1; srv.word(kD, 900) = 9; }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "sp", "big", "bcd", "aux", "outside", "ro" }) rt.tag(n, TagType::Int);
    rt.tag("f", TagType::Float);
    rt.tag("c1", TagType::Bool);
    rt.tag("name", std::string(""));
    json map = json::parse(R"([
        { "tag": "sp", "addr": "D300" },
        { "tag": "big", "addr": "D302", "dtype": "int32" },
        { "tag": "f", "addr": "D304", "dtype": "float" },
        { "tag": "bcd", "addr": "D306", "dtype": "bcd16" },
        { "tag": "name", "addr": "D310", "dtype": "string", "length": 6 },
        { "tag": "c1", "addr": "CIO10.01" },
        { "tag": "aux", "addr": "A440" },
        { "tag": "outside", "addr": "D900" },
        { "tag": "ro", "addr": "D301", "access": "ro" }
    ])");
    json cfg = fins_cfg(srv, map);
    cfg["write_ranges"] = json::array({ "D300-D399", "CIO10-CIO10", "A0-A959" });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("aux") == 1; }));

    tags().find("sp")->set_number(4321, nullptr);
    tags().find("big")->set_number(-70000, nullptr);
    tags().find("f")->set_number(1.5, nullptr);
    tags().find("bcd")->set_number(987, nullptr);
    tags().find("name")->set_string("BYE", nullptr);
    tags().find("c1")->set_number(1, nullptr);
    tags().find("aux")->set_number(5, nullptr);            // the Auxiliary Area: not without allow_system_write
    tags().find("outside")->set_number(1, nullptr);        // not in write_ranges
    tags().find("ro")->set_number(1, nullptr);
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return srv.word(kCIO, 10) == 0x8003 && srv.word(kD, 310) == 0x4259; }));
    std::lock_guard<std::mutex> g(srv.mutex);
    CHECK(srv.word(kD, 300) == 4321);
    CHECK(srv.word(kD, 302) == 0xEE90);                    // -70000 = 0xFFFEEE90, the low word first
    CHECK(srv.word(kD, 303) == 0xFFFE);
    CHECK(srv.word(kD, 304) == 0x0000);
    CHECK(srv.word(kD, 305) == 0x3FC0);
    CHECK(srv.word(kD, 306) == 0x0987);
    CHECK(srv.word(kD, 311) == 0x4500);                    // "BYE": 'B' 'Y', 'E' 0
    CHECK(srv.word(kD, 312) == 0x0000);
    CHECK(srv.word(kA, 440) == 1);
    CHECK(srv.word(kD, 900) == 9);
    CHECK(srv.word(kD, 301) == 0);
    // the bit was written as one bit (bit area code 0x30, bit 1), not as a whole word
    bool bit_write = false;
    for(const Bytes& f : srv.frames) if(f.size() == 19 && f[11] == 0x02 && f[12] == 0x30 && f[14] == 10 && f[15] == 1 && f[18] == 1) bit_write = true;
    CHECK(bit_write);
}

TEST_CASE("fins: a value that does not fit into BCD is not written")
{
    Runtime rt;
    FakeFinsServer srv;
    rt.tag("online", TagType::Bool);
    rt.tag("bcd", TagType::Int);
    rt.tag("sp", TagType::Int);
    REQUIRE(start(rt, fins_cfg(srv, json::parse(R"([{ "tag": "bcd", "addr": "D10", "dtype": "bcd16" }, { "tag": "sp", "addr": "D11" }])"))) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 10) = 0x0042; }
    REQUIRE(wait_for([] { return tagv("bcd") == 42; }));
    tags().find("bcd")->set_number(10000, nullptr);
    tags().find("sp")->set_number(7, nullptr);
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return srv.word(kD, 11) == 7; }));
    std::lock_guard<std::mutex> g(srv.mutex);
    CHECK(srv.word(kD, 10) == 0x0042);
}

// ================================================================ reliability

TEST_CASE("fins: end codes, silence, garbage and a dropped connection")
{
    Runtime rt;
    FakeFinsServer srv;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 0) = 1; }
    rt.tag("online", TagType::Bool);
    rt.tag("v", TagType::Int);
    REQUIRE(start(rt, fins_cfg(srv, json::parse(R"([{ "tag": "v", "addr": "D0" }])"))) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("v") == 1; }));

    // an end code: the connection stays, the value stays
    srv.end_code = 0x1103;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 0) = 2; }
    const int before = srv.reads;
    sleep_ms(200);
    tags().dispatch(false);
    CHECK(tagv("online") == 1);
    CHECK(tagv("v") == 1);
    CHECK(srv.connections == 1);
    srv.end_code = 0;
    REQUIRE(wait_for([] { return tagv("v") == 2; }));
    CHECK(srv.reads > before);

    // the flags a CPU with an error sets in the end code do not make the answer an error
    srv.flags_sub = 0x40;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 0) = 3; }
    REQUIRE(wait_for([] { return tagv("v") == 3; }));
    srv.flags_sub = 0;

    // silence: offline after the timeout, and back with a new connection
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = [](const Bytes&) { return Bytes{ 0xAA }; }; }
    REQUIRE(wait_for([] { return tagv("online") == 0; }, 3000));
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = nullptr; srv.word(kD, 0) = 4; }
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("v") == 4; }, 8000));
    CHECK(srv.connections >= 2);

    // an answer to another request (wrong service id) is not taken
    { std::lock_guard<std::mutex> g(srv.mutex);
      srv.word(kD, 0) = 99;
      srv.override_answer = [](const Bytes& f) { return Bytes{ 0xC0, 0, 2, f[6], f[7], f[8], f[3], f[4], f[5], static_cast<uint8_t>(f[9] + 1), 1, 1, 0, 0, 0, 99 }; }; }
    REQUIRE(wait_for([] { return tagv("online") == 0; }, 3000));
    CHECK(tagv("v") == 4);
    // a dropped connection
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = [](const Bytes&) { return Bytes{ 0xEE }; }; }
    sleep_ms(300);
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = nullptr; srv.word(kD, 0) = 5; }
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("v") == 5; }, 12000));
}

TEST_CASE("fins: a PLC that refuses the connection keeps the driver offline, and it stops promptly")
{
    Runtime rt;
    FakeFinsServer srv;
    srv.refuse = 0x21;                                      // "the node is already connected"
    rt.tag("online", TagType::Bool, 1);
    rt.tag("v", TagType::Int);
    REQUIRE(start(rt, fins_cfg(srv, json::parse(R"([{ "tag": "v", "addr": "D0" }])"))) == 1);
    REQUIRE(wait_for([&] { return srv.connections >= 1 && tagv("online") == 0; }, 3000));
    sleep_ms(200);
    CHECK(srv.reads == 0);
    const uint32_t t0 = millis();
    rt.drivers.stop_all();
    CHECK(millis() - t0 < 1500);
}

TEST_CASE("fins: configuration")
{
    Runtime rt;
    rt.tag("a", TagType::Int);
    rt.tag("b", TagType::Bool);
    // no host, no map: no driver
    json none = json::array({ json{ { "type", "fins" }, { "map", json::array() } } });
    CHECK(rt.drivers.load(&none) == 0);
    json nomap = json::array({ json{ { "type", "fins" }, { "host", "127.0.0.1" } } });
    CHECK(rt.drivers.load(&nomap) == 0);
    // wrong rows are left out, the rest works
    FakeFinsServer srv;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 1) = 11; }
    json map = json::parse(R"([
        { "tag": "nope", "addr": "D0" },
        { "tag": "a", "addr": "Q5" },
        { "tag": "a", "addr": "D65535", "dtype": "int32" },
        { "tag": "a", "addr": "D1", "dtype": "wrong" },
        { "tag": "b", "addr": "T1.00" }
    ])");
    json cfg = fins_cfg(srv, map);
    cfg.erase("status_tag");
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("a") == 11; }));
    CHECK(read_targets(srv).size() == 1);
}
