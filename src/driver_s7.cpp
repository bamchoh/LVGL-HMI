// S7 client driver (Siemens S7-300 / 400 / 1200 / 1500), S7comm ("PUT/GET") over ISO-on-TCP.
//
// {
//   "type": "s7",
//   "host": "192.168.0.1", "port": 102,
//   "rack": 0, "slot": 1,                         // S7-1200 / 1500: 0 / 1 (default), S7-300: 0 / 2
//   "poll_ms": 200, "timeout_ms": 1000, "status_tag": "plc_online",
//   "write_ranges": ["DB1.DBB0-DB1.DBB99", "MB10-MB19", "DB5"],   // optional: only these bytes may be written ("DB5": all of DB5)
//   "allow_system_write": false,                  // the inputs (I) are never written unless true
//   "map": [
//     { "tag": "run",   "addr": "DB1.DBX0.0" },                      // a bit: bool, read and written
//     { "tag": "state", "addr": "DB1.DBB2" },                        // a byte: uint8 (default) | int8
//     { "tag": "speed", "addr": "DB1.DBW4" },                        // a word: int16 (default) | uint16
//     { "tag": "total", "addr": "DB1.DBD6" },                        // a double word: int32 (default) | uint32 | float
//     { "tag": "temp",  "addr": "DB1.DBD10", "dtype": "float" },
//     { "tag": "name",  "addr": "DB1.DBB20", "dtype": "string", "length": 16 },   // a STRING[16]
//     { "tag": "lamp",  "addr": "Q0.3", "access": "ro" },
//     { "tag": "flag",  "addr": "M10.0" }, { "tag": "count", "addr": "MW20" }
//   ]
// }
//
// The address says how many bytes a value has, "dtype" how they are read; every value is
// big-endian, as the CPU stores it. A string is an S7 STRING: the byte the address names holds
// the declared length, the next one the present length, then come the characters ("length" =
// the declared length; a write leaves the first byte alone).
// Reads are collected: neighbours in one area / DB become one item (gaps of up to 32 bytes are
// read through), and as many items go into one "read var" as the PDU length the CPU agreed to
// lets through - the request and the answer. Writes: "write var", one value per request; a bit
// as one bit (the CPU changes only that bit).
#include "driver.hpp"
#include "modbus_link.hpp"
#include "s7.hpp"
#include "tag.hpp"
#include "util.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace hml {
namespace {

constexpr size_t kWriteQueueLen = 64;        // more than these waiting: from there on a tag has one place in the queue (its newest value)
constexpr size_t kWriteQueueMax = 2048;      // ... and this is all there is room for (many tags written at once: data_transfer.hpp)
constexpr uint32_t kGapBytes = 32;
constexpr int    kMaxBody = 1024;            // of one packet (the TPDU size we asked for)
constexpr size_t kMaxMessage = 4096;         // of an answer put together from its parts

enum class DType { Bool, U8, S8, U16, S16, U32, S32, Float, String };

struct Item {
    Tag*        tag = nullptr;
    S7Address   addr;
    DType       dtype = DType::S16;
    int         length = 0;            // string: the declared length (characters)
    double      scale = 1, offset = 0;
    bool        readable = true, writable = false;
    int         pending_writes = 0;    // guarded by qlock_
    int         block = -1, at = 0;    // where the value is in the blocks read

    int bytes() const { return dtype == DType::String ? 2 + length : addr.size; }
};

struct Block {                         // bytes of one area / DB that are read together
    uint8_t  area = 0;
    uint16_t db = 0;
    uint32_t start = 0;
    int      bytes = 0;
    int      parts = 0, got = 0;       // the read items it takes (more than one: a value longer than a PDU), and those in
    uint8_t  last_code = kS7ItemOk;    // the return code last logged
    std::vector<uint8_t> data;
    std::vector<size_t>  items;
};

struct Part { size_t block; int at, bytes; };     // one read item

struct Range { uint8_t area; uint16_t db; uint32_t from, to; };

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while(a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while(b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

class S7Driver final : public Driver {
public:
    explicit S7Driver(const json& cfg);
    ~S7Driver() override { stop(); }
    bool ok() const { return ok_; }
    const char* type() const override { return "s7"; }
    bool start() override;
    void stop() override;
    bool device_access() const override { return true; }
    std::string device_help() const override { return "DB1.DBW4  DB1.DBD6  DB1.DBB2  MW10  IB0  QB0,  a bit: DB1.DBX0.0  M0.0  I0.0  Q0.0"; }

private:
    struct Write { size_t item; Value value; };
    // kRefused: the PLC answered and said no (the connection is fine); kFatal: start over with a new connection
    enum Result { kOk = 1, kRefused = 0, kFatal = -1 };

    Result device_do(const DeviceRequest& rq, DeviceResult& rs);
    bool   handshake();
    bool   receive(std::vector<uint8_t>& pdu, std::chrono::steady_clock::time_point deadline);
    Result transact(const std::vector<uint8_t>& req, uint16_t ref, uint8_t function, S7Response& r, std::string& why);
    int    read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline);
    Result read_request(size_t q);
    Result read_bytes(const S7Address& a, int bytes, std::vector<uint8_t>& out, std::string& why);
    Result write_bytes(const S7Address& a, const uint8_t* data, int bytes, std::string& why);
    Result write_item(const Item& it, const Value& v);
    void   apply(Item& it, const uint8_t* p);
    void   plan(int pdu);
    bool   write_allowed(const S7Address& a, int bytes, std::string& why) const;
    std::string block_name(const Block& b) const;
    void   connect_failed(const std::string& why);
    void   set_online(bool on);
    bool   link_polls_cyclically() const override { return true; }
    void   thread_main();
    void   on_tag_changed(Tag& tag, const Value& v, const void* source);

    std::unique_ptr<ModbusStream> stream_;
    int         rack_ = 0, slot_ = 1;
    uint32_t    poll_ms_ = 200, timeout_ms_ = 1000;
    Tag*        status_tag_ = nullptr;
    bool        allow_system_ = false;
    bool        has_ranges_ = false;
    std::vector<Range> write_ranges_;
    std::vector<Item>  items_;

    // what is read, laid out for the PDU length of the connection (plan)
    int                pdu_ = 0;
    std::vector<Block> blocks_;
    std::vector<Part>  parts_;
    std::vector<std::vector<size_t>> requests_;   // the parts of each "read var"
    std::vector<uint16_t> request_error_;         // per request: the error class / code last logged (0 = fine)

    std::mutex        qlock_;
    std::deque<Write> queue_;
    bool              queue_full_said_ = false;
    std::thread       thread_;
    std::atomic<bool> running_{ false };
    uint16_t          ref_ = 0;
    int               online_ = -1;
    std::string       connect_said_;              // why the last connection failed (said once)
    bool              ok_ = false;
};

// ---------------------------------------------------------------- construction

S7Driver::S7Driver(const json& cfg)
{
    link_set(false);
    const json* map = jget(cfg, "map");
    if(!map || !map->is_array()) { HML_WARN("s7: \"map\" array missing"); return; }
    if(!jhas(cfg, "host")) { HML_ERROR("s7: \"host\" is needed"); return; }
    const double rack = jnum(cfg, "rack", 0), slot = jnum(cfg, "slot", 1);
    if(rack < 0 || rack > 7 || slot < 0 || slot > 31) HML_WARN("s7: \"rack\" is 0..7 and \"slot\" 0..31 (S7-1200 / 1500: 0 / 1, S7-300: 0 / 2)");
    rack_ = static_cast<int>(std::clamp(rack, 0.0, 7.0));
    slot_ = static_cast<int>(std::clamp(slot, 0.0, 31.0));
    poll_ms_ = static_cast<uint32_t>(std::max(10.0, jnum(cfg, "poll_ms", 200)));
    timeout_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "timeout_ms", 1000), 50.0, 60000.0));
    if(auto st = jstr_opt(cfg, "status_tag"); st && !st->empty()) {
        status_tag_ = tags().find(*st);
        if(!status_tag_) HML_WARN("s7: unknown status_tag '%s'", st->c_str());
    }
    allow_system_ = jbool(cfg, "allow_system_write", false);
    if(const json* wr = jget(cfg, "write_ranges"); wr && wr->is_array()) {
        has_ranges_ = true;
        for(const json& r : *wr) {
            const std::string s = trim(r.is_string() ? r.get<std::string>() : "");
            std::string up;
            for(char c : s) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            // "DB5": the whole DB
            if(up.size() > 2 && up.size() <= 7 && up.compare(0, 2, "DB") == 0 && up.find_first_not_of("0123456789", 2) == std::string::npos) {
                const unsigned long db = std::strtoul(up.c_str() + 2, nullptr, 10);
                if(db >= 1 && db <= 65535) { write_ranges_.push_back({ kS7AreaDb, static_cast<uint16_t>(db), 0, kS7MaxByte }); continue; }
            }
            const size_t dash = s.find('-');
            std::string err;
            auto a = s7_parse_address(s.substr(0, dash), &err);
            auto b = dash == std::string::npos ? a : s7_parse_address(s.substr(dash + 1), &err);
            if(!a || !b || a->area != b->area || a->db != b->db || a->bit >= 0 || b->bit >= 0 || b->byte < a->byte) {
                HML_WARN("s7: write_ranges: \"%s\" is not a range like \"DB1.DBB0-DB1.DBB99\", \"MB0-MB19\" or \"DB5\"%s%s", s.c_str(), err.empty() ? "" : ": ", err.c_str());
                continue;
            }
            write_ranges_.push_back({ a->area, a->db, a->byte, b->byte + static_cast<uint32_t>(b->size) - 1 });
        }
        if(write_ranges_.empty()) HML_WARN("s7: write_ranges has no valid range: nothing can be written");
    }
    stream_ = make_tcp_stream(jstr(cfg, "host", "127.0.0.1"), static_cast<uint16_t>(jnum(cfg, "port", 102)), timeout_ms_);

    for(const json& m : *map) {
        std::string tagname = jstr(m, "tag", "");
        Tag* tag = tags().find(tagname);
        if(!tag) { HML_WARN("s7: unknown tag '%s'", tagname.c_str()); continue; }
        std::string err;
        auto addr = s7_parse_address(jstr(m, "addr", ""), &err);
        if(!addr) { HML_WARN("s7: tag '%s': %s", tagname.c_str(), err.c_str()); continue; }
        Item it;
        it.tag = tag;
        it.addr = *addr;
        const std::string name = s7_format_address(*addr);
        auto dt = jstr_opt(m, "dtype");
        if(dt && dt->empty()) dt.reset();
        // the address says how wide the value is; the dtype has to be one of that width
        auto other = [&](const char* what, const char* choice) {
            HML_WARN("s7: tag '%s': %s is %s, dtype '%s' ignored (%s)", tagname.c_str(), name.c_str(), what, dt->c_str(), choice);
        };
        if(addr->bit >= 0) {
            if(dt && !iequals(*dt, "bool")) other("a bit", "bool");
            it.dtype = DType::Bool;
        }
        else if(addr->size == 1) {
            it.dtype = DType::U8;
            if(!dt || iequals(*dt, "uint8") || iequals(*dt, "u8") || iequals(*dt, "byte")) {}
            else if(iequals(*dt, "int8") || iequals(*dt, "s8")) it.dtype = DType::S8;
            else if(iequals(*dt, "string")) {
                it.dtype = DType::String;
                it.length = std::clamp(static_cast<int>(jnum(m, "length", 16)), 1, kS7MaxString);
            }
            else other("a byte", "uint8, int8 or string; a word is DBW / MW, a double word DBD / MD");
        }
        else if(addr->size == 2) {
            it.dtype = DType::S16;
            if(!dt || iequals(*dt, "s16") || iequals(*dt, "int16")) {}
            else if(iequals(*dt, "u16") || iequals(*dt, "uint16")) it.dtype = DType::U16;
            else other("a word", "int16 or uint16; a double word is DBD / MD, a string starts at a byte: DBB");
        }
        else {
            it.dtype = DType::S32;
            if(!dt || iequals(*dt, "s32") || iequals(*dt, "int32")) {}
            else if(iequals(*dt, "u32") || iequals(*dt, "uint32")) it.dtype = DType::U32;
            else if(iequals(*dt, "float") || iequals(*dt, "real") || iequals(*dt, "f32")) it.dtype = DType::Float;
            else other("a double word", "int32, uint32 or float; a word is DBW / MW, a string starts at a byte: DBB");
        }
        if(addr->byte + static_cast<uint32_t>(it.bytes()) - 1 > kS7MaxByte) {
            HML_WARN("s7: tag '%s': %s does not fit below byte %u", tagname.c_str(), name.c_str(), static_cast<unsigned>(kS7MaxByte));
            continue;
        }
        it.scale = jnum(m, "scale", 1);
        it.offset = jnum(m, "offset", 0);
        std::string access = jstr(m, "access", "rw");
        it.readable = !iequals(access, "wo");
        it.writable = !iequals(access, "ro");
        std::string why;
        if(it.writable && !write_allowed(*addr, it.bytes(), why)) {
            if(jhas(m, "access") || addr->area != kS7AreaInput) HML_WARN("s7: tag '%s': %s is not written (%s)", tagname.c_str(), name.c_str(), why.c_str());
            it.writable = false;
        }
        if(!it.readable && !it.writable) continue;
        items_.push_back(it);
    }
    plan(kS7PduAsk);
    ok_ = true;
}

bool S7Driver::write_allowed(const S7Address& a, int bytes, std::string& why) const
{
    // (the CPU overwrites the inputs with what the modules read, every cycle)
    if(a.area == kS7AreaInput && !allow_system_) { why = "the inputs (I) are written only with \"allow_system_write\""; return false; }
    if(!has_ranges_) return true;
    const uint32_t last = a.byte + static_cast<uint32_t>(bytes - 1);
    for(const Range& r : write_ranges_) if(r.area == a.area && r.db == a.db && a.byte >= r.from && last <= r.to) return true;
    why = "outside \"write_ranges\"";
    return false;
}

std::string S7Driver::block_name(const Block& b) const
{
    S7Address a;
    a.area = b.area; a.db = b.db; a.byte = b.start;
    return s7_format_address(a);
}

// Lay out what is read for this PDU length: per area / DB, sorted, neighbours merged into
// blocks; a block is one read item (several when one value is longer than an answer can be);
// the items are put into as few requests as the PDU length allows.
void S7Driver::plan(int pdu)
{
    pdu_ = pdu;
    blocks_.clear(); parts_.clear(); requests_.clear();
    for(Item& it : items_) it.block = -1;
    const uint32_t max_bytes = static_cast<uint32_t>(s7_max_read_bytes(pdu));
    std::map<std::pair<uint8_t, uint16_t>, std::vector<size_t>> by_area;
    for(size_t i = 0; i < items_.size(); i++) if(items_[i].readable) by_area[{ items_[i].addr.area, items_[i].addr.db }].push_back(i);
    for(auto& [key, list] : by_area) {
        auto span = [&](size_t i) { return std::make_pair(items_[i].addr.byte, items_[i].addr.byte + static_cast<uint32_t>(items_[i].bytes())); };
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) { return span(a).first < span(b).first; });
        bool first_of_area = true;
        for(size_t i : list) {
            auto [from, to] = span(i);
            Block* cur = first_of_area ? nullptr : &blocks_.back();
            const uint32_t cur_end = cur ? cur->start + static_cast<uint32_t>(cur->bytes) : 0;
            if(!cur || from > cur_end + kGapBytes || std::max(cur_end, to) - cur->start > max_bytes) {
                blocks_.emplace_back();
                cur = &blocks_.back();
                cur->area = key.first; cur->db = key.second; cur->start = from;
                first_of_area = false;
            }
            cur->bytes = static_cast<int>(std::max(cur->start + static_cast<uint32_t>(cur->bytes), to) - cur->start);
            cur->items.push_back(i);
            items_[i].block = static_cast<int>(blocks_.size() - 1);
            items_[i].at = static_cast<int>(from - cur->start);
        }
    }
    for(size_t b = 0; b < blocks_.size(); b++) {
        Block& bl = blocks_[b];
        bl.data.assign(static_cast<size_t>(bl.bytes), 0);
        for(int at = 0; at < bl.bytes; at += static_cast<int>(max_bytes)) {
            parts_.push_back({ b, at, std::min(bl.bytes - at, static_cast<int>(max_bytes)) });
            bl.parts++;
        }
    }
    // each part into the first request that still has room: for its 12 bytes in the request, and
    // for its data (4 bytes of header, filled up to an even length) in the answer
    std::vector<int> answer;                 // bytes of the answer of each request so far
    for(size_t p = 0; p < parts_.size(); p++) {
        const int cost = 4 + parts_[p].bytes + parts_[p].bytes % 2;
        size_t q = 0;
        for(; q < requests_.size(); q++) {
            const int n = static_cast<int>(requests_[q].size());
            if(n < kS7MaxItems && 12 + 12 * (n + 1) <= pdu && answer[q] + cost <= pdu) break;
        }
        if(q == requests_.size()) { requests_.emplace_back(); answer.push_back(14); }
        requests_[q].push_back(p);
        answer[q] += cost;
    }
    request_error_.assign(requests_.size(), 0);
}

bool S7Driver::start()
{
    for(auto& it : items_) {
        if(it.writable) it.tag->subscribe(SubGroup::Driver, [this](Tag& t, const Value& v, const void* src) { on_tag_changed(t, v, src); });
    }
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
    return true;
}

void S7Driver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------- packets

int S7Driver::read_exact(uint8_t* buf, int n, std::chrono::steady_clock::time_point deadline)
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

// One S7 message: the data packets up to the one marked as the last.
bool S7Driver::receive(std::vector<uint8_t>& pdu, std::chrono::steady_clock::time_point deadline)
{
    pdu.clear();
    for(;;) {
        uint8_t buf[4 + kMaxBody];
        if(read_exact(buf, 4, deadline) != 1) return false;
        const int body = s7_tpkt_body_len(buf, kMaxBody);
        if(body < 0) { HML_WARN("s7 %s: not an ISO-on-TCP (TPKT) answer", stream_->describe().c_str()); return false; }
        if(read_exact(buf + 4, body, deadline) != 1) return false;
        bool last = false;
        const int at = s7_cotp_data(buf + 4, body, &last);
        if(at < 0) { HML_WARN("s7 %s: not a data packet (the PLC closed the connection?)", stream_->describe().c_str()); return false; }
        pdu.insert(pdu.end(), buf + 4 + at, buf + 4 + body);
        if(pdu.size() > kMaxMessage) { HML_WARN("s7 %s: the answer is too long", stream_->describe().c_str()); return false; }
        if(last) return true;
    }
}

void S7Driver::connect_failed(const std::string& why)
{
    if(why == connect_said_) return;         // (it is tried again and again: said once)
    connect_said_ = why;
    HML_WARN("s7 %s: %s", stream_->describe().c_str(), why.c_str());
}

// After the TCP connection: the ISO connection to the CPU in rack / slot, then the PDU length.
bool S7Driver::handshake()
{
    const std::vector<uint8_t> cr = s7_connect_request(rack_, slot_);
    if(!stream_->write(cr.data(), static_cast<int>(cr.size()))) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    uint8_t buf[4 + kMaxBody];
    if(read_exact(buf, 4, deadline) != 1) { connect_failed(vformat("no answer to the connection request (is this an S7 CPU? rack %d / slot %d?)", rack_, slot_)); return false; }
    const int body = s7_tpkt_body_len(buf, kMaxBody);
    if(body < 0) { connect_failed("not an ISO-on-TCP (TPKT) answer"); return false; }
    if(read_exact(buf + 4, body, deadline) != 1) return false;
    std::string err;
    if(!s7_parse_connect_confirm(buf + 4, body, &err)) { connect_failed(err); return false; }

    const uint16_t ref = ++ref_;
    const std::vector<uint8_t> setup = s7_setup_request(ref, static_cast<uint16_t>(kS7PduAsk));
    if(!stream_->write(setup.data(), static_cast<int>(setup.size()))) return false;
    std::vector<uint8_t> pdu;
    S7Response r;
    if(!receive(pdu, deadline)) { connect_failed("no answer to \"setup communication\""); return false; }
    if(!s7_parse_response(pdu.data(), static_cast<int>(pdu.size()), r, &err)) { connect_failed("setup communication: " + err); return false; }
    if(r.ref != ref) { connect_failed("setup communication: answer to another request"); return false; }
    if(r.error_class || r.error_code) { connect_failed("the PLC refused \"setup communication\": " + s7_error_text(r.error_class, r.error_code)); return false; }
    uint16_t length = 0;
    if(!s7_parse_setup(r, &length)) { connect_failed("setup communication: unexpected answer"); return false; }
    // what the CPU says, within what we asked for and what every CPU can do
    const int agreed = std::clamp(static_cast<int>(length), kS7PduMin, kS7PduAsk);
    if(agreed != pdu_) plan(agreed);
    connect_said_.clear();
    return true;
}

S7Driver::Result S7Driver::transact(const std::vector<uint8_t>& req, uint16_t ref, uint8_t function, S7Response& r, std::string& why)
{
    if(!stream_->write(req.data(), static_cast<int>(req.size()))) return kFatal;

    // every failure on the connection (also a timeout or an answer we do not understand) means:
    // start over with a new connection, so that a late answer cannot be taken for the next one
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    std::vector<uint8_t> pdu;
    if(!receive(pdu, deadline)) return kFatal;
    std::string err;
    if(!s7_parse_response(pdu.data(), static_cast<int>(pdu.size()), r, &err)) { HML_WARN("s7 %s: %s", stream_->describe().c_str(), err.c_str()); return kFatal; }
    if(r.ref != ref) { HML_WARN("s7 %s: answer to another request", stream_->describe().c_str()); return kFatal; }
    if(r.error_class || r.error_code) { why = s7_error_text(r.error_class, r.error_code); return kRefused; }
    if(r.rosctr != 0x03 || r.param.empty() || r.param[0] != function) { HML_WARN("s7 %s: answer to another function", stream_->describe().c_str()); return kFatal; }
    return kOk;
}

// ---------------------------------------------------------------- read / write

void S7Driver::apply(Item& it, const uint8_t* p)
{
    double v = 0;
    uint32_t u32 = 0;                        // the bytes of a number (as many as the address has), the first one highest
    if(it.dtype != DType::String) for(int k = 0; k < it.addr.size; k++) u32 = (u32 << 8) | p[k];
    switch(it.dtype) {
        case DType::Bool: it.tag->set_number_from_device((p[0] >> it.addr.bit) & 1, this); return;
        case DType::U8: case DType::U16: case DType::U32: v = u32; break;
        case DType::S8: v = static_cast<int8_t>(u32); break;
        case DType::S16: v = static_cast<int16_t>(u32); break;
        case DType::S32: v = static_cast<int32_t>(u32); break;
        case DType::Float: { float f; std::memcpy(&f, &u32, 4); v = f; break; }
        case DType::String: {
            // (the present length is the PLC's word: never more than was read)
            const int n = std::min<int>(p[1], it.length);
            std::string s;
            for(int i = 0; i < n; i++) {
                const char c = static_cast<char>(p[2 + i]);
                if(c == '\0') break;
                s.push_back(c);
            }
            it.tag->set_from_device(Value::string(s), this);
            return;
        }
    }
    it.tag->set_number_from_device(v * it.scale + it.offset, this);
}

// One "read var" of the plan.
S7Driver::Result S7Driver::read_request(size_t q)
{
    const std::vector<size_t>& list = requests_[q];
    std::vector<S7Item> items;
    for(size_t p : list) {
        const Part& pt = parts_[p];
        const Block& bl = blocks_[pt.block];
        S7Item it;
        it.area = bl.area; it.db = bl.db; it.byte = bl.start + static_cast<uint32_t>(pt.at); it.count = static_cast<uint16_t>(pt.bytes);
        items.push_back(it);
    }
    const uint16_t ref = ++ref_;
    S7Response r;
    std::string why, err;
    const Result res = transact(s7_read_request(ref, items), ref, 0x04, r, why);
    if(res == kFatal) return res;
    if(res == kRefused) {
        const uint16_t code = static_cast<uint16_t>((r.error_class << 8) | r.error_code);
        if(request_error_[q] != code) {
            HML_WARN("s7: reading %s ...: the PLC refused: %s", block_name(blocks_[parts_[list[0]].block]).c_str(), why.c_str());
            request_error_[q] = code;
        }
        return res;
    }
    request_error_[q] = 0;
    std::vector<S7ItemResult> got;
    if(!s7_parse_read_data(r, items.size(), got, &err)) { HML_WARN("s7 %s: %s", stream_->describe().c_str(), err.c_str()); return kFatal; }
    for(size_t k = 0; k < list.size(); k++) {
        const Part& pt = parts_[list[k]];
        Block& bl = blocks_[pt.block];
        if(got[k].code != kS7ItemOk) {       // this item is refused: the others of the request are fine
            if(bl.last_code != got[k].code) {
                HML_WARN("s7: reading %s (%d bytes): the PLC refused: %s", block_name(bl).c_str(), bl.bytes, s7_item_error_text(got[k].code).c_str());
                bl.last_code = got[k].code;
            }
            continue;
        }
        if(static_cast<int>(got[k].data.size()) != pt.bytes) {
            HML_WARN("s7: reading %s: %d bytes instead of %d", block_name(bl).c_str(), static_cast<int>(got[k].data.size()), pt.bytes);
            return kFatal;
        }
        std::memcpy(bl.data.data() + pt.at, got[k].data.data(), static_cast<size_t>(pt.bytes));
        if(++bl.got < bl.parts) continue;
        if(bl.last_code != kS7ItemOk) { HML_INFO("s7: %s can be read again", block_name(bl).c_str()); bl.last_code = kS7ItemOk; }
        for(size_t i : bl.items) {
            Item& it = items_[i];
            // (asked after the answer is in: a write queued while the read was on the wire is newer)
            { std::lock_guard<std::mutex> lock(qlock_); if(it.pending_writes > 0) continue; }
            if(it.at < 0 || it.at + it.bytes() > bl.bytes) continue;
            apply(it, bl.data.data() + it.at);
        }
    }
    return kOk;
}

// Bytes that no block of the plan holds (System > Device): one item, in as many requests as it takes.
S7Driver::Result S7Driver::read_bytes(const S7Address& a, int bytes, std::vector<uint8_t>& out, std::string& why)
{
    out.clear();
    const int step = s7_max_read_bytes(pdu_);
    for(int at = 0; at < bytes; at += step) {
        S7Item it;
        it.area = a.area; it.db = a.db; it.byte = a.byte + static_cast<uint32_t>(at); it.count = static_cast<uint16_t>(std::min(step, bytes - at));
        const uint16_t ref = ++ref_;
        S7Response r;
        std::string err;
        const Result res = transact(s7_read_request(ref, { it }), ref, 0x04, r, why);
        if(res != kOk) return res;
        std::vector<S7ItemResult> got;
        if(!s7_parse_read_data(r, 1, got, &err)) { HML_WARN("s7 %s: %s", stream_->describe().c_str(), err.c_str()); return kFatal; }
        if(got[0].code != kS7ItemOk) { why = s7_item_error_text(got[0].code); return kRefused; }
        if(got[0].data.size() != it.count) { HML_WARN("s7 %s: %d bytes instead of %d", stream_->describe().c_str(), static_cast<int>(got[0].data.size()), static_cast<int>(it.count)); return kFatal; }
        out.insert(out.end(), got[0].data.begin(), got[0].data.end());
    }
    return kOk;
}

// a.bit >= 0: that bit (data[0] = 0 / 1); otherwise `bytes` bytes from a.byte on.
S7Driver::Result S7Driver::write_bytes(const S7Address& a, const uint8_t* data, int bytes, std::string& why)
{
    const int step = a.bit >= 0 ? 1 : s7_max_write_bytes(pdu_);
    if(a.bit >= 0) bytes = 1;
    for(int at = 0; at < bytes; at += step) {
        S7Item it;
        it.area = a.area; it.db = a.db; it.byte = a.byte + static_cast<uint32_t>(at); it.bit = a.bit;
        const int n = std::min(step, bytes - at);
        const uint16_t ref = ++ref_;
        S7Response r;
        std::string err;
        const Result res = transact(s7_write_request(ref, it, data + at, static_cast<size_t>(n)), ref, 0x05, r, why);
        if(res != kOk) return res;
        std::vector<uint8_t> codes;
        if(!s7_parse_write_data(r, 1, codes, &err)) { HML_WARN("s7 %s: %s", stream_->describe().c_str(), err.c_str()); return kFatal; }
        if(codes[0] != kS7ItemOk) { why = s7_item_error_text(codes[0]); return kRefused; }
    }
    return kOk;
}

S7Driver::Result S7Driver::write_item(const Item& it, const Value& v)
{
    std::vector<uint8_t> data;
    S7Address a = it.addr;
    if(it.dtype == DType::Bool) data.push_back(v.as_number() != 0 ? 1 : 0);
    else if(it.dtype == DType::String) {
        // the present length and the characters; the declared length (the first byte) is the PLC's
        std::string s = v.is_string() ? v.str : v.format();
        if(static_cast<int>(s.size()) > it.length) s.resize(static_cast<size_t>(it.length));
        data.assign(static_cast<size_t>(1 + it.length), 0);
        data[0] = static_cast<uint8_t>(s.size());
        std::memcpy(data.data() + 1, s.data(), s.size());
        a.byte += 1;
    }
    else {
        const double raw = it.scale != 0 ? (v.as_number() - it.offset) / it.scale : v.as_number();
        uint32_t u32 = 0;
        switch(it.dtype) {
            case DType::U8: u32 = static_cast<uint8_t>(static_cast<uint32_t>(std::llround(raw))); break;
            case DType::S8: u32 = static_cast<uint8_t>(static_cast<int8_t>(std::llround(raw))); break;
            case DType::U16: u32 = static_cast<uint16_t>(static_cast<uint32_t>(std::llround(raw))); break;
            case DType::S16: u32 = static_cast<uint16_t>(static_cast<int16_t>(std::llround(raw))); break;
            case DType::U32: u32 = static_cast<uint32_t>(std::llround(raw)); break;
            case DType::S32: u32 = static_cast<uint32_t>(static_cast<int32_t>(std::llround(raw))); break;
            case DType::Float: { float f = static_cast<float>(raw); std::memcpy(&u32, &f, 4); break; }
            default: break;
        }
        for(int k = it.addr.size - 1; k >= 0; k--) data.push_back(static_cast<uint8_t>(u32 >> (k * 8)));
    }
    std::string why;
    const Result r = write_bytes(a, data.data(), static_cast<int>(data.size()), why);
    if(r == kRefused) HML_WARN("s7: writing '%s' (%s): the PLC refused: %s", it.tag->name().c_str(), s7_format_address(it.addr).c_str(), why.c_str());
    return r;
}

// An address no tag is mapped to: one read or one write, answered with text. The address says
// how many bytes a value has, the type how they are read (int16 / int32: with a sign, uint16 /
// uint32: without, hex, and float for a double word).
S7Driver::Result S7Driver::device_do(const DeviceRequest& rq, DeviceResult& rs)
{
    std::string err, why;
    const auto a = s7_parse_address(rq.address, &err);
    if(!a) { rs.error = err; return kOk; }
    std::vector<uint8_t> out;
    Result r = kOk;
    const int count = std::clamp(rq.count, 1, kDeviceMaxCount);
    if(a->bit >= 0) {
        if(rq.write) {
            bool on = false;
            if(!device_parse_bit(rq.value, on)) { rs.error = "a bit is written with 0 or 1"; return kOk; }
            if(!write_allowed(*a, 1, why)) { rs.error = "not written: " + why; return kOk; }
            const uint8_t b = on ? 1 : 0;
            r = write_bytes(*a, &b, 1, why);
        }
        else {
            // the bits one after the other, on into the next bytes
            const int bytes = (a->bit + count + 7) / 8;
            if(a->byte + static_cast<uint32_t>(bytes) - 1 > kS7MaxByte) { rs.error = "this goes past the last byte"; return kOk; }
            S7Address from = *a;
            from.bit = -1;
            r = read_bytes(from, bytes, out, why);
            if(r == kOk) for(int i = 0; i < count; i++) rs.values.push_back((out[static_cast<size_t>((a->bit + i) / 8)] >> ((a->bit + i) % 8)) & 1 ? "1" : "0");
        }
    }
    else {
        const std::string type = rq.type.empty() ? "int16" : rq.type;
        if(device_type_words(type) == 0) { rs.error = "unknown type '" + type + "'"; return kOk; }
        const bool is_float = type == "float", hex = type == "hex", is_signed = type == "int16" || type == "int32";
        if(is_float && a->size != 4) { rs.error = "a float is a double word: " + std::string(a->area == kS7AreaDb ? "DB1.DBD4" : "MD4"); return kOk; }
        const int total = rq.write ? a->size : a->size * count;
        if(a->byte + static_cast<uint32_t>(total) - 1 > kS7MaxByte) { rs.error = "this goes past the last byte"; return kOk; }
        if(rq.write) {
            uint32_t u32 = 0;
            if(hex) {
                std::string s = trim(rq.value);
                if(s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.erase(0, 2);
                if(s.empty() || s.size() > static_cast<size_t>(a->size * 2) || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
                    rs.error = vformat("in hexadecimal this is 0 .. %s", std::string(static_cast<size_t>(a->size * 2), 'F').c_str());
                    return kOk;
                }
                u32 = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
            }
            else {
                // (read as the 16 / 32-bit type of that sign; a byte has to fit into its own range)
                const std::string as = is_float ? "float" : a->size == 4 ? (is_signed ? "int32" : "uint32") : (is_signed ? "int16" : "uint16");
                uint16_t w[2] = {};
                if(!device_parse(as, rq.value, w, &err)) { rs.error = err; return kOk; }
                u32 = (static_cast<uint32_t>(w[1]) << 16) | w[0];
                if(a->size == 1) {
                    const int v = is_signed ? static_cast<int16_t>(w[0]) : static_cast<int>(w[0]);
                    if(v < (is_signed ? -128 : 0) || v > (is_signed ? 127 : 255)) { rs.error = is_signed ? "a byte with a sign is -128 .. 127" : "a byte is 0 .. 255"; return kOk; }
                }
            }
            if(!write_allowed(*a, a->size, why)) { rs.error = "not written: " + why; return kOk; }
            uint8_t data[4] = {};
            for(int k = 0; k < a->size; k++) data[k] = static_cast<uint8_t>(u32 >> ((a->size - 1 - k) * 8));
            r = write_bytes(*a, data, a->size, why);
        }
        else {
            r = read_bytes(*a, total, out, why);
            if(r == kOk) {
                for(int i = 0; i < count; i++) {
                    uint32_t u32 = 0;
                    for(int k = 0; k < a->size; k++) u32 = (u32 << 8) | out[static_cast<size_t>(i * a->size + k)];
                    std::string text;
                    if(hex) text = vformat("%0*X", a->size * 2, static_cast<unsigned>(u32));
                    else if(is_float) { float f; std::memcpy(&f, &u32, 4); text = vformat("%g", static_cast<double>(f)); }
                    else if(!is_signed) text = std::to_string(u32);
                    else text = std::to_string(a->size == 1 ? static_cast<int32_t>(static_cast<int8_t>(u32)) : a->size == 2 ? static_cast<int32_t>(static_cast<int16_t>(u32)) : static_cast<int32_t>(u32));
                    rs.values.push_back(text);
                }
            }
        }
    }
    if(r == kRefused) rs.error = "the PLC refused: " + why;
    else if(r == kFatal) rs.error = "no answer";
    else rs.ok = true;
    if(rs.ok && rq.write) HML_INFO("s7: %s written with %s (System > Device)", s7_format_address(*a).c_str(), rq.value.c_str());
    return r;
}

void S7Driver::set_online(bool on)
{
    link_set(on);
    if(online_ == (on ? 1 : 0)) return;
    online_ = on ? 1 : 0;
    HML_INFO("s7 %s %s", stream_->describe().c_str(), on ? vformat("connected (rack %d, slot %d, PDU length %d)", rack_, slot_, pdu_).c_str() : "disconnected");
    if(status_tag_) status_tag_->set_number(on ? 1 : 0, this);
}

void S7Driver::thread_main()
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
        for(Block& b : blocks_) b.got = 0;
        for(size_t q = 0; link_ok && q < requests_.size() && running_; q++) if(read_request(q) == kFatal) link_ok = false;
        if(!link_ok) {
            device_fail_all("not connected");
            if(running_) HML_WARN("s7 %s: communication error (%s), reconnecting", stream_->describe().c_str(), stream_->last_error().empty() ? "timeout" : stream_->last_error().c_str());
            stream_->close();
            set_online(false);
            wait(backoff);
            if(backoff < 5000) backoff *= 2;
            continue;
        }
        link_polled();
        const uint32_t elapsed = millis() - cycle_start;
        const uint32_t rest = elapsed < poll_ms_ ? poll_ms_ - elapsed : 0;
        for(uint32_t t = 0; t < rest && running_; t += 10) sleep_ms(rest - t < 10 ? rest - t : 10);
    }
    stream_->close();
    set_online(false);
}

// UI thread: a mapped tag changed. Queue a write unless we caused it.
void S7Driver::on_tag_changed(Tag& tag, const Value& v, const void* source)
{
    if(source == this) return;
    for(size_t i = 0; i < items_.size(); i++) {
        Item& it = items_[i];
        if(it.tag != &tag || !it.writable) continue;
        std::lock_guard<std::mutex> lock(qlock_);
        if(queue_.size() < kWriteQueueLen) { queue_.push_back({ i, v }); it.pending_writes++; continue; }
        // Many are waiting: nothing has been sent for a while (no connection), or many tags were
        // written at once. The newest value of a tag is the one that counts: it replaces the one
        // waiting. A tag that is not waiting yet gets a place while there is room; with none left
        // the oldest write gives its place.
        if(!queue_full_said_ && link_state() != 1) {
            queue_full_said_ = true;
            HML_WARN("s7: %zu writes are waiting (no connection?): from now on only the newest value of each tag is kept", queue_.size());
        }
        auto old = std::find_if(queue_.rbegin(), queue_.rend(), [i](const Write& w) { return w.item == i; });
        if(old != queue_.rend()) { old->value = v; continue; }
        if(queue_.size() < kWriteQueueMax) { queue_.push_back({ i, v }); it.pending_writes++; continue; }
        items_[queue_.front().item].pending_writes--;
        queue_.pop_front();
        queue_.push_back({ i, v });
        it.pending_writes++;
    }
}

} // namespace

std::unique_ptr<Driver> make_s7_driver(const json& cfg)
{
    auto d = std::make_unique<S7Driver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml
