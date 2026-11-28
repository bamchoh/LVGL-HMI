// S7 (Siemens) client: packets and addresses byte by byte (s7.hpp), and the driver end to end
// against an in-process fake PLC (ISO-on-TCP, S7comm). Grouped by ISO/IEC 25010 characteristic.
// The fake PLC is written from the same descriptions as the driver: tools/snap7_check.py checks
// the driver against another implementation (the Snap7 server).
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
#include "s7.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

using Bytes = std::vector<uint8_t>;

int64_t now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- fake PLC

constexpr uint8_t kI = 0x81, kQ = 0x82, kM = 0x83, kDB = 0x84;

class FakeS7Plc {
public:
    FakeS7Plc()
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        for(uint8_t a : { kI, kQ, kM }) mem[{ a, 0 }] = Bytes(4096, 0);
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
    ~FakeS7Plc()
    {
        running_ = false;
        if(thread_.joinable()) thread_.join();
        close_sock(listen_);
    }
    uint16_t port() const { return port_; }

    // memory: (area, DB number) -> its bytes. I, Q and M are there (4096 bytes); a DB is made by the test.
    std::map<std::pair<uint8_t, uint16_t>, Bytes> mem;
    std::mutex mutex;
    Bytes& db(uint16_t n, size_t size = 0) { Bytes& b = mem[{ kDB, n }]; if(size) b.resize(size, 0); return b; }
    Bytes& area(uint8_t a) { return mem[{ a, 0 }]; }

    std::atomic<int> requests{ 0 }, read_items{ 0 }, writes{ 0 }, connections{ 0 }, setups{ 0 };
    std::atomic<int> too_long{ 0 };                 // requests or answers above the PDU length agreed
    std::atomic<int> pdu{ 480 };                    // the PDU length the PLC answers
    std::atomic<int> tsap{ -1 };                    // the last byte of the destination TSAP asked for
    std::atomic<uint16_t> refuse{ 0 };              // answer every read / write with this error class << 8 | code
    std::atomic<uint8_t> item_code{ 0 };            // ... or every item with this return code
    std::atomic<bool> refuse_connect{ false };      // answer the connection request with a disconnect
    std::atomic<bool> deaf{ false };                // say nothing to a connection request
    std::atomic<bool> in_parts{ false };            // send every answer as two COTP data packets
    std::atomic<int64_t> cycle_start_us{ 0 };       // when the first read came in since this was 0
    // replace the S7 bytes of an answer to a read / write (to send something broken); empty = normal answer.
    // { 0xEE }: close the connection, { 0xAA }: say nothing, { 0xBB, ... }: send the rest as it is (no TPKT)
    std::function<Bytes(const Bytes& pdu)> override_answer;
    std::vector<Bytes> frames;                      // the S7 bytes of every read / write seen (guarded by mutex)

    static uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
    static void put16(Bytes& b, size_t v) { b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
    // the S7 bytes of an answer with data
    static Bytes ack(uint16_t ref, const Bytes& param, const Bytes& data, uint16_t error = 0)
    {
        Bytes r{ 0x32, 0x03, 0x00, 0x00 };
        put16(r, ref); put16(r, param.size()); put16(r, data.size());
        r.push_back(static_cast<uint8_t>(error >> 8)); r.push_back(static_cast<uint8_t>(error));
        r.insert(r.end(), param.begin(), param.end());
        r.insert(r.end(), data.begin(), data.end());
        return r;
    }
    int limit() const { const int p = pdu; return p < 240 ? 240 : p > 480 ? 480 : p; }

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
    static bool send_raw(sock_t c, const Bytes& b)
    {
        return send(c, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0) == static_cast<int>(b.size());
    }
    static Bytes packet(const Bytes& s7, size_t from, size_t to, bool last)
    {
        Bytes b{ 0x03, 0x00 };
        put16(b, 4 + 3 + (to - from));
        b.insert(b.end(), { 0x02, 0xF0, static_cast<uint8_t>(last ? 0x80 : 0x00) });
        b.insert(b.end(), s7.begin() + static_cast<std::ptrdiff_t>(from), s7.begin() + static_cast<std::ptrdiff_t>(to));
        return b;
    }
    bool send_s7(sock_t c, const Bytes& s7)
    {
        if(in_parts && s7.size() > 4) {
            Bytes b = packet(s7, 0, s7.size() / 2, false);
            const Bytes rest = packet(s7, s7.size() / 2, s7.size(), true);
            b.insert(b.end(), rest.begin(), rest.end());
            return send_raw(c, b);
        }
        return send_raw(c, packet(s7, 0, s7.size(), true));
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
            uint8_t h[4];
            if(!recv_all(c, h, 4)) return;
            if(h[0] != 0x03 || h[1] != 0x00) return;
            const int len = get16(h + 2);
            if(len < 7 || len > 2000) return;
            Bytes body(static_cast<size_t>(len - 4));
            if(!recv_all(c, body.data(), static_cast<int>(body.size()))) return;
            if(body[1] == 0xE0) {                               // the connection request
                tsap = body.back();
                if(deaf) continue;
                if(refuse_connect) { send_raw(c, { 0x03, 0x00, 0x00, 0x0B, 0x06, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00 }); return; }
                if(!send_raw(c, { 0x03, 0x00, 0x00, 0x16, 0x11, 0xD0, 0x00, 0x01, 0x00, 0x02, 0x00, 0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, body.back() })) return;
                continue;
            }
            if(body.size() < 3 + 12 || body[0] != 0x02 || body[1] != 0xF0 || body[3] != 0x32 || body[4] != 0x01) return;
            const Bytes s7(body.begin() + 3, body.end());
            const uint16_t ref = get16(s7.data() + 4);
            if(s7[10] == 0xF0) {                                // setup communication
                setups++;
                Bytes p{ 0xF0, 0x00, 0x00, 0x01, 0x00, 0x01 };
                put16(p, static_cast<size_t>(pdu.load()));
                if(!send_s7(c, ack(ref, p, {}))) return;
                continue;
            }
            if(static_cast<int>(s7.size()) > limit()) too_long++;
            Bytes ans;
            {
                std::lock_guard<std::mutex> g(mutex);
                frames.push_back(s7);
                if(override_answer) ans = override_answer(s7);
                if(ans.empty()) ans = answer(s7, ref);
            }
            if(ans.size() == 1 && ans[0] == 0xEE) return;      // "close the connection"
            if(ans.size() == 1 && ans[0] == 0xAA) continue;     // "say nothing"
            if(ans.size() > 1 && ans[0] == 0xBB) { if(!send_raw(c, Bytes(ans.begin() + 1, ans.end()))) return; continue; }
            if(static_cast<int>(ans.size()) > limit()) too_long++;
            if(!send_s7(c, ans)) return;
        }
    }
    // an item of a request: 12 0A 10 | transport size | count (2) | DB (2) | area | address (3)
    struct Var { uint8_t transport, area; uint16_t count, db; uint32_t byte; int bit; };
    static Var var(const uint8_t* p)
    {
        const uint32_t address = (static_cast<uint32_t>(p[9]) << 16) | (static_cast<uint32_t>(p[10]) << 8) | p[11];
        return { p[3], p[8], get16(p + 4), get16(p + 6), address / 8, static_cast<int>(address % 8) };
    }
    // the bytes an item names, or the return code why not
    uint8_t find(const Var& v, size_t bytes, uint8_t*& at)
    {
        auto it = mem.find({ v.area, static_cast<uint16_t>(v.area == kDB ? v.db : 0) });
        if(it == mem.end()) return 0x0A;
        if(v.byte + bytes > it->second.size()) return 0x05;
        at = it->second.data() + v.byte;
        return 0xFF;
    }
    Bytes answer(const Bytes& s7, uint16_t ref)
    {
        const size_t param = get16(s7.data() + 6);
        const uint8_t function = s7[10];
        if(function == 0x04) {
            if(cycle_start_us == 0) cycle_start_us = now_us();
            requests++;
            if(refuse) return ack(ref, {}, {}, refuse);
            const size_t n = s7[11];
            Bytes data;
            for(size_t i = 0; i < n && 12 + i * 12 + 12 <= s7.size(); i++) {
                read_items++;
                const Var v = var(s7.data() + 12 + i * 12);
                const size_t bytes = v.transport == 0x01 ? 1 : v.count;
                uint8_t* at = nullptr;
                uint8_t code = find(v, bytes, at);
                if(item_code) code = item_code;
                if(code != 0xFF) { data.insert(data.end(), { code, 0x00, 0x00, 0x00 }); continue; }
                if(v.transport == 0x01) data.insert(data.end(), { 0xFF, 0x03, 0x00, 0x01, static_cast<uint8_t>((*at >> v.bit) & 1) });
                else {
                    data.insert(data.end(), { 0xFF, 0x04 });
                    put16(data, bytes * 8);
                    data.insert(data.end(), at, at + bytes);
                }
                if(bytes % 2 && i + 1 < n) data.push_back(0x00);
            }
            return ack(ref, { 0x04, static_cast<uint8_t>(n) }, data);
        }
        if(function == 0x05 && s7.size() >= 10 + param + 4) {
            if(refuse) return ack(ref, {}, {}, refuse);
            writes++;
            const Var v = var(s7.data() + 12);
            const uint8_t* d = s7.data() + 10 + param;         // 00 | transport size | length | data
            const bool bit = d[1] == 0x03;
            const size_t bytes = bit ? 1 : get16(d + 2) / 8;
            uint8_t* at = nullptr;
            uint8_t code = s7.size() == 10 + param + 4 + bytes ? find(v, bytes, at) : static_cast<uint8_t>(0x07);
            if(item_code) code = item_code;
            if(code == 0xFF) {
                if(bit) *at = d[4] ? static_cast<uint8_t>(*at | (1u << v.bit)) : static_cast<uint8_t>(*at & ~(1u << v.bit));
                else std::memcpy(at, d + 4, bytes);
            }
            return ack(ref, { 0x05, 0x01 }, { code });
        }
        return ack(ref, {}, {}, 0x8104);
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

// the items (area, DB, byte, count) of the reads the PLC saw
std::set<std::tuple<uint8_t, uint16_t, uint32_t, uint16_t>> read_targets(FakeS7Plc& srv)
{
    std::lock_guard<std::mutex> g(srv.mutex);
    std::set<std::tuple<uint8_t, uint16_t, uint32_t, uint16_t>> out;
    for(const Bytes& f : srv.frames) {
        if(f.size() < 12 || f[10] != 0x04) continue;
        for(size_t i = 0; i < f[11] && 24 + i * 12 <= f.size(); i++) {
            const uint8_t* p = f.data() + 12 + i * 12;
            const uint32_t address = (static_cast<uint32_t>(p[9]) << 16) | (static_cast<uint32_t>(p[10]) << 8) | p[11];
            out.insert({ p[8], FakeS7Plc::get16(p + 6), address / 8, FakeS7Plc::get16(p + 4) });
        }
    }
    return out;
}

json s7_cfg(const FakeS7Plc& srv, const json& map)
{
    return json{ { "type", "s7" }, { "host", "127.0.0.1" }, { "port", srv.port() }, { "poll_ms", 20 }, { "timeout_ms", 400 },
                 { "status_tag", "online" }, { "map", map } };
}
int start(Runtime& rt, const json& cfg) { json arr = json::array({ cfg }); return rt.drivers.load(&arr); }

// System > Device: ask the driver for an address no tag is mapped to, wait for the answer
DeviceResult ask(Driver* d, const char* address, const char* type = "", int count = 1)
{
    DeviceRequest q;
    q.address = address; q.type = type; q.count = count;
    const uint64_t id = d->device_post(q);
    REQUIRE(id != 0);
    DeviceResult r;
    REQUIRE(wait_for([&] { return d->device_take(id, r); }));
    return r;
}
DeviceResult put(Driver* d, const char* address, const char* value, const char* type = "")
{
    DeviceRequest q;
    q.address = address; q.type = type; q.write = true; q.value = value;
    const uint64_t id = d->device_post(q);
    REQUIRE(id != 0);
    DeviceResult r;
    REQUIRE(wait_for([&] { return d->device_take(id, r); }));
    return r;
}
using Strings = std::vector<std::string>;

// what the runtime logs while this lives
std::mutex g_log_mutex;
std::vector<std::string> g_log;
void capture(LogLevel level, const std::string& msg)
{
    if(level == LogLevel::Info) return;
    std::lock_guard<std::mutex> l(g_log_mutex);
    g_log.push_back(msg);
}
struct LogCapture {
    LogCapture() { { std::lock_guard<std::mutex> l(g_log_mutex); g_log.clear(); } set_log_sink(capture); }
    ~LogCapture() { set_log_sink(nullptr); }
    int count(const std::string& part) const
    {
        std::lock_guard<std::mutex> l(g_log_mutex);
        int n = 0;
        for(const std::string& m : g_log) if(m.find(part) != std::string::npos) n++;
        return n;
    }
    bool has(const std::string& part) const { return count(part) > 0; }
};

void set(Bytes& b, size_t at, std::initializer_list<uint8_t> bytes) { size_t i = at; for(uint8_t x : bytes) b[i++] = x; }
Bytes get(const Bytes& b, size_t at, size_t n) { return Bytes(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + n)); }

} // namespace

// ================================================================ functional suitability: packets

TEST_CASE("s7: addresses as STEP 7 and TIA Portal write them")
{
    auto a = s7_parse_address("DB1.DBX0.0");
    REQUIRE(a);
    CHECK(a->area == 0x84);
    CHECK(a->db == 1);
    CHECK(a->byte == 0);
    CHECK(a->bit == 0);
    CHECK(a->size == 1);
    a = s7_parse_address("db12.dbx300.7");                              // any case
    REQUIRE(a);
    CHECK(a->db == 12);
    CHECK(a->byte == 300);
    CHECK(a->bit == 7);
    a = s7_parse_address("DB1.DBB2");
    REQUIRE(a);
    CHECK(a->bit == -1);
    CHECK(a->size == 1);
    CHECK(a->byte == 2);
    CHECK(s7_parse_address("DB1.DBW4")->size == 2);
    CHECK(s7_parse_address("DB1.DBD6")->size == 4);
    CHECK(s7_parse_address("DB65535.DBW0")->db == 65535);
    CHECK(s7_parse_address(" %DB1.DBX0.0 ")->bit == 0);                 // as TIA Portal shows it
    // flags, inputs, outputs; a bit with or without X
    a = s7_parse_address("M0.0");
    REQUIRE(a);
    CHECK(a->area == 0x83);
    CHECK(a->db == 0);
    CHECK(a->bit == 0);
    CHECK(s7_parse_address("%M10.3")->byte == 10);
    CHECK(s7_parse_address("MX10.3")->bit == 3);
    CHECK(s7_parse_address("MB1")->size == 1);
    CHECK(s7_parse_address("MB1")->bit == -1);
    CHECK(s7_parse_address("MW2")->size == 2);
    CHECK(s7_parse_address("MD4")->size == 4);
    CHECK(s7_parse_address("I0.0")->area == 0x81);
    CHECK(s7_parse_address("IB0")->area == 0x81);
    CHECK(s7_parse_address("IW0")->size == 2);
    CHECK(s7_parse_address("ID0")->size == 4);
    CHECK(s7_parse_address("Q0.1")->area == 0x82);
    CHECK(s7_parse_address("QB0")->area == 0x82);
    CHECK(s7_parse_address("QW2")->size == 2);
    CHECK(s7_parse_address("QD4")->size == 4);
    // the German letters: E = I, A = Q
    CHECK(s7_parse_address("E0.7")->area == 0x81);
    CHECK(s7_parse_address("EW2")->area == 0x81);
    CHECK(s7_parse_address("A1.1")->area == 0x82);
    CHECK(s7_parse_address("AB0")->area == 0x82);
    CHECK(s7_parse_address("AD4")->size == 4);
    CHECK(s7_parse_address("MW65534"));
    CHECK(s7_parse_address("MB65535"));

    CHECK(s7_format_address(*s7_parse_address("db1.dbx0.0")) == "DB1.DBX0.0");
    CHECK(s7_format_address(*s7_parse_address("%db7.dbd0012")) == "DB7.DBD12");
    CHECK(s7_format_address(*s7_parse_address("mx1.2")) == "M1.2");
    CHECK(s7_format_address(*s7_parse_address("e0.7")) == "I0.7");
    CHECK(s7_format_address(*s7_parse_address("aw6")) == "QW6");
    CHECK(s7_format_address(*s7_parse_address("MB1")) == "MB1");

    std::string err;
    CHECK_FALSE(s7_parse_address("", &err));
    CHECK_FALSE(s7_parse_address("%", &err));
    CHECK_FALSE(s7_parse_address("D100", &err));                        // (another maker's)
    CHECK(err.find("unknown area") != std::string::npos);
    CHECK(err.find("D100") != std::string::npos);
    CHECK_FALSE(s7_parse_address("T5", &err));
    CHECK_FALSE(s7_parse_address("DB1", &err));
    CHECK_FALSE(s7_parse_address("DB1.DBQ0", &err));
    CHECK(err.find("DBX") != std::string::npos);
    CHECK_FALSE(s7_parse_address("DB1.W4", &err));
    CHECK_FALSE(s7_parse_address("DB1.DB4", &err));
    CHECK_FALSE(s7_parse_address("DB0.DBW0", &err));
    CHECK(err.find("DB number") != std::string::npos);
    CHECK_FALSE(s7_parse_address("DB65536.DBW0", &err));
    CHECK_FALSE(s7_parse_address("DBX.DBW0", &err));
    CHECK_FALSE(s7_parse_address("DB1.DBX0", &err));                    // a bit needs its number
    CHECK(err.find("0..7") != std::string::npos);
    CHECK_FALSE(s7_parse_address("M0", &err));
    CHECK_FALSE(s7_parse_address("M0.8", &err));
    CHECK_FALSE(s7_parse_address("M0.", &err));
    CHECK_FALSE(s7_parse_address("M0.10", &err));
    CHECK_FALSE(s7_parse_address("MW2.1", &err));                       // a word has no bit number
    CHECK_FALSE(s7_parse_address("MW", &err));
    CHECK_FALSE(s7_parse_address("M", &err));
    CHECK_FALSE(s7_parse_address("MW1A", &err));
    CHECK(err.find("decimal") != std::string::npos);
    CHECK_FALSE(s7_parse_address("MW-1", &err));
    CHECK_FALSE(s7_parse_address("MW65535", &err));                     // its second byte would be 65536
    CHECK_FALSE(s7_parse_address("MD65533", &err));
    CHECK_FALSE(s7_parse_address("MB65536", &err));
    CHECK_FALSE(s7_parse_address("MB999999", &err));
    CHECK_FALSE(s7_parse_address("%%M0.0", &err));
    CHECK_FALSE(s7_parse_address(std::string(5000, 'M'), &err));
    CHECK(err.size() < 200);                                            // (what was typed is not quoted whole)
}

TEST_CASE("s7: connection, setup, read and write packets byte by byte")
{
    // the connection request: the destination TSAP names rack and slot
    CHECK(s7_connect_request(0, 1) == Bytes{ 0x03, 0x00, 0x00, 0x16, 0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
                                             0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x01 });
    CHECK(s7_connect_request(0, 2).back() == 0x02);
    CHECK(s7_connect_request(1, 3).back() == 0x23);
    CHECK(s7_connect_request(0, 2)[20] == 0x01);

    CHECK(s7_setup_request(1, 480) == Bytes{ 0x03, 0x00, 0x00, 0x19, 0x02, 0xF0, 0x80,
                                             0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
                                             0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0 });

    // read DB1 bytes 4..5 and the bit M10.3 (address = byte * 8 + bit)
    S7Item word; word.area = 0x84; word.db = 1; word.byte = 4; word.count = 2;
    S7Item bit; bit.area = 0x83; bit.byte = 10; bit.bit = 3;
    CHECK(s7_read_request(0x0102, { word, bit }) == Bytes{ 0x03, 0x00, 0x00, 0x2B, 0x02, 0xF0, 0x80,
                                                         0x32, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x1A, 0x00, 0x00,
                                                         0x04, 0x02,
                                                         0x12, 0x0A, 0x10, 0x02, 0x00, 0x02, 0x00, 0x01, 0x84, 0x00, 0x00, 0x20,
                                                         0x12, 0x0A, 0x10, 0x01, 0x00, 0x01, 0x00, 0x00, 0x83, 0x00, 0x00, 0x53 });
    // a far byte: the address takes three bytes
    S7Item end; end.area = 0x84; end.db = 0x1234; end.byte = 65535; end.count = 1;
    const Bytes rf = s7_read_request(1, { end });
    CHECK(Bytes(rf.end() - 12, rf.end()) == Bytes{ 0x12, 0x0A, 0x10, 0x02, 0x00, 0x01, 0x12, 0x34, 0x84, 0x07, 0xFF, 0xF8 });

    // write a word: the length of the data is in bits
    const uint8_t w[] = { 0x12, 0x34 };
    CHECK(s7_write_request(5, word, w, 2) == Bytes{ 0x03, 0x00, 0x00, 0x25, 0x02, 0xF0, 0x80,
                                                    0x32, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x0E, 0x00, 0x06,
                                                    0x05, 0x01,
                                                    0x12, 0x0A, 0x10, 0x02, 0x00, 0x02, 0x00, 0x01, 0x84, 0x00, 0x00, 0x20,
                                                    0x00, 0x04, 0x00, 0x10, 0x12, 0x34 });
    // write one bit: Q0.3 = 1
    S7Item q; q.area = 0x82; q.byte = 0; q.bit = 3;
    const uint8_t on[] = { 7 };
    CHECK(s7_write_request(6, q, on, 1) == Bytes{ 0x03, 0x00, 0x00, 0x24, 0x02, 0xF0, 0x80,
                                                  0x32, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0E, 0x00, 0x05,
                                                  0x05, 0x01,
                                                  0x12, 0x0A, 0x10, 0x01, 0x00, 0x01, 0x00, 0x00, 0x82, 0x00, 0x00, 0x03,
                                                  0x00, 0x03, 0x00, 0x01, 0x01 });

    // TPKT, the connection confirm, a data packet
    const uint8_t tp[] = { 0x03, 0x00, 0x00, 0x16 };
    CHECK(s7_tpkt_body_len(tp, 100) == 18);
    const Bytes cc{ 0x11, 0xD0, 0x00, 0x01, 0x00, 0x02, 0x00, 0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x01 };
    CHECK(s7_parse_connect_confirm(cc.data(), static_cast<int>(cc.size())));
    const Bytes dt{ 0x02, 0xF0, 0x80, 0x32 };
    bool last = false;
    CHECK(s7_cotp_data(dt.data(), 4, &last) == 3);
    CHECK(last);
    const Bytes more{ 0x02, 0xF0, 0x00, 0x32 };
    CHECK(s7_cotp_data(more.data(), 4, &last) == 3);
    CHECK_FALSE(last);

    // the answer to the setup: the PDU length the CPU gives
    const Bytes setup{ 0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0xF0 };
    S7Response r;
    REQUIRE(s7_parse_response(setup.data(), static_cast<int>(setup.size()), r));
    uint16_t pdu = 0;
    CHECK(s7_parse_setup(r, &pdu));
    CHECK(pdu == 240);

    // the answer to a read of two items: one byte (filled up to an even length), one word
    const Bytes resp{ 0x32, 0x03, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x00, 0x04, 0x02,
                      0xFF, 0x04, 0x00, 0x08, 0xAB, 0x00, 0xFF, 0x04, 0x00, 0x10, 0x12, 0x34 };
    REQUIRE(s7_parse_response(resp.data(), static_cast<int>(resp.size()), r));
    CHECK(r.rosctr == 3);
    CHECK(r.ref == 0x0102);
    CHECK(r.error_class == 0);
    CHECK(r.param == Bytes{ 0x04, 0x02 });
    std::vector<S7ItemResult> items;
    REQUIRE(s7_parse_read_data(r, 2, items));
    CHECK(items[0].code == 0xFF);
    CHECK(items[0].data == Bytes{ 0xAB });
    CHECK(items[1].data == Bytes{ 0x12, 0x34 });
    CHECK_FALSE(s7_parse_read_data(r, 3, items));                       // not the number asked for
    // a refused item has no data; a bit counts in bits; a REAL and octets count in bytes
    r.data = { 0x0A, 0x00, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x01, 0x01, 0x00, 0xFF, 0x07, 0x00, 0x04, 0x41, 0x48, 0x00, 0x00, 0xFF, 0x09, 0x00, 0x02, 0x41, 0x42 };
    r.param = { 0x04, 0x04 };
    REQUIRE(s7_parse_read_data(r, 4, items));
    CHECK(items[0].code == 0x0A);
    CHECK(items[0].data.empty());
    CHECK(items[1].data == Bytes{ 0x01 });
    CHECK(items[2].data == Bytes{ 0x41, 0x48, 0x00, 0x00 });
    CHECK(items[3].data == Bytes{ 0x41, 0x42 });

    // the answer to a write: a return code per item
    r.param = { 0x05, 0x01 };
    r.data = { 0x05 };
    Bytes codes;
    REQUIRE(s7_parse_write_data(r, 1, codes));
    CHECK(codes == Bytes{ 0x05 });

    // an error in the header
    const Bytes refused{ 0x32, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x81, 0x04 };
    REQUIRE(s7_parse_response(refused.data(), static_cast<int>(refused.size()), r));
    CHECK(r.error_class == 0x81);
    CHECK(r.error_code == 0x04);

    // what a refusal means, in words; where PUT/GET not being permitted explains it, that is said
    CHECK(s7_item_error_text(0x05) == "address out of range (code 5)");
    CHECK(s7_item_error_text(0x0A).find("does not exist (code 10)") != std::string::npos);
    CHECK(s7_item_error_text(0x03).find("access denied (code 3)") != std::string::npos);
    CHECK(s7_item_error_text(0x03).find("PUT/GET") != std::string::npos);
    CHECK(s7_item_error_text(0x06).find("not supported") != std::string::npos);
    CHECK(s7_item_error_text(0x07).find("inconsistent") != std::string::npos);
    CHECK(s7_item_error_text(0x05).find("PUT/GET") == std::string::npos);
    CHECK(s7_item_error_text(0x77).find("(code 119)") != std::string::npos);
    CHECK(s7_error_text(0x81, 0x04).find("PUT/GET") != std::string::npos);
    CHECK(s7_error_text(0x81, 0x04).find("class 0x81, code 0x04") != std::string::npos);
    CHECK(s7_error_text(0x85, 0x00).find("PDU") != std::string::npos);
    CHECK(s7_error_text(0x87, 0x01).find("access error (class 0x87, code 0x01)") == 0);
    CHECK(s7_error_text(0x12, 0x34).find("class 0x12, code 0x34") != std::string::npos);

    CHECK(s7_max_read_bytes(480) == 462);
    CHECK(s7_max_write_bytes(240) == 212);
}

// ================================================================ security: what comes back

TEST_CASE("s7: broken, foreign and oversized answers are refused")
{
    uint8_t h[4] = { 0x03, 0x00, 0x00, 0x07 };
    CHECK(s7_tpkt_body_len(h, 100) == 3);
    h[0] = 'H';
    CHECK(s7_tpkt_body_len(h, 100) == -1);                              // not TPKT
    h[0] = 0x03; h[1] = 0x01;
    CHECK(s7_tpkt_body_len(h, 100) == -1);
    h[1] = 0x00; h[3] = 0x05;
    CHECK(s7_tpkt_body_len(h, 100) == -1);                              // shorter than a COTP header
    h[3] = 0x69;
    CHECK(s7_tpkt_body_len(h, 100) == -1);                              // more than we will take
    h[2] = 0xFF; h[3] = 0xFF;
    CHECK(s7_tpkt_body_len(h, 1024) == -1);

    std::string err;
    const Bytes dr{ 0x06, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00 };         // a disconnect instead of the confirm
    CHECK_FALSE(s7_parse_connect_confirm(dr.data(), 7, &err));
    CHECK(err.find("refused") != std::string::npos);
    const Bytes cc{ 0x11, 0xD0, 0x00, 0x01, 0x00, 0x02, 0x00 };         // says 17 bytes follow, 6 do
    CHECK_FALSE(s7_parse_connect_confirm(cc.data(), 7, &err));
    CHECK_FALSE(s7_parse_connect_confirm(cc.data(), 2, &err));
    CHECK_FALSE(s7_parse_connect_confirm(cc.data(), 0, &err));
    const Bytes dt{ 0x02, 0xF0, 0x80 };
    CHECK(s7_cotp_data(dt.data(), 3, nullptr) == 3);
    CHECK(s7_cotp_data(dt.data(), 2, nullptr) == -1);
    const Bytes notdt{ 0x02, 0xE0, 0x80 }, longer{ 0x09, 0xF0, 0x80 };
    CHECK(s7_cotp_data(notdt.data(), 3, nullptr) == -1);
    CHECK(s7_cotp_data(longer.data(), 3, nullptr) == -1);               // a header longer than the packet

    S7Response r;
    const Bytes good{ 0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x06, 0x00, 0x00, 0x04, 0x01, 0xFF, 0x04, 0x00, 0x10, 0x12, 0x34 };
    REQUIRE(s7_parse_response(good.data(), static_cast<int>(good.size()), r));
    for(int n = 0; n < static_cast<int>(good.size()); n++) {            // cut off anywhere
        const Bytes cut(good.begin(), good.begin() + n);
        CHECK_FALSE(s7_parse_response(cut.data(), n, r, &err));
    }
    Bytes b = good;
    b[0] = 0x72;                                                        // S7comm-plus: another protocol
    CHECK_FALSE(s7_parse_response(b.data(), static_cast<int>(b.size()), r, &err));
    b = good; b[1] = 0x01;                                              // a job, not an answer
    CHECK_FALSE(s7_parse_response(b.data(), static_cast<int>(b.size()), r, &err));
    b = good; b[1] = 0x07;
    CHECK_FALSE(s7_parse_response(b.data(), static_cast<int>(b.size()), r, &err));
    b = good; b[8] = 0xFF; b[9] = 0xFF;                                 // 65535 bytes of data announced
    CHECK_FALSE(s7_parse_response(b.data(), static_cast<int>(b.size()), r, &err));
    b = good; b[7] = 0x03;                                              // the lengths do not add up
    CHECK_FALSE(s7_parse_response(b.data(), static_cast<int>(b.size()), r, &err));

    // items that say more than is there
    std::vector<S7ItemResult> items;
    REQUIRE(s7_parse_response(good.data(), static_cast<int>(good.size()), r));
    REQUIRE(s7_parse_read_data(r, 1, items));
    r.data = { 0xFF, 0x04, 0xFF, 0xF8, 0x12, 0x34 };                    // 8191 bytes announced, 2 there
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    r.data = { 0xFF, 0x09, 0xFF, 0xFF, 0x12, 0x34 };                    // 65535 bytes
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    r.data = { 0xFF, 0x04, 0x00 };                                      // ends inside the header of an item
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    r.data = { 0xFF, 0x04, 0x00, 0x08, 0x12 };                          // the second item is missing
    r.param = { 0x04, 0x02 };
    CHECK_FALSE(s7_parse_read_data(r, 2, items, &err));
    r.data = { 0xFF, 0x04, 0x00, 0x10, 0x12, 0x34, 0x00, 0x00, 0x00 };  // more than its items
    r.param = { 0x04, 0x01 };
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    r.data.clear();
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    r.param = { 0x05, 0x01 };                                           // the answer to a write
    r.data = { 0xFF, 0x04, 0x00, 0x10, 0x12, 0x34 };
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    r.param = { 0x04 };
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    r.param = { 0x04, 0xFF };                                           // 255 items announced
    CHECK_FALSE(s7_parse_read_data(r, 1, items, &err));
    CHECK_FALSE(s7_parse_read_data(r, 255, items, &err));
    Bytes codes;
    r.param = { 0x05, 0x01 };
    r.data = { 0xFF, 0xFF };
    CHECK_FALSE(s7_parse_write_data(r, 1, codes, &err));
    r.data.clear();
    CHECK_FALSE(s7_parse_write_data(r, 1, codes, &err));
    r.param = { 0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01 };             // a setup answer one byte short
    uint16_t pdu = 0;
    CHECK_FALSE(s7_parse_setup(r, &pdu));
    r.param = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0 };
    CHECK_FALSE(s7_parse_setup(r, &pdu));
}

TEST_CASE("s7: random and cut-off bytes into every parser: nothing is read outside them")
{
    // (each buffer is a block of its own size on the heap: a read past its end is one a checker sees)
    std::mt19937 rng(11);
    int responses = 0, reads = 0, writes = 0, setups = 0, confirms = 0, datas = 0, addresses = 0;
    const Bytes good{ 0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x00, 0x04, 0x02,
                      0xFF, 0x04, 0x00, 0x08, 0xAB, 0x00, 0xFF, 0x04, 0x00, 0x10, 0x12, 0x34 };
    const Bytes good_setup{ 0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0xF0 };
    const Bytes good_confirm{ 0x11, 0xD0, 0x00, 0x01, 0x00, 0x02, 0x00, 0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x01 };
    const std::string letters = "DBXWMIQEA%.0123456789 dbxwm";
    for(int i = 0; i < 40000; i++) {
        Bytes b;
        switch(rng() % 7) {
            case 4:                                                     // a setup answer / a connection confirm / a data packet, changed or cut off
            case 5: {
                b = rng() % 3 == 0 ? good_confirm : good_setup;
                if(rng() % 3 == 0) b.insert(b.begin(), { 0x02, 0xF0, static_cast<uint8_t>(rng() % 2 ? 0x80 : 0x00) });
                if(rng() % 2) b[rng() % b.size()] = static_cast<uint8_t>(rng());
                if(rng() % 3 == 0) b.resize(rng() % (b.size() + 1));
                break;
            }
            case 6:                                                     // something like an address
                b.resize(1 + rng() % 12);
                for(auto& x : b) x = static_cast<uint8_t>(letters[rng() % letters.size()]);
                break;
            case 0:                                                     // noise
                b.resize(rng() % 48);
                for(auto& x : b) x = static_cast<uint8_t>(rng());
                break;
            case 1:                                                     // noise that starts like an answer
                b.resize(12 + rng() % 40);
                for(auto& x : b) x = static_cast<uint8_t>(rng());
                b[0] = 0x32; b[1] = static_cast<uint8_t>(2 + rng() % 2);
                b[6] = 0; b[7] = static_cast<uint8_t>(rng() % 4); b[8] = 0;
                b[9] = static_cast<uint8_t>(b.size() - 12 - std::min<size_t>(b[7], b.size() - 12));
                b[10] = b[11] = 0;
                if(b.size() > 13) { b[12] = static_cast<uint8_t>(4 + rng() % 2); b[13] = static_cast<uint8_t>(rng() % 5); }
                break;
            case 2:                                                     // a good answer with some bytes changed
                b = good;
                for(int k = 0; k < 3; k++) b[rng() % b.size()] = static_cast<uint8_t>(rng());
                break;
            default:                                                    // a good answer cut off
                b.assign(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(rng() % (good.size() + 1)));
                break;
        }
        const int n = static_cast<int>(b.size());
        const std::unique_ptr<uint8_t[]> exact(new uint8_t[b.size() + 1]);      // (+1: new[0] is not a place to point at)
        std::memcpy(exact.get(), b.data(), b.size());
        const uint8_t* p = exact.get();

        if(n >= 4) (void)s7_tpkt_body_len(p, 1024);
        if(s7_parse_connect_confirm(p, n)) confirms++;
        bool last = false;
        const int at = s7_cotp_data(p, n, &last);
        if(at >= 0) { datas++; CHECK(at <= n); }
        S7Response r;
        if(s7_parse_response(p, n, r)) {
            responses++;
            CHECK(static_cast<int>(r.param.size() + r.data.size()) + 12 == n);
            uint16_t pdu = 0;
            if(s7_parse_setup(r, &pdu)) setups++;
            const size_t want = 1 + rng() % 4;
            std::vector<S7ItemResult> items;
            if(s7_parse_read_data(r, want, items)) {
                reads++;
                CHECK(items.size() == want);
                size_t total = 0;
                for(const S7ItemResult& it : items) total += it.data.size();
                CHECK(total <= r.data.size());
            }
            Bytes codes;
            if(s7_parse_write_data(r, want, codes)) { writes++; CHECK(codes.size() == want); }
        }
        // the same bytes as the text of an address
        std::string text(b.begin(), b.end());
        if(rng() % 2) text = (rng() % 2 ? "DB1.DB" : "M") + text;
        std::string err;
        if(auto a = s7_parse_address(text, &err)) {
            addresses++;
            CHECK(a->byte + static_cast<uint32_t>(a->size) - 1 <= kS7MaxByte);
            CHECK(a->bit <= 7);
            CHECK(s7_parse_address(s7_format_address(*a)));
        }
        else CHECK(err.size() < 200);
    }
    for(int n : { responses, reads, writes, setups, confirms, datas, addresses }) CHECK(n > 0);     // (every parser also met something it takes)
    MESSAGE("random bytes taken as well-formed: " << responses << " answers (" << reads << " reads, " << writes << " writes, " << setups
            << " setups), " << confirms << " connection confirms, " << datas << " data packets, " << addresses << " addresses");
}

// ================================================================ functional suitability: the driver

TEST_CASE("s7: reads every area and data type, in one request")
{
    Runtime rt;
    FakeS7Plc srv;
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        Bytes& d = srv.db(1, 100);
        d[0] = 0x05;                                                    // bits 0 and 2
        d[2] = 200; d[3] = 0xC8;
        set(d, 4, { 0xFB, 0x2E });                                      // -1234
        set(d, 6, { 0x9C, 0x40 });                                      // 40000
        set(d, 8, { 0xFF, 0xFE, 0x79, 0x60 });                          // -100000
        set(d, 12, { 0xB2, 0xD0, 0x5E, 0x00 });                         // 3000000000
        set(d, 16, { 0x41, 0x48, 0x00, 0x00 });                         // 12.5
        set(d, 20, { 0x00, 0xFA });                                     // 250, scaled
        set(d, 30, { 8, 5, 'H', 'E', 'L', 'L', 'O', 'X', 'Y', 'Z' });   // STRING[8], 5 characters in use
        set(srv.db(7, 10), 0, { 0x00, 0x4D });
        Bytes& m = srv.area(kM);
        m[10] = 0x08;
        set(m, 20, { 0x12, 0x34 });
        set(m, 24, { 0x3F, 0xC0, 0x00, 0x00 });                         // 1.5
        m[28] = 7;
        set(m, 30, { 0x00, 0x05 });
        Bytes& in = srv.area(kI);
        in[0] = 0x81;
        set(in, 2, { 0x01, 0x2C });                                     // 300
        Bytes& out = srv.area(kQ);
        out[1] = 0x02; out[4] = 9;
        set(out, 6, { 0x01, 0x02 });
    }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "u8", "s8", "s16", "u16", "s32", "u32", "db7", "mw", "mb", "pmw", "iw", "qb", "aw" }) rt.tag(n, TagType::Int);
    for(const char* n : { "f", "scaled", "mf" }) rt.tag(n, TagType::Float);
    for(const char* n : { "x0", "x1", "x2", "m3", "i0", "e7", "q1" }) rt.tag(n, TagType::Bool);
    rt.tag("name", std::string(""));
    json map = json::parse(R"([
        { "tag": "x0", "addr": "DB1.DBX0.0" }, { "tag": "x1", "addr": "DB1.DBX0.1" }, { "tag": "x2", "addr": "db1.dbx0.2" },
        { "tag": "u8", "addr": "DB1.DBB2" },
        { "tag": "s8", "addr": "DB1.DBB3", "dtype": "int8" },
        { "tag": "s16", "addr": "DB1.DBW4" },
        { "tag": "u16", "addr": "DB1.DBW6", "dtype": "uint16" },
        { "tag": "s32", "addr": "DB1.DBD8" },
        { "tag": "u32", "addr": "DB1.DBD12", "dtype": "uint32" },
        { "tag": "f", "addr": "DB1.DBD16", "dtype": "float" },
        { "tag": "scaled", "addr": "DB1.DBW20", "scale": 0.1, "offset": -5 },
        { "tag": "name", "addr": "DB1.DBB30", "dtype": "string", "length": 8 },
        { "tag": "db7", "addr": "DB7.DBW0" },
        { "tag": "m3", "addr": "M10.3" }, { "tag": "mw", "addr": "MW20" }, { "tag": "mf", "addr": "MD24", "dtype": "real" },
        { "tag": "mb", "addr": "MB28" }, { "tag": "pmw", "addr": "%MW30" },
        { "tag": "i0", "addr": "I0.0" }, { "tag": "e7", "addr": "E0.7" }, { "tag": "iw", "addr": "IW2" },
        { "tag": "q1", "addr": "Q1.1" }, { "tag": "qb", "addr": "QB4" }, { "tag": "aw", "addr": "AW6" }
    ])");
    REQUIRE(start(rt, s7_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("aw") == 258 && tags_str("name") == "HELLO"; }));
    CHECK(tagv("x0") == 1);
    CHECK(tagv("x1") == 0);
    CHECK(tagv("x2") == 1);
    CHECK(tagv("u8") == 200);
    CHECK(tagv("s8") == -56);
    CHECK(tagv("s16") == -1234);
    CHECK(tagv("u16") == 40000);
    CHECK(tagv("s32") == -100000);
    CHECK(tagv("u32") == 3000000000.0);
    CHECK(tagv("f") == doctest::Approx(12.5));
    CHECK(tagv("scaled") == doctest::Approx(20));
    CHECK(tagv("db7") == 77);
    CHECK(tagv("m3") == 1);
    CHECK(tagv("mw") == 0x1234);
    CHECK(tagv("mf") == doctest::Approx(1.5));
    CHECK(tagv("mb") == 7);
    CHECK(tagv("pmw") == 5);
    CHECK(tagv("i0") == 1);
    CHECK(tagv("e7") == 1);
    CHECK(tagv("iw") == 300);
    CHECK(tagv("q1") == 1);
    CHECK(tagv("qb") == 9);
    CHECK(srv.tsap == 0x01);                                            // rack 0, slot 1

    // one item per area / DB (neighbours merged, gaps read through), all five in one request
    const auto reads = read_targets(srv);
    CHECK(reads.count({ kI, 0, 0, 4 }) == 1);
    CHECK(reads.count({ kQ, 0, 1, 7 }) == 1);
    CHECK(reads.count({ kM, 0, 10, 22 }) == 1);
    CHECK(reads.count({ kDB, 1, 0, 40 }) == 1);
    CHECK(reads.count({ kDB, 7, 0, 2 }) == 1);
    CHECK(reads.size() == 5);
    CHECK(srv.read_items == srv.requests * 5);
    CHECK(srv.too_long == 0);

    // the present length of a string is the PLC's word, but never more than was declared
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1)[31] = 200; srv.db(1)[5] = 0x2F; }
    REQUIRE(wait_for([] { return tagv("s16") == -1233; }));
    CHECK(tags_str("name") == "HELLOXYZ");
}

TEST_CASE("s7: rack and slot go into the connection request")
{
    Runtime rt;
    FakeS7Plc srv;
    rt.tag("online", TagType::Bool);
    json cfg = s7_cfg(srv, json::array());
    cfg["rack"] = 1; cfg["slot"] = 2;
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    CHECK(srv.tsap == 0x22);
    CHECK(srv.setups == 1);
}

TEST_CASE("s7: writes every data type byte for byte, a bit as one bit; write protection")
{
    Runtime rt;
    FakeS7Plc srv;
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        Bytes& d = srv.db(2, 100);
        d[0] = 0x80;
        set(d, 30, { 6, 6, 'X', 'X', 'X', 'X', 'X', 'X', 'X' });        // STRING[6] "XXXXXX", then a byte of something else
        set(d, 60, { 0x00, 0x09 });
        srv.db(3, 10);
        srv.area(kM)[0] = 0x80; srv.area(kQ)[0] = 0x01; srv.area(kI)[0] = 0x01; srv.area(kM)[10] = 0x01;
    }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "u8", "s8", "s16", "u16", "s32", "u32", "outside", "ro", "db3" }) rt.tag(n, TagType::Int);
    for(const char* n : { "f", "scaled" }) rt.tag(n, TagType::Float);
    for(const char* n : { "x1", "m1", "q2", "i0", "m10" }) rt.tag(n, TagType::Bool);
    rt.tag("name", std::string(""));
    json map = json::parse(R"([
        { "tag": "x1", "addr": "DB2.DBX0.1" },
        { "tag": "u8", "addr": "DB2.DBB1" },
        { "tag": "s8", "addr": "DB2.DBB2", "dtype": "int8" },
        { "tag": "s16", "addr": "DB2.DBW4" },
        { "tag": "u16", "addr": "DB2.DBW6", "dtype": "uint16" },
        { "tag": "s32", "addr": "DB2.DBD8" },
        { "tag": "u32", "addr": "DB2.DBD12", "dtype": "uint32" },
        { "tag": "f", "addr": "DB2.DBD16", "dtype": "float" },
        { "tag": "scaled", "addr": "DB2.DBW20", "scale": 0.1 },
        { "tag": "name", "addr": "DB2.DBB30", "dtype": "string", "length": 6 },
        { "tag": "m1", "addr": "M0.1" }, { "tag": "q2", "addr": "Q0.2" },
        { "tag": "db3", "addr": "DB3.DBW8" },
        { "tag": "outside", "addr": "DB2.DBW60" }, { "tag": "m10", "addr": "M10.0" },
        { "tag": "i0", "addr": "I0.0" },
        { "tag": "ro", "addr": "DB2.DBW22", "access": "ro" }
    ])");
    json cfg = s7_cfg(srv, map);
    cfg["write_ranges"] = json::array({ "DB2.DBB0-DB2.DBW58", "MB0-MB9", "QB0", "db3", "IB0" });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("outside") == 9 && tagv("i0") == 1 && tagv("m10") == 1; }));

    tags().find("x1")->set_number(1, nullptr);
    tags().find("u8")->set_number(200, nullptr);
    tags().find("s8")->set_number(-2, nullptr);
    tags().find("s16")->set_number(-1234, nullptr);
    tags().find("u16")->set_number(40000, nullptr);
    tags().find("s32")->set_number(-70000, nullptr);
    tags().find("u32")->set_number(3000000000.0, nullptr);
    tags().find("f")->set_number(1.5, nullptr);
    tags().find("scaled")->set_number(12.5, nullptr);
    tags().find("name")->set_string("BYE", nullptr);
    tags().find("m1")->set_number(1, nullptr);
    tags().find("q2")->set_number(1, nullptr);
    tags().find("outside")->set_number(1, nullptr);         // not in write_ranges
    tags().find("m10")->set_number(0, nullptr);             // not in write_ranges
    tags().find("i0")->set_number(0, nullptr);              // an input: not without allow_system_write, range or not
    tags().find("ro")->set_number(1, nullptr);
    tags().find("db3")->set_number(0x0A0B, nullptr);        // "db3": all of DB3
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return srv.db(3)[9] == 0x0B && srv.area(kQ)[0] == 0x05; }));
    std::lock_guard<std::mutex> g(srv.mutex);
    const Bytes& d = srv.db(2);
    CHECK(d[0] == 0x82);                                    // bit 1 set, bit 7 as it was
    CHECK(d[1] == 200);
    CHECK(d[2] == 0xFE);
    CHECK(d[3] == 0x00);
    CHECK(get(d, 4, 2) == Bytes{ 0xFB, 0x2E });
    CHECK(get(d, 6, 2) == Bytes{ 0x9C, 0x40 });
    CHECK(get(d, 8, 4) == Bytes{ 0xFF, 0xFE, 0xEE, 0x90 });
    CHECK(get(d, 12, 4) == Bytes{ 0xB2, 0xD0, 0x5E, 0x00 });
    CHECK(get(d, 16, 4) == Bytes{ 0x3F, 0xC0, 0x00, 0x00 });
    CHECK(get(d, 20, 2) == Bytes{ 0x00, 0x7D });            // 12.5 / 0.1
    CHECK(get(d, 22, 2) == Bytes{ 0x00, 0x00 });
    // the string: the declared length stays, the present length and the characters are written, the byte after is not touched
    CHECK(get(d, 30, 9) == Bytes{ 6, 3, 'B', 'Y', 'E', 0, 0, 0, 'X' });
    CHECK(get(d, 60, 2) == Bytes{ 0x00, 0x09 });
    CHECK(srv.area(kM)[0] == 0x82);
    CHECK(srv.area(kM)[10] == 0x01);
    CHECK(srv.area(kI)[0] == 0x01);
    CHECK(get(srv.db(3), 8, 2) == Bytes{ 0x0A, 0x0B });
    // the bits went as bits (transport size 1 in the item, 3 in the data), not as a whole byte
    int bit_writes = 0, writes = 0;
    for(const Bytes& f : srv.frames) {
        if(f[10] != 0x05) continue;
        writes++;
        if(f.size() == 29 && f[15] == 0x01 && f[25] == 0x03 && f[27] == 0x01 && f[28] == 0x01) bit_writes++;
    }
    CHECK(bit_writes == 3);
    CHECK(writes == 13);                                    // one per value; none for the four that may not be written
    CHECK(srv.too_long == 0);
}

TEST_CASE("s7: the inputs are written with allow_system_write; write_ranges without a valid range lets nothing through")
{
    Runtime rt;
    FakeS7Plc srv;
    rt.tag("online", TagType::Bool);
    rt.tag("i0", TagType::Bool);
    rt.tag("mw", TagType::Int);
    json cfg = s7_cfg(srv, json::parse(R"([{ "tag": "i0", "addr": "I0.0" }])"));
    cfg["allow_system_write"] = true;
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    tags().find("i0")->set_number(1, nullptr);
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return srv.area(kI)[0] == 0x01; }));
    rt.drivers.stop_all();

    cfg = s7_cfg(srv, json::parse(R"([{ "tag": "mw", "addr": "MW0" }])"));
    cfg["write_ranges"] = json::array({ "M0.0-M0.7", "nonsense", "MB9-MB0", "MB0-QB9", 5 });
    LogCapture log;
    REQUIRE(start(rt, cfg) == 1);
    CHECK(log.count("write_ranges: ") == 5);
    CHECK(log.has("nothing can be written"));
    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    const int before = srv.writes;
    tags().find("mw")->set_number(7, nullptr);
    Driver* d = rt.drivers.all().at(0);
    CHECK(put(d, "MW0", "7").error.find("write_ranges") != std::string::npos);
    sleep_ms(150);
    CHECK(srv.writes == before);
}

// ================================================================ performance efficiency: the PDU length

TEST_CASE("s7: a small PDU length: long reads and writes are split, nothing is longer than agreed")
{
    Runtime rt;
    FakeS7Plc srv;
    srv.pdu = 240;
    std::string text;
    for(int i = 0; i < 254; i++) text.push_back(static_cast<char>('A' + i % 26));
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        Bytes& d = srv.db(1, 600);
        for(int i = 0; i < 300; i++) { d[static_cast<size_t>(i) * 2] = static_cast<uint8_t>(i >> 8); d[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>(i); }
        Bytes& s = srv.db(5, 256);
        s[0] = 254; s[1] = 254;
        std::memcpy(s.data() + 2, text.data(), 254);
    }
    rt.tag("online", TagType::Bool);
    rt.tag("long", std::string(""));
    json map = json::array();
    for(int i = 0; i < 300; i++) { std::string n = "w" + std::to_string(i); rt.tag(n, TagType::Int); map.push_back({ { "tag", n }, { "addr", "DB1.DBW" + std::to_string(i * 2) } }); }
    map.push_back({ { "tag", "long" }, { "addr", "DB5.DBB0" }, { "dtype", "string" }, { "length", 254 } });
    REQUIRE(start(rt, s7_cfg(srv, map)) == 1);
    REQUIRE(wait_for([&] { return tagv("online") == 1 && tagv("w299") == 299 && tags_str("long") == text; }));
    CHECK(tagv("w0") == 0);
    CHECK(tagv("w110") == 110);
    CHECK(tagv("w111") == 111);
    CHECK(tagv("w222") == 222);
    // 240 bytes: an item carries at most 222. DB1: 222 + 222 + 156; the string (256): 222 + 34
    const auto t = read_targets(srv);
    CHECK(t.count({ kDB, 1, 0, 222 }) == 1);
    CHECK(t.count({ kDB, 1, 222, 222 }) == 1);
    CHECK(t.count({ kDB, 1, 444, 156 }) == 1);
    CHECK(t.count({ kDB, 5, 0, 222 }) == 1);
    CHECK(t.count({ kDB, 5, 222, 34 }) == 1);
    CHECK(t.size() == 5);
    const int r0 = srv.requests, i0 = srv.read_items;
    REQUIRE(wait_for([&] { return srv.read_items >= i0 + 50; }));
    CHECK((srv.read_items - i0) * 4 == doctest::Approx((srv.requests - r0) * 5).epsilon(0.2));     // 5 items in 4 requests

    // a write longer than a request can be goes in two
    std::string back(text.rbegin(), text.rend());
    const int w0 = srv.writes;
    tags().find("long")->set_string(back, nullptr);
    tags().find("w7")->set_number(-7, nullptr);
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return srv.db(1)[14] == 0xFF && srv.db(5)[255] == static_cast<uint8_t>(back[253]); }));
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        CHECK(srv.db(5)[0] == 254);
        CHECK(srv.db(5)[1] == 254);
        CHECK(std::string(srv.db(5).begin() + 2, srv.db(5).end()) == back);
        CHECK(get(srv.db(1), 14, 2) == Bytes{ 0xFF, 0xF9 });
    }
    CHECK(srv.writes == w0 + 3);
    REQUIRE(wait_for([&] { return tags_str("long") == back; }));
    CHECK(srv.too_long == 0);
}

TEST_CASE("s7: the PDU length is what the PLC answers, between 240 and what was asked for")
{
    for(int answered : { 960, 100, 480, 300 }) {
        CAPTURE(answered);
        Runtime rt;
        FakeS7Plc srv;
        srv.pdu = answered;
        { std::lock_guard<std::mutex> g(srv.mutex); Bytes& d = srv.db(1, 600); d[599] = 42; }
        rt.tag("online", TagType::Bool);
        rt.tag("first", TagType::Int);
        rt.tag("last", TagType::Int);
        json map = json::array();
        // (a tag every 30 bytes: the gaps are read through, DB1 is one run of 600 bytes)
        for(int i = 1; i < 20; i++) { std::string n = "g" + std::to_string(i); rt.tag(n, TagType::Int); map.push_back({ { "tag", n }, { "addr", "DB1.DBB" + std::to_string(i * 30) } }); }
        map.push_back({ { "tag", "first" }, { "addr", "DB1.DBB0" } });
        map.push_back({ { "tag", "last" }, { "addr", "DB1.DBB599" } });
        REQUIRE(start(rt, s7_cfg(srv, map)) == 1);
        REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("last") == 42; }));
        const int limit = answered < 240 ? 240 : answered > 480 ? 480 : answered;
        size_t longest = 0;
        {
            std::lock_guard<std::mutex> g(srv.mutex);
            for(const Bytes& f : srv.frames) longest = std::max(longest, f.size());
        }
        CHECK(longest <= static_cast<size_t>(limit));
        uint16_t biggest = 0;
        for(const auto& item : read_targets(srv)) biggest = std::max(biggest, std::get<3>(item));
        CHECK(biggest <= limit - 18);
        CHECK(biggest > limit - 18 - 62);                               // (and it is used: a block ends at a tag, at most 30 + 32 bytes early)
        CHECK(srv.too_long == 0);
        rt.drivers.stop_all();
    }
}

TEST_CASE("s7: 1000 tags are read in a few requests")
{
    Runtime rt;
    FakeS7Plc srv;
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        set(srv.db(1, 1600), 1598, { 0x00, 0x2A });
        set(srv.db(2, 400), 396, { 0x41, 0x48, 0x00, 0x00 });
        srv.area(kM)[12] = 0x08;
    }
    rt.tag("online", TagType::Bool);
    json map = json::array();
    for(int i = 0; i < 800; i++) { std::string n = "w" + std::to_string(i); rt.tag(n, TagType::Int); map.push_back({ { "tag", n }, { "addr", "DB1.DBW" + std::to_string(i * 2) } }); }
    for(int i = 0; i < 100; i++) { std::string n = "f" + std::to_string(i); rt.tag(n, TagType::Float); map.push_back({ { "tag", n }, { "addr", "DB2.DBD" + std::to_string(i * 4) }, { "dtype", "float" } }); }
    for(int i = 0; i < 100; i++) { std::string n = "m" + std::to_string(i); rt.tag(n, TagType::Bool); map.push_back({ { "tag", n }, { "addr", "M" + std::to_string(i / 8) + "." + std::to_string(i % 8) } }); }
    json cfg = s7_cfg(srv, map);
    cfg["poll_ms"] = 60;
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("w799") == 42 && tagv("f99") == doctest::Approx(12.5) && tagv("m99") == 1; }));
    // PDU length 480: an item carries at most 462 bytes. DB1 (1600 bytes): 462 + 462 + 462 + 214; DB2: 400; M: 13.
    // The three full ones fill a request each; M and the rest of DB1 share one; DB2 takes the fifth.
    const auto t = read_targets(srv);
    CHECK(t.count({ kDB, 1, 0, 462 }) == 1);
    CHECK(t.count({ kDB, 1, 1386, 214 }) == 1);
    CHECK(t.count({ kDB, 2, 0, 400 }) == 1);
    CHECK(t.count({ kM, 0, 0, 13 }) == 1);
    CHECK(t.size() == 6);
    Driver* d = rt.drivers.all().at(0);
    // one cycle: from the first request reaching the PLC to the driver having put the 1000 values into the tags
    std::vector<int64_t> cycles;
    int requests = 0;
    for(int k = 0; k < 20; k++) {
        uint64_t p = d->link_polls();
        while(d->link_polls() == p) std::this_thread::yield();          // a cycle ended: the driver waits for the next
        p = d->link_polls();
        const int r0 = srv.requests;
        srv.cycle_start_us = 0;
        while(d->link_polls() == p) std::this_thread::yield();
        cycles.push_back(now_us() - srv.cycle_start_us);
        requests = srv.requests - r0;
    }
    std::sort(cycles.begin(), cycles.end());
    CHECK(requests == 5);
    CHECK(cycles[cycles.size() / 2] < 200000);
    MESSAGE("1000 tags (800 words, 100 floats, 100 bits): " << requests << " requests per cycle, a cycle takes "
            << cycles[cycles.size() / 2] / 1000.0 << " ms (median of 20; " << cycles.front() / 1000.0 << " .. " << cycles.back() / 1000.0 << " ms)");
    CHECK(srv.too_long == 0);
    rt.drivers.stop_all();
}

// ================================================================ reliability

TEST_CASE("s7: a refused item does not stop the others; a refused request does not drop the connection")
{
    Runtime rt;
    FakeS7Plc srv;
    { std::lock_guard<std::mutex> g(srv.mutex); set(srv.db(1, 10), 0, { 0x00, 0x01 }); set(srv.area(kM), 0, { 0x00, 0x02 }); }
    rt.tag("online", TagType::Bool);
    for(const char* n : { "good", "m", "nodb", "beyond" }) rt.tag(n, TagType::Int, -1);
    json map = json::parse(R"([
        { "tag": "good", "addr": "DB1.DBW0" }, { "tag": "m", "addr": "MW0" },
        { "tag": "nodb", "addr": "DB9.DBW0" }, { "tag": "beyond", "addr": "DB1.DBW500" }
    ])");
    LogCapture log;
    REQUIRE(start(rt, s7_cfg(srv, map)) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("good") == 1 && tagv("m") == 2; }));
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1)[1] = 3; }
    REQUIRE(wait_for([] { return tagv("good") == 3; }));
    CHECK(tagv("nodb") == -1);                                          // left as they were
    CHECK(tagv("beyond") == -1);
    CHECK(srv.connections == 1);
    // said once each, with the address and the reason in words
    CHECK(log.count("reading DB9.DBB0 (2 bytes): the PLC refused: the object does not exist (code 10)") == 1);
    CHECK(log.count("reading DB1.DBB500 (2 bytes): the PLC refused: address out of range (code 5)") == 1);
    // the DB appears: the tag follows
    { std::lock_guard<std::mutex> g(srv.mutex); set(srv.db(9, 4), 0, { 0x00, 0x09 }); }
    REQUIRE(wait_for([] { return tagv("nodb") == 9; }));

    // every request refused in its header (PUT/GET not permitted): the values stay, the connection stays, said once
    srv.refuse = 0x8104;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1)[1] = 4; }
    const int before = srv.requests;
    REQUIRE(wait_for([&] { return srv.requests > before + 5; }));
    tags().dispatch(false);
    CHECK(tagv("online") == 1);
    CHECK(tagv("good") == 3);
    CHECK(srv.connections == 1);
    CHECK(log.count("PUT/GET") == 1);
    CHECK(log.has("the PLC refused: the function is not available (class 0x81, code 0x04)"));
    // a write that is refused is told with the tag
    tags().find("good")->set_number(50, nullptr);
    REQUIRE(wait_for([&] { return log.has("writing 'good' (DB1.DBW0): the PLC refused"); }));
    srv.refuse = 0;
    REQUIRE(wait_for([] { return tagv("good") == 4; }));

    // access denied for an item: the reason names PUT/GET too
    srv.item_code = 0x03;
    REQUIRE(wait_for([&] { return log.has("access denied (code 3)"); }));
    srv.item_code = 0;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1)[1] = 5; }
    REQUIRE(wait_for([] { return tagv("good") == 5; }));
    CHECK(srv.connections == 1);
}

TEST_CASE("s7: silence, an answer to another request, damaged answers and a dropped connection: offline, then back")
{
    Runtime rt;
    FakeS7Plc srv;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1, 10)[1] = 1; }
    rt.tag("online", TagType::Bool);
    rt.tag("v", TagType::Int);
    REQUIRE(start(rt, s7_cfg(srv, json::parse(R"([{ "tag": "v", "addr": "DB1.DBW0" }])"))) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("v") == 1; }));

    // an answer in two packets is one answer
    srv.in_parts = true;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1)[1] = 2; }
    REQUIRE(wait_for([] { return tagv("v") == 2; }));
    CHECK(srv.connections == 1);
    srv.in_parts = false;

    using Answer = std::function<Bytes(const Bytes&)>;
    auto ref = [](const Bytes& f) { return FakeS7Plc::get16(f.data() + 4); };
    const Bytes value99{ 0xFF, 0x04, 0x00, 0x10, 0x00, 0x63 };
    const std::vector<std::pair<const char*, Answer>> faults = {
        { "silence", [](const Bytes&) { return Bytes{ 0xAA }; } },
        { "an answer to another request", [&](const Bytes& f) { return FakeS7Plc::ack(static_cast<uint16_t>(ref(f) + 1), { 0x04, 0x01 }, value99); } },
        { "an answer to another function", [&](const Bytes& f) { return FakeS7Plc::ack(ref(f), { 0x05, 0x01 }, { 0xFF }); } },
        { "a job instead of an answer", [&](const Bytes& f) { Bytes a = FakeS7Plc::ack(ref(f), { 0x04, 0x01 }, value99); a[1] = 0x01; return a; } },
        { "cut off", [&](const Bytes& f) { Bytes a = FakeS7Plc::ack(ref(f), { 0x04, 0x01 }, value99); a.resize(a.size() - 3); return a; } },
        { "an item longer than the answer", [&](const Bytes& f) { return FakeS7Plc::ack(ref(f), { 0x04, 0x01 }, { 0xFF, 0x04, 0xFF, 0xF8, 0x00, 0x63 }); } },
        { "more data than asked for", [&](const Bytes& f) { return FakeS7Plc::ack(ref(f), { 0x04, 0x01 }, { 0xFF, 0x04, 0x00, 0x20, 0x00, 0x63, 0x00, 0x63 }); } },
        { "less data than asked for", [&](const Bytes& f) { return FakeS7Plc::ack(ref(f), { 0x04, 0x01 }, { 0xFF, 0x04, 0x00, 0x08, 0x63 }); } },
        { "no items", [&](const Bytes& f) { return FakeS7Plc::ack(ref(f), { 0x04, 0x00 }, {}); } },
        { "another protocol", [](const Bytes&) { return Bytes{ 0xBB, 'H', 'T', 'T', 'P', '/', '1', '.', '1', ' ', '4', '0', '0', '\r', '\n' }; } },
        { "a TPKT of 65535 bytes", [](const Bytes&) { return Bytes{ 0xBB, 0x03, 0x00, 0xFF, 0xFF, 0x02, 0xF0, 0x80 }; } },
        { "not a data packet", [](const Bytes&) { return Bytes{ 0xBB, 0x03, 0x00, 0x00, 0x0B, 0x06, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00 }; } },
        { "parts without an end", [](const Bytes&) { Bytes a{ 0xBB }; for(int i = 0; i < 400; i++) a.insert(a.end(), { 0x03, 0x00, 0x00, 0x17, 0x02, 0xF0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }); return a; } },
        { "a dropped connection", [](const Bytes&) { return Bytes{ 0xEE }; } },
    };
    int value = 2;
    for(const auto& fault : faults) {
        const char* what = fault.first;
        CAPTURE(what);
        const int connections = srv.connections;
        { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = fault.second; }
        REQUIRE(wait_for([] { return tagv("online") == 0; }, 3000));
        CHECK(tagv("v") == value);                                      // nothing of the damaged answer reached the tag
        value++;
        { std::lock_guard<std::mutex> g(srv.mutex); srv.override_answer = nullptr; srv.db(1)[1] = static_cast<uint8_t>(value); }
        REQUIRE(wait_for([&] { return tagv("online") == 1 && tagv("v") == value; }, 12000));
        CHECK(srv.connections > connections);
    }
}

TEST_CASE("s7: a PLC that refuses or ignores the connection keeps the driver offline, and it stops promptly")
{
    Runtime rt;
    FakeS7Plc srv;
    srv.refuse_connect = true;
    rt.tag("online", TagType::Bool, 1);
    rt.tag("v", TagType::Int);
    LogCapture log;
    REQUIRE(start(rt, s7_cfg(srv, json::parse(R"([{ "tag": "v", "addr": "MW0" }])"))) == 1);
    REQUIRE(wait_for([&] { return srv.connections >= 2 && tagv("online") == 0; }, 5000));
    CHECK(srv.requests == 0);
    CHECK(srv.setups == 0);
    CHECK(log.count("the PLC refused the connection (rack / slot?)") == 1);     // said once, tried again and again
    uint32_t t0 = millis();
    rt.drivers.stop_all();
    CHECK(millis() - t0 < 1500);

    // no answer to the connection request at all
    srv.refuse_connect = false;
    srv.deaf = true;
    REQUIRE(start(rt, s7_cfg(srv, json::parse(R"([{ "tag": "v", "addr": "MW0" }])"))) == 1);
    const int before = srv.connections;
    REQUIRE(wait_for([&] { return srv.connections >= before + 2; }, 5000));
    CHECK(tagv("online") == 0);
    CHECK(log.has("no answer to the connection request"));
    // it answers at last
    { std::lock_guard<std::mutex> g(srv.mutex); set(srv.area(kM), 0, { 0x00, 0x07 }); }
    srv.deaf = false;
    REQUIRE(wait_for([] { return tagv("online") == 1 && tagv("v") == 7; }, 12000));
    t0 = millis();
    rt.drivers.stop_all();
    CHECK(millis() - t0 < 1500);
}

TEST_CASE("s7: writes made without a connection do not pile up: the newest value of each tag arrives")
{
    Runtime rt;
    FakeS7Plc srv;
    srv.deaf = true;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1, 8000); }
    rt.tag("online", TagType::Bool);
    json map = json::array();
    for(int i = 0; i < 3000; i++) { std::string n = "t" + std::to_string(i); rt.tag(n, TagType::Int); map.push_back({ { "tag", n }, { "addr", "DB1.DBW" + std::to_string(i * 2) }, { "access", "wo" } }); }
    LogCapture log;
    REQUIRE(start(rt, s7_cfg(srv, map)) == 1);
    Tag* t0 = tags().find("t0");
    for(int i = 1; i <= 2000; i++) { t0->set_number(i, nullptr); tags().dispatch(false); }           // one tag, again and again
    for(int i = 1; i < 3000; i++) tags().find("t" + std::to_string(i))->set_number(i, nullptr);     // more tags than there is room for
    tags().dispatch(false);
    t0->set_number(4321, nullptr);
    tags().dispatch(false);
    CHECK(log.count("writes are waiting") == 1);
    srv.deaf = false;
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return get(srv.db(1), 5998, 2) == Bytes{ 0x0B, 0xB7 }; }, 20000));
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return get(srv.db(1), 0, 2) == Bytes{ 0x10, 0xE1 }; }, 5000));
    CHECK(srv.writes <= 2048);                                          // all the queue has room for, not 5000
    std::lock_guard<std::mutex> g(srv.mutex);
    CHECK(get(srv.db(1), 5000, 2) == Bytes{ 0x09, 0xC4 });               // t2500 = 2500
}

TEST_CASE("s7: configuration: what is wrong is said with the tag and the address")
{
    Runtime rt;
    rt.tag("a", TagType::Int);
    rt.tag("b", TagType::Bool);
    rt.tag("s", std::string(""));
    // no host, no map: no driver
    json none = json::array({ json{ { "type", "s7" }, { "map", json::array() } } });
    CHECK(rt.drivers.load(&none) == 0);
    json nomap = json::array({ json{ { "type", "s7" }, { "host", "127.0.0.1" } } });
    CHECK(rt.drivers.load(&nomap) == 0);

    // --check reads the settings without connecting: every wrong row is named
    json map = json::parse(R"([
        { "tag": "nope", "addr": "MW0" },
        { "tag": "a", "addr": "D100" },
        { "tag": "a", "addr": "DB1.DBQ4" },
        { "tag": "a", "addr": "DB0.DBW4" },
        { "tag": "b", "addr": "M0" },
        { "tag": "b", "addr": "M0.8" },
        { "tag": "a", "addr": "MW65535" },
        { "tag": "a", "addr": "MW10", "dtype": "float" },
        { "tag": "a", "addr": "MD12", "dtype": "wrong" },
        { "tag": "b", "addr": "M1.0", "dtype": "int16" },
        { "tag": "s", "addr": "MW20", "dtype": "string" },
        { "tag": "s", "addr": "MB65400", "dtype": "string", "length": 200 },
        { "tag": "a", "addr": "MW30", "access": "rw" }
    ])");
    json cfg = json{ { "type", "s7" }, { "host", "10.0.0.1" }, { "status_tag", "ghost" }, { "rack", 9 }, { "write_ranges", json::array({ "MB0-MB9" }) }, { "map", map } };
    {
        LogCapture log;
        json arr = json::array({ cfg });
        rt.drivers.check(&arr);
        CHECK(log.has("s7: unknown status_tag 'ghost'"));
        CHECK(log.has("\"rack\" is 0..7"));
        CHECK(log.has("s7: unknown tag 'nope'"));
        CHECK(log.has("s7: tag 'a': \"D100\": unknown area"));
        CHECK(log.has("s7: tag 'a': \"DB1.DBQ4\": after the DB comes DBX"));
        CHECK(log.has("s7: tag 'a': \"DB0.DBW4\": the DB number is 1..65535"));
        CHECK(log.has("s7: tag 'b': \"M0\": a bit is written with its number 0..7: M0.0"));
        CHECK(log.has("s7: tag 'b': \"M0.8\": the bit is 0..7"));
        CHECK(log.has("s7: tag 'a': \"MW65535\": the byte number is above 65535"));
        CHECK(log.has("s7: tag 'a': MW10 is a word, dtype 'float' ignored"));
        CHECK(log.has("s7: tag 'a': MD12 is a double word, dtype 'wrong' ignored"));
        CHECK(log.has("s7: tag 'b': M1.0 is a bit, dtype 'int16' ignored"));
        CHECK(log.has("s7: tag 's': MW20 is a word, dtype 'string' ignored"));
        CHECK(log.has("s7: tag 's': MB65400 does not fit below byte 65535"));
        CHECK(log.has("s7: tag 'a': MW30 is not written (outside \"write_ranges\")"));
        CHECK(rt.drivers.all().empty());
    }

    // at a start the wrong rows are left out, the rest works
    FakeS7Plc srv;
    { std::lock_guard<std::mutex> g(srv.mutex); set(srv.area(kM), 12, { 0x00, 0x00, 0x00, 0x0B }); }
    cfg = s7_cfg(srv, json::parse(R"([
        { "tag": "nope", "addr": "MW0" }, { "tag": "a", "addr": "D100" }, { "tag": "b", "addr": "M0" },
        { "tag": "a", "addr": "MD12", "dtype": "wrong" }
    ])"));
    cfg.erase("status_tag");
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("a") == 11; }));
    CHECK(read_targets(srv).size() == 1);
}

// ================================================================ addresses without a tag (System > Device)

TEST_CASE("s7: an address without a tag is read and written, the PLC's refusal is told")
{
    Runtime rt;
    FakeS7Plc srv;
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        Bytes& d = srv.db(1, 150);
        set(d, 100, { 0xFF, 0xFE, 0x00, 0x01, 0x01, 0x2C });
        set(d, 120, { 0x3F, 0xC0, 0x00, 0x00 });
        set(srv.area(kM), 10, { 0x0A, 0x01 });
    }
    rt.tag("online", TagType::Bool);
    rt.tag("d0", TagType::Int);
    json cfg = s7_cfg(srv, json::parse(R"([ { "tag": "d0", "addr": "DB1.DBW0" } ])"));
    cfg["write_ranges"] = json::array({ "DB1.DBB0-DB1.DBB199", "MB0-MB99", "IB0-IB9" });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    Driver* d = rt.drivers.all().at(0);
    REQUIRE(d->device_access());
    CHECK_FALSE(d->device_help().empty());

    // the address says how many bytes, the type how they are read; several one after the other
    DeviceResult r = ask(d, "DB1.DBW100");
    CHECK(r.ok);
    CHECK(r.values == Strings{ "-2" });
    CHECK(ask(d, "db1.dbw100", "uint16").values == Strings{ "65534" });
    CHECK(ask(d, "DB1.DBW100", "hex").values == Strings{ "FFFE" });
    CHECK(ask(d, "DB1.DBW100", "int16", 3).values == Strings{ "-2", "1", "300" });
    CHECK(ask(d, "DB1.DBD100", "int32").values == Strings{ "-131071" });       // FF FE 00 01
    CHECK(ask(d, "DB1.DBD100", "int16").values == Strings{ "-131071" });       // (the sign from the type, the size from the address)
    CHECK(ask(d, "DB1.DBD100", "uint32").values == Strings{ "4294836225" });
    CHECK(ask(d, "DB1.DBD100", "hex").values == Strings{ "FFFE0001" });
    CHECK(ask(d, "DB1.DBD120", "float").values == Strings{ "1.5" });
    CHECK(ask(d, "DB1.DBB100", "int16").values == Strings{ "-1" });
    CHECK(ask(d, "DB1.DBB100", "uint16", 2).values == Strings{ "255", "254" });
    CHECK(ask(d, "DB1.DBB101", "hex").values == Strings{ "FE" });
    // bits, one after the other, on into the next byte
    CHECK(ask(d, "M10.0", "", 4).values == Strings{ "0", "1", "0", "1" });
    CHECK(ask(d, "M10.6", "", 4).values == Strings{ "0", "0", "1", "0" });
    CHECK(ask(d, "%MX10.1").values == Strings{ "1" });

    // writes
    CHECK(put(d, "DB1.DBW110", "-5").ok);
    CHECK(put(d, "DB1.DBD112", "70000", "int32").ok);
    CHECK(put(d, "DB1.DBD116", "1.5", "float").ok);
    CHECK(put(d, "DB1.DBW124", "1a2b", "hex").ok);
    CHECK(put(d, "DB1.DBD126", "0xDEADBEEF", "hex").ok);
    CHECK(put(d, "DB1.DBB130", "-2").ok);
    CHECK(put(d, "DB1.DBB131", "200", "uint16").ok);
    CHECK(put(d, "M10.2", "1").ok);
    CHECK(put(d, "M10.1", "off").ok);
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        const Bytes& b = srv.db(1);
        CHECK(get(b, 110, 2) == Bytes{ 0xFF, 0xFB });
        CHECK(get(b, 112, 4) == Bytes{ 0x00, 0x01, 0x11, 0x70 });
        CHECK(get(b, 116, 4) == Bytes{ 0x3F, 0xC0, 0x00, 0x00 });
        CHECK(get(b, 124, 2) == Bytes{ 0x1A, 0x2B });
        CHECK(get(b, 126, 4) == Bytes{ 0xDE, 0xAD, 0xBE, 0xEF });
        CHECK(get(b, 130, 2) == Bytes{ 0xFE, 0xC8 });
        CHECK(srv.area(kM)[10] == 0x0C);
    }

    // what is wrong with the request is said without asking the PLC
    const int before = srv.writes, reads = srv.requests;
    r = put(d, "DB1.DBW110", "abc");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("not a whole number") != std::string::npos);
    CHECK(put(d, "DB1.DBW110", "40000").error.find("int16") != std::string::npos);
    CHECK(put(d, "DB1.DBB110", "200").error.find("-128 .. 127") != std::string::npos);
    CHECK(put(d, "DB1.DBB110", "256", "uint16").error.find("0 .. 255") != std::string::npos);
    CHECK(put(d, "DB1.DBB110", "100", "hex").error.find("0 .. FF") != std::string::npos);
    CHECK(put(d, "DB1.DBW110", "1.5", "float").error.find("double word") != std::string::npos);
    CHECK(put(d, "DB2.DBW0", "1").error.find("write_ranges") != std::string::npos);
    CHECK(put(d, "IB0", "1").error.find("allow_system_write") != std::string::npos);
    CHECK(put(d, "M10.2", "2").error.find("0 or 1") != std::string::npos);
    CHECK(ask(d, "D100").error.find("unknown area") != std::string::npos);
    CHECK(ask(d, "DB1.DBW100", "double").error.find("unknown type") != std::string::npos);
    CHECK(ask(d, "MW100", "float").error.find("MD4") != std::string::npos);
    CHECK(ask(d, "MD65530", "int32", 2).error.find("last byte") != std::string::npos);
    CHECK(srv.writes == before);
    const int reads_after = srv.requests;
    // (the tag is read all the while: nothing but its reads went out)
    {
        std::lock_guard<std::mutex> g(srv.mutex);
        int other = 0;
        for(const Bytes& f : srv.frames) if(f[10] == 0x04 && f[11] == 1 && (f[20] != kDB || f[23] != 0)) other++;
        CHECK(other == 15);                                             // the 15 good questions above
    }
    CHECK(reads_after > reads);

    // what the PLC refuses is told in words; the tags go on
    r = ask(d, "DB9.DBW0");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("the PLC refused: the object does not exist (code 10)") == 0);
    CHECK(ask(d, "DB1.DBW149").error == "the PLC refused: address out of range (code 5)");      // (the DB has 150 bytes)
    CHECK(put(d, "DB1.DBW149", "1").error == "the PLC refused: address out of range (code 5)");
    srv.refuse = 0x8104;
    r = ask(d, "DB1.DBW100");
    CHECK(r.error.find("the PLC refused: the function is not available") == 0);
    CHECK(r.error.find("PUT/GET") != std::string::npos);
    srv.refuse = 0;
    CHECK(ask(d, "DB1.DBW104").values == Strings{ "300" });
    CHECK(tagv("online") == 1);
    CHECK(srv.connections == 1);
}

TEST_CASE("s7: without a connection a question about an address is answered at once")
{
    Runtime rt;
    rt.tag("online", TagType::Bool);
    json cfg = json{ { "type", "s7" }, { "host", "127.0.0.1" }, { "port", 1 }, { "timeout_ms", 200 }, { "status_tag", "online" }, { "map", json::array() } };
    REQUIRE(start(rt, cfg) == 1);
    Driver* d = rt.drivers.all().at(0);
    DeviceResult r = ask(d, "MW0");
    CHECK_FALSE(r.ok);
    CHECK(r.error == "not connected");
}

TEST_CASE("s7: nonsense in the address and the value of System > Device sends nothing")
{
    Runtime rt;
    FakeS7Plc srv;
    rt.tag("online", TagType::Bool);
    REQUIRE(start(rt, s7_cfg(srv, json::array())) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    Driver* d = rt.drivers.all().at(0);
    const int reads = srv.requests, writes = srv.writes;
    const std::vector<std::string> nonsense = {
        "", " ", std::string(5000, 'M'), "\xff\xfe\xfd", "MW-1", "MW99999999999999999999999", "DB99999999999.DBW0", "DB1.DBW99999999999",
        "%s%s%n", "../../etc/passwd", "MW100; reboot", "M100.99", "@", ":", "holding:1", "D100", "DB1", "DB1.", "DB.DBW0", "\t\r", "%", "%%MW0" };
    for(const std::string& a : nonsense) {
        CAPTURE(a);
        DeviceResult r = ask(d, a.c_str());
        CHECK_FALSE(r.ok);
        CHECK(r.error != "");
        CHECK(r.error.size() < 300);
        r = put(d, a.c_str(), "1");
        CHECK_FALSE(r.ok);
    }
    for(const char* v : { "", " ", "1e400", "0x", "--5", "1 2", "\xff", "9999999999999999999999" }) {
        CAPTURE(v);
        CHECK_FALSE(put(d, "MW100", v).ok);
        CHECK_FALSE(put(d, "MB100", v, "uint16").ok);
        CHECK(put(d, "M100.0", v).error != "");
    }
    CHECK(put(d, "MW100", std::string(5000, '7').c_str()).ok == false);
    CHECK(ask(d, "MW100", "int16", -5).values.size() == 1);            // the number of values is kept in bounds
    CHECK(ask(d, "MW100", "int16", 100000).values.size() == 16);
    CHECK(srv.writes == writes);
    CHECK(srv.requests == reads + 2);                                   // (the two good questions above)
    CHECK(tagv("online") == 1);
}

TEST_CASE("s7: System > Device and the tags at the same time, hundreds of times")
{
    Runtime rt;
    FakeS7Plc srv;
    { std::lock_guard<std::mutex> g(srv.mutex); srv.db(1, 500); }
    rt.tag("online", TagType::Bool);
    rt.tag("sp", TagType::Int);
    rt.tag("pv", TagType::Int);
    json cfg = s7_cfg(srv, json::parse(R"([ { "tag": "sp", "addr": "DB1.DBW300" }, { "tag": "pv", "addr": "DB1.DBW302", "access": "ro" } ])"));
    cfg["poll_ms"] = 5;
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for([] { return tagv("online") == 1; }));
    Driver* d = rt.drivers.all().at(0);
    Tag* sp = tags().find("sp");
    int wrong = 0;
    for(int i = 1; i <= 300; i++) {
        sp->set_number(i, nullptr);                                        // the operator writes a tag ...
        { std::lock_guard<std::mutex> g(srv.mutex); set(srv.db(1), 302, { static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i) }); }   // ... the PLC changes another ...
        const std::string v = std::to_string(i * 7 % 30000);
        if(!put(d, "DB1.DBW400", v.c_str()).ok) wrong++;                   // ... and the page writes and reads an address
        if(ask(d, "DB1.DBW400").values != Strings{ v }) wrong++;
        if(ask(d, "DB1.DBW300", "int16", 2).values.size() != 2) wrong++;
    }
    CHECK(wrong == 0);
    REQUIRE(wait_for([&] { std::lock_guard<std::mutex> g(srv.mutex); return get(srv.db(1), 300, 2) == Bytes{ 0x01, 0x2C }; }));
    REQUIRE(wait_for([] { return tagv("pv") == 300 && tagv("sp") == 300; }));
    CHECK(tagv("online") == 1);
    CHECK(srv.connections == 1);                                           // never a reconnect
    CHECK(srv.too_long == 0);
}
