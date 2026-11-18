// Modbus RTU and Modbus ASCII: the frame helpers, and the drivers against a fake slave line.
// The fake line is reached through TCP ("host" / "port" instead of "device"), which carries
// exactly the same frames as a serial port; on Linux one test also runs over a pseudo terminal
// to cover the serial port code.
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using tsock_t = SOCKET;
using socklen_t = int;
static const tsock_t kBadSock = INVALID_SOCKET;
static void close_sock(tsock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <pty.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using tsock_t = int;
static const tsock_t kBadSock = -1;
static void close_sock(tsock_t s) { close(s); }
#endif

#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "modbus_link.hpp"
#include "util.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

// ---------------------------------------------------------------- a line with two slaves
// Units 1 and 2 answer (each with its own registers), every other unit stays silent, as on a
// real RS-485 line. Speaks RTU or ASCII frames.
struct Slaves {
    std::mutex mu;
    std::array<std::array<uint16_t, 64>, 3> holding{};   // [unit]
    std::array<std::array<uint16_t, 64>, 3> input{};
    std::array<std::array<bool, 64>, 3>     coils{};
    std::atomic<int>  requests{ 0 };
    std::atomic<int>  corrupt{ 0 };                       // damage the next N answers (wrong checksum)
    std::atomic<bool> mute{ false };                      // nobody answers
    int requests_to[256] = {};

    // adu = unit + PDU; returns the answer adu, empty = no answer
    std::vector<uint8_t> handle(const std::vector<uint8_t>& adu)
    {
        std::lock_guard<std::mutex> lock(mu);
        requests++;
        if(adu.size() < 6) return {};
        uint8_t unit = adu[0], fc = adu[1];
        requests_to[unit]++;
        if(mute || (unit != 1 && unit != 2)) return {};
        uint16_t addr = static_cast<uint16_t>((adu[2] << 8) | adu[3]), n = static_cast<uint16_t>((adu[4] << 8) | adu[5]);
        std::vector<uint8_t> out{ unit, fc };
        if(addr >= 60) return { unit, static_cast<uint8_t>(fc | 0x80), 2 };                 // illegal data address
        switch(fc) {
            case 0x01: out.push_back(1); out.push_back(coils[unit][addr] ? 1 : 0); break;
            case 0x03: case 0x04:
                out.push_back(static_cast<uint8_t>(n * 2));
                for(int i = 0; i < n; i++) {
                    uint16_t v = fc == 3 ? holding[unit][addr + i] : input[unit][addr + i];
                    out.push_back(static_cast<uint8_t>(v >> 8)); out.push_back(static_cast<uint8_t>(v & 0xFF));
                }
                break;
            case 0x05: coils[unit][addr] = adu[4] == 0xFF; out.insert(out.end(), adu.begin() + 2, adu.begin() + 6); break;
            case 0x06: holding[unit][addr] = n; out.insert(out.end(), adu.begin() + 2, adu.begin() + 6); break;
            case 0x10:
                for(int i = 0; i < n; i++) holding[unit][addr + i] = static_cast<uint16_t>((adu[7 + i * 2] << 8) | adu[8 + i * 2]);
                out.insert(out.end(), adu.begin() + 2, adu.begin() + 6);
                break;
            default: return { unit, static_cast<uint8_t>(fc | 0x80), 1 };
        }
        return out;
    }

    // Feed received bytes; returns what to send back (possibly nothing yet).
    std::string feed(bool ascii, std::string& buf, const char* data, int len)
    {
        buf.append(data, static_cast<size_t>(len));
        std::vector<uint8_t> adu;
        if(ascii) {
            size_t nl = buf.find('\n');
            if(nl == std::string::npos) return {};
            bool ok = modbus_ascii_decode(buf.substr(0, nl + 1), adu);
            buf.erase(0, nl + 1);
            if(!ok) return {};
        }
        else {
            // requests of this client: FC 1-6 are 8 bytes, FC 16 is 9 + byte count
            if(buf.size() < 8) return {};
            size_t total = static_cast<uint8_t>(buf[1]) == 0x10 ? 9 + static_cast<uint8_t>(buf[6]) : 8;
            if(buf.size() < total) return {};
            int n = modbus_rtu_check(reinterpret_cast<const uint8_t*>(buf.data()), static_cast<int>(total));
            if(n > 0) adu.assign(buf.begin(), buf.begin() + n);
            buf.erase(0, total);
            if(n <= 0) return {};
        }
        std::vector<uint8_t> out = handle(adu);
        if(out.empty()) return {};
        std::string frame;
        if(ascii) frame = modbus_ascii_frame(out.data(), out.size());
        else { auto f = modbus_rtu_frame(out.data(), out.size()); frame.assign(f.begin(), f.end()); }
        if(corrupt > 0) { corrupt--; frame[frame.size() - (ascii ? 3 : 1)] ^= 0x01; }       // spoil the checksum
        return frame;
    }
};

class FakeLine {
public:
    explicit FakeLine(bool ascii) : ascii_(ascii)
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
    ~FakeLine()
    {
        running_ = false;
        if(thread_.joinable()) thread_.join();
        close_sock(listen_);
    }
    int port() const { return port_; }
    Slaves s;

private:
    static bool readable(tsock_t fd, int ms)
    {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        timeval tv{ 0, ms * 1000 };
        return select(static_cast<int>(fd) + 1, &rd, nullptr, nullptr, &tv) > 0;
    }
    void run()
    {
        while(running_) {
            if(!readable(listen_, 20)) continue;
            tsock_t c = accept(listen_, nullptr, nullptr);
            if(c == kBadSock) continue;
            std::string buf;
            while(running_) {
                if(!readable(c, 20)) continue;
                char in[256];
                int n = static_cast<int>(recv(c, in, sizeof(in), 0));
                if(n <= 0) break;
                std::string out = s.feed(ascii_, buf, in, n);
                if(!out.empty()) send(c, out.data(), static_cast<int>(out.size()), 0);
            }
            close_sock(c);
        }
    }
    bool ascii_;
    tsock_t listen_ = kBadSock;
    int port_ = 0;
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

double tagv(const char* name) { return tags().find(name)->get_number(); }

int start(Runtime& rt, const json& cfg)
{
    json arr = json::array({ cfg });
    return rt.drivers.load(&arr);
}

json line_cfg(const char* type, int port, const json& map)
{
    return json{ { "type", type }, { "host", "127.0.0.1" }, { "port", port }, { "unit_id", 1 },
                 { "poll_ms", 20 }, { "timeout_ms", 150 }, { "status_tag", "online" }, { "map", map } };
}

} // namespace

// ---------------------------------------------------------------- frames

TEST_CASE("modbus frames: CRC-16, LRC, RTU and ASCII encoding")
{
    const uint8_t req[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };          // read 10 holding registers of unit 1
    CHECK(modbus_crc16(req, sizeof(req)) == 0xCDC5);                        // on the wire: C5 CD
    std::vector<uint8_t> rtu = modbus_rtu_frame(req, sizeof(req));
    REQUIRE(rtu.size() == 8);
    CHECK(rtu[6] == 0xC5);
    CHECK(rtu[7] == 0xCD);
    CHECK(modbus_rtu_check(rtu.data(), 8) == 6);
    rtu[3] ^= 1;
    CHECK(modbus_rtu_check(rtu.data(), 8) == -1);
    CHECK(modbus_rtu_check(rtu.data(), 3) == -1);

    CHECK(modbus_lrc(req, sizeof(req)) == 0xF2);
    CHECK(modbus_ascii_frame(req, sizeof(req)) == ":01030000000AF2\r\n");

    std::vector<uint8_t> adu;
    REQUIRE(modbus_ascii_decode(":01030000000AF2\r\n", adu));
    CHECK(adu == std::vector<uint8_t>(req, req + 6));
    CHECK(modbus_ascii_decode("noise:01030000000af2\n", adu));              // lower case, junk before the frame
    CHECK(adu.size() == 6);
    CHECK_FALSE(modbus_ascii_decode(":01030000000AF3\r\n", adu));           // wrong LRC
    CHECK_FALSE(modbus_ascii_decode(":01030000000AF\r\n", adu));            // odd number of digits
    CHECK_FALSE(modbus_ascii_decode(":0103000000ZZF2\r\n", adu));
    CHECK_FALSE(modbus_ascii_decode("01030000000AF2\r\n", adu));            // no start character
    CHECK_FALSE(modbus_ascii_decode(":01\r\n", adu));
}

TEST_CASE("modbus frames: the length of an RTU response is known from its first bytes")
{
    const uint8_t rd[] = { 0x01, 0x03, 0x04, 0, 0, 0, 0 };
    CHECK(modbus_rtu_response_len(rd, 1) == 0);
    CHECK(modbus_rtu_response_len(rd, 2) == 0);                             // the byte count is still missing
    CHECK(modbus_rtu_response_len(rd, 3) == 3 + 4 + 2);
    const uint8_t wr[] = { 0x01, 0x06 };
    CHECK(modbus_rtu_response_len(wr, 2) == 8);
    const uint8_t wr16[] = { 0x01, 0x10 };
    CHECK(modbus_rtu_response_len(wr16, 2) == 8);
    const uint8_t ex[] = { 0x01, 0x83 };
    CHECK(modbus_rtu_response_len(ex, 2) == 5);
    const uint8_t odd[] = { 0x01, 0x2B };
    CHECK(modbus_rtu_response_len(odd, 2) == -1);
}

TEST_CASE("serial settings: description and character time")
{
    SerialSettings s;
    s.device = "/dev/ttyUSB0";
    CHECK(s.describe() == "/dev/ttyUSB0 9600 8N1");
    CHECK(s.char_time_us(1) == 1041);                                       // 10 bits at 9600 baud
    s.baud = 19200; s.data_bits = 7; s.parity = 'E';
    CHECK(s.describe() == "/dev/ttyUSB0 19200 7E1");
    CHECK(s.char_time_us(3.5) == 1822);
}

// ---------------------------------------------------------------- drivers

static void exercise(const char* type, bool ascii)
{
    Runtime rt;
    FakeLine line(ascii);
    {
        std::lock_guard<std::mutex> lock(line.s.mu);
        line.s.holding[1][0] = 1234;
        line.s.holding[1][1] = 0xFFFE;                  // s16 -2
        line.s.holding[1][2] = 250;                     // scaled
        line.s.holding[1][3] = 0x3FC0; line.s.holding[1][4] = 0;   // float 1.5
        line.s.input[1][0] = 77;
        line.s.coils[1][2] = true;
        line.s.holding[2][0] = 2222;                    // the same address on the second unit
    }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "h", "hs", "hsc", "ir", "u2", "u9", "bad" }) rt.tag(n, TagType::Int);
    rt.tag("hf", TagType::Float);
    rt.tag("c", TagType::Bool);
    json map = json::parse(R"([
        { "tag": "h",   "area": "holding", "addr": 0 },
        { "tag": "hs",  "area": "holding", "addr": 1, "dtype": "s16" },
        { "tag": "hsc", "area": "holding", "addr": 2, "scale": 0.1 },
        { "tag": "hf",  "area": "holding", "addr": 3, "dtype": "float" },
        { "tag": "ir",  "area": "input",   "addr": 0 },
        { "tag": "c",   "area": "coil",    "addr": 2 },
        { "tag": "u2",  "area": "holding", "addr": 0, "unit_id": 2 },
        { "tag": "u9",  "area": "holding", "addr": 0, "unit_id": 9 },
        { "tag": "bad", "area": "holding", "addr": 61 }
    ])");
    REQUIRE(start(rt, line_cfg(type, line.port(), map)) == 1);

    // ---- everything that answers is read; the silent unit 9 does not stop the others
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("h") == 1234 && tagv("u2") == 2222; }));
    CHECK(tagv("hs") == -2);
    CHECK(tagv("hsc") == 25);
    CHECK(tagv("hf") == doctest::Approx(1.5));
    CHECK(tagv("ir") == 77);
    CHECK(tagv("c") == 1);
    CHECK(tagv("u9") == 0);
    CHECK(tagv("bad") == 0);                             // an exception answer: the unit is alive, the value stays

    // ---- writes: single register, two registers, coil, the other unit
    tags().find("h")->set_number(4321, nullptr);
    tags().find("hf")->set_number(-2.5, nullptr);
    tags().find("c")->set_number(0, nullptr);
    tags().find("u2")->set_number(99, nullptr);
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> lock(line.s.mu); return line.s.holding[1][0] == 4321 && line.s.holding[2][0] == 99 && !line.s.coils[1][2]; }));
    {
        std::lock_guard<std::mutex> lock(line.s.mu);
        CHECK(line.s.holding[1][3] == 0xC020);
        CHECK(line.s.holding[1][4] == 0x0000);
    }
    CHECK(tagv("h") == 4321);
    CHECK(tagv("online") == 1);

    // ---- damaged answers are not taken for values
    {
        std::lock_guard<std::mutex> lock(line.s.mu);
        line.s.holding[1][0] = 5;
    }
    line.s.corrupt = 3;
    REQUIRE(wait_for([] { return tagv("h") == 5; }));    // after the damaged ones, a good one arrives
    CHECK(line.s.corrupt == 0);

    // ---- nobody answers: offline, and online again when they are back
    line.s.mute = true;
    REQUIRE(wait_for([] { return tagv("online") == 0; }));
    { std::lock_guard<std::mutex> lock(line.s.mu); line.s.holding[1][0] = 6; }
    line.s.mute = false;
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("h") == 6; }, 8000));
}

TEST_CASE("modbus_rtu: polls, writes, several units, damaged frames, offline and back")
{
    exercise("modbus_rtu", false);
}

TEST_CASE("modbus_ascii: polls, writes, several units, damaged frames, offline and back")
{
    exercise("modbus_ascii", true);
}

TEST_CASE("modbus_rtu: a silent unit is asked once per cycle, not once per item")
{
    Runtime rt;
    FakeLine line(false);
    rt.tag("online", TagType::Bool);
    for(const char* n : { "a", "b", "c", "d", "ok" }) rt.tag(n, TagType::Int);
    json map = json::parse(R"([
        { "tag": "a", "area": "holding", "addr": 0, "unit_id": 7 },
        { "tag": "b", "area": "holding", "addr": 1, "unit_id": 7 },
        { "tag": "c", "area": "holding", "addr": 2, "unit_id": 7 },
        { "tag": "d", "area": "holding", "addr": 3, "unit_id": 7 },
        { "tag": "ok", "area": "holding", "addr": 0 }
    ])");
    { std::lock_guard<std::mutex> lock(line.s.mu); line.s.holding[1][0] = 8; }
    REQUIRE(start(rt, line_cfg("modbus_rtu", line.port(), map)) == 1);
    REQUIRE(wait_for([] { return tagv("ok") == 8; }));
    sleep_ms(700);
    std::lock_guard<std::mutex> lock(line.s.mu);
    // every cycle: one request to unit 7 (150 ms timeout), one to unit 1
    CHECK(line.s.requests_to[7] >= 2);
    CHECK(line.s.requests_to[7] <= line.s.requests_to[1] + 1);
    CHECK(tagv("online") == 1);                          // unit 1 answers: the line is online
}

TEST_CASE("modbus_rtu / modbus_ascii: configuration")
{
    Runtime rt;
    rt.tag("x", TagType::Int);
    json map = json::parse(R"([{ "tag": "x", "area": "holding", "addr": 0 }])");
    // neither a serial device nor a host
    json none = json{ { "type", "modbus_rtu" }, { "map", map } };
    CHECK(start(rt, none) == 0);
    json no_map = json{ { "type", "modbus_ascii" }, { "device", "/dev/null" } };
    CHECK(start(rt, no_map) == 0);
    // a device that is not there: the driver starts, stays offline and stops promptly
    rt.tag("online", TagType::Bool, 1);
    json gone = json{ { "type", "modbus_rtu" }, { "device", "/dev/hml-no-such-port" }, { "status_tag", "online" }, { "map", map } };
    REQUIRE(start(rt, gone) == 1);
    CHECK(wait_for([] { return tagv("online") == 0; }));
    uint32_t t0 = millis();
    rt.drivers.stop_all();
    CHECK(millis() - t0 < 1500);
}

#ifndef _WIN32
// The serial port code itself, on a pseudo terminal: the test plays the slave on the master side.
TEST_CASE("modbus_rtu over a (pseudo) serial port")
{
    Runtime rt;
    int master = -1, slave = -1;
    char name[128] = {};
    REQUIRE(openpty(&master, &slave, name, nullptr, nullptr) == 0);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);

    Slaves s;
    s.holding[1][0] = 321;
    std::atomic<bool> running{ true };
    std::thread line([&] {
        std::string buf;
        while(running) {
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(master, &rd);
            timeval tv{ 0, 20000 };
            if(select(master + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
            char in[256];
            ssize_t n = read(master, in, sizeof(in));
            if(n <= 0) continue;
            std::string out = s.feed(false, buf, in, static_cast<int>(n));
            if(!out.empty()) { ssize_t w = write(master, out.data(), out.size()); (void)w; }
        }
    });

    rt.tag("online", TagType::Bool);
    rt.tag("v", TagType::Int);
    json cfg = json{ { "type", "modbus_rtu" }, { "device", name }, { "baud", 19200 }, { "parity", "even" }, { "poll_ms", 20 }, { "timeout_ms", 200 },
                     { "status_tag", "online" }, { "map", json::parse(R"([{ "tag": "v", "area": "holding", "addr": 0 }])") } };
    REQUIRE(start(rt, cfg) == 1);
    CHECK(wait_for([] { return tagv("online") == 1 && tagv("v") == 321; }));
    tags().find("v")->set_number(654, nullptr);
    CHECK(wait_for([&] { std::lock_guard<std::mutex> lock(s.mu); return s.holding[1][0] == 654; }));

    rt.drivers.stop_all();
    running = false;
    line.join();
    close(master);
    close(slave);
}
#endif
