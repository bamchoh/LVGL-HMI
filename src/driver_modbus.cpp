// Modbus TCP client driver.
//
// {
//   "type": "modbus_tcp",
//   "host": "192.168.0.10", "port": 502, "unit_id": 1,
//   "poll_ms": 200, "timeout_ms": 1000,
//   "status_tag": "plc_online",            // optional bool tag: connection state
//   "map": [
//     { "tag": "speed", "area": "holding", "addr": 0, "dtype": "u16", "scale": 1, "offset": 0 },
//     { "tag": "temp",  "area": "input",   "addr": 10, "dtype": "s16", "scale": 0.1 },
//     { "tag": "flow",  "area": "holding", "addr": 20, "dtype": "float", "swap_words": false },
//     { "tag": "run",   "area": "coil",    "addr": 0 },
//     { "tag": "fault", "area": "discrete","addr": 3 },
//     { "tag": "cmd",   "area": "holding", "addr": 5, "access": "wo" }   // write only, never polled
//   ]
// }
//
// area:   coil (FC1/FC5) | discrete (FC2) | holding (FC3/FC6/FC16) | input (FC4)
// dtype:  bool | u16 | s16 | u32 | s32 | float   (32-bit types occupy two registers, big-endian
//         word order unless "swap_words" is true)
// access: "rw" (default for coil/holding) | "ro" | "wo"
//
// Reads run on a worker thread; UI writes to a mapped tag are queued and sent by the same thread.
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kInvalidSock = INVALID_SOCKET;
static void sock_close(sock_t s) { closesocket(s); }
static int  sock_errno() { return WSAGetLastError(); }
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
using sock_t = int;
static const sock_t kInvalidSock = -1;
static void sock_close(sock_t s) { close(s); }
static int  sock_errno() { return errno; }
#endif

#include "driver.hpp"
#include "tag.hpp"
#include "util.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace hml {
namespace {

constexpr size_t kWriteQueueLen = 64;

bool sockets_startup()
{
#ifdef _WIN32
    static bool done = false;
    if(!done) {
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        done = true;
    }
#endif
    return true;
}

void set_timeout(sock_t s, uint32_t ms)
{
#ifdef _WIN32
    DWORD tv = ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

bool send_all(sock_t s, const uint8_t* buf, int len)
{
    while(len > 0) {
        int n = static_cast<int>(send(s, reinterpret_cast<const char*>(buf), len, 0));
        if(n <= 0) return false;
        buf += n; len -= n;
    }
    return true;
}

bool recv_all(sock_t s, uint8_t* buf, int len)
{
    while(len > 0) {
        int n = static_cast<int>(recv(s, reinterpret_cast<char*>(buf), len, 0));
        if(n <= 0) return false;
        buf += n; len -= n;
    }
    return true;
}

class ModbusTcpDriver final : public Driver {
public:
    explicit ModbusTcpDriver(const json& cfg);
    ~ModbusTcpDriver() override { stop(); }
    bool ok() const { return ok_; }

    const char* type() const override { return "modbus_tcp"; }
    bool start() override;
    void stop() override;

private:
    enum class Area { Coil, Discrete, Holding, Input };
    enum class DType { Bool, U16, S16, U32, S32, Float };

    struct Item {
        Tag*     tag = nullptr;
        Area     area = Area::Holding;
        DType    dtype = DType::U16;
        uint16_t addr = 0;
        double   scale = 1, offset = 0;
        bool     readable = true, writable = false, swap_words = false;
        int      pending_writes = 0;   // guarded by qlock_: reads are ignored while a write is queued
        int      regs() const { return (dtype == DType::U32 || dtype == DType::S32 || dtype == DType::Float) ? 2 : 1; }
    };
    struct Write { size_t item; double value; };

    bool connect();
    void disconnect();
    int  transact(const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max);   // response PDU length, -1 I/O error, -2 exception
    bool read_item(Item& it);
    bool write_item(const Item& it, double value);
    static double decode(const Item& it, const uint8_t* b);
    static void   encode(const Item& it, double raw, uint8_t* b);
    void set_online(bool on);
    void thread_main();
    void on_tag_changed(Tag& tag, const Value& v, const void* source);

    std::string host_;
    uint16_t    port_ = 502;
    uint8_t     unit_ = 1;
    uint32_t    poll_ms_ = 200, timeout_ms_ = 1000;
    Tag*        status_tag_ = nullptr;
    std::vector<Item> items_;

    std::mutex        qlock_;
    std::deque<Write> queue_;
    std::thread       thread_;
    std::atomic<bool> running_{ false };
    sock_t            sock_ = kInvalidSock;
    uint16_t          tid_ = 0;
    bool              online_ = false;
    bool              ok_ = false;
};

// ---------------------------------------------------------------- construction

ModbusTcpDriver::ModbusTcpDriver(const json& cfg)
{
    if(!sockets_startup()) { HML_ERROR("modbus: socket init failed"); return; }
    const json* map = jget(cfg, "map");
    if(!map || !map->is_array()) { HML_WARN("modbus: \"map\" array missing"); return; }

    host_ = jstr(cfg, "host", "127.0.0.1");
    port_ = static_cast<uint16_t>(jnum(cfg, "port", 502));
    unit_ = static_cast<uint8_t>(jnum(cfg, "unit_id", 1));
    poll_ms_ = static_cast<uint32_t>(jnum(cfg, "poll_ms", 200));
    timeout_ms_ = static_cast<uint32_t>(jnum(cfg, "timeout_ms", 1000));
    if(auto st = jstr_opt(cfg, "status_tag")) status_tag_ = tags().find(*st);

    for(const json& m : *map) {
        std::string tagname = jstr(m, "tag", "");
        Tag* tag = tags().find(tagname);
        if(!tag) { HML_WARN("modbus: unknown tag '%s'", tagname.c_str()); continue; }
        Item it;
        it.tag = tag;
        std::string area = jstr(m, "area", "holding");
        if(iequals(area, "coil")) it.area = Area::Coil;
        else if(iequals(area, "discrete") || iequals(area, "di")) it.area = Area::Discrete;
        else if(iequals(area, "input") || iequals(area, "ir")) it.area = Area::Input;
        else it.area = Area::Holding;

        auto dt = jstr_opt(m, "dtype");
        if(it.area == Area::Coil || it.area == Area::Discrete) it.dtype = DType::Bool;
        else if(!dt || iequals(*dt, "u16") || iequals(*dt, "uint16")) it.dtype = DType::U16;
        else if(iequals(*dt, "s16") || iequals(*dt, "int16")) it.dtype = DType::S16;
        else if(iequals(*dt, "u32") || iequals(*dt, "uint32")) it.dtype = DType::U32;
        else if(iequals(*dt, "s32") || iequals(*dt, "int32")) it.dtype = DType::S32;
        else if(iequals(*dt, "float") || iequals(*dt, "f32") || iequals(*dt, "real")) it.dtype = DType::Float;
        else { HML_WARN("modbus: unknown dtype '%s'", dt->c_str()); it.dtype = DType::U16; }

        it.addr = static_cast<uint16_t>(jnum(m, "addr", 0));
        it.scale = jnum(m, "scale", 1);
        it.offset = jnum(m, "offset", 0);
        it.swap_words = jbool(m, "swap_words", false);
        bool can_write = (it.area == Area::Coil || it.area == Area::Holding);
        std::string access = jstr(m, "access", can_write ? "rw" : "ro");
        it.readable = !iequals(access, "wo");
        it.writable = can_write && !iequals(access, "ro");
        items_.push_back(it);
    }
    ok_ = true;
}

bool ModbusTcpDriver::start()
{
    for(auto& it : items_) {
        if(it.writable) {
            it.tag->subscribe(SubGroup::Driver, [this](Tag& t, const Value& v, const void* src) { on_tag_changed(t, v, src); });
        }
    }
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
    return true;
}

void ModbusTcpDriver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------- sockets

bool ModbusTcpDriver::connect()
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string port = std::to_string(port_);
    if(getaddrinfo(host_.c_str(), port.c_str(), &hints, &res) != 0 || !res) return false;

    sock_t s = kInvalidSock;
    for(addrinfo* ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if(s == kInvalidSock) continue;
        set_timeout(s, timeout_ms_);
        if(::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
        sock_close(s);
        s = kInvalidSock;
    }
    freeaddrinfo(res);
    if(s == kInvalidSock) return false;
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    sock_ = s;
    return true;
}

void ModbusTcpDriver::disconnect()
{
    if(sock_ != kInvalidSock) { sock_close(sock_); sock_ = kInvalidSock; }
}

int ModbusTcpDriver::transact(const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max)
{
    uint8_t frame[7 + 260];
    uint16_t tid = ++tid_;
    frame[0] = static_cast<uint8_t>(tid >> 8); frame[1] = static_cast<uint8_t>(tid & 0xFF);
    frame[2] = 0; frame[3] = 0;
    frame[4] = static_cast<uint8_t>((pdu_len + 1) >> 8); frame[5] = static_cast<uint8_t>((pdu_len + 1) & 0xFF);
    frame[6] = unit_;
    std::memcpy(frame + 7, pdu, static_cast<size_t>(pdu_len));
    if(!send_all(sock_, frame, 7 + pdu_len)) return -1;

    uint8_t hdr[7];
    if(!recv_all(sock_, hdr, 7)) return -1;
    int len = ((hdr[4] << 8) | hdr[5]) - 1;
    if(len < 1 || len > resp_max) return -1;
    if(!recv_all(sock_, resp, len)) return -1;
    if(((hdr[0] << 8) | hdr[1]) != tid) return -1;
    if(resp[0] & 0x80) {
        HML_WARN("modbus: exception fc=0x%02x code=%d", resp[0] & 0x7F, len > 1 ? resp[1] : 0);
        return -2;
    }
    return len;
}

// ---------------------------------------------------------------- encode / decode

double ModbusTcpDriver::decode(const Item& it, const uint8_t* b)
{
    uint16_t w0 = static_cast<uint16_t>((b[0] << 8) | b[1]);
    uint16_t w1 = static_cast<uint16_t>((b[2] << 8) | b[3]);
    if(it.swap_words) std::swap(w0, w1);
    uint32_t u32 = (static_cast<uint32_t>(w0) << 16) | w1;
    switch(it.dtype) {
        case DType::U16: return w0;
        case DType::S16: return static_cast<int16_t>(w0);
        case DType::U32: return u32;
        case DType::S32: return static_cast<int32_t>(u32);
        case DType::Float: { float f; std::memcpy(&f, &u32, 4); return f; }
        default: return w0 != 0;
    }
}

void ModbusTcpDriver::encode(const Item& it, double raw, uint8_t* b)
{
    uint32_t u32 = 0;
    switch(it.dtype) {
        case DType::U16: u32 = static_cast<uint16_t>(static_cast<uint32_t>(std::llround(raw))); break;
        case DType::S16: u32 = static_cast<uint16_t>(static_cast<int16_t>(std::llround(raw))); break;
        case DType::U32: u32 = static_cast<uint32_t>(std::llround(raw)); break;
        case DType::S32: u32 = static_cast<uint32_t>(static_cast<int32_t>(std::llround(raw))); break;
        case DType::Float: { float f = static_cast<float>(raw); std::memcpy(&u32, &f, 4); break; }
        default: u32 = raw != 0; break;
    }
    uint16_t w0, w1;
    if(it.regs() == 2) {
        w0 = static_cast<uint16_t>(u32 >> 16); w1 = static_cast<uint16_t>(u32 & 0xFFFF);
        if(it.swap_words) std::swap(w0, w1);
    }
    else { w0 = static_cast<uint16_t>(u32 & 0xFFFF); w1 = 0; }
    b[0] = static_cast<uint8_t>(w0 >> 8); b[1] = static_cast<uint8_t>(w0 & 0xFF);
    b[2] = static_cast<uint8_t>(w1 >> 8); b[3] = static_cast<uint8_t>(w1 & 0xFF);
}

// ---------------------------------------------------------------- read / write

bool ModbusTcpDriver::read_item(Item& it)
{
    uint8_t pdu[5], resp[260];
    bool skip_update;
    {
        std::lock_guard<std::mutex> lock(qlock_);
        skip_update = it.pending_writes > 0;
    }
    pdu[1] = static_cast<uint8_t>(it.addr >> 8); pdu[2] = static_cast<uint8_t>(it.addr & 0xFF);
    pdu[3] = 0;
    if(it.area == Area::Coil || it.area == Area::Discrete) {
        pdu[0] = it.area == Area::Coil ? 0x01 : 0x02;
        pdu[4] = 1;
        int n = transact(pdu, 5, resp, sizeof(resp));
        if(n == -1) return false;
        if(n >= 3 && !skip_update) it.tag->set_number((resp[2] & 1) ? 1 : 0, this);
        return true;
    }
    int nregs = it.regs();
    pdu[0] = it.area == Area::Holding ? 0x03 : 0x04;
    pdu[4] = static_cast<uint8_t>(nregs);
    int n = transact(pdu, 5, resp, sizeof(resp));
    if(n == -1) return false;
    if(n >= 2 + nregs * 2 && !skip_update) {
        uint8_t b[4] = { 0, 0, 0, 0 };
        std::memcpy(b, resp + 2, static_cast<size_t>(nregs) * 2);
        it.tag->set_number(decode(it, b) * it.scale + it.offset, this);
    }
    return true;
}

bool ModbusTcpDriver::write_item(const Item& it, double value)
{
    uint8_t pdu[16], resp[260];
    pdu[1] = static_cast<uint8_t>(it.addr >> 8); pdu[2] = static_cast<uint8_t>(it.addr & 0xFF);
    if(it.area == Area::Coil) {
        pdu[0] = 0x05;
        pdu[3] = value != 0 ? 0xFF : 0x00; pdu[4] = 0x00;
        return transact(pdu, 5, resp, sizeof(resp)) != -1;
    }
    if(it.area != Area::Holding) return true;
    double raw = it.scale != 0 ? (value - it.offset) / it.scale : value;
    uint8_t b[4];
    encode(it, raw, b);
    if(it.regs() == 1) {
        pdu[0] = 0x06;
        pdu[3] = b[0]; pdu[4] = b[1];
        return transact(pdu, 5, resp, sizeof(resp)) != -1;
    }
    pdu[0] = 0x10;
    pdu[3] = 0; pdu[4] = 2; pdu[5] = 4;
    std::memcpy(pdu + 6, b, 4);
    return transact(pdu, 10, resp, sizeof(resp)) != -1;
}

void ModbusTcpDriver::set_online(bool on)
{
    if(online_ == on) return;
    online_ = on;
    HML_INFO("modbus %s:%u %s", host_.c_str(), port_, on ? "connected" : "disconnected");
    if(status_tag_) status_tag_->set_number(on ? 1 : 0, this);
}

void ModbusTcpDriver::thread_main()
{
    uint32_t backoff = 500;
    while(running_) {
        if(sock_ == kInvalidSock) {
            if(!connect()) {
                set_online(false);
                for(uint32_t t = 0; t < backoff && running_; t += 50) sleep_ms(50);   // responsive to stop()
                if(backoff < 5000) backoff *= 2;
                continue;
            }
            backoff = 500;
            set_online(true);
        }

        uint32_t cycle_start = millis();
        bool ok = true;

        // pending writes first so operator actions feel immediate
        for(;;) {
            Write w;
            {
                std::lock_guard<std::mutex> lock(qlock_);
                if(queue_.empty() || !ok) break;
                w = queue_.front();
                queue_.pop_front();
            }
            ok = write_item(items_[w.item], w.value);
            std::lock_guard<std::mutex> lock(qlock_);
            items_[w.item].pending_writes--;
        }

        for(size_t i = 0; ok && i < items_.size() && running_; i++) {
            if(items_[i].readable) ok = read_item(items_[i]);
        }

        if(!ok) {
            HML_WARN("modbus: communication error (%d), reconnecting", sock_errno());
            disconnect();
            set_online(false);
            continue;
        }

        uint32_t elapsed = millis() - cycle_start;
        uint32_t wait = elapsed < poll_ms_ ? poll_ms_ - elapsed : 0;
        for(uint32_t t = 0; t < wait && running_; t += 10) sleep_ms(wait - t < 10 ? wait - t : 10);
    }
    disconnect();
    set_online(false);
}

// UI thread: a mapped tag changed. Queue a write unless we caused it.
void ModbusTcpDriver::on_tag_changed(Tag& tag, const Value& v, const void* source)
{
    if(source == this) return;
    for(size_t i = 0; i < items_.size(); i++) {
        Item& it = items_[i];
        if(it.tag != &tag || !it.writable) continue;
        std::lock_guard<std::mutex> lock(qlock_);
        if(queue_.size() < kWriteQueueLen) {
            queue_.push_back({ i, v.as_number() });
            it.pending_writes++;
        }
        else HML_WARN("modbus: write queue full, dropping write to '%s'", tag.name().c_str());
    }
}

} // namespace

std::unique_ptr<Driver> make_modbus_tcp_driver(const json& cfg)
{
    auto d = std::make_unique<ModbusTcpDriver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml
