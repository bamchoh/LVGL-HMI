// End-to-end tests for the Modbus TCP client driver against an in-process fake server
// (127.0.0.1, ephemeral port). Covers register decoding, scaling, writes, access modes,
// exceptions and reconnection.
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
#include "util.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

// ---------------------------------------------------------------- fake Modbus TCP server

class FakeModbusServer {
public:
    FakeModbusServer()
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
        addr.sin_port = 0;
        REQUIRE(bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(listen(listen_, 1) == 0);
        socklen_t len = sizeof(addr);
        REQUIRE(getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    ~FakeModbusServer()
    {
        running_ = false;
        if(thread_.joinable()) thread_.join();
        close_sock(listen_);
    }

    uint16_t port() const { return port_; }

    // Register tables (guard with lock() when the server is running)
    std::array<uint16_t, 64> holding{}, input{};
    std::array<bool, 64>     coils{}, discrete{};
    std::mutex               mutex;
    std::lock_guard<std::mutex> lock() { return std::lock_guard<std::mutex>(mutex); }

    uint8_t          unit_expected = 1;
    std::atomic<int> exception_fc{ 0 };        // reply with exception code 2 to this function code
    std::atomic<bool> kick{ false };            // drop the current client once
    std::atomic<int> connections{ 0 };
    std::atomic<int> bad_unit{ 0 };
    std::array<std::atomic<int>, 17> fc_count{};
    std::set<uint16_t> polled_holding;          // addresses seen in FC3 requests (guarded by mutex)

    int count(int fc) const { return fc_count[static_cast<size_t>(fc)].load(); }

private:
    static bool recv_all(sock_t s, uint8_t* buf, int len)
    {
        while(len > 0) {
            int n = static_cast<int>(recv(s, reinterpret_cast<char*>(buf), len, 0));
            if(n <= 0) return false;
            buf += n; len -= n;
        }
        return true;
    }

    static bool wait_readable(sock_t s, int ms)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
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
            if(kick.exchange(false)) return;
            if(!wait_readable(c, 20)) continue;
            uint8_t hdr[7];
            if(!recv_all(c, hdr, 7)) return;
            int len = ((hdr[4] << 8) | hdr[5]) - 1;
            if(len < 1 || len > 260) return;
            uint8_t pdu[260];
            if(!recv_all(c, pdu, len)) return;
            if(hdr[6] != unit_expected) bad_unit++;

            uint8_t resp[260];
            int rlen = handle(pdu, len, resp);
            uint8_t frame[7 + 260];
            std::memcpy(frame, hdr, 7);
            frame[4] = static_cast<uint8_t>((rlen + 1) >> 8);
            frame[5] = static_cast<uint8_t>((rlen + 1) & 0xFF);
            std::memcpy(frame + 7, resp, static_cast<size_t>(rlen));
            if(send(c, reinterpret_cast<const char*>(frame), 7 + rlen, 0) != 7 + rlen) return;
        }
    }

    int handle(const uint8_t* pdu, int len, uint8_t* resp)
    {
        uint8_t fc = pdu[0];
        if(fc <= 16) fc_count[fc]++;
        uint16_t addr = static_cast<uint16_t>((pdu[1] << 8) | pdu[2]);
        uint16_t qty = len >= 5 ? static_cast<uint16_t>((pdu[3] << 8) | pdu[4]) : 0;
        if(exception_fc == fc) { resp[0] = static_cast<uint8_t>(fc | 0x80); resp[1] = 2; return 2; }

        std::lock_guard<std::mutex> g(mutex);
        switch(fc) {
            case 0x01: case 0x02: {
                const auto& bits = fc == 0x01 ? coils : discrete;
                int nbytes = (qty + 7) / 8;
                resp[0] = fc; resp[1] = static_cast<uint8_t>(nbytes);
                std::memset(resp + 2, 0, static_cast<size_t>(nbytes));
                for(int i = 0; i < qty; i++) if(bits[(addr + i) % bits.size()]) resp[2 + i / 8] |= static_cast<uint8_t>(1 << (i % 8));
                return 2 + nbytes;
            }
            case 0x03: case 0x04: {
                const auto& regs = fc == 0x03 ? holding : input;
                if(fc == 0x03) for(int i = 0; i < qty; i++) polled_holding.insert(static_cast<uint16_t>(addr + i));
                resp[0] = fc; resp[1] = static_cast<uint8_t>(qty * 2);
                for(int i = 0; i < qty; i++) {
                    uint16_t v = regs[(addr + i) % regs.size()];
                    resp[2 + i * 2] = static_cast<uint8_t>(v >> 8);
                    resp[3 + i * 2] = static_cast<uint8_t>(v & 0xFF);
                }
                return 2 + qty * 2;
            }
            case 0x05:
                coils[addr % coils.size()] = pdu[3] == 0xFF;
                std::memcpy(resp, pdu, 5);
                return 5;
            case 0x06:
                holding[addr % holding.size()] = static_cast<uint16_t>((pdu[3] << 8) | pdu[4]);
                std::memcpy(resp, pdu, 5);
                return 5;
            case 0x10: {
                for(int i = 0; i < qty; i++) holding[(addr + i) % holding.size()] = static_cast<uint16_t>((pdu[6 + i * 2] << 8) | pdu[7 + i * 2]);
                std::memcpy(resp, pdu, 5);
                return 5;
            }
            default:
                resp[0] = static_cast<uint8_t>(fc | 0x80); resp[1] = 1;
                return 2;
        }
    }

    sock_t            listen_ = kBadSock;
    uint16_t          port_ = 0;
    std::atomic<bool> running_{ false };
    std::thread       thread_;
};

// ---------------------------------------------------------------- helpers

template <class Pred>
bool wait_for(Pred pred, uint32_t timeout_ms = 3000)
{
    uint32_t t0 = millis();
    for(;;) {
        tags().dispatch(false);           // deliver UI writes to the driver, like the main loop does
        if(pred()) return true;
        if(millis() - t0 > timeout_ms) return false;
        sleep_ms(5);
    }
}

json modbus_cfg(const FakeModbusServer& srv, const json& map, int poll_ms = 20)
{
    return json{ { "type", "modbus_tcp" }, { "host", "127.0.0.1" }, { "port", srv.port() }, { "unit_id", 1 },
                 { "poll_ms", poll_ms }, { "timeout_ms", 500 }, { "status_tag", "online" }, { "map", map } };
}

int start_modbus(Runtime& rt, const json& cfg)
{
    json arr = json::array({ cfg });
    return rt.drivers.load(&arr);
}

double tagv(const char* name) { return tags().find(name)->get_number(); }

} // namespace

// ---------------------------------------------------------------- tests

TEST_CASE("modbus: connects, reports status and polls every area and data type")
{
    Runtime rt;
    FakeModbusServer srv;
    {
        auto g = srv.lock();
        srv.holding[0] = 1234;
        srv.holding[1] = 0xFFFE;              // s16 -2
        srv.holding[2] = 250;                 // scaled
        srv.holding[3] = 0x0001; srv.holding[4] = 0x0002;     // u32 65538, swapped 131073
        srv.holding[5] = 0xFFFF; srv.holding[6] = 0xFFFF;     // s32 -1
        srv.holding[7] = 0x3FC0; srv.holding[8] = 0x0000;     // float 1.5
        srv.holding[9] = 0x0000; srv.holding[10] = 0x3FC0;    // float 1.5 with swapped words
        srv.holding[11] = 7;                  // unknown dtype -> u16
        srv.input[0] = 42;
        srv.coils[3] = true;
        srv.discrete[1] = true;
    }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "u16", "s16", "scaled", "u32", "u32sw", "s32", "f32", "f32sw", "odd", "in", "coil", "di" }) rt.tag(n, TagType::Float);

    json map = json::parse(R"([
        { "tag": "u16",    "area": "holding", "addr": 0 },
        { "tag": "s16",    "area": "holding", "addr": 1, "dtype": "s16" },
        { "tag": "scaled", "area": "holding", "addr": 2, "scale": 0.1, "offset": -5 },
        { "tag": "u32",    "area": "holding", "addr": 3, "dtype": "u32" },
        { "tag": "u32sw",  "area": "holding", "addr": 3, "dtype": "uint32", "swap_words": true },
        { "tag": "s32",    "area": "holding", "addr": 5, "dtype": "int32" },
        { "tag": "f32",    "area": "holding", "addr": 7, "dtype": "float" },
        { "tag": "f32sw",  "area": "holding", "addr": 9, "dtype": "real", "swap_words": true },
        { "tag": "odd",    "area": "holding", "addr": 11, "dtype": "quaternion" },
        { "tag": "in",     "area": "input",   "addr": 0 },
        { "tag": "coil",   "area": "coil",    "addr": 3 },
        { "tag": "di",     "area": "discrete","addr": 1 },
        { "tag": "ghost",  "area": "holding", "addr": 20 }
    ])");
    REQUIRE(start_modbus(rt, modbus_cfg(srv, map)) == 1);

    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    REQUIRE(wait_for([] { return tagv("di") == 1 && tagv("f32sw") != 0; }));
    CHECK(tagv("u16") == doctest::Approx(1234));
    CHECK(tagv("s16") == doctest::Approx(-2));
    CHECK(tagv("scaled") == doctest::Approx(20));          // 250 * 0.1 - 5
    CHECK(tagv("u32") == doctest::Approx(65538));
    CHECK(tagv("u32sw") == doctest::Approx(131073));
    CHECK(tagv("s32") == doctest::Approx(-1));
    CHECK(tagv("f32") == doctest::Approx(1.5));
    CHECK(tagv("f32sw") == doctest::Approx(1.5));
    CHECK(tagv("odd") == doctest::Approx(7));
    CHECK(tagv("in") == doctest::Approx(42));
    CHECK(tagv("coil") == doctest::Approx(1));
    CHECK(tagv("di") == doctest::Approx(1));
    CHECK(srv.bad_unit == 0);
    CHECK(srv.count(0x03) > 0);
    CHECK(srv.count(0x04) > 0);
    CHECK(srv.count(0x01) > 0);
    CHECK(srv.count(0x02) > 0);
    CHECK(srv.count(0x06) == 0);                          // polling never writes

    SUBCASE("changes on the server show up in the tags") {
        { auto g = srv.lock(); srv.holding[0] = 99; srv.coils[3] = false; }
        CHECK(wait_for([] { return tagv("u16") == 99 && tagv("coil") == 0; }));
    }
    SUBCASE("stopping the driver clears the status tag") {
        rt.drivers.stop_all();
        CHECK(tagv("online") == 0);
    }
}

TEST_CASE("modbus: writes from the UI are sent with the right function code and encoding")
{
    Runtime rt;
    FakeModbusServer srv;
    rt.tag("online", TagType::Bool);
    rt.tag("h16", TagType::Int);
    rt.tag("hs16", TagType::Int);
    rt.tag("hscaled", TagType::Float);
    rt.tag("h32", TagType::Float);
    rt.tag("h32sw", TagType::Float);
    rt.tag("hf", TagType::Float);
    rt.tag("c", TagType::Bool);
    json map = json::parse(R"([
        { "tag": "h16",     "area": "holding", "addr": 0 },
        { "tag": "hs16",    "area": "holding", "addr": 1, "dtype": "s16" },
        { "tag": "hscaled", "area": "holding", "addr": 2, "scale": 0.1, "offset": -5 },
        { "tag": "h32",     "area": "holding", "addr": 3, "dtype": "u32" },
        { "tag": "h32sw",   "area": "holding", "addr": 5, "dtype": "s32", "swap_words": true },
        { "tag": "hf",      "area": "holding", "addr": 7, "dtype": "float" },
        { "tag": "c",       "area": "coil",    "addr": 2 }
    ])");
    REQUIRE(start_modbus(rt, modbus_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));

    tags().find("h16")->set_number(1234, nullptr);
    tags().find("hs16")->set_number(-2, nullptr);
    tags().find("hscaled")->set_number(20, nullptr);       // raw = (20 + 5) / 0.1 = 250
    tags().find("h32")->set_number(65538, nullptr);
    tags().find("h32sw")->set_number(-2, nullptr);         // 0xFFFFFFFE, words swapped -> FFFE FFFF
    tags().find("hf")->set_number(1.5, nullptr);
    tags().find("c")->set_number(1, nullptr);

    REQUIRE(wait_for([&] { auto g = srv.lock(); return srv.coils[2] && srv.holding[7] != 0 && srv.holding[0] == 1234; }));
    auto g = srv.lock();
    CHECK(srv.holding[0] == 1234);
    CHECK(srv.holding[1] == 0xFFFE);
    CHECK(srv.holding[2] == 250);
    CHECK(srv.holding[3] == 0x0001);
    CHECK(srv.holding[4] == 0x0002);
    CHECK(srv.holding[5] == 0xFFFE);
    CHECK(srv.holding[6] == 0xFFFF);
    CHECK(srv.holding[7] == 0x3FC0);
    CHECK(srv.holding[8] == 0x0000);
    CHECK(srv.coils[2]);
    CHECK(srv.count(0x06) == 3);                           // three 16-bit registers
    CHECK(srv.count(0x10) == 3);                           // three 32-bit values
    CHECK(srv.count(0x05) == 1);
}

TEST_CASE("modbus: written values are not overwritten by a stale poll")
{
    Runtime rt;
    FakeModbusServer srv;
    { auto g = srv.lock(); srv.holding[0] = 5; }
    rt.tag("online", TagType::Bool);
    Tag* sp = rt.tag("sp", TagType::Int);
    REQUIRE(start_modbus(rt, modbus_cfg(srv, json::parse(R"([{ "tag": "sp", "area": "holding", "addr": 0 }])"), 10)) == 1);
    REQUIRE(wait_for([] { return tagv("sp") == 5; }));

    for(int v = 10; v <= 30; v += 10) {
        sp->set_number(v, nullptr);
        REQUIRE(wait_for([&] { auto g = srv.lock(); return srv.holding[0] == v; }));
    }
    // let several polls go by: the tag must still hold the last written value
    sleep_ms(100);
    wait_for([] { return false; }, 50);
    CHECK(sp->get_number() == doctest::Approx(30));
    auto g = srv.lock();
    CHECK(srv.holding[0] == 30);
}

TEST_CASE("modbus: access modes")
{
    Runtime rt;
    FakeModbusServer srv;
    { auto g = srv.lock(); srv.holding[0] = 1; srv.holding[1] = 2; srv.holding[2] = 3; srv.input[0] = 4; srv.discrete[0] = true; }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "rw", "ro", "wo", "in", "di", "c" }) rt.tag(n, TagType::Float);
    json map = json::parse(R"([
        { "tag": "rw", "area": "holding", "addr": 0 },
        { "tag": "ro", "area": "holding", "addr": 1, "access": "ro" },
        { "tag": "wo", "area": "holding", "addr": 2, "access": "WO" },
        { "tag": "in", "area": "input",   "addr": 0 },
        { "tag": "di", "area": "discrete","addr": 0 },
        { "tag": "c",  "area": "coil",    "addr": 0, "access": "ro" }
    ])");
    REQUIRE(start_modbus(rt, modbus_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("rw") == 1 && tagv("ro") == 2 && tagv("in") == 4 && tagv("di") == 1; }));
    CHECK(tagv("wo") == 0);                                 // write-only: never polled
    {
        auto g = srv.lock();
        CHECK(srv.polled_holding.count(0) == 1);
        CHECK(srv.polled_holding.count(1) == 1);
        CHECK(srv.polled_holding.count(2) == 0);
    }

    tags().find("wo")->set_number(77, nullptr);
    REQUIRE(wait_for([&] { auto g = srv.lock(); return srv.holding[2] == 77; }));

    // read-only / input / discrete tags: UI writes never reach the wire
    int before06 = srv.count(0x06), before05 = srv.count(0x05);
    tags().find("ro")->set_number(50, nullptr);
    tags().find("in")->set_number(50, nullptr);
    tags().find("di")->set_number(0, nullptr);
    tags().find("c")->set_number(1, nullptr);
    tags().dispatch(false);
    sleep_ms(150);
    CHECK(srv.count(0x06) == before06);
    CHECK(srv.count(0x05) == before05);
    auto g = srv.lock();
    CHECK(srv.holding[1] == 2);
    CHECK_FALSE(srv.coils[0]);
}

TEST_CASE("modbus: an exception response keeps the connection and the other items alive")
{
    Runtime rt;
    FakeModbusServer srv;
    { auto g = srv.lock(); srv.holding[0] = 11; srv.input[0] = 22; }
    srv.exception_fc = 0x03;
    rt.tag("online", TagType::Bool);
    rt.tag("h", TagType::Float);
    rt.tag("in", TagType::Float);
    json map = json::parse(R"([ { "tag": "h", "area": "holding", "addr": 0 }, { "tag": "in", "area": "input", "addr": 0 } ])");
    REQUIRE(start_modbus(rt, modbus_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("in") == 22; }));
    sleep_ms(100);
    CHECK(tagv("h") == 0);                                  // FC3 answered with an exception
    CHECK(tagv("online") == 1);
    CHECK(srv.connections == 1);

    srv.exception_fc = 0;
    CHECK(wait_for([] { return tagv("h") == 11; }));
    CHECK(srv.connections == 1);
}

TEST_CASE("modbus: reconnects after the server drops the connection")
{
    Runtime rt;
    FakeModbusServer srv;
    { auto g = srv.lock(); srv.holding[0] = 1; }
    rt.tag("online", TagType::Bool);
    rt.tag("h", TagType::Float);
    REQUIRE(start_modbus(rt, modbus_cfg(srv, json::parse(R"([{ "tag": "h", "area": "holding", "addr": 0 }])"))) == 1);
    REQUIRE(wait_for([] { return tagv("h") == 1; }));
    CHECK(srv.connections == 1);

    // The server drops the client; the driver notices on its next poll and reconnects at once
    // (the offline blip on the status tag is too short to observe from here).
    srv.kick = true;
    REQUIRE(wait_for([&] { return srv.connections == 2; }, 5000));
    { auto g = srv.lock(); srv.holding[0] = 2; }
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("h") == 2; }, 5000));
    CHECK(srv.connections == 2);
}

TEST_CASE("modbus: with nobody listening the driver stays offline and stops promptly")
{
    Runtime rt;
    uint16_t free_port;
    { FakeModbusServer probe; free_port = probe.port(); }   // closed again: nothing listens there now
    rt.tag("online", TagType::Bool, 1);
    rt.tag("h", TagType::Float);
    json cfg = json{ { "type", "modbus_tcp" }, { "host", "127.0.0.1" }, { "port", free_port }, { "poll_ms", 20 }, { "timeout_ms", 300 },
                     { "status_tag", "online" }, { "map", json::parse(R"([{ "tag": "h", "area": "holding", "addr": 0 }])") } };
    REQUIRE(start_modbus(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 0; }, 3000));

    uint32_t t0 = millis();
    rt.drivers.stop_all();
    CHECK(millis() - t0 < 1500);
    CHECK(tagv("h") == 0);
}

TEST_CASE("modbus: configuration defaults and validation")
{
    Runtime rt;
    FakeModbusServer srv;
    srv.unit_expected = 9;
    { auto g = srv.lock(); srv.holding[0] = 3; }
    rt.tag("h", TagType::Float);

    SUBCASE("unit_id is sent in every request") {
        json cfg = modbus_cfg(srv, json::parse(R"([{ "tag": "h", "addr": 0 }])"));   // area defaults to holding
        cfg["unit_id"] = 9;
        cfg.erase("status_tag");
        REQUIRE(start_modbus(rt, cfg) == 1);
        REQUIRE(wait_for([] { return tagv("h") == 3; }));
        CHECK(srv.bad_unit == 0);
    }
    SUBCASE("a wrong unit id is still answered by this lenient server, but counted") {
        REQUIRE(start_modbus(rt, modbus_cfg(srv, json::parse(R"([{ "tag": "h", "addr": 0 }])"))) == 1);
        REQUIRE(wait_for([] { return tagv("h") == 3; }));
        CHECK(srv.bad_unit > 0);
    }
    SUBCASE("a map whose tags are all unknown still yields a driver that polls nothing") {
        REQUIRE(start_modbus(rt, modbus_cfg(srv, json::parse(R"([{ "tag": "nope", "addr": 0 }])"))) == 1);
        sleep_ms(100);
        CHECK(srv.count(0x03) == 0);
    }
}
