// FINS client driver (OMRON CS / CJ / CP / NJ / NX), FINS/TCP.
//
// {
//   "type": "fins",
//   "host": "192.168.250.1", "port": 9600,
//   "poll_ms": 200, "timeout_ms": 1000, "status_tag": "plc_online",
//   "dest_network": 0, "dest_node": 0, "dest_unit": 0,    // route (0 / 0 / 0: the CPU the connection goes to)
//   "node": 0,                                    // our own node address (0: the PLC gives us one)
//   "write_ranges": ["D0-D999", "W0-W99"],        // optional: only these may be written
//   "allow_system_write": false,                  // the Auxiliary Area (A) is never written unless true
//   "map": [
//     { "tag": "speed", "addr": "D100" },                        // dtype s16 (default)
//     { "tag": "total", "addr": "D102", "dtype": "s32" },        // D102 = low word, D103 = high word
//     { "tag": "temp",  "addr": "D110", "dtype": "float" },
//     { "tag": "count", "addr": "D120", "dtype": "bcd16" },      // 0x1234 in the PLC = 1234
//     { "tag": "run",   "addr": "W0.00" },                       // a bit: bool, read and written
//     { "tag": "in3",   "addr": "CIO1.03", "access": "ro" },
//     { "tag": "name",  "addr": "D200", "dtype": "string", "length": 16 }
//   ]
// }
//
// dtype: bool | u16 | s16 | u32 | s32 | float | bcd16 | bcd32 | string ("length" = characters, 2 per
// word, the first in the high byte as OMRON stores them). 32-bit values: low word first unless
// "swap_words". Reads are collected into as few "memory area read" commands as possible (same
// area, at most 990 words, gaps of up to 32 words are read through); a bit is taken out of the
// word that was read. Writes: "memory area write", words as words, a bit as one bit (the PLC
// changes only that bit).
#include "driver.hpp"
#include "fins.hpp"
#include "modbus_link.hpp"
#include "tag.hpp"
#include "util.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace hml {
namespace {

constexpr size_t kWriteQueueLen = 64;
constexpr int    kGapWords = 32;

enum class DType { Bool, U16, S16, U32, S32, Float, Bcd16, Bcd32, String };

struct Item {
    Tag*        tag = nullptr;
    FinsAddress addr;
    DType       dtype = DType::S16;
    int         length = 0;            // string: characters
    double      scale = 1, offset = 0;
    bool        readable = true, writable = false, swap_words = false;
    int         pending_writes = 0;    // guarded by qlock_
    int         block = -1, word = 0;  // where the value is in the blocks read
    bool        bad_bcd = false;       // (logged once)

    int words() const
    {
        switch(dtype) {
            case DType::U32: case DType::S32: case DType::Float: case DType::Bcd32: return 2;
            case DType::String: return (length + 1) / 2;
            default: return 1;
        }
    }
};

struct Block {                         // one memory area read
    uint8_t  code = 0;                 // word area code
    std::string area;
    uint32_t start = 0;                // word number as on the wire
    int      words = 0;
};

struct Range { uint8_t code; uint32_t from, to; };

class FinsDriver final : public Driver {
public:
    explicit FinsDriver(const json& cfg);
    ~FinsDriver() override { stop(); }
    bool ok() const { return ok_; }
    const char* type() const override { return "fins"; }
    bool start() override;
    void stop() override;
    bool device_access() const override { return true; }
    std::string device_help() const override { return "D100  CIO10  W5  H3  A440  E0_100  T5  C5,  a bit: CIO10.03"; }

private:
    struct Write { size_t item; Value value; };
    enum Result { kOk = 1, kEndCode = 0, kFatal = -1 };

    Result device_do(const DeviceRequest& rq, DeviceResult& rs);
    bool   handshake();
    Result transact(uint8_t mrc, uint8_t src, const std::vector<uint8_t>& data, std::vector<uint8_t>& out, uint16_t* end_code);
    int    read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline);
    Result read_block(size_t b);
    Result write_item(const Item& it, const Value& v);
    void   apply(Item& it, const uint16_t* w);
    void   plan_blocks();
    bool   write_allowed(const FinsAddress& a, int words, std::string& why) const;
    std::string block_name(const Block& b) const;
    void   set_online(bool on);
    void   thread_main();
    void   on_tag_changed(Tag& tag, const Value& v, const void* source);

    std::unique_ptr<ModbusStream> stream_;
    FinsNode    dst_, src_;
    uint8_t     want_node_ = 0;
    bool        dest_node_auto_ = true;
    uint32_t    poll_ms_ = 200, timeout_ms_ = 1000;
    Tag*        status_tag_ = nullptr;
    bool        allow_system_ = false;
    std::vector<Range> write_ranges_;
    std::vector<Item>  items_;
    std::vector<Block> blocks_;
    std::vector<std::vector<size_t>> block_items_;
    std::vector<uint16_t> last_end_;          // per block: the end code last logged (0 = fine)

    std::mutex        qlock_;
    std::deque<Write> queue_;
    bool              queue_full_said_ = false;
    std::thread       thread_;
    std::atomic<bool> running_{ false };
    uint8_t           sid_ = 0;
    int               online_ = -1;
    bool              ok_ = false;
};

// ---------------------------------------------------------------- construction

FinsDriver::FinsDriver(const json& cfg)
{
    const json* map = jget(cfg, "map");
    if(!map || !map->is_array()) { HML_WARN("fins: \"map\" array missing"); return; }
    if(!jhas(cfg, "host")) { HML_ERROR("fins: \"host\" is needed"); return; }
    dst_.network = static_cast<uint8_t>(jnum(cfg, "dest_network", 0));
    dst_.node = static_cast<uint8_t>(jnum(cfg, "dest_node", 0));
    dst_.unit = static_cast<uint8_t>(jnum(cfg, "dest_unit", 0));
    dest_node_auto_ = dst_.node == 0;
    want_node_ = static_cast<uint8_t>(std::clamp(jnum(cfg, "node", 0), 0.0, 254.0));
    poll_ms_ = static_cast<uint32_t>(std::max(10.0, jnum(cfg, "poll_ms", 200)));
    timeout_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "timeout_ms", 1000), 50.0, 60000.0));
    if(auto st = jstr_opt(cfg, "status_tag"); st && !st->empty()) {
        status_tag_ = tags().find(*st);
        if(!status_tag_) HML_WARN("fins: unknown status_tag '%s'", st->c_str());
    }
    allow_system_ = jbool(cfg, "allow_system_write", false);
    if(const json* wr = jget(cfg, "write_ranges"); wr && wr->is_array()) {
        for(const json& r : *wr) {
            std::string s = r.is_string() ? r.get<std::string>() : "";
            size_t dash = s.find('-');
            std::string err;
            auto a = fins_parse_address(s.substr(0, dash), &err);
            auto b = dash == std::string::npos ? a : fins_parse_address(s.substr(dash + 1), &err);
            if(!a || !b || a->word_code != b->word_code || a->bit >= 0 || b->bit >= 0 || b->number < a->number) {
                HML_WARN("fins: write_ranges: \"%s\" is not a range like \"D0-D999\"%s%s", s.c_str(), err.empty() ? "" : ": ", err.c_str());
                continue;
            }
            write_ranges_.push_back({ a->word_code, a->number, b->number });
        }
        if(write_ranges_.empty()) HML_WARN("fins: write_ranges has no valid range: nothing can be written");
    }
    stream_ = make_tcp_stream(jstr(cfg, "host", "127.0.0.1"), static_cast<uint16_t>(jnum(cfg, "port", 9600)), timeout_ms_);

    for(const json& m : *map) {
        std::string tagname = jstr(m, "tag", "");
        Tag* tag = tags().find(tagname);
        if(!tag) { HML_WARN("fins: unknown tag '%s'", tagname.c_str()); continue; }
        std::string err;
        auto addr = fins_parse_address(jstr(m, "addr", ""), &err);
        if(!addr) { HML_WARN("fins: tag '%s': %s", tagname.c_str(), err.c_str()); continue; }
        Item it;
        it.tag = tag;
        it.addr = *addr;
        const std::string name = fins_format_address(*addr);
        auto dt = jstr_opt(m, "dtype");
        if(addr->bit >= 0) {
            if(dt && !iequals(*dt, "bool")) HML_WARN("fins: tag '%s': %s is a bit, dtype '%s' ignored", tagname.c_str(), name.c_str(), dt->c_str());
            it.dtype = DType::Bool;
        }
        else if(!dt || iequals(*dt, "s16") || iequals(*dt, "int16")) it.dtype = DType::S16;
        else if(iequals(*dt, "u16") || iequals(*dt, "uint16")) it.dtype = DType::U16;
        else if(iequals(*dt, "s32") || iequals(*dt, "int32")) it.dtype = DType::S32;
        else if(iequals(*dt, "u32") || iequals(*dt, "uint32")) it.dtype = DType::U32;
        else if(iequals(*dt, "float") || iequals(*dt, "real") || iequals(*dt, "f32")) it.dtype = DType::Float;
        else if(iequals(*dt, "bcd16") || iequals(*dt, "bcd")) it.dtype = DType::Bcd16;
        else if(iequals(*dt, "bcd32")) it.dtype = DType::Bcd32;
        else if(iequals(*dt, "string")) {
            it.dtype = DType::String;
            it.length = std::clamp(static_cast<int>(jnum(m, "length", 16)), 1, 2 * kFinsMaxWords);
        }
        else { HML_WARN("fins: tag '%s': unknown dtype '%s', using s16", tagname.c_str(), dt->c_str()); it.dtype = DType::S16; }
        const uint32_t area_end = addr->area == "T" ? 0x7FFF : 0xFFFF;
        if(static_cast<uint32_t>(addr->number) + static_cast<uint32_t>(it.words()) - 1 > area_end) {
            HML_WARN("fins: tag '%s': %s does not fit below the end of the area", tagname.c_str(), name.c_str());
            continue;
        }
        it.scale = jnum(m, "scale", 1);
        it.offset = jnum(m, "offset", 0);
        it.swap_words = jbool(m, "swap_words", false);
        std::string access = jstr(m, "access", "rw");
        it.readable = !iequals(access, "wo");
        it.writable = !iequals(access, "ro");
        std::string why;
        if(it.writable && !write_allowed(*addr, it.dtype == DType::Bool ? 1 : it.words(), why)) {
            if(jhas(m, "access") || !addr->system) HML_WARN("fins: tag '%s': %s is not written (%s)", tagname.c_str(), name.c_str(), why.c_str());
            it.writable = false;
        }
        if(!it.readable && !it.writable) continue;
        items_.push_back(it);
    }
    plan_blocks();
    ok_ = true;
}

bool FinsDriver::write_allowed(const FinsAddress& a, int words, std::string& why) const
{
    if(a.system && !allow_system_) { why = "the Auxiliary Area is written only with \"allow_system_write\""; return false; }
    if(write_ranges_.empty()) return true;
    const uint32_t last = a.number + static_cast<uint32_t>(words - 1);
    for(const Range& r : write_ranges_) if(r.code == a.word_code && a.number >= r.from && last <= r.to) return true;
    why = "outside \"write_ranges\"";
    return false;
}

std::string FinsDriver::block_name(const Block& b) const
{
    FinsAddress a;
    a.area = b.area; a.number = static_cast<uint16_t>(b.start);
    return fins_format_address(a);
}

// Group what is read into memory area reads: per area, sorted, neighbours merged.
void FinsDriver::plan_blocks()
{
    // (timers and counters share an area code: the counters start at 0x8000, far from the timers)
    std::map<uint8_t, std::vector<size_t>> by_area;
    for(size_t i = 0; i < items_.size(); i++) if(items_[i].readable) by_area[items_[i].addr.word_code].push_back(i);
    for(auto& [code, list] : by_area) {
        auto span = [&](size_t i) {
            const Item& it = items_[i];
            const uint32_t first = it.addr.number;
            return std::make_pair(first, first + static_cast<uint32_t>(it.dtype == DType::Bool ? 1 : it.words()));
        };
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) { return span(a).first < span(b).first; });
        bool first_of_area = true;
        for(size_t i : list) {
            auto [from, to] = span(i);
            Block* cur = first_of_area ? nullptr : &blocks_.back();
            const uint32_t cur_end = cur ? cur->start + static_cast<uint32_t>(cur->words) : 0;
            const bool counter_after_timer = cur && code == 0x89 && cur->start < 0x8000 && from >= 0x8000;
            if(!cur || counter_after_timer || from > cur_end + kGapWords || std::max(cur_end, to) - cur->start > static_cast<uint32_t>(kFinsMaxWords)) {
                blocks_.push_back({ code, items_[i].addr.area, from, 0 });
                block_items_.emplace_back();
                cur = &blocks_.back();
                first_of_area = false;
            }
            cur->words = static_cast<int>(std::max(cur->start + static_cast<uint32_t>(cur->words), to) - cur->start);
            items_[i].block = static_cast<int>(blocks_.size() - 1);
            items_[i].word = static_cast<int>(from - cur->start);
            block_items_.back().push_back(i);
        }
    }
    last_end_.assign(blocks_.size(), 0);
}

bool FinsDriver::start()
{
    for(auto& it : items_) {
        if(it.writable) it.tag->subscribe(SubGroup::Driver, [this](Tag& t, const Value& v, const void* src) { on_tag_changed(t, v, src); });
    }
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
    return true;
}

void FinsDriver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------- frames

int FinsDriver::read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline)
{
    int have = 0;
    while(have < n) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if(left <= 0 || !running_) return 0;
        int r = stream_->read(buf + have, n - have, static_cast<uint32_t>(left > 50 ? 50 : left));
        if(r < 0) return -1;
        have += r;
    }
    return 1;
}

// After the TCP connection: ask for a node address and learn the PLC's.
bool FinsDriver::handshake()
{
    const std::vector<uint8_t> req = fins_tcp_handshake(want_node_);
    if(!stream_->write(req.data(), static_cast<int>(req.size()))) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    uint8_t h[kFinsTcpHeader], body[16];
    if(read_exact(h, kFinsTcpHeader, deadline) != 1) return false;
    uint32_t command = 0, error = 0;
    const int len = fins_tcp_body_len(h, static_cast<int>(sizeof(body)), &command, &error);
    if(len < 0) { HML_WARN("fins %s: not a FINS/TCP answer", stream_->describe().c_str()); return false; }
    if(len > 0 && read_exact(body, len, deadline) != 1) return false;
    if(error != 0) { HML_WARN("fins %s: the PLC refused the connection (%s)", stream_->describe().c_str(), fins_tcp_error_text(error).c_str()); return false; }
    uint8_t client = 0, server = 0;
    if(command != 1 || !fins_tcp_parse_handshake(body, len, &client, &server)) {
        HML_WARN("fins %s: unexpected answer to the node address request", stream_->describe().c_str());
        return false;
    }
    src_.node = client;
    if(dest_node_auto_) dst_.node = server;
    return true;
}

FinsDriver::Result FinsDriver::transact(uint8_t mrc, uint8_t src, const std::vector<uint8_t>& data, std::vector<uint8_t>& out, uint16_t* end_code)
{
    const uint8_t sid = ++sid_;
    const std::vector<uint8_t> req = fins_tcp_wrap(fins_frame(dst_, src_, sid, mrc, src, data));
    if(!stream_->write(req.data(), static_cast<int>(req.size()))) return kFatal;

    // every failure on the connection (also a timeout or a frame we do not understand) means:
    // start over with a new connection, so that a late answer cannot be taken for the next one
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    uint8_t buf[kFinsTcpHeader + 14 + kFinsMaxWords * 2 + 16];
    if(read_exact(buf, kFinsTcpHeader, deadline) != 1) return kFatal;
    uint32_t command = 0, error = 0;
    const int body = fins_tcp_body_len(buf, static_cast<int>(sizeof(buf)) - kFinsTcpHeader, &command, &error);
    if(body < 0) { HML_WARN("fins %s: not a FINS/TCP answer", stream_->describe().c_str()); return kFatal; }
    if(read_exact(buf + kFinsTcpHeader, body, deadline) != 1) return kFatal;
    if(error != 0 || command != 2) {
        HML_WARN("fins %s: FINS/TCP error %s", stream_->describe().c_str(), fins_tcp_error_text(error).c_str());
        return kFatal;
    }
    FinsResponse r;
    std::string err;
    if(!fins_parse_response(buf + kFinsTcpHeader, body, r, &err)) { HML_WARN("fins %s: %s", stream_->describe().c_str(), err.c_str()); return kFatal; }
    if(r.sid != sid || r.mrc != mrc || r.src != src) { HML_WARN("fins %s: answer to another request", stream_->describe().c_str()); return kFatal; }
    out = std::move(r.data);
    if(end_code) *end_code = r.end_code;
    return r.end_code != 0 ? kEndCode : kOk;
}

// ---------------------------------------------------------------- read / write

void FinsDriver::apply(Item& it, const uint16_t* w)
{
    double v = 0;
    const uint32_t u32 = it.swap_words ? (static_cast<uint32_t>(w[0]) << 16) | w[1] : (static_cast<uint32_t>(w[1]) << 16) | w[0];
    switch(it.dtype) {
        case DType::Bool: it.tag->set_number((w[0] >> it.addr.bit) & 1, this); return;
        case DType::U16: v = w[0]; break;
        case DType::S16: v = static_cast<int16_t>(w[0]); break;
        case DType::U32: v = u32; break;
        case DType::S32: v = static_cast<int32_t>(u32); break;
        case DType::Float: { float f; std::memcpy(&f, &u32, 4); v = f; break; }
        case DType::Bcd16: case DType::Bcd32: {
            bool good = true;
            v = fins_from_bcd(it.dtype == DType::Bcd16 ? w[0] : u32, &good);
            if(!good) {          // not BCD (A..F in a digit): leave the tag as it is
                if(!it.bad_bcd) HML_WARN("fins: tag '%s' (%s): the value in the PLC is not BCD", it.tag->name().c_str(), fins_format_address(it.addr).c_str());
                it.bad_bcd = true;
                return;
            }
            it.bad_bcd = false;
            break;
        }
        case DType::String: {
            std::string s;
            for(int i = 0; i < it.length; i++) {
                char c = static_cast<char>(i % 2 == 0 ? (w[i / 2] >> 8) : (w[i / 2] & 0xFF));
                if(c == '\0') break;
                s.push_back(c);
            }
            it.tag->set_string(s, this);
            return;
        }
    }
    it.tag->set_number_from_device(v * it.scale + it.offset, this);
}

FinsDriver::Result FinsDriver::read_block(size_t b)
{
    const Block& bl = blocks_[b];
    std::vector<uint8_t> out;
    uint16_t code = 0;
    Result r = transact(0x01, 0x01, fins_area_data(bl.code, static_cast<uint16_t>(bl.start), 0, static_cast<uint16_t>(bl.words)), out, &code);
    if(r == kFatal) return r;
    if(r == kEndCode) {
        if(last_end_[b] != code) {
            HML_WARN("fins: reading %s (%d words): %s", block_name(bl).c_str(), bl.words, fins_end_code_text(code).c_str());
            last_end_[b] = code;
        }
        return r;
    }
    if(static_cast<int>(out.size()) != bl.words * 2) {
        HML_WARN("fins: reading %s: %d bytes instead of %d", block_name(bl).c_str(), static_cast<int>(out.size()), bl.words * 2);
        return kFatal;
    }
    if(last_end_[b]) { HML_INFO("fins: %s can be read again", block_name(bl).c_str()); last_end_[b] = 0; }
    std::vector<uint16_t> words(static_cast<size_t>(bl.words));
    for(int i = 0; i < bl.words; i++) words[static_cast<size_t>(i)] = static_cast<uint16_t>((out[static_cast<size_t>(i) * 2] << 8) | out[static_cast<size_t>(i) * 2 + 1]);
    for(size_t i : block_items_[b]) {
        Item& it = items_[i];
        // (asked after the answer is in: a write queued while the read was on the wire is newer)
        { std::lock_guard<std::mutex> lock(qlock_); if(it.pending_writes > 0) continue; }
        uint16_t w[kFinsMaxWords] = {};
        const int n = std::min(it.dtype == DType::Bool ? 1 : it.words(), bl.words - it.word);
        for(int k = 0; k < n; k++) w[k] = words[static_cast<size_t>(it.word + k)];
        apply(it, w);
    }
    return kOk;
}

FinsDriver::Result FinsDriver::write_item(const Item& it, const Value& v)
{
    std::vector<uint8_t> data, out;
    uint16_t code = 0;
    if(it.dtype == DType::Bool) {
        data = fins_area_data(it.addr.bit_code, it.addr.number, static_cast<uint8_t>(it.addr.bit), 1);
        data.push_back(v.as_number() != 0 ? 0x01 : 0x00);
    }
    else {
        std::vector<uint16_t> w(static_cast<size_t>(it.words()), 0);
        if(it.dtype == DType::String) {
            std::string s = v.is_string() ? v.str : v.format();
            for(int i = 0; i < it.length && i < static_cast<int>(s.size()); i++)
                w[static_cast<size_t>(i / 2)] |= static_cast<uint16_t>(static_cast<uint8_t>(s[static_cast<size_t>(i)]) << (i % 2 ? 0 : 8));
        }
        else {
            const double raw = it.scale != 0 ? (v.as_number() - it.offset) / it.scale : v.as_number();
            uint32_t u32 = 0;
            switch(it.dtype) {
                case DType::U16: u32 = static_cast<uint16_t>(static_cast<uint32_t>(std::llround(raw))); break;
                case DType::S16: u32 = static_cast<uint16_t>(static_cast<int16_t>(std::llround(raw))); break;
                case DType::U32: u32 = static_cast<uint32_t>(std::llround(raw)); break;
                case DType::S32: u32 = static_cast<uint32_t>(static_cast<int32_t>(std::llround(raw))); break;
                case DType::Float: { float f = static_cast<float>(raw); std::memcpy(&u32, &f, 4); break; }
                case DType::Bcd16: case DType::Bcd32: {
                    const double top = it.dtype == DType::Bcd16 ? 9999.0 : 99999999.0;
                    if(raw < 0 || raw > top) {
                        HML_WARN("fins: writing '%s' (%s): %g does not fit into BCD (0..%.0f), not written", it.tag->name().c_str(),
                                 fins_format_address(it.addr).c_str(), raw, top);
                        return kOk;
                    }
                    u32 = fins_to_bcd(static_cast<uint32_t>(std::llround(raw)));
                    break;
                }
                default: break;
            }
            w[0] = static_cast<uint16_t>(u32 & 0xFFFF);
            if(w.size() > 1) w[1] = static_cast<uint16_t>(u32 >> 16);
            if(w.size() > 1 && it.swap_words) std::swap(w[0], w[1]);
        }
        data = fins_area_data(it.addr.word_code, it.addr.number, 0, static_cast<uint16_t>(w.size()));
        for(uint16_t x : w) { data.push_back(static_cast<uint8_t>(x >> 8)); data.push_back(static_cast<uint8_t>(x)); }
    }
    Result r = transact(0x01, 0x02, data, out, &code);
    if(r == kEndCode) HML_WARN("fins: writing '%s' (%s): %s", it.tag->name().c_str(), fins_format_address(it.addr).c_str(), fins_end_code_text(code).c_str());
    return r;
}

// An address no tag is mapped to: one read or one write, answered with text.
FinsDriver::Result FinsDriver::device_do(const DeviceRequest& rq, DeviceResult& rs)
{
    std::string err, why;
    const auto a = fins_parse_address(rq.address, &err);
    if(!a) { rs.error = err; return kOk; }
    std::vector<uint8_t> out;
    uint16_t code = 0;
    Result r = kOk;
    const int count = std::clamp(rq.count, 1, kDeviceMaxCount);
    if(a->bit >= 0) {
        if(a->bit_code == 0) { rs.error = "this area has no bits"; return kOk; }
        if(rq.write) {
            bool on = false;
            if(!device_parse_bit(rq.value, on)) { rs.error = "a bit is written with 0 or 1"; return kOk; }
            if(!write_allowed(*a, 1, why)) { rs.error = "not written: " + why; return kOk; }
            std::vector<uint8_t> data = fins_area_data(a->bit_code, a->number, static_cast<uint8_t>(a->bit), 1);
            data.push_back(on ? 0x01 : 0x00);
            r = transact(0x01, 0x02, data, out, &code);
        }
        else {
            const int n = std::min(count, 16 - a->bit);
            r = transact(0x01, 0x01, fins_area_data(a->bit_code, a->number, static_cast<uint8_t>(a->bit), static_cast<uint16_t>(n)), out, &code);
            if(r == kOk) {
                if(static_cast<int>(out.size()) != n) { rs.error = "the answer has another length"; return kFatal; }
                for(int i = 0; i < n; i++) rs.values.push_back((out[static_cast<size_t>(i)] & 1) ? "1" : "0");
            }
        }
    }
    else {
        const std::string type = rq.type.empty() ? "int16" : rq.type;
        const int words = device_type_words(type);
        if(words == 0) { rs.error = "unknown type '" + type + "'"; return kOk; }
        const int total = rq.write ? words : words * count;
        const uint32_t area_end = a->area == "T" ? 0x7FFF : 0xFFFF;
        if(static_cast<uint32_t>(a->number) + static_cast<uint32_t>(total) - 1 > area_end) { rs.error = "this goes past the end of the area"; return kOk; }
        if(rq.write) {
            uint16_t w[2] = {};
            if(!device_parse(type, rq.value, w, &err)) { rs.error = err; return kOk; }
            if(!write_allowed(*a, words, why)) { rs.error = "not written: " + why; return kOk; }
            std::vector<uint8_t> data = fins_area_data(a->word_code, a->number, 0, static_cast<uint16_t>(words));
            for(int k = 0; k < words; k++) { data.push_back(static_cast<uint8_t>(w[k] >> 8)); data.push_back(static_cast<uint8_t>(w[k])); }
            r = transact(0x01, 0x02, data, out, &code);
        }
        else {
            r = transact(0x01, 0x01, fins_area_data(a->word_code, a->number, 0, static_cast<uint16_t>(total)), out, &code);
            if(r == kOk) {
                if(static_cast<int>(out.size()) != total * 2) { rs.error = "the answer has another length"; return kFatal; }
                for(int i = 0; i < count; i++) {
                    uint16_t w[2] = {};
                    for(int k = 0; k < words; k++) {
                        const size_t at = static_cast<size_t>(i * words + k) * 2;
                        w[k] = static_cast<uint16_t>((out[at] << 8) | out[at + 1]);
                    }
                    rs.values.push_back(device_format(type, w));
                }
            }
        }
    }
    if(r == kEndCode) rs.error = fins_end_code_text(code);
    else if(r == kFatal) rs.error = "no answer";
    else rs.ok = true;
    if(rs.ok && rq.write) HML_INFO("fins: %s written with %s (System > Device)", fins_format_address(*a).c_str(), rq.value.c_str());
    return r;
}

void FinsDriver::set_online(bool on)
{
    if(online_ == (on ? 1 : 0)) return;
    online_ = on ? 1 : 0;
    HML_INFO("fins %s %s", stream_->describe().c_str(), on ? "connected" : "disconnected");
    if(status_tag_) status_tag_->set_number(on ? 1 : 0, this);
}

void FinsDriver::thread_main()
{
    uint32_t backoff = 500;
    auto wait = [this](uint32_t ms) { for(uint32_t t = 0; t < ms && running_; t += 50) sleep_ms(50); };
    while(running_) {
        if(!stream_->is_open()) {
            if(!stream_->open() || !handshake()) {
                stream_->close();
                set_online(false);
                device_fail_all("not connected");
                wait(backoff);
                if(backoff < 5000) backoff *= 2;
                continue;
            }
            backoff = 500;
            set_online(true);
        }
        const uint32_t cycle_start = millis();
        bool link_ok = true;
        for(;;) {                                   // writes first: operator actions feel immediate
            Write w;
            {
                std::lock_guard<std::mutex> lock(qlock_);
                if(queue_.empty()) queue_full_said_ = false;
                if(queue_.empty() || !link_ok) break;
                w = queue_.front();
                queue_.pop_front();
            }
            if(write_item(items_[w.item], w.value) == kFatal) link_ok = false;
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
        for(size_t b = 0; link_ok && b < blocks_.size() && running_; b++) if(read_block(b) == kFatal) link_ok = false;
        if(!link_ok) {
            device_fail_all("not connected");
            if(running_) HML_WARN("fins %s: communication error (%s), reconnecting", stream_->describe().c_str(), stream_->last_error().empty() ? "timeout" : stream_->last_error().c_str());
            stream_->close();
            set_online(false);
            wait(backoff);
            if(backoff < 5000) backoff *= 2;
            continue;
        }
        const uint32_t elapsed = millis() - cycle_start;
        const uint32_t rest = elapsed < poll_ms_ ? poll_ms_ - elapsed : 0;
        for(uint32_t t = 0; t < rest && running_; t += 10) sleep_ms(rest - t < 10 ? rest - t : 10);
    }
    stream_->close();
    set_online(false);
}

// UI thread: a mapped tag changed. Queue a write unless we caused it.
void FinsDriver::on_tag_changed(Tag& tag, const Value& v, const void* source)
{
    if(source == this) return;
    for(size_t i = 0; i < items_.size(); i++) {
        Item& it = items_[i];
        if(it.tag != &tag || !it.writable) continue;
        std::lock_guard<std::mutex> lock(qlock_);
        if(queue_.size() < kWriteQueueLen) { queue_.push_back({ i, v }); it.pending_writes++; continue; }
        // Full: nothing has been sent for a while (no connection). The newest value of a tag is
        // the one that counts: it replaces the one waiting, or the oldest write gives its place.
        if(!queue_full_said_) {
            queue_full_said_ = true;
            HML_WARN("fins: %zu writes are waiting (no connection?): from now on only the newest value of each tag is kept", queue_.size());
        }
        auto old = std::find_if(queue_.rbegin(), queue_.rend(), [i](const Write& w) { return w.item == i; });
        if(old != queue_.rend()) { old->value = v; continue; }
        items_[queue_.front().item].pending_writes--;
        queue_.pop_front();
        queue_.push_back({ i, v });
        it.pending_writes++;
    }
}

} // namespace

std::unique_ptr<Driver> make_fins_driver(const json& cfg)
{
    auto d = std::make_unique<FinsDriver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml
