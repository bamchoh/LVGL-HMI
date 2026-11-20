#include "fins.hpp"
#include <cctype>
#include "util.hpp"

namespace hml {
namespace {

struct Area { const char* name; uint8_t word_code, bit_code; uint32_t count; uint16_t offset; bool system; };

// (longer names first: "CIO" before "C", "DM" before "D")
// (the number of words is what the protocol can address; how far an area really goes depends
// on the CPU, which answers 1103 beyond its end)
const Area kAreas[] = {
    { "CIO", 0xB0, 0x30, 65536, 0, false },
    { "DM",  0x82, 0x02, 65536, 0, false },
    { "W",   0xB1, 0x31, 65536, 0, false },
    { "H",   0xB2, 0x32, 65536, 0, false },
    { "A",   0xB3, 0x33, 65536, 0, true },
    { "D",   0x82, 0x02, 65536, 0, false },
    { "T",   0x89, 0x00, 32768, 0, false },
    { "C",   0x89, 0x00, 32768, 0x8000, false },
};

void put32(std::vector<uint8_t>& b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 24)); b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 8)); b.push_back(static_cast<uint8_t>(v));
}
uint32_t get32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]; }

bool digits(const std::string& s, uint32_t& out, size_t max_len = 6)
{
    if(s.empty() || s.size() > max_len) return false;
    out = 0;
    for(char c : s) { if(c < '0' || c > '9') return false; out = out * 10 + static_cast<uint32_t>(c - '0'); }
    return true;
}

} // namespace

std::optional<FinsAddress> fins_parse_address(const std::string& text, std::string* err)
{
    auto fail = [&](const std::string& why) -> std::optional<FinsAddress> { if(err) *err = "\"" + text + "\": " + why; return std::nullopt; };
    std::string s;
    for(char c : text) if(!std::isspace(static_cast<unsigned char>(c))) s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if(s.empty() || s.size() > 16) return fail("not an address (D100, CIO10, W0.00, H5, E0_100, T1 ...)");

    FinsAddress a;
    std::string rest;
    uint32_t count = 0;
    uint16_t offset = 0;
    if(std::isdigit(static_cast<unsigned char>(s[0]))) {                 // a bare number: the CIO area
        a.area = "CIO"; a.word_code = 0xB0; a.bit_code = 0x30; count = 65536; rest = s;
    }
    else if(s[0] == 'E' && s.find('_') != std::string::npos) {           // E<bank>_<address>
        const size_t us = s.find('_');
        const std::string bank = s.substr(s.rfind("EM", 0) == 0 ? 2 : 1, us - (s.rfind("EM", 0) == 0 ? 2 : 1));
        if(bank.empty() || bank.size() > 2) return fail("the EM bank is 0..18 (hexadecimal), as in E0_100");
        uint32_t b = 0;
        for(char c : bank) {
            if(!std::isxdigit(static_cast<unsigned char>(c))) return fail("the EM bank is 0..18 (hexadecimal), as in E0_100");
            b = b * 16 + static_cast<uint32_t>(c <= '9' ? c - '0' : c - 'A' + 10);
        }
        if(b > 0x18) return fail("the EM bank is 0..18 (hexadecimal)");
        a.area = "E" + bank;
        a.word_code = static_cast<uint8_t>(b <= 0x0C ? 0xA0 + b : 0x60 + (b - 0x0D));
        a.bit_code = static_cast<uint8_t>(b <= 0x0C ? 0x20 + b : 0xE0 + (b - 0x0D));
        count = 65536;
        rest = s.substr(us + 1);
    }
    else {
        const Area* found = nullptr;
        for(const Area& ar : kAreas) {
            const size_t n = std::char_traits<char>::length(ar.name);
            if(s.compare(0, n, ar.name) == 0 && s.size() > n && std::isdigit(static_cast<unsigned char>(s[n]))) { found = &ar; rest = s.substr(n); break; }
        }
        if(!found) return fail("unknown area (CIO, W, H, A, D, E0_, T, C)");
        a.area = found->name[0] == 'D' ? "D" : found->name;
        a.word_code = found->word_code; a.bit_code = found->bit_code; a.system = found->system;
        count = found->count; offset = found->offset;
    }
    const size_t dot = rest.find('.');
    uint32_t number = 0;
    if(!digits(rest.substr(0, dot), number)) return fail("the number is decimal");
    if(number >= count) return fail("the number is above the end of the area (" + std::to_string(count - 1) + ")");
    a.number = static_cast<uint16_t>(number + offset);
    if(dot != std::string::npos) {
        uint32_t bit = 0;
        if(!digits(rest.substr(dot + 1), bit, 2) || bit > 15) return fail("the bit is 00..15");
        if(a.bit_code == 0) return fail("a timer / counter value has no bits");
        a.bit = static_cast<int>(bit);
    }
    return a;
}

std::string fins_format_address(const FinsAddress& a)
{
    const bool em = a.area.size() > 1 && a.area[0] == 'E';
    const uint32_t n = a.area == "C" ? a.number - 0x8000u : a.number;
    std::string s = a.area + (em ? "_" : "") + std::to_string(n);
    if(a.bit >= 0) s += vformat(".%02d", a.bit);
    return s;
}

std::vector<uint8_t> fins_tcp_handshake(uint8_t client_node)
{
    std::vector<uint8_t> b{ 'F', 'I', 'N', 'S' };
    put32(b, 12); put32(b, 0); put32(b, 0); put32(b, client_node);
    return b;
}

std::vector<uint8_t> fins_tcp_wrap(const std::vector<uint8_t>& frame)
{
    std::vector<uint8_t> b{ 'F', 'I', 'N', 'S' };
    put32(b, static_cast<uint32_t>(8 + frame.size())); put32(b, 2); put32(b, 0);
    b.insert(b.end(), frame.begin(), frame.end());
    return b;
}

int fins_tcp_body_len(const uint8_t* h, int max_body, uint32_t* command, uint32_t* error)
{
    if(h[0] != 'F' || h[1] != 'I' || h[2] != 'N' || h[3] != 'S') return -1;
    const uint32_t len = get32(h + 4);
    if(len < 8 || len - 8 > static_cast<uint32_t>(max_body)) return -1;
    if(command) *command = get32(h + 8);
    if(error) *error = get32(h + 12);
    return static_cast<int>(len - 8);
}

bool fins_tcp_parse_handshake(const uint8_t* body, int len, uint8_t* client_node, uint8_t* server_node)
{
    if(len < 8) return false;
    const uint32_t c = get32(body), s = get32(body + 4);
    if(c == 0 || c > 254 || s > 254) return false;
    if(client_node) *client_node = static_cast<uint8_t>(c);
    if(server_node) *server_node = static_cast<uint8_t>(s);
    return true;
}

std::string fins_tcp_error_text(uint32_t code)
{
    const char* t = "unknown error";
    switch(code) {
        case 1: t = "the header is not \"FINS\""; break;
        case 2: t = "the data is too long"; break;
        case 3: t = "the command is not supported"; break;
        case 0x20: t = "all connections are in use"; break;
        case 0x21: t = "the node is already connected"; break;
        case 0x22: t = "the node address is protected (another IP address uses it)"; break;
        case 0x23: t = "the client node address is out of range"; break;
        case 0x24: t = "the same node address is used by client and server"; break;
        case 0x25: t = "no node address is free"; break;
        default: break;
    }
    return vformat("%X: %s", static_cast<unsigned>(code), t);
}

std::vector<uint8_t> fins_frame(const FinsNode& dst, const FinsNode& src, uint8_t sid, uint8_t mrc, uint8_t src_code, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> f{ 0x80, 0x00, 0x02, dst.network, dst.node, dst.unit, src.network, src.node, src.unit, sid, mrc, src_code };
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

std::vector<uint8_t> fins_area_data(uint8_t area_code, uint16_t number, uint8_t bit, uint16_t count)
{
    return { area_code, static_cast<uint8_t>(number >> 8), static_cast<uint8_t>(number), bit, static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count) };
}

bool fins_parse_response(const uint8_t* f, int len, FinsResponse& out, std::string* err)
{
    if(len < 14) { if(err) *err = "the answer is too short"; return false; }
    if((f[0] & 0x40) == 0) { if(err) *err = "not a response frame"; return false; }
    out.sid = f[9]; out.mrc = f[10]; out.src = f[11];
    // bit 7 of the main code: a relay error; bits 6 and 7 of the sub code: a (non-)fatal CPU error.
    // The command itself was carried out or not as the rest of the code says.
    out.end_code = static_cast<uint16_t>(((f[12] & 0x7F) << 8) | (f[13] & 0x3F));
    out.data.assign(f + 14, f + len);
    return true;
}

std::string fins_end_code_text(uint16_t code)
{
    const char* t = "";
    switch(code) {
        case 0x0001: t = "service was interrupted"; break;
        case 0x0101: t = "the local node is not in the network"; break;
        case 0x0102: t = "token timeout"; break;
        case 0x0103: t = "retries failed"; break;
        case 0x0201: t = "the destination node is not in the network"; break;
        case 0x0202: t = "no such unit"; break;
        case 0x0204: t = "the destination node is busy"; break;
        case 0x0205: t = "response timeout"; break;
        case 0x0401: t = "the command is not supported"; break;
        case 0x0402: t = "not supported by this model / version"; break;
        case 0x0501: t = "the destination node address is not in the routing table"; break;
        case 0x1001: t = "the command is too long"; break;
        case 0x1002: t = "the command is too short"; break;
        case 0x1003: t = "the number of items does not match the data"; break;
        case 0x1004: t = "wrong command format"; break;
        case 0x1101: t = "no such memory area"; break;
        case 0x1102: t = "wrong access size"; break;
        case 0x1103: t = "address out of range"; break;
        case 0x1104: t = "the end of the range is exceeded"; break;
        case 0x110B: t = "the response would be too long"; break;
        case 0x110C: t = "wrong parameter"; break;
        case 0x2002: t = "the area is protected"; break;
        case 0x2101: t = "the area is read only"; break;
        case 0x2102: t = "the area is write protected"; break;
        case 0x2201: case 0x2202: case 0x2203: case 0x2204: case 0x2205: case 0x2206: case 0x2207: case 0x2208:
            t = "not possible in the present operating mode"; break;
        case 0x2301: t = "no such device"; break;
        case 0x3001: t = "no access right"; break;
        default: t = "see the FINS command reference"; break;
    }
    return vformat("%04X: %s", code, t);
}

uint32_t fins_from_bcd(uint32_t bcd, bool* ok)
{
    uint32_t v = 0, mul = 1;
    bool good = true;
    for(int i = 0; i < 8; i++) {
        const uint32_t d = (bcd >> (i * 4)) & 0xF;
        if(d > 9) good = false;
        v += d * mul;
        mul *= 10;
    }
    if(ok) *ok = good;
    return v;
}

uint32_t fins_to_bcd(uint32_t value)
{
    uint32_t b = 0;
    for(int i = 0; i < 8 && value; i++) { b |= (value % 10) << (i * 4); value /= 10; }
    return b;
}

} // namespace hml
