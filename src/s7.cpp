#include "s7.hpp"
#include <cctype>
#include "util.hpp"

namespace hml {
namespace {

void put16(std::vector<uint8_t>& b, uint32_t v) { b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v)); }
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

bool digits(const std::string& s, uint32_t& out, size_t max_len)
{
    if(s.empty() || s.size() > max_len) return false;
    out = 0;
    for(char c : s) { if(c < '0' || c > '9') return false; out = out * 10 + static_cast<uint32_t>(c - '0'); }
    return true;
}

// TPKT and COTP data header in front of an S7 PDU.
std::vector<uint8_t> wrap(const std::vector<uint8_t>& pdu)
{
    std::vector<uint8_t> b{ 0x03, 0x00 };
    put16(b, static_cast<uint32_t>(4 + 3 + pdu.size()));
    b.insert(b.end(), { 0x02, 0xF0, 0x80 });
    b.insert(b.end(), pdu.begin(), pdu.end());
    return b;
}

// The 10 bytes every job starts with.
std::vector<uint8_t> job(uint16_t ref, size_t param_len, size_t data_len)
{
    std::vector<uint8_t> b{ 0x32, 0x01, 0x00, 0x00 };
    put16(b, ref); put16(b, static_cast<uint32_t>(param_len)); put16(b, static_cast<uint32_t>(data_len));
    return b;
}

void put_item(std::vector<uint8_t>& b, const S7Item& it)
{
    const uint32_t address = it.byte * 8 + static_cast<uint32_t>(it.bit >= 0 ? it.bit : 0);
    b.insert(b.end(), { 0x12, 0x0A, 0x10, static_cast<uint8_t>(it.bit >= 0 ? 0x01 : 0x02) });
    put16(b, it.bit >= 0 ? 1u : it.count);
    put16(b, it.db);
    b.push_back(it.area);
    b.push_back(static_cast<uint8_t>(address >> 16)); b.push_back(static_cast<uint8_t>(address >> 8)); b.push_back(static_cast<uint8_t>(address));
}

const char* kPutGet = "on S7-1200 / 1500: permit \"PUT/GET communication\" in the protection settings of the CPU";

} // namespace

std::optional<S7Address> s7_parse_address(const std::string& text, std::string* err)
{
    auto fail = [&](const std::string& why) -> std::optional<S7Address> {
        // (what was typed may be anything: only its beginning is quoted)
        if(err) *err = "\"" + (text.size() > 40 ? text.substr(0, 40) + " ..." : text) + "\": " + why;
        return std::nullopt;
    };
    std::string s;
    for(char c : text) {
        if(std::isspace(static_cast<unsigned char>(c))) continue;
        if(s.size() > 24) return fail("not an address (DB1.DBW4, DB1.DBX0.0, MW10, M0.0, IB0, Q0.0 ...)");
        s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if(!s.empty() && s[0] == '%') s.erase(0, 1);
    if(s.empty()) return fail("not an address (DB1.DBW4, DB1.DBX0.0, MW10, M0.0, IB0, Q0.0 ...)");

    S7Address a;
    std::string rest;                        // from the letter of the size on: "W4", "X0.0", "0.0"
    if(s.compare(0, 2, "DB") == 0) {
        const size_t dot = s.find('.');
        uint32_t db = 0;
        if(dot == std::string::npos) return fail("a DB address names what is in the DB: DB1.DBW4, DB1.DBX0.0");
        if(!digits(s.substr(2, dot - 2), db, 5) || db < 1 || db > 65535) return fail("the DB number is 1..65535");
        if(s.compare(dot + 1, 2, "DB") != 0 || s.size() < dot + 4 || std::string("XBWD").find(s[dot + 3]) == std::string::npos)
            return fail("after the DB comes DBX (bit), DBB (byte), DBW (word) or DBD (double word): DB1.DBW4");
        a.area = kS7AreaDb;
        a.db = static_cast<uint16_t>(db);
        rest = s.substr(dot + 3);
    }
    else {
        switch(s[0]) {
            case 'I': case 'E': a.area = kS7AreaInput; break;
            case 'Q': case 'A': a.area = kS7AreaOutput; break;
            case 'M': a.area = kS7AreaMemory; break;
            default: return fail("unknown area (DB1.DB..., M, I or E, Q or A)");
        }
        rest = s.substr(1);
    }
    if(rest.empty()) return fail("the byte number is missing");
    bool is_bit = false;
    switch(rest[0]) {
        case 'X': is_bit = true; rest.erase(0, 1); break;
        case 'B': a.size = 1; rest.erase(0, 1); break;
        case 'W': a.size = 2; rest.erase(0, 1); break;
        case 'D': a.size = 4; rest.erase(0, 1); break;
        default:
            if(!std::isdigit(static_cast<unsigned char>(rest[0]))) return fail("after the area comes X (bit), B (byte), W (word) or D (double word), or the number of a bit: M0.0");
            is_bit = true;                   // "M0.0": a bit without the X
            break;
    }
    const size_t dot = rest.find('.');
    uint32_t number = 0;
    if(!digits(rest.substr(0, dot), number, 5)) return fail("the byte number is decimal, 0..65535");
    if(number + static_cast<uint32_t>(a.size) - 1 > kS7MaxByte) return fail("the byte number is above 65535");
    a.byte = number;
    if(is_bit) {
        uint32_t bit = 0;
        if(dot == std::string::npos) return fail("a bit is written with its number 0..7: " + std::string(a.area == kS7AreaDb ? "DB1.DBX0.0" : "M0.0") + " (a byte is B, a word W, a double word D)");
        if(!digits(rest.substr(dot + 1), bit, 1) || bit > 7) return fail("the bit is 0..7");
        a.bit = static_cast<int>(bit);
    }
    else if(dot != std::string::npos) return fail("only a bit (X) has a bit number");
    return a;
}

std::string s7_format_address(const S7Address& a)
{
    std::string s;
    switch(a.area) {
        case kS7AreaInput: s = "I"; break;
        case kS7AreaOutput: s = "Q"; break;
        case kS7AreaMemory: s = "M"; break;
        default: s = "DB" + std::to_string(a.db) + ".DB"; break;
    }
    if(a.bit >= 0) return s + (a.area == kS7AreaDb ? "X" : "") + std::to_string(a.byte) + "." + std::to_string(a.bit);
    return s + (a.size == 4 ? "D" : a.size == 2 ? "W" : "B") + std::to_string(a.byte);
}

// ---------------------------------------------------------------- the connection

std::vector<uint8_t> s7_connect_request(int rack, int slot)
{
    // destination TSAP 01 00 + rack * 0x20 + slot: a connection of the kind a PG opens, to that CPU
    const uint8_t tsap = static_cast<uint8_t>(((rack & 0x07) << 5) | (slot & 0x1F));
    return { 0x03, 0x00, 0x00, 0x16,                 // TPKT, 22 bytes
             0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,   // COTP connection request, class 0
             0xC0, 0x01, 0x0A,                       // TPDU size 1024
             0xC1, 0x02, 0x01, 0x00,                 // source TSAP
             0xC2, 0x02, 0x01, tsap };               // destination TSAP
}

int s7_tpkt_body_len(const uint8_t* h, int max_body)
{
    if(h[0] != 0x03 || h[1] != 0x00) return -1;
    const int len = get16(h + 2);
    if(len < 4 + 2 || len - 4 > max_body) return -1;        // (COTP is at least its length and its type)
    return len - 4;
}

bool s7_parse_connect_confirm(const uint8_t* body, int len, std::string* err)
{
    auto fail = [&](const char* why) { if(err) *err = why; return false; };
    if(len < 7 || body[0] < 6 || body[0] + 1 > len) return fail("the answer to the connection request is too short");
    switch(body[1] & 0xF0) {
        case 0xD0: return true;
        case 0x80: return fail("the PLC refused the connection (rack / slot?)");      // disconnect request
        default: return fail("not an answer to the connection request");
    }
}

int s7_cotp_data(const uint8_t* body, int len, bool* last)
{
    if(len < 3 || body[0] < 2 || body[0] + 1 > len || body[1] != 0xF0) return -1;
    if(last) *last = (body[2] & 0x80) != 0;
    return body[0] + 1;
}

// ---------------------------------------------------------------- requests

std::vector<uint8_t> s7_setup_request(uint16_t ref, uint16_t pdu_length)
{
    std::vector<uint8_t> p = job(ref, 8, 0);
    p.insert(p.end(), { 0xF0, 0x00, 0x00, 0x01, 0x00, 0x01 });      // one job at a time, each way
    put16(p, pdu_length);
    return wrap(p);
}

std::vector<uint8_t> s7_read_request(uint16_t ref, const std::vector<S7Item>& items)
{
    std::vector<uint8_t> p = job(ref, 2 + 12 * items.size(), 0);
    p.push_back(0x04);
    p.push_back(static_cast<uint8_t>(items.size()));
    for(const S7Item& it : items) put_item(p, it);
    return wrap(p);
}

std::vector<uint8_t> s7_write_request(uint16_t ref, const S7Item& item, const uint8_t* data, size_t len)
{
    const bool bit = item.bit >= 0;
    if(bit) len = 1;
    S7Item it = item;
    it.count = static_cast<uint16_t>(len);
    std::vector<uint8_t> p = job(ref, 2 + 12, 4 + len);
    p.push_back(0x05);
    p.push_back(0x01);
    put_item(p, it);
    p.push_back(0x00);
    p.push_back(bit ? 0x03 : 0x04);                  // the length: of a bit in bits (1), of bytes in bits too
    put16(p, static_cast<uint32_t>(bit ? 1 : len * 8));
    if(bit) p.push_back(data[0] ? 0x01 : 0x00);
    else p.insert(p.end(), data, data + len);
    return wrap(p);
}

// ---------------------------------------------------------------- answers

bool s7_parse_response(const uint8_t* p, int len, S7Response& out, std::string* err)
{
    auto fail = [&](const char* why) { if(err) *err = why; return false; };
    if(len < 10) return fail("the answer is too short");
    if(p[0] != 0x32) return fail("not an S7 answer");
    if(p[1] != 0x02 && p[1] != 0x03) return fail("not an answer (a job or a message of another kind)");
    if(len < 12) return fail("the answer is too short");
    const int param = get16(p + 6), data = get16(p + 8);
    if(12 + param + data != len) return fail("the lengths in the answer do not match its size");
    out.rosctr = p[1];
    out.ref = get16(p + 4);
    out.error_class = p[10];
    out.error_code = p[11];
    out.param.assign(p + 12, p + 12 + param);
    out.data.assign(p + 12 + param, p + len);
    return true;
}

bool s7_parse_setup(const S7Response& r, uint16_t* pdu_length)
{
    if(r.param.size() != 8 || r.param[0] != 0xF0) return false;
    if(pdu_length) *pdu_length = get16(r.param.data() + 6);
    return true;
}

bool s7_parse_read_data(const S7Response& r, size_t items, std::vector<S7ItemResult>& out, std::string* err)
{
    auto fail = [&](const char* why) { if(err) *err = why; return false; };
    out.clear();
    if(r.param.size() != 2 || r.param[0] != 0x04) return fail("not an answer to a read");
    if(r.param[1] != items) return fail("the answer has another number of items");
    const std::vector<uint8_t>& d = r.data;
    size_t at = 0;
    for(size_t i = 0; i < items; i++) {
        if(d.size() - at < 4) return fail("the answer ends inside an item");
        S7ItemResult it;
        it.code = d[at];
        const uint8_t transport = d[at + 1];
        size_t n = get16(d.data() + at + 2);
        at += 4;
        if(it.code == kS7ItemOk) {
            // the length counts bits for bits, bytes / words and integers; bytes for the rest (REAL, octets)
            if(transport == 0x03 || transport == 0x04 || transport == 0x05) n = (n + 7) / 8;
            if(n > d.size() - at) return fail("an item is longer than the answer");
            it.data.assign(d.begin() + static_cast<std::ptrdiff_t>(at), d.begin() + static_cast<std::ptrdiff_t>(at + n));
            at += n;
            if(n % 2 && i + 1 < items) {             // every item but the last is filled up to an even length
                if(at >= d.size()) return fail("the answer ends inside an item");
                at++;
            }
        }
        out.push_back(std::move(it));
    }
    if(d.size() - at > 1) return fail("the answer is longer than its items");       // (one fill byte after the last is let through)
    return true;
}

bool s7_parse_write_data(const S7Response& r, size_t items, std::vector<uint8_t>& codes, std::string* err)
{
    auto fail = [&](const char* why) { if(err) *err = why; return false; };
    codes.clear();
    if(r.param.size() != 2 || r.param[0] != 0x05) return fail("not an answer to a write");
    if(r.param[1] != items || r.data.size() != items) return fail("the answer has another number of items");
    codes = r.data;
    return true;
}

std::string s7_error_text(uint8_t error_class, uint8_t error_code)
{
    const char* t = "";
    bool put_get = false;
    switch((error_class << 8) | error_code) {
        case 0x8104: t = "the function is not available"; put_get = true; break;
        case 0x8404: t = "the function was aborted (a serious error in the CPU)"; break;
        case 0x8500: t = "wrong PDU length"; break;
        case 0xD241: t = "the protection level of the CPU does not allow it (a password is set)"; break;
        case 0xDC01: t = "invalid value"; break;
        default:
            switch(error_class) {
                case 0x81: t = "application relationship error"; break;
                case 0x82: t = "object definition error"; break;
                case 0x83: t = "no resources available in the CPU"; break;
                case 0x84: t = "error while the service was carried out"; break;
                case 0x85: t = "error in the data sent"; break;
                case 0x87: t = "access error"; break;
                default: t = "error"; break;
            }
            break;
    }
    std::string s = vformat("%s (class 0x%02X, code 0x%02X)", t, error_class, error_code);
    if(put_get) s += std::string(": ") + kPutGet;
    return s;
}

std::string s7_item_error_text(uint8_t code)
{
    const char* t = "";
    const char* hint = nullptr;
    switch(code) {
        case 0x01: t = "hardware fault"; break;
        case 0x03: t = "access denied"; hint = kPutGet; break;
        case 0x05: t = "address out of range"; break;
        case 0x06: t = "data type not supported"; break;
        case 0x07: t = "data type inconsistent"; break;
        case 0x0A: t = "the object does not exist"; hint = "no such DB (or, on S7-1200 / 1500, the DB has \"Optimized block access\")"; break;
        default: t = "unknown return code"; break;
    }
    std::string s = vformat("%s (code %u)", t, static_cast<unsigned>(code));
    if(hint) s += std::string(": ") + hint;
    return s;
}

} // namespace hml
