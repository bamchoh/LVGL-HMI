// SLMP (MC protocol) client: frames and addresses byte by byte (slmp.hpp), and the driver end to
// end against an in-process fake SLMP server (3E / 4E). Grouped by ISO/IEC 25010 characteristic.
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
#include "lvgl_fixture.hpp"
#include "slmp.hpp"
#include "util.hpp"

#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <tuple>
#include <string>
#include <thread>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------- fake SLMP server

class FakeSlmpServer {
public:
    FakeSlmpServer()
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
    ~FakeSlmpServer()
    {
        running_ = false;
        if(thread_.joinable()) thread_.join();
        close_sock(listen_);
    }
    uint16_t port() const { return port_; }

    // device memory: (device code, word number) -> word. Bit devices: word n holds points 16n..16n+15.
    std::map<std::pair<uint8_t, uint32_t>, uint16_t> mem;
    std::mutex mutex;
    uint16_t& word(uint8_t code, uint32_t n) { return mem[{ code, n }]; }
    bool bit(uint8_t code, uint32_t point) { return (word(code, point / 16) >> (point % 16)) & 1; }

    std::atomic<int> reads{ 0 }, writes{ 0 }, connections{ 0 };
    std::atomic<uint16_t> end_code{ 0 };            // answer every request with this end code
    // replace the whole answer (to send something broken); return empty = normal answer
    std::function<Bytes(const Bytes& request)> override_answer;
    std::vector<Bytes> requests;                    // every request seen (guarded by mutex)

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
            uint8_t h[2];
            if(!recv_all(c, h, 2)) return;
            const bool e4 = h[0] == 0x54;
            Bytes req(h, h + 2);
            uint8_t rest[11];
            const int more = e4 ? 11 : 7;
            if(!recv_all(c, rest, more)) return;
            req.insert(req.end(), rest, rest + more);
            const int len = req[req.size() - 2] | (req[req.size() - 1] << 8);
            if(len < 6 || len > 4000) return;
            Bytes body(static_cast<size_t>(len));
            if(!recv_all(c, body.data(), len)) return;
            req.insert(req.end(), body.begin(), body.end());
            Bytes ans;
            {
                std::lock_guard<std::mutex> g(mutex);
                requests.push_back(req);
                if(override_answer) ans = override_answer(req);
                if(ans.empty()) ans = answer(req, e4);
            }
            if(ans.size() == 1 && ans[0] == 0xEE) return;      // "close the connection"
            if(ans.size() == 1 && ans[0] == 0xAA) continue;     // "say nothing"
            if(send(c, reinterpret_cast<const char*>(ans.data()), static_cast<int>(ans.size()), 0) != static_cast<int>(ans.size())) return;
        }
    }
    Bytes answer(const Bytes& req, bool e4)
    {
        const size_t at = e4 ? 13 : 9;                 // the timer
        const uint16_t cmd = static_cast<uint16_t>(req[at + 2] | (req[at + 3] << 8));
        const uint16_t sub = static_cast<uint16_t>(req[at + 4] | (req[at + 5] << 8));
        const uint8_t* d = req.data() + at + 6;
        const uint32_t num = d[0] | (d[1] << 8) | (d[2] << 16);
        const uint8_t code = d[3];
        const uint16_t pts = static_cast<uint16_t>(d[4] | (d[5] << 8));
        Bytes data;
        uint16_t end = end_code;
        const bool bitdev = code == 0x90 || code == 0x9C || code == 0x9D || code == 0xA0 || code == 0x91;
        if(end == 0 && cmd == 0x0401 && sub == 0x0000) {
            reads++;
            for(uint32_t i = 0; i < pts; i++) {
                uint16_t w = bitdev ? word(code, num / 16 + i) : word(code, num + i);
                data.push_back(static_cast<uint8_t>(w)); data.push_back(static_cast<uint8_t>(w >> 8));
            }
        }
        else if(end == 0 && cmd == 0x1401 && sub == 0x0000) {
            writes++;
            for(uint32_t i = 0; i < pts; i++) word(code, num + i) = static_cast<uint16_t>(d[6 + i * 2] | (d[7 + i * 2] << 8));
        }
        else if(end == 0 && cmd == 0x1401 && sub == 0x0001) {
            writes++;
            for(uint32_t i = 0; i < pts; i++) {
                const bool on = ((i % 2 == 0) ? (d[6 + i / 2] >> 4) : d[6 + i / 2]) & 1;
                uint16_t& w = word(code, (num + i) / 16);
                const uint16_t m = static_cast<uint16_t>(1u << ((num + i) % 16));
                w = on ? static_cast<uint16_t>(w | m) : static_cast<uint16_t>(w & ~m);
            }
        }
        else if(end == 0) end = 0xC059;
        Bytes r = e4 ? Bytes{ 0xD4, 0x00, req[2], req[3], 0x00, 0x00 } : Bytes{ 0xD0, 0x00 };
        const size_t route = e4 ? 6 : 2;
        r.insert(r.end(), req.begin() + static_cast<long>(route), req.begin() + static_cast<long>(route) + 5);
        const size_t len = 2 + data.size();
        r.push_back(static_cast<uint8_t>(len)); r.push_back(static_cast<uint8_t>(len >> 8));
        r.push_back(static_cast<uint8_t>(end)); r.push_back(static_cast<uint8_t>(end >> 8));
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

// the distinct batch reads (device code, number, points) the server saw since `from`
std::set<std::tuple<uint8_t, uint32_t, uint16_t>> read_targets(FakeSlmpServer& srv, size_t from = 0)
{
    std::lock_guard<std::mutex> g(srv.mutex);
    std::set<std::tuple<uint8_t, uint32_t, uint16_t>> out;
    for(size_t i = from; i < srv.requests.size(); i++) {
        const Bytes& r = srv.requests[i];
        const size_t at = r[0] == 0x54 ? 13 : 9;
        if((r[at + 2] | (r[at + 3] << 8)) != 0x0401) continue;
        const uint8_t* d = r.data() + at + 6;
        out.insert({ d[3], static_cast<uint32_t>(d[0] | (d[1] << 8) | (d[2] << 16)), static_cast<uint16_t>(d[4] | (d[5] << 8)) });
    }
    return out;
}
std::string tags_str(const char* n) { return tags().find(n)->get().format(); }

json slmp_cfg(const FakeSlmpServer& srv, const json& map, const char* frame = "3e")
{
    return json{ { "type", "slmp" }, { "host", "127.0.0.1" }, { "port", srv.port() }, { "frame", frame }, { "poll_ms", 20 },
                 { "timeout_ms", 400 }, { "status_tag", "online" }, { "map", map } };
}
int start(Runtime& rt, const json& cfg) { json arr = json::array({ cfg }); return rt.drivers.load(&arr); }

constexpr uint8_t kD = 0xA8, kM = 0x90, kX = 0x9C, kSD = 0xA9, kW = 0xB4;

} // namespace

// ================================================================ functional suitability: frames

TEST_CASE("slmp: addresses as GX Works writes them")
{
    auto a = slmp_parse_address("D100");
    REQUIRE(a);
    CHECK(std::string(a->dev->name) == "D");
    CHECK(a->dev->code == 0xA8);
    CHECK(a->number == 100);
    CHECK(slmp_parse_address("x1f")->number == 0x1F);            // X is hexadecimal, any case
    CHECK(slmp_parse_address("W 1A")->number == 0x1A);
    CHECK(std::string(slmp_parse_address("SM400")->dev->name) == "SM");
    CHECK(std::string(slmp_parse_address("SD0")->dev->name) == "SD");
    CHECK(std::string(slmp_parse_address("SB0")->dev->name) == "SB");
    CHECK(std::string(slmp_parse_address("DX10")->dev->name) == "DX");
    CHECK(std::string(slmp_parse_address("ZR0FFFF")->dev->name) == "ZR");
    CHECK(std::string(slmp_parse_address("S5")->dev->name) == "S");
    CHECK(std::string(slmp_parse_address("TN3")->dev->name) == "TN");
    auto b = slmp_parse_address("D10.F");
    REQUIRE(b);
    CHECK(b->bit == 15);
    CHECK(slmp_format_address(*b) == "D10.F");
    CHECK(slmp_format_address(*slmp_parse_address("x1f")) == "X1F");
    CHECK(slmp_format_address(*slmp_parse_address("M0010")) == "M10");

    std::string err;
    CHECK_FALSE(slmp_parse_address("", &err));
    CHECK_FALSE(slmp_parse_address("Q10", &err));
    CHECK(err.find("unknown device") != std::string::npos);
    CHECK_FALSE(slmp_parse_address("D1A", &err));                  // D is decimal
    CHECK(err.find("decimal") != std::string::npos);
    CHECK_FALSE(slmp_parse_address("D16777216", &err));            // above 3 bytes
    CHECK(slmp_parse_address("D16777215"));
    CHECK_FALSE(slmp_parse_address("M10.1", &err));                // a bit device has no bit number
    CHECK_FALSE(slmp_parse_address("D10.G", &err));
    CHECK_FALSE(slmp_parse_address("D10.10", &err));
    CHECK_FALSE(slmp_parse_address("D", &err));
    CHECK_FALSE(slmp_parse_address("D-1", &err));
    CHECK_FALSE(slmp_parse_address(std::string(100, 'D'), &err));
}

TEST_CASE("slmp: request and response frames byte by byte")
{
    // the manual's example: batch read of D100..D102 in word units, 3E frame, to the station itself
    SlmpRoute route;
    Bytes req = slmp_request(false, 0, route, 0x0010, 0x0401, 0x0000, slmp_batch_data(*slmp_parse_address("D100")->dev, 100, 3));
    CHECK(req == Bytes{ 0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0C, 0x00, 0x10, 0x00, 0x01, 0x04, 0x00, 0x00, 0x64, 0x00, 0x00, 0xA8, 0x03, 0x00 });
    Bytes req4 = slmp_request(true, 0x1234, route, 4, 0x1401, 0x0001, { 1, 2 });
    CHECK(req4 == Bytes{ 0x54, 0x00, 0x34, 0x12, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08, 0x00, 0x04, 0x00, 0x01, 0x14, 0x01, 0x00, 0x01, 0x02 });

    Bytes resp = { 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x34, 0x12, 0x02, 0x00, 0xEF, 0x1D };
    CHECK(slmp_response_body_len(resp.data(), false, 100) == 8);
    SlmpResponse r;
    REQUIRE(slmp_parse_response(resp.data(), static_cast<int>(resp.size()), false, r));
    CHECK(r.end_code == 0);
    CHECK(r.data == Bytes{ 0x34, 0x12, 0x02, 0x00, 0xEF, 0x1D });
    Bytes err4 = { 0xD4, 0x00, 0x34, 0x12, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x51, 0xC0 };
    REQUIRE(slmp_parse_response(err4.data(), static_cast<int>(err4.size()), true, r));
    CHECK(r.serial == 0x1234);
    CHECK(r.end_code == 0xC051);
    CHECK(slmp_end_code_text(0xC051) == "C051: number of points out of range");

    // bit units: the first point in the upper half of the byte
    CHECK(slmp_pack_bits({ true, false, true }) == Bytes{ 0x10, 0x10 });
    CHECK(slmp_pack_bits({ false, true }) == Bytes{ 0x01 });
    Bytes nib = { 0x10, 0x11 };
    CHECK(slmp_unpack_bits(nib.data(), 2, 4) == std::vector<bool>{ true, false, true, true });
}

// ================================================================ security / robustness: what comes back

TEST_CASE("slmp: broken, foreign and oversized responses are refused")
{
    SlmpResponse r;
    Bytes not_slmp = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x02 };         // a Modbus answer
    CHECK(slmp_response_body_len(not_slmp.data(), false, 2000) == -1);
    Bytes huge = { 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0xFF, 0xFF };              // 65535 bytes announced
    CHECK(slmp_response_body_len(huge.data(), false, 1922) == -1);
    Bytes tiny = { 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x01, 0x00 };              // shorter than an end code
    CHECK(slmp_response_body_len(tiny.data(), false, 1922) == -1);
    Bytes e3as4 = { 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
    CHECK(slmp_response_body_len(e3as4.data(), true, 1922) == -1);
    Bytes truncated = { 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01 };
    CHECK_FALSE(slmp_parse_response(truncated.data(), static_cast<int>(truncated.size()), false, r));
    CHECK_FALSE(slmp_parse_response(truncated.data(), 3, false, r));

    // random bytes never pass as a response, never read past the buffer
    std::mt19937 rng(7);
    int accepted = 0;
    for(int i = 0; i < 20000; i++) {
        Bytes b(static_cast<size_t>(rng() % 40));
        for(auto& x : b) x = static_cast<uint8_t>(rng());
        if(rng() % 4 == 0 && b.size() > 2) { b[0] = 0xD0; b[1] = 0x00; }
        if(slmp_parse_response(b.data(), static_cast<int>(b.size()), false, r)) {
            accepted++;
            CHECK(static_cast<int>(r.data.size()) + 11 == static_cast<int>(b.size()));
        }
    }
    MESSAGE("random frames accepted as well-formed: " << accepted);
}

// ================================================================ end to end

TEST_CASE("slmp: reads every device kind and data type, in few requests")
{
    Runtime rt;
    FakeSlmpServer srv;
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        srv.word(kD, 100) = 1234;
        srv.word(kD, 101) = 0xFFFE;                                 // -2
        srv.word(kD, 102) = 0x0002; srv.word(kD, 103) = 0x0001;      // s32 low word first: 0x00010002
        srv.word(kD, 104) = 0x0000; srv.word(kD, 105) = 0x3FC0;      // float 1.5
        srv.word(kD, 106) = 250;
        srv.word(kD, 110) = 0x0008;                                  // D110.3
        srv.word(kD, 200) = 'H' | ('M' << 8); srv.word(kD, 201) = 'L';   // "HML"
        srv.word(kM, 0) = 0x0004;                                    // M2
        srv.word(kX, 1) = 0x8000;                                    // X1F
        srv.word(kW, 0x1A) = 77;
    }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "d100", "s16", "s32", "f32", "scaled", "bit3", "m2", "m3", "x1f", "w1a" }) rt.tag(n, TagType::Float);
    rt.tag("name", std::string(""));
    json map = json::parse(R"([
        { "tag": "d100",   "addr": "D100", "dtype": "u16" },
        { "tag": "s16",    "addr": "D101" },
        { "tag": "s32",    "addr": "D102", "dtype": "s32" },
        { "tag": "f32",    "addr": "D104", "dtype": "float" },
        { "tag": "scaled", "addr": "D106", "scale": 0.1, "offset": 1 },
        { "tag": "bit3",   "addr": "D110.3" },
        { "tag": "name",   "addr": "D200", "dtype": "string", "length": 8 },
        { "tag": "m2",     "addr": "M2" },
        { "tag": "m3",     "addr": "M3" },
        { "tag": "x1f",    "addr": "X1F" },
        { "tag": "w1a",    "addr": "W1A" },
        { "tag": "nope",   "addr": "D0" },
        { "tag": "d100",   "addr": "Q1" }
    ])");
    REQUIRE(start(rt, slmp_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("x1f") == 1 && tags_str("name") == "HML"; }));
    CHECK(tagv("d100") == 1234);
    CHECK(tagv("s16") == -2);
    CHECK(tagv("s32") == 65538);
    CHECK(tagv("f32") == doctest::Approx(1.5));
    CHECK(tagv("scaled") == doctest::Approx(26));
    CHECK(tagv("bit3") == 1);
    CHECK(tagv("m2") == 1);
    CHECK(tagv("m3") == 0);
    CHECK(tagv("w1a") == 77);

    // D100..D110 are one read (gaps of up to 32 words are read through), D200 another,
    // M, X and W one each: 5 different reads
    auto t = read_targets(srv);
    CHECK(t.size() == 5);
    CHECK(t.count({ kD, 100, 11 }) == 1);
    CHECK(t.count({ kD, 200, 4 }) == 1);
    CHECK(t.count({ kM, 0, 1 }) == 1);
    CHECK(t.count({ kX, 16, 1 }) == 1);                         // X1F: the word X10..X1F
    CHECK(t.count({ kW, 0x1A, 1 }) == 1);
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        const Bytes& last = srv.requests.back();
        CHECK(last[0] == 0x50);                                  // 3E
    }
    rt.drivers.stop_all();
}

TEST_CASE("slmp: writes, 4E frame, write protection")
{
    Runtime rt;
    FakeSlmpServer srv;
    rt.tag("online", TagType::Bool);
    Tag* sp = rt.tag("sp", TagType::Float, 0);
    Tag* big = rt.tag("big", TagType::Float, 0);
    Tag* fl = rt.tag("fl", TagType::Float, 0);
    Tag* run = rt.tag("run", TagType::Bool, 0);
    Tag* txt = rt.tag("txt", std::string(""));
    Tag* sys = rt.tag("sys", TagType::Float, 0);
    Tag* outside = rt.tag("outside", TagType::Float, 0);
    Tag* bitw = rt.tag("bitw", TagType::Bool, 0);
    json map = json::parse(R"([
        { "tag": "sp",      "addr": "D10", "scale": 0.1 },
        { "tag": "big",     "addr": "D20", "dtype": "s32", "swap_words": true },
        { "tag": "fl",      "addr": "D30", "dtype": "float" },
        { "tag": "run",     "addr": "M5" },
        { "tag": "txt",     "addr": "D40", "dtype": "string", "length": 5 },
        { "tag": "sys",     "addr": "SD100" },
        { "tag": "outside", "addr": "D2000" },
        { "tag": "bitw",    "addr": "D50.2", "access": "rw" }
    ])");
    json cfg = slmp_cfg(srv, map, "4e");
    cfg["write_ranges"] = json::array({ "D0-D999", "M0-M99", "nonsense" });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));

    sp->set_number(12.3, nullptr);
    big->set_number(-100000, nullptr);
    fl->set_number(2.5, nullptr);
    run->set_number(1, nullptr);
    txt->set_string("ABCDEFG", nullptr);                         // cut to 5 characters
    sys->set_number(9, nullptr);
    outside->set_number(9, nullptr);
    bitw->set_number(1, nullptr);
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return srv.word(kM, 0) == 0x20 && srv.word(kD, 42) != 0; }));
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        CHECK(srv.word(kD, 10) == 123);
        const uint32_t v = static_cast<uint32_t>(-100000);
        CHECK(srv.word(kD, 20) == static_cast<uint16_t>(v >> 16));      // swapped: high word first
        CHECK(srv.word(kD, 21) == static_cast<uint16_t>(v & 0xFFFF));
        CHECK(srv.word(kD, 31) == 0x4020);                             // 2.5f = 0x40200000, low word first
        CHECK(srv.word(kD, 40) == ('A' | ('B' << 8)));
        CHECK(srv.word(kD, 41) == ('C' | ('D' << 8)));
        CHECK(srv.word(kD, 42) == 'E');
        CHECK(srv.word(kSD, 100) == 0);                                 // SM / SD: not written
        CHECK(srv.word(kD, 2000) == 0);                                 // outside write_ranges
        CHECK(srv.word(kD, 50) == 0);                                   // a bit of a word: read only
        for(const Bytes& r : srv.requests) CHECK(r[0] == 0x54);         // 4E all the way
        // the serial numbers of 4E requests change from one request to the next
        CHECK(srv.requests.size() > 2);
        CHECK((srv.requests[0][2] | (srv.requests[0][3] << 8)) != (srv.requests[1][2] | (srv.requests[1][3] << 8)));
    }
    // the value read back is what was written
    REQUIRE(wait_for([] { return tagv("big") == -100000 && tagv("sp") == doctest::Approx(12.3); }));
    rt.drivers.stop_all();
}

// ================================================================ reliability

TEST_CASE("slmp: end codes, silence, garbage and a dropped connection")
{
    Runtime rt;
    FakeSlmpServer srv;
    rt.tag("online", TagType::Bool);
    Tag* d = rt.tag("d", TagType::Float, 0);
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        srv.word(kD, 0) = 5;
    }
    REQUIRE(start(rt, slmp_cfg(srv, json::parse(R"([ { "tag": "d", "addr": "D0" } ])"))) == 1);
    REQUIRE(wait_for([&] { return d->get_number() == 5; }));

    // an end code: the connection stays, the value is kept, it reads again when it is fine
    srv.end_code = 0xC056;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 0) = 6; }
    sleep_ms(150);
    CHECK(d->get_number() == 5);
    CHECK(tagv("online") == 1);
    const int conns = srv.connections;
    srv.end_code = 0;
    REQUIRE(wait_for([&] { return d->get_number() == 6; }));
    CHECK(srv.connections == conns);

    // nothing comes back: timeout, offline, a new connection
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = [](const Bytes&) { return Bytes{ 0xAA }; }; }
    REQUIRE(wait_for([] { return tagv("online") == 0; }, 4000));
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = nullptr; srv.word(kD, 0) = 7; }
    REQUIRE(wait_for([&] { return d->get_number() == 7 && tagv("online") == 1; }, 6000));

    // garbage, a huge announced length, a data part too short: refused, reconnected
    const std::vector<Bytes> bad = {
        { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 },
        { 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0xFF, 0xFF },
        { 0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01 },         // 1 data byte for 1 word
    };
    for(const Bytes& b : bad) {
        const int c0 = srv.connections;
        { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = [b](const Bytes&) { return b; }; }
        REQUIRE(wait_for([&] { return srv.connections > c0 + 1; }, 6000));
        { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = nullptr; }
    }
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 0) = 8; }
    REQUIRE(wait_for([&] { return d->get_number() == 8 && tagv("online") == 1; }, 6000));

    // the server drops the connection
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = [](const Bytes&) { return Bytes{ 0xEE }; }; }
    REQUIRE(wait_for([] { return tagv("online") == 0; }, 4000));
    { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = nullptr; srv.word(kD, 0) = 9; }
    REQUIRE(wait_for([&] { return d->get_number() == 9; }, 6000));
    rt.drivers.stop_all();
}

TEST_CASE("slmp: a 4E answer to another request is not taken")
{
    Runtime rt;
    FakeSlmpServer srv;
    rt.tag("online", TagType::Bool);
    Tag* d = rt.tag("d", TagType::Float, 0);
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 0) = 3; }
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        srv.override_answer = [&srv](const Bytes& req) {
            // answer with the right data but the serial number of some other request
            Bytes r = { 0xD4, 0x00, static_cast<uint8_t>(req[2] + 1), req[3], 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00 };
            (void)srv;
            return r;
        };
    }
    REQUIRE(start(rt, slmp_cfg(srv, json::parse(R"([ { "tag": "d", "addr": "D0" } ])"), "4e")) == 1);
    sleep_ms(400);
    CHECK(d->get_number() == 0);
    CHECK(srv.connections >= 1);
    rt.drivers.stop_all();
}

// ================================================================ performance efficiency

TEST_CASE("slmp: 1000 points are read in a few requests")
{
    Runtime rt;
    FakeSlmpServer srv;
    rt.tag("online", TagType::Bool);
    json map = json::array();
    for(int i = 0; i < 800; i++) { std::string n = "d" + std::to_string(i); rt.tag(n, TagType::Float); map.push_back({ { "tag", n }, { "addr", "D" + std::to_string(i * 2) } }); }
    for(int i = 0; i < 200; i++) { std::string n = "m" + std::to_string(i); rt.tag(n, TagType::Bool); map.push_back({ { "tag", n }, { "addr", "M" + std::to_string(i) } }); }
    { std::lock_guard<std::mutex> g(srv.mutex); srv.word(kD, 1598) = 42; srv.word(kM, 12) = 0x80; }
    REQUIRE(start(rt, slmp_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("d799") == 42 && tagv("m199") == 1; }));
    // D0, D2 ... D1598 -> 2 reads of at most 960 words (D0..D958, D960..D1598), M0..M199 = 13 words -> 1 read
    auto t = read_targets(srv);
    CHECK(t.size() == 3);
    CHECK(t.count({ kD, 0, 959 }) == 1);
    CHECK(t.count({ kD, 960, 639 }) == 1);
    CHECK(t.count({ kM, 0, 13 }) == 1);
    const int r0 = srv.reads;
    const uint32_t t0 = millis();
    sleep_ms(500);
    MESSAGE("1000 points: " << (srv.reads - r0) << " requests (3 per cycle) in " << (millis() - t0) << " ms");
    rt.drivers.stop_all();
}
