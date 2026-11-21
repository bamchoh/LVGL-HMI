// Modbus client drivers: Modbus TCP, Modbus RTU and Modbus ASCII.
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
//     { "tag": "cmd",   "area": "holding", "addr": 5, "access": "wo" },  // write only, never polled
//     { "tag": "temp2", "area": "input",   "addr": 10, "unit_id": 2 }    // another unit on the same line
//   ]
// }
//
// {
//   "type": "modbus_rtu",                  // or "modbus_ascii"
//   "device": "/dev/ttyUSB0",              // serial port ("COM4" on Windows)
//   "baud": 9600, "data_bits": 8, "parity": "none", "stop_bits": 1,
//   "unit_id": 1, "poll_ms": 200, "timeout_ms": 500, "status_tag": "plc_online",
//   "map": [ ... ]                         // as above
// }
//   Defaults: modbus_rtu 9600 8N1, modbus_ascii 9600 7E1. Instead of "device", "host" + "port"
//   send the same frames through a TCP connection (serial-to-Ethernet gateway).
//
// area:   coil (FC1/FC5) | discrete (FC2) | holding (FC3/FC6/FC16) | input (FC4)
// dtype:  bool | u16 | s16 | u32 | s32 | float   (32-bit types occupy two registers, big-endian
//         word order unless "swap_words" is true)
// access: "rw" (default for coil/holding) | "ro" | "wo"
//
// Reads run on a worker thread; UI writes to a mapped tag are queued and sent by the same thread.
//
// Online state: Modbus TCP is online while the connection is up (an I/O error or a timeout
// closes it and it is opened again). A serial line has no connection: it is online while at
// least one unit answers. A unit that does not answer costs one timeout per poll cycle, the
// other units on the line keep being polled.
#include "driver.hpp"
#include "modbus_link.hpp"
#include "tag.hpp"
#include "util.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace hml {
namespace {

constexpr size_t kWriteQueueLen = 64;        // more than these waiting: from there on a tag has one place in the queue (its newest value)
constexpr size_t kWriteQueueMax = 2048;      // ... and this is all there is room for (many tags written at once: data_transfer.hpp)
enum class Framing { Tcp, Rtu, Ascii };

// transact() results below zero
constexpr int kFatal = -1;       // the link is broken: close it and open it again
constexpr int kException = -2;   // the unit answered with an exception
constexpr int kNoAnswer = -3;    // timeout or a damaged frame (serial framings)

class ModbusDriver final : public Driver {
public:
    ModbusDriver(const json& cfg, Framing framing);
    bool device_access() const override { return true; }
    std::string device_help() const override { return "holding:100  input:5  coil:3  discrete:0   (another unit: holding:100@2)"; }
    ~ModbusDriver() override { stop(); }
    bool ok() const { return ok_; }

    const char* type() const override { return framing_ == Framing::Tcp ? "modbus_tcp" : framing_ == Framing::Rtu ? "modbus_rtu" : "modbus_ascii"; }
    bool start() override;
    void stop() override;

private:
    enum class Area { Coil, Discrete, Holding, Input };
    enum class DType { Bool, U16, S16, U32, S32, Float };

    struct Item {
        Tag*     tag = nullptr;
        Area     area = Area::Holding;
        DType    dtype = DType::U16;
        uint8_t  unit = 1;
        uint16_t addr = 0;
        double   scale = 1, offset = 0;
        bool     readable = true, writable = false, swap_words = false;
        int      pending_writes = 0;   // guarded by qlock_: reads are ignored while a write is queued
        int      regs() const { return (dtype == DType::U32 || dtype == DType::S32 || dtype == DType::Float) ? 2 : 1; }
    };
    struct Write { size_t item; double value; };

    // Response PDU length (>= 1), or kFatal / kException / kNoAnswer.
    int  transact(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max);
    int  transact_tcp(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max);
    int  transact_rtu(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max);
    int  transact_ascii(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max);
    int  read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline);   // 1 ok, 0 timeout, -1 error
    int  read_item(Item& it);                       // 1 answered, kNoAnswer, kFatal
    int  write_item(const Item& it, double value);
    int  device_do(const DeviceRequest& rq, DeviceResult& rs);
    static double decode(const Item& it, const uint8_t* b);
    static void   encode(const Item& it, double raw, uint8_t* b);
    void set_online(bool on);
    bool link_polls_cyclically() const override { return true; }
    void note_unit(uint8_t unit, bool answers);
    void thread_main();
    void on_tag_changed(Tag& tag, const Value& v, const void* source);

    Framing     framing_;
    bool        serial_ = false;      // a serial port (not RTU / ASCII through TCP)
    SerialSettings serial_cfg_;
    std::unique_ptr<ModbusStream> stream_;
    uint8_t     unit_ = 1;
    uint32_t    poll_ms_ = 200, timeout_ms_ = 1000;
    Tag*        status_tag_ = nullptr;
    std::vector<Item> items_;

    std::mutex        qlock_;
    std::deque<Write> queue_;
    bool              queue_full_said_ = false;    // guarded by qlock_: the warning is given once until the queue is empty again
    std::thread       thread_;
    std::atomic<bool> running_{ false };
    uint16_t          tid_ = 0;
    int               online_ = -1;       // -1 unknown, 0 offline, 1 online
    int8_t            unit_state_[256];   // -1 unknown, 0 silent, 1 answers (serial framings: for the log)
    bool              open_failed_logged_ = false;
    bool              ok_ = false;
    bool              quiet_exception_ = false;    // System > Device asks: the exception goes to the page, not to the log
    uint8_t           last_exception_ = 0;
};

// ---------------------------------------------------------------- construction

ModbusDriver::ModbusDriver(const json& cfg, Framing framing) : framing_(framing)
{
    link_set(false);
    std::memset(unit_state_, -1, sizeof(unit_state_));
    const json* map = jget(cfg, "map");
    if(!map || !map->is_array()) { HML_WARN("%s: \"map\" array missing", type()); return; }

    unit_ = static_cast<uint8_t>(jnum(cfg, "unit_id", 1));
    poll_ms_ = static_cast<uint32_t>(jnum(cfg, "poll_ms", 200));
    timeout_ms_ = static_cast<uint32_t>(jnum(cfg, "timeout_ms", framing == Framing::Tcp ? 1000 : 500));
    if(timeout_ms_ < 10) timeout_ms_ = 10;
    if(auto st = jstr_opt(cfg, "status_tag")) status_tag_ = tags().find(*st);

    const std::string device = jstr(cfg, "device", "");
    if(framing == Framing::Tcp || device.empty()) {
        // Modbus TCP, or RTU / ASCII frames through a TCP connection (a serial gateway)
        if(framing != Framing::Tcp && !jhas(cfg, "host")) { HML_ERROR("%s: \"device\" (serial port) or \"host\" is needed", type()); return; }
        stream_ = make_tcp_stream(jstr(cfg, "host", "127.0.0.1"), static_cast<uint16_t>(jnum(cfg, "port", 502)), timeout_ms_);
    }
    else {
        serial_ = true;
        serial_cfg_.device = device;
        serial_cfg_.baud = static_cast<int>(jnum(cfg, "baud", 9600));
        serial_cfg_.data_bits = static_cast<int>(jnum(cfg, "data_bits", framing == Framing::Ascii ? 7 : 8));
        std::string parity = jstr(cfg, "parity", framing == Framing::Ascii ? "even" : "none");
        serial_cfg_.parity = iequals(parity, "even") || iequals(parity, "e") ? 'E' : iequals(parity, "odd") || iequals(parity, "o") ? 'O' : 'N';
        if(serial_cfg_.parity == 'N' && !iequals(parity, "none") && !iequals(parity, "n")) HML_WARN("%s: unknown parity '%s' (none | even | odd), using none", type(), parity.c_str());
        serial_cfg_.stop_bits = static_cast<int>(jnum(cfg, "stop_bits", 1)) == 2 ? 2 : 1;
        if(serial_cfg_.data_bits != 7 && serial_cfg_.data_bits != 8) { HML_WARN("%s: data_bits must be 7 or 8, using 8", type()); serial_cfg_.data_bits = 8; }
        if(framing == Framing::Rtu && serial_cfg_.data_bits != 8) { HML_WARN("modbus_rtu needs 8 data bits, using 8"); serial_cfg_.data_bits = 8; }
        stream_ = make_serial_stream(serial_cfg_);
    }

    for(const json& m : *map) {
        std::string tagname = jstr(m, "tag", "");
        Tag* tag = tags().find(tagname);
        if(!tag) { HML_WARN("%s: unknown tag '%s'", type(), tagname.c_str()); continue; }
        Item it;
        it.tag = tag;
        it.unit = static_cast<uint8_t>(jnum(m, "unit_id", unit_));
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
        else { HML_WARN("%s: unknown dtype '%s'", type(), dt->c_str()); it.dtype = DType::U16; }

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

bool ModbusDriver::start()
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

void ModbusDriver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------- frames

int ModbusDriver::read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline)
{
    int have = 0;
    while(have < n) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if(left <= 0 || !running_) return 0;
        int r = stream_->read(buf + have, n - have, static_cast<uint32_t>(left > 50 ? 50 : left));   // short waits: stop() stays quick
        if(r < 0) return -1;
        have += r;
    }
    return 1;
}

int ModbusDriver::transact(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max)
{
    int n = framing_ == Framing::Tcp ? transact_tcp(unit, pdu, pdu_len, resp, resp_max)
          : framing_ == Framing::Rtu ? transact_rtu(unit, pdu, pdu_len, resp, resp_max)
                                     : transact_ascii(unit, pdu, pdu_len, resp, resp_max);
    if(n >= 1 && (resp[0] & 0x80)) {
        last_exception_ = n > 1 ? resp[1] : 0;
        if(!quiet_exception_) HML_WARN("%s: unit %d: exception fc=0x%02x code=%d", type(), unit, resp[0] & 0x7F, last_exception_);
        return kException;
    }
    // a serial line is shared: leave the silent time between frames before the next request
    if(serial_ && framing_ == Framing::Rtu) {
        uint32_t us = serial_cfg_.char_time_us(3.5);
        sleep_ms(us < 2000 ? 2 : (us + 999) / 1000);
    }
    return n;
}

int ModbusDriver::transact_tcp(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max)
{
    uint8_t frame[7 + 260];
    uint16_t tid = ++tid_;
    frame[0] = static_cast<uint8_t>(tid >> 8); frame[1] = static_cast<uint8_t>(tid & 0xFF);
    frame[2] = 0; frame[3] = 0;
    frame[4] = static_cast<uint8_t>((pdu_len + 1) >> 8); frame[5] = static_cast<uint8_t>((pdu_len + 1) & 0xFF);
    frame[6] = unit;
    std::memcpy(frame + 7, pdu, static_cast<size_t>(pdu_len));
    if(!stream_->write(frame, 7 + pdu_len)) return kFatal;

    // On a connection every failure (also a timeout) means: start over with a new one.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    uint8_t hdr[7];
    if(read_exact(hdr, 7, deadline) != 1) return kFatal;
    int len = ((hdr[4] << 8) | hdr[5]) - 1;
    if(len < 1 || len > resp_max) return kFatal;
    if(read_exact(resp, len, deadline) != 1) return kFatal;
    if(((hdr[0] << 8) | hdr[1]) != tid) return kFatal;
    return len;
}

int ModbusDriver::transact_rtu(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max)
{
    uint8_t adu[1 + 253];
    adu[0] = unit;
    std::memcpy(adu + 1, pdu, static_cast<size_t>(pdu_len));
    std::vector<uint8_t> frame = modbus_rtu_frame(adu, static_cast<size_t>(pdu_len) + 1);
    stream_->discard_input();                                   // a late answer to an earlier request
    if(!stream_->write(frame.data(), static_cast<int>(frame.size()))) return kFatal;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    uint8_t buf[260];
    int have = 0, total = 0;
    while(total == 0 || have < total) {
        int want = total ? total - have : (have < 2 ? 2 - have : 1);
        int r = read_exact(buf + have, want, deadline);
        if(r < 0) return kFatal;
        if(r == 0) return kNoAnswer;
        have += want;
        if(total == 0) {
            total = modbus_rtu_response_len(buf, have);
            if(total < 0 || total > static_cast<int>(sizeof(buf))) return kNoAnswer;      // not a frame we know: resynchronise next time
        }
    }
    int adu_len = modbus_rtu_check(buf, total);
    if(adu_len < 2 || buf[0] != unit || (buf[1] & 0x7F) != pdu[0]) return kNoAnswer;      // damaged, or somebody else's frame
    if(adu_len - 1 > resp_max) return kNoAnswer;
    std::memcpy(resp, buf + 1, static_cast<size_t>(adu_len - 1));
    return adu_len - 1;
}

int ModbusDriver::transact_ascii(uint8_t unit, const uint8_t* pdu, int pdu_len, uint8_t* resp, int resp_max)
{
    uint8_t adu[1 + 253];
    adu[0] = unit;
    std::memcpy(adu + 1, pdu, static_cast<size_t>(pdu_len));
    std::string frame = modbus_ascii_frame(adu, static_cast<size_t>(pdu_len) + 1);
    stream_->discard_input();
    if(!stream_->write(reinterpret_cast<const uint8_t*>(frame.data()), static_cast<int>(frame.size()))) return kFatal;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    std::string line;
    for(;;) {
        uint8_t c;
        int r = read_exact(&c, 1, deadline);
        if(r < 0) return kFatal;
        if(r == 0) return kNoAnswer;
        if(c == ':') line.clear();                              // a frame starts (again)
        line += static_cast<char>(c);
        if(c == '\n') break;
        if(line.size() > 600) return kNoAnswer;
    }
    std::vector<uint8_t> in;
    if(!modbus_ascii_decode(line, in) || in.size() < 2 || in[0] != unit || (in[1] & 0x7F) != pdu[0]) return kNoAnswer;
    if(static_cast<int>(in.size()) - 1 > resp_max) return kNoAnswer;
    std::memcpy(resp, in.data() + 1, in.size() - 1);
    return static_cast<int>(in.size()) - 1;
}

// ---------------------------------------------------------------- encode / decode

double ModbusDriver::decode(const Item& it, const uint8_t* b)
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

void ModbusDriver::encode(const Item& it, double raw, uint8_t* b)
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

int ModbusDriver::read_item(Item& it)
{
    uint8_t pdu[5], resp[260];
    // Asked after the answer is in, not before the request goes out: the operator may write
    // the tag while this read is on the wire, and the value read is older than that write.
    auto write_pending = [&] { std::lock_guard<std::mutex> lock(qlock_); return it.pending_writes > 0; };
    pdu[1] = static_cast<uint8_t>(it.addr >> 8); pdu[2] = static_cast<uint8_t>(it.addr & 0xFF);
    pdu[3] = 0;
    if(it.area == Area::Coil || it.area == Area::Discrete) {
        pdu[0] = it.area == Area::Coil ? 0x01 : 0x02;
        pdu[4] = 1;
        int n = transact(it.unit, pdu, 5, resp, sizeof(resp));
        if(n == kFatal || n == kNoAnswer) return n;
        if(n >= 3 && !write_pending()) it.tag->set_number_from_device((resp[2] & 1) ? 1 : 0, this);
        return 1;
    }
    int nregs = it.regs();
    pdu[0] = it.area == Area::Holding ? 0x03 : 0x04;
    pdu[4] = static_cast<uint8_t>(nregs);
    int n = transact(it.unit, pdu, 5, resp, sizeof(resp));
    if(n == kFatal || n == kNoAnswer) return n;
    if(n >= 2 + nregs * 2 && !write_pending()) {
        uint8_t b[4] = { 0, 0, 0, 0 };
        std::memcpy(b, resp + 2, static_cast<size_t>(nregs) * 2);
        it.tag->set_number_from_device(decode(it, b) * it.scale + it.offset, this);
    }
    return 1;
}

int ModbusDriver::write_item(const Item& it, double value)
{
    uint8_t pdu[16], resp[260];
    int n;
    pdu[1] = static_cast<uint8_t>(it.addr >> 8); pdu[2] = static_cast<uint8_t>(it.addr & 0xFF);
    if(it.area == Area::Coil) {
        pdu[0] = 0x05;
        pdu[3] = value != 0 ? 0xFF : 0x00; pdu[4] = 0x00;
        n = transact(it.unit, pdu, 5, resp, sizeof(resp));
    }
    else if(it.area != Area::Holding) return 1;
    else {
        double raw = it.scale != 0 ? (value - it.offset) / it.scale : value;
        uint8_t b[4];
        encode(it, raw, b);
        if(it.regs() == 1) {
            pdu[0] = 0x06;
            pdu[3] = b[0]; pdu[4] = b[1];
            n = transact(it.unit, pdu, 5, resp, sizeof(resp));
        }
        else {
            pdu[0] = 0x10;
            pdu[3] = 0; pdu[4] = 2; pdu[5] = 4;
            std::memcpy(pdu + 6, b, 4);
            n = transact(it.unit, pdu, 10, resp, sizeof(resp));
        }
    }
    return (n == kFatal || n == kNoAnswer) ? n : 1;
}

// An address no tag is mapped to ("holding:100", "coil:3@2"): one read or one write, answered
// with text. Returns what transact() does (1 = the unit answered, also with an exception).
int ModbusDriver::device_do(const DeviceRequest& rq, DeviceResult& rs)
{
    // <area>:<number>[@<unit>]
    std::string text = rq.address;
    for(char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    int unit = unit_;
    if(const size_t at = text.find('@'); at != std::string::npos) {
        char* end = nullptr;
        const long u = std::strtol(text.c_str() + at + 1, &end, 10);
        if(at + 1 >= text.size() || *end != '\0' || u < 0 || u > 255) { rs.error = "the unit after @ is 0 .. 255"; return 1; }
        unit = static_cast<int>(u);
        text.erase(at);
    }
    size_t digits = text.find_first_of("0123456789");
    std::string area = text.substr(0, digits == std::string::npos ? text.size() : digits);
    if(!area.empty() && area.back() == ':') area.pop_back();
    Area ar;
    if(area == "holding" || area == "hr") ar = Area::Holding;
    else if(area == "input" || area == "ir") ar = Area::Input;
    else if(area == "coil") ar = Area::Coil;
    else if(area == "discrete" || area == "di") ar = Area::Discrete;
    else { rs.error = "an address is holding:100, input:5, coil:3 or discrete:0"; return 1; }
    char* end = nullptr;
    const long number = digits == std::string::npos ? -1 : std::strtol(text.c_str() + digits, &end, 10);
    if(number < 0 || number > 65535 || !end || *end != '\0') { rs.error = "the number of a register or coil is 0 .. 65535"; return 1; }

    const int count = std::clamp(rq.count, 1, kDeviceMaxCount);
    const bool bits = ar == Area::Coil || ar == Area::Discrete;
    const std::string type = rq.type.empty() ? "uint16" : rq.type;
    const int words = bits ? 1 : device_type_words(type);
    if(words == 0) { rs.error = "unknown type '" + type + "'"; return 1; }
    const int total = rq.write ? words : words * count;
    if(number + total - 1 > 65535) { rs.error = "this goes past the last address"; return 1; }
    if(rq.write && (ar == Area::Input || ar == Area::Discrete)) { rs.error = "inputs cannot be written"; return 1; }

    uint8_t pdu[16], resp[260];
    pdu[1] = static_cast<uint8_t>(number >> 8); pdu[2] = static_cast<uint8_t>(number & 0xFF);
    int n = 0;
    quiet_exception_ = true;
    if(bits && rq.write) {
        bool on = false;
        if(!device_parse_bit(rq.value, on)) { quiet_exception_ = false; rs.error = "a coil is written with 0 or 1"; return 1; }
        pdu[0] = 0x05; pdu[3] = on ? 0xFF : 0x00; pdu[4] = 0x00;
        n = transact(static_cast<uint8_t>(unit), pdu, 5, resp, sizeof(resp));
    }
    else if(bits) {
        pdu[0] = ar == Area::Coil ? 0x01 : 0x02; pdu[3] = 0; pdu[4] = static_cast<uint8_t>(count);
        n = transact(static_cast<uint8_t>(unit), pdu, 5, resp, sizeof(resp));
        if(n >= 2 + (count + 7) / 8) for(int i = 0; i < count; i++) rs.values.push_back((resp[2 + i / 8] >> (i % 8)) & 1 ? "1" : "0");
        else if(n >= 0) n = kNoAnswer;
    }
    else if(rq.write) {
        uint16_t w[2] = {};
        std::string err;
        if(!device_parse(type, rq.value, w, &err)) { quiet_exception_ = false; rs.error = err; return 1; }
        if(words == 1) {
            pdu[0] = 0x06; pdu[3] = static_cast<uint8_t>(w[0] >> 8); pdu[4] = static_cast<uint8_t>(w[0]);
            n = transact(static_cast<uint8_t>(unit), pdu, 5, resp, sizeof(resp));
        }
        else {                                              // the high word in the first register (as "dtype" without swap_words)
            pdu[0] = 0x10; pdu[3] = 0; pdu[4] = 2; pdu[5] = 4;
            pdu[6] = static_cast<uint8_t>(w[1] >> 8); pdu[7] = static_cast<uint8_t>(w[1]);
            pdu[8] = static_cast<uint8_t>(w[0] >> 8); pdu[9] = static_cast<uint8_t>(w[0]);
            n = transact(static_cast<uint8_t>(unit), pdu, 10, resp, sizeof(resp));
        }
    }
    else {
        pdu[0] = ar == Area::Holding ? 0x03 : 0x04; pdu[3] = 0; pdu[4] = static_cast<uint8_t>(total);
        n = transact(static_cast<uint8_t>(unit), pdu, 5, resp, sizeof(resp));
        if(n >= 2 + total * 2) {
            for(int i = 0; i < count; i++) {
                const uint8_t* b = resp + 2 + i * words * 2;
                uint16_t w[2] = { static_cast<uint16_t>((b[0] << 8) | b[1]), 0 };
                if(words == 2) { w[1] = w[0]; w[0] = static_cast<uint16_t>((b[2] << 8) | b[3]); }
                rs.values.push_back(device_format(type, w));
            }
        }
        else if(n >= 0) n = kNoAnswer;
    }
    quiet_exception_ = false;
    if(n == kException) {
        const char* what = last_exception_ == 1 ? "illegal function" : last_exception_ == 2 ? "illegal data address" : last_exception_ == 3 ? "illegal data value"
                         : last_exception_ == 4 ? "device failure" : "exception";
        rs.error = vformat("the unit answers: %s (%d)", what, last_exception_);
        return 1;
    }
    if(n == kFatal || n == kNoAnswer) { rs.values.clear(); rs.error = vformat("unit %d does not answer", unit); return n; }
    rs.ok = true;
    if(rq.write) HML_INFO("%s: %s written with %s (System > Device)", this->type(), rq.address.c_str(), rq.value.c_str());
    return 1;
}

void ModbusDriver::set_online(bool on)
{
    link_set(on);
    // online_ starts as "unknown" so that the status tag is written on the first attempt
    // even when the server was never reachable.
    if(online_ == (on ? 1 : 0)) return;
    online_ = on ? 1 : 0;
    if(framing_ == Framing::Tcp) HML_INFO("modbus %s %s", stream_->describe().c_str(), on ? "connected" : "disconnected");
    else HML_INFO("%s %s: %s", type(), stream_->describe().c_str(), on ? "online" : "offline");
    if(status_tag_) status_tag_->set_number(on ? 1 : 0, this);
}

// serial framings: say once when a unit stops or starts answering
void ModbusDriver::note_unit(uint8_t unit, bool answers)
{
    int8_t s = answers ? 1 : 0;
    if(unit_state_[unit] == s) return;
    if(!answers) HML_WARN("%s: unit %d does not answer", type(), unit);
    else if(unit_state_[unit] == 0) HML_INFO("%s: unit %d answers again", type(), unit);
    unit_state_[unit] = s;
}

void ModbusDriver::thread_main()
{
    uint32_t backoff = 500;
    auto wait = [this](uint32_t ms) { for(uint32_t t = 0; t < ms && running_; t += 50) sleep_ms(50); };   // responsive to stop()
    while(running_) {
        if(!stream_->is_open()) {
            if(!stream_->open()) {
                if(framing_ != Framing::Tcp && !open_failed_logged_) {
                    HML_WARN("%s: %s", type(), stream_->last_error().c_str());
                    open_failed_logged_ = true;
                }
                set_online(false);
                device_fail_all("not connected");
                wait(backoff);
                if(backoff < 5000) backoff *= 2;
                continue;
            }
            open_failed_logged_ = false;
            if(framing_ == Framing::Tcp) { backoff = 500; set_online(true); }
        }

        uint32_t cycle_start = millis();
        bool link_ok = true;
        int answered = 0, silent = 0;
        bool unit_silent[256] = {};                              // this cycle: do not wait for a silent unit again

        // pending writes first so operator actions feel immediate
        for(;;) {
            Write w;
            {
                std::lock_guard<std::mutex> lock(qlock_);
                if(queue_.empty()) queue_full_said_ = false;
                if(queue_.empty() || !link_ok) break;
                w = queue_.front();
                queue_.pop_front();
            }
            const Item& it = items_[w.item];
            int r = write_item(it, w.value);
            if(r == kFatal) link_ok = false;
            else if(r == kNoAnswer) {
                HML_WARN("%s: unit %d did not answer a write to '%s'", type(), it.unit, it.tag->name().c_str());
                unit_silent[it.unit] = true;
                silent++;
            }
            else answered++;
            std::lock_guard<std::mutex> lock(qlock_);
            items_[w.item].pending_writes--;
        }
        for(uint64_t id = 0; link_ok && running_;) {    // addresses without a tag (System > Device)
            DeviceRequest rq;
            if(!device_next(id, rq)) break;
            DeviceResult rs;
            if(device_do(rq, rs) == kFatal) link_ok = false;
            device_done(id, std::move(rs));
        }

        for(size_t i = 0; link_ok && i < items_.size() && running_; i++) {
            Item& it = items_[i];
            if(!it.readable || unit_silent[it.unit]) continue;
            int r = read_item(it);
            if(r == kFatal) link_ok = false;
            else if(r == kNoAnswer) { unit_silent[it.unit] = true; silent++; note_unit(it.unit, false); }
            else { answered++; note_unit(it.unit, true); }
        }

        if(!link_ok) {
            if(framing_ == Framing::Tcp) HML_WARN("modbus: communication error (%s), reconnecting", stream_->last_error().empty() ? "timeout" : stream_->last_error().c_str());
            else HML_WARN("%s: %s", type(), stream_->last_error().empty() ? "the link was lost" : stream_->last_error().c_str());
            stream_->close();
            set_online(false);
            device_fail_all("not connected");
            if(framing_ != Framing::Tcp) wait(500);
            continue;
        }
        if(framing_ != Framing::Tcp) {
            // no connection to tell us: online = somebody answers (or there is nothing to ask)
            if(answered > 0 || silent == 0) { set_online(true); backoff = 500; }
            else {
                set_online(false);
                wait(backoff);                                    // nobody there: ask less often
                if(backoff < 5000) backoff *= 2;
                continue;
            }
        }

        link_polled();
        uint32_t elapsed = millis() - cycle_start;
        uint32_t rest = elapsed < poll_ms_ ? poll_ms_ - elapsed : 0;
        for(uint32_t t = 0; t < rest && running_; t += 10) sleep_ms(rest - t < 10 ? rest - t : 10);
    }
    stream_->close();
    set_online(false);
}

// UI thread: a mapped tag changed. Queue a write unless we caused it.
void ModbusDriver::on_tag_changed(Tag& tag, const Value& v, const void* source)
{
    if(source == this) return;
    for(size_t i = 0; i < items_.size(); i++) {
        Item& it = items_[i];
        if(it.tag != &tag || !it.writable) continue;
        std::lock_guard<std::mutex> lock(qlock_);
        if(queue_.size() < kWriteQueueLen) {
            queue_.push_back({ i, v.as_number() });
            it.pending_writes++;
            continue;
        }
        // Many are waiting: nothing has been sent for a while (no connection), or many tags were
        // written at once. The newest value of a tag is the one that counts: it replaces the one
        // waiting. A tag that is not waiting yet gets a place while there is room; with none left
        // the oldest write gives its place.
        if(!queue_full_said_ && link_state() != 1) {
            queue_full_said_ = true;
            HML_WARN("%s: %zu writes are waiting (no connection?): from now on only the newest value of each tag is kept", type(), queue_.size());
        }
        auto old = std::find_if(queue_.rbegin(), queue_.rend(), [i](const Write& w) { return w.item == i; });
        if(old != queue_.rend()) { old->value = v.as_number(); continue; }
        if(queue_.size() < kWriteQueueMax) { queue_.push_back({ i, v.as_number() }); it.pending_writes++; continue; }
        items_[queue_.front().item].pending_writes--;
        queue_.pop_front();
        queue_.push_back({ i, v.as_number() });
        it.pending_writes++;
    }
}

std::unique_ptr<Driver> make(const json& cfg, Framing framing)
{
    auto d = std::make_unique<ModbusDriver>(cfg, framing);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace

std::unique_ptr<Driver> make_modbus_tcp_driver(const json& cfg) { return make(cfg, Framing::Tcp); }
std::unique_ptr<Driver> make_modbus_rtu_driver(const json& cfg) { return make(cfg, Framing::Rtu); }
std::unique_ptr<Driver> make_modbus_ascii_driver(const json& cfg) { return make(cfg, Framing::Ascii); }

} // namespace hml
