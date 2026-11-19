// SLMP client driver (Mitsubishi Electric MELSEC, MC protocol 3E / 4E binary frames over TCP).
//
// {
//   "type": "slmp",
//   "host": "192.168.0.10", "port": 5007,       // the CPU's or the Ethernet unit's SLMP port
//   "frame": "3e",                               // "3e" (default) | "4e"
//   "network": 0, "pc": 255, "io": 1023, "station": 0,    // route (defaults: the station itself)
//   "poll_ms": 200, "timeout_ms": 1000, "status_tag": "plc_online",
//   "allow_system_write": false,                 // SM / SD are never written unless true
//   "write_ranges": ["D0-D999", "M0-M99"],       // optional: only these may be written
//   "map": [
//     { "tag": "speed", "addr": "D100" },                        // dtype s16 (default for words)
//     { "tag": "total", "addr": "D102", "dtype": "s32" },        // D102 = low word, D103 = high word
//     { "tag": "temp",  "addr": "D110", "dtype": "float", "scale": 1 },
//     { "tag": "run",   "addr": "M0" },                          // bit devices: bool
//     { "tag": "in3",   "addr": "X1F", "access": "ro" },
//     { "tag": "ready", "addr": "D120.3" },                      // bit 3 of D120 (read only)
//     { "tag": "name",  "addr": "D200", "dtype": "string", "length": 16 }
//   ]
// }
//
// dtype: bool | u16 | s16 | u32 | s32 | float | string ("length" = characters, 2 per word, the
// first in the low byte as MELSEC stores them). 32-bit values: low word first unless "swap_words".
// Reads are collected into as few batch reads as possible (same device, at most 960 words,
// gaps of up to 32 words are read through). Bit devices are read in word units (16 points).
// Writes: words with batch write in word units, bit devices with batch write in bit units.
// A bit of a word ("D120.3") is read only: writing it would mean read-modify-write of a word
// the PLC may change at the same moment.
#include "driver.hpp"
#include "modbus_link.hpp"
#include "slmp.hpp"
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
constexpr uint16_t kCmdRead = 0x0401, kCmdWrite = 0x1401;

enum class DType { Bool, U16, S16, U32, S32, Float, String };

struct Item {
    Tag*        tag = nullptr;
    SlmpAddress addr;
    DType       dtype = DType::S16;
    int         length = 0;            // string: characters
    double      scale = 1, offset = 0;
    bool        readable = true, writable = false, swap_words = false;
    int         pending_writes = 0;    // guarded by qlock_
    // where the value is in the blocks read
    int         block = -1, word = 0;  // first word inside the block

    int words() const
    {
        switch(dtype) {
            case DType::U32: case DType::S32: case DType::Float: return 2;
            case DType::String: return (length + 1) / 2;
            default: return 1;
        }
    }
};

struct Block {                         // one batch read
    const SlmpDevice* dev = nullptr;
    uint32_t start = 0;                // device number (bit devices: a multiple of 16)
    int      words = 0;
};

struct Range { const SlmpDevice* dev; uint32_t from, to; };

class SlmpDriver final : public Driver {
public:
    explicit SlmpDriver(const json& cfg);
    ~SlmpDriver() override { stop(); }
    bool ok() const { return ok_; }
    const char* type() const override { return "slmp"; }
    bool start() override;
    void stop() override;

    // (for the tests)
    const std::vector<Block>& blocks() const { return blocks_; }

private:
    struct Write { size_t item; Value value; };
    enum Result { kOk = 1, kEndCode = 0, kFatal = -1 };

    Result transact(uint16_t cmd, uint16_t sub, const std::vector<uint8_t>& data, std::vector<uint8_t>& out);
    int    read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline);
    Result read_block(size_t b);
    Result write_item(const Item& it, const Value& v);
    void   apply(Item& it, const uint16_t* w);
    void   plan_blocks();
    bool   write_allowed(const SlmpAddress& a, int words, std::string& why) const;
    void   set_online(bool on);
    void   thread_main();
    void   on_tag_changed(Tag& tag, const Value& v, const void* source);

    std::unique_ptr<ModbusStream> stream_;
    bool        frame4e_ = false;
    SlmpRoute   route_;
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
    std::thread       thread_;
    std::atomic<bool> running_{ false };
    uint16_t          serial_ = 0;
    int               online_ = -1;
    bool              ok_ = false;
};

// ---------------------------------------------------------------- construction

SlmpDriver::SlmpDriver(const json& cfg)
{
    const json* map = jget(cfg, "map");
    if(!map || !map->is_array()) { HML_WARN("slmp: \"map\" array missing"); return; }
    if(!jhas(cfg, "host")) { HML_ERROR("slmp: \"host\" is needed"); return; }
    frame4e_ = iequals(jstr(cfg, "frame", "3e"), "4e");
    route_.network = static_cast<uint8_t>(jnum(cfg, "network", 0));
    route_.pc = static_cast<uint8_t>(jnum(cfg, "pc", 255));
    route_.io = static_cast<uint16_t>(jnum(cfg, "io", 0x3FF));
    route_.station = static_cast<uint8_t>(jnum(cfg, "station", 0));
    poll_ms_ = static_cast<uint32_t>(std::max(10.0, jnum(cfg, "poll_ms", 200)));
    timeout_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "timeout_ms", 1000), 50.0, 60000.0));
    if(auto st = jstr_opt(cfg, "status_tag"); st && !st->empty()) {
        status_tag_ = tags().find(*st);
        if(!status_tag_) HML_WARN("slmp: unknown status_tag '%s'", st->c_str());
    }
    allow_system_ = jbool(cfg, "allow_system_write", false);
    if(const json* wr = jget(cfg, "write_ranges"); wr && wr->is_array()) {
        for(const json& r : *wr) {
            std::string s = r.is_string() ? r.get<std::string>() : "";
            size_t dash = s.find('-');
            std::string err;
            auto a = slmp_parse_address(s.substr(0, dash), &err);
            auto b = dash == std::string::npos ? a : slmp_parse_address(s.substr(dash + 1), &err);
            if(!a || !b || a->dev != b->dev || a->bit >= 0 || b->bit >= 0 || b->number < a->number) {
                HML_WARN("slmp: write_ranges: \"%s\" is not a range like \"D0-D999\"%s%s", s.c_str(), err.empty() ? "" : ": ", err.c_str());
                continue;
            }
            write_ranges_.push_back({ a->dev, a->number, b->number });
        }
        if(write_ranges_.empty()) HML_WARN("slmp: write_ranges has no valid range: nothing can be written");
    }
    stream_ = make_tcp_stream(jstr(cfg, "host", "127.0.0.1"), static_cast<uint16_t>(jnum(cfg, "port", 5007)), timeout_ms_);

    for(const json& m : *map) {
        std::string tagname = jstr(m, "tag", "");
        Tag* tag = tags().find(tagname);
        if(!tag) { HML_WARN("slmp: unknown tag '%s'", tagname.c_str()); continue; }
        std::string err;
        auto addr = slmp_parse_address(jstr(m, "addr", ""), &err);
        if(!addr) { HML_WARN("slmp: tag '%s': %s", tagname.c_str(), err.c_str()); continue; }
        Item it;
        it.tag = tag;
        it.addr = *addr;
        const bool bitwise = addr->dev->bit || addr->bit >= 0;
        auto dt = jstr_opt(m, "dtype");
        if(bitwise) {
            if(dt && !iequals(*dt, "bool")) HML_WARN("slmp: tag '%s': %s is a bit, dtype '%s' ignored", tagname.c_str(), slmp_format_address(*addr).c_str(), dt->c_str());
            it.dtype = DType::Bool;
        }
        else if(!dt || iequals(*dt, "s16") || iequals(*dt, "int16")) it.dtype = DType::S16;
        else if(iequals(*dt, "u16") || iequals(*dt, "uint16")) it.dtype = DType::U16;
        else if(iequals(*dt, "s32") || iequals(*dt, "int32")) it.dtype = DType::S32;
        else if(iequals(*dt, "u32") || iequals(*dt, "uint32")) it.dtype = DType::U32;
        else if(iequals(*dt, "float") || iequals(*dt, "real") || iequals(*dt, "f32")) it.dtype = DType::Float;
        else if(iequals(*dt, "string")) {
            it.dtype = DType::String;
            it.length = std::clamp(static_cast<int>(jnum(m, "length", 16)), 1, 2 * kSlmpMaxWords);
        }
        else { HML_WARN("slmp: tag '%s': unknown dtype '%s', using s16", tagname.c_str(), dt->c_str()); it.dtype = DType::S16; }
        if(!addr->dev->bit && static_cast<uint64_t>(addr->number) + static_cast<uint64_t>(it.words()) - 1 > kSlmpMaxNumber) {
            HML_WARN("slmp: tag '%s': %s does not fit below the last device number", tagname.c_str(), slmp_format_address(*addr).c_str());
            continue;
        }
        it.scale = jnum(m, "scale", 1);
        it.offset = jnum(m, "offset", 0);
        it.swap_words = jbool(m, "swap_words", false);
        std::string access = jstr(m, "access", "rw");
        it.readable = !iequals(access, "wo");
        it.writable = !iequals(access, "ro");
        if(it.writable && addr->bit >= 0) {
            if(jhas(m, "access")) HML_WARN("slmp: tag '%s': a bit of a word (%s) is read only", tagname.c_str(), slmp_format_address(*addr).c_str());
            it.writable = false;
        }
        std::string why;
        if(it.writable && !write_allowed(*addr, bitwise ? 1 : it.words(), why)) {
            if(jhas(m, "access") || !addr->dev->system) HML_WARN("slmp: tag '%s': %s is not written (%s)", tagname.c_str(), slmp_format_address(*addr).c_str(), why.c_str());
            it.writable = false;
        }
        if(!it.readable && !it.writable) continue;
        items_.push_back(it);
    }
    plan_blocks();
    ok_ = true;
}

bool SlmpDriver::write_allowed(const SlmpAddress& a, int words, std::string& why) const
{
    if(a.dev->system && !allow_system_) { why = "SM / SD are written only with \"allow_system_write\""; return false; }
    if(write_ranges_.empty()) return true;
    const uint32_t last = a.number + static_cast<uint32_t>(a.dev->bit ? 0 : words - 1);
    for(const Range& r : write_ranges_) if(r.dev == a.dev && a.number >= r.from && last <= r.to) return true;
    why = "outside \"write_ranges\"";
    return false;
}

// Group what is read into batch reads: per device, sorted, neighbours merged.
void SlmpDriver::plan_blocks()
{
    std::map<uint8_t, std::vector<size_t>> by_dev;
    for(size_t i = 0; i < items_.size(); i++) if(items_[i].readable) by_dev[items_[i].addr.dev->code].push_back(i);
    for(auto& [code, list] : by_dev) {
        (void)code;
        // word span of each item, in the device's word numbering (bit devices: bit number / 16)
        auto span = [&](size_t i) {
            const Item& it = items_[i];
            const uint32_t first = it.addr.dev->bit ? it.addr.number / 16 : it.addr.number;
            return std::make_pair(first, first + static_cast<uint32_t>(it.addr.dev->bit ? 1 : it.words()));
        };
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) { return span(a).first < span(b).first; });
        for(size_t i : list) {
            auto [from, to] = span(i);
            const SlmpDevice* dev = items_[i].addr.dev;
            Block* cur = blocks_.empty() ? nullptr : &blocks_.back();
            const uint32_t cur_first = cur ? (cur->dev->bit ? cur->start / 16 : cur->start) : 0;
            const uint32_t cur_end = cur ? cur_first + static_cast<uint32_t>(cur->words) : 0;
            if(!cur || cur->dev != dev || from > cur_end + kGapWords || std::max(cur_end, to) - cur_first > static_cast<uint32_t>(kSlmpMaxWords)) {
                blocks_.push_back({ dev, dev->bit ? from * 16 : from, 0 });
                block_items_.emplace_back();
                cur = &blocks_.back();
            }
            const uint32_t first = cur->dev->bit ? cur->start / 16 : cur->start;
            cur->words = static_cast<int>(std::max(first + static_cast<uint32_t>(cur->words), to) - first);
            items_[i].block = static_cast<int>(blocks_.size() - 1);
            items_[i].word = static_cast<int>(from - first);
            block_items_.back().push_back(i);
        }
    }
    last_end_.assign(blocks_.size(), 0);
}

bool SlmpDriver::start()
{
    for(auto& it : items_) {
        if(it.writable) it.tag->subscribe(SubGroup::Driver, [this](Tag& t, const Value& v, const void* src) { on_tag_changed(t, v, src); });
    }
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
    return true;
}

void SlmpDriver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------- frames

int SlmpDriver::read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline)
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

SlmpDriver::Result SlmpDriver::transact(uint16_t cmd, uint16_t sub, const std::vector<uint8_t>& data, std::vector<uint8_t>& out)
{
    const uint16_t serial = ++serial_;
    // the monitoring timer (units of 250 ms) a little below our own timeout
    const uint16_t timer = static_cast<uint16_t>(std::max<uint32_t>(1, timeout_ms_ / 250));
    std::vector<uint8_t> req = slmp_request(frame4e_, serial, route_, timer, cmd, sub, data);
    if(!stream_->write(req.data(), static_cast<int>(req.size()))) return kFatal;

    // every failure on the connection (also a timeout or a frame we do not understand) means:
    // start over with a new connection, so that a late answer cannot be taken for the next one
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    const int hl = slmp_response_header_len(frame4e_);
    uint8_t buf[13 + 2 + kSlmpMaxWords * 2];
    if(read_exact(buf, hl, deadline) != 1) return kFatal;
    const int body = slmp_response_body_len(buf, frame4e_, static_cast<int>(sizeof(buf)) - hl);
    if(body < 0) { HML_WARN("slmp %s: not an SLMP response", stream_->describe().c_str()); return kFatal; }
    if(read_exact(buf + hl, body, deadline) != 1) return kFatal;
    SlmpResponse r;
    if(!slmp_parse_response(buf, hl + body, frame4e_, r)) return kFatal;
    if(frame4e_ && r.serial != serial) { HML_WARN("slmp %s: answer to another request", stream_->describe().c_str()); return kFatal; }
    out = std::move(r.data);
    if(r.end_code != 0) { out.assign({ static_cast<uint8_t>(r.end_code), static_cast<uint8_t>(r.end_code >> 8) }); return kEndCode; }
    return kOk;
}

// ---------------------------------------------------------------- read / write

void SlmpDriver::apply(Item& it, const uint16_t* w)
{
    double v = 0;
    uint32_t u32 = it.swap_words ? (static_cast<uint32_t>(w[0]) << 16) | w[1] : (static_cast<uint32_t>(w[1]) << 16) | w[0];
    switch(it.dtype) {
        case DType::Bool:
            v = it.addr.dev->bit ? (w[0] >> (it.addr.number % 16)) & 1 : (w[0] >> it.addr.bit) & 1;
            it.tag->set_number(v, this);
            return;
        case DType::U16: v = w[0]; break;
        case DType::S16: v = static_cast<int16_t>(w[0]); break;
        case DType::U32: v = u32; break;
        case DType::S32: v = static_cast<int32_t>(u32); break;
        case DType::Float: { float f; std::memcpy(&f, &u32, 4); v = f; break; }
        case DType::String: {
            std::string s;
            for(int i = 0; i < it.length; i++) {
                char c = static_cast<char>(i % 2 == 0 ? (w[i / 2] & 0xFF) : (w[i / 2] >> 8));
                if(c == '\0') break;
                s.push_back(c);
            }
            it.tag->set_string(s, this);
            return;
        }
    }
    it.tag->set_number_from_device(v * it.scale + it.offset, this);
}

SlmpDriver::Result SlmpDriver::read_block(size_t b)
{
    const Block& bl = blocks_[b];
    std::vector<uint8_t> out;
    Result r = transact(kCmdRead, 0x0000, slmp_batch_data(*bl.dev, bl.start, static_cast<uint16_t>(bl.words)), out);
    if(r == kFatal) return r;
    if(r == kEndCode) {
        const uint16_t code = static_cast<uint16_t>(out[0] | (out[1] << 8));
        if(last_end_[b] != code) {
            HML_WARN("slmp: reading %s (%d words): %s", slmp_format_address({ bl.dev, bl.start, -1 }).c_str(), bl.words, slmp_end_code_text(code).c_str());
            last_end_[b] = code;
        }
        return r;
    }
    if(static_cast<int>(out.size()) != bl.words * 2) { HML_WARN("slmp: reading %s: %d bytes instead of %d", slmp_format_address({ bl.dev, bl.start, -1 }).c_str(), static_cast<int>(out.size()), bl.words * 2); return kFatal; }
    if(last_end_[b]) { HML_INFO("slmp: %s can be read again", slmp_format_address({ bl.dev, bl.start, -1 }).c_str()); last_end_[b] = 0; }
    std::vector<uint16_t> words(static_cast<size_t>(bl.words));
    for(int i = 0; i < bl.words; i++) words[static_cast<size_t>(i)] = static_cast<uint16_t>(out[static_cast<size_t>(i) * 2] | (out[static_cast<size_t>(i) * 2 + 1] << 8));
    for(size_t i : block_items_[b]) {
        Item& it = items_[i];
        // (asked after the answer is in: a write queued while the read was on the wire is newer)
        { std::lock_guard<std::mutex> lock(qlock_); if(it.pending_writes > 0) continue; }
        uint16_t w[kSlmpMaxWords] = {};
        const int n = std::min(it.dtype == DType::Bool ? 1 : it.words(), bl.words - it.word);
        for(int k = 0; k < n; k++) w[k] = words[static_cast<size_t>(it.word + k)];
        apply(it, w);
    }
    return kOk;
}

SlmpDriver::Result SlmpDriver::write_item(const Item& it, const Value& v)
{
    std::vector<uint8_t> data, out;
    Result r;
    if(it.dtype == DType::Bool) {
        data = slmp_batch_data(*it.addr.dev, it.addr.number, 1);
        data.push_back(v.as_number() != 0 ? 0x10 : 0x00);
        r = transact(kCmdWrite, 0x0001, data, out);
    }
    else {
        std::vector<uint16_t> w(static_cast<size_t>(it.words()), 0);
        if(it.dtype == DType::String) {
            std::string s = v.is_string() ? v.str : v.format();
            for(int i = 0; i < it.length && i < static_cast<int>(s.size()); i++)
                w[static_cast<size_t>(i / 2)] |= static_cast<uint16_t>(static_cast<uint8_t>(s[static_cast<size_t>(i)]) << (i % 2 ? 8 : 0));
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
                default: break;
            }
            w[0] = static_cast<uint16_t>(u32 & 0xFFFF);
            if(w.size() > 1) w[1] = static_cast<uint16_t>(u32 >> 16);
            if(w.size() > 1 && it.swap_words) std::swap(w[0], w[1]);
        }
        data = slmp_batch_data(*it.addr.dev, it.addr.number, static_cast<uint16_t>(w.size()));
        for(uint16_t x : w) { data.push_back(static_cast<uint8_t>(x)); data.push_back(static_cast<uint8_t>(x >> 8)); }
        r = transact(kCmdWrite, 0x0000, data, out);
    }
    if(r == kEndCode) HML_WARN("slmp: writing '%s' (%s): %s", it.tag->name().c_str(), slmp_format_address(it.addr).c_str(),
                               slmp_end_code_text(static_cast<uint16_t>(out[0] | (out[1] << 8))).c_str());
    return r;
}

void SlmpDriver::set_online(bool on)
{
    if(online_ == (on ? 1 : 0)) return;
    online_ = on ? 1 : 0;
    HML_INFO("slmp %s %s", stream_->describe().c_str(), on ? "connected" : "disconnected");
    if(status_tag_) status_tag_->set_number(on ? 1 : 0, this);
}

void SlmpDriver::thread_main()
{
    uint32_t backoff = 500;
    auto wait = [this](uint32_t ms) { for(uint32_t t = 0; t < ms && running_; t += 50) sleep_ms(50); };
    while(running_) {
        if(!stream_->is_open()) {
            if(!stream_->open()) {
                set_online(false);
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
                if(queue_.empty() || !link_ok) break;
                w = queue_.front();
                queue_.pop_front();
            }
            if(write_item(items_[w.item], w.value) == kFatal) link_ok = false;
            std::lock_guard<std::mutex> lock(qlock_);
            items_[w.item].pending_writes--;
        }
        for(size_t b = 0; link_ok && b < blocks_.size() && running_; b++) if(read_block(b) == kFatal) link_ok = false;
        if(!link_ok) {
            HML_WARN("slmp %s: communication error (%s), reconnecting", stream_->describe().c_str(), stream_->last_error().empty() ? "timeout" : stream_->last_error().c_str());
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

void SlmpDriver::on_tag_changed(Tag& tag, const Value& v, const void* source)
{
    if(source == this) return;
    for(size_t i = 0; i < items_.size(); i++) {
        Item& it = items_[i];
        if(it.tag != &tag || !it.writable) continue;
        std::lock_guard<std::mutex> lock(qlock_);
        if(queue_.size() < kWriteQueueLen) { queue_.push_back({ i, v }); it.pending_writes++; }
        else HML_WARN("slmp: write queue full, dropping write to '%s'", tag.name().c_str());
    }
}

} // namespace

std::unique_ptr<Driver> make_slmp_driver(const json& cfg)
{
    auto d = std::make_unique<SlmpDriver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml
