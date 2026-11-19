#include "slmp.hpp"
#include <cctype>
#include <cstdio>

namespace hml {

const std::vector<SlmpDevice>& slmp_devices()
{
    // (longest names first, so that "SM" is not read as "S" + "M")
    static const std::vector<SlmpDevice> devs = {
        { "STS", 0xC7, true, false, false }, { "STC", 0xC6, true, false, false }, { "STN", 0xC8, false, false, false },
        { "SM", 0x91, true, false, true },   { "SD", 0xA9, false, false, true },
        { "SB", 0xA1, true, true, false },   { "SW", 0xB5, false, true, false },
        { "SS", 0xC7, true, false, false },  { "SC", 0xC6, true, false, false },  { "SN", 0xC8, false, false, false },
        { "DX", 0xA2, true, true, false },   { "DY", 0xA3, true, true, false },
        { "TS", 0xC1, true, false, false },  { "TC", 0xC0, true, false, false },  { "TN", 0xC2, false, false, false },
        { "CS", 0xC4, true, false, false },  { "CC", 0xC3, true, false, false },  { "CN", 0xC5, false, false, false },
        { "ZR", 0xB0, false, true, false },
        { "D", 0xA8, false, false, false },  { "W", 0xB4, false, true, false },   { "R", 0xAF, false, false, false },
        { "Z", 0xCC, false, false, false },
        { "M", 0x90, true, false, false },   { "X", 0x9C, true, true, false },    { "Y", 0x9D, true, true, false },
        { "B", 0xA0, true, true, false },    { "L", 0x92, true, false, false },   { "F", 0x93, true, false, false },
        { "V", 0x94, true, false, false },   { "S", 0x98, true, false, false },
    };
    return devs;
}

namespace {
bool fail(std::string* err, const std::string& msg) { if(err) *err = msg; return false; }

bool parse_number(const std::string& s, bool hex, uint32_t& out)
{
    if(s.empty() || s.size() > 8) return false;
    uint64_t v = 0;
    for(char c : s) {
        int d;
        if(c >= '0' && c <= '9') d = c - '0';
        else if(hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v * (hex ? 16 : 10) + static_cast<uint64_t>(d);
        if(v > kSlmpMaxNumber) return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}
} // namespace

std::optional<SlmpAddress> slmp_parse_address(const std::string& text, std::string* err)
{
    std::string s;
    for(char c : text) if(!std::isspace(static_cast<unsigned char>(c))) s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if(s.empty() || s.size() > 16) { fail(err, "\"" + text + "\" is not a device address"); return std::nullopt; }
    for(const SlmpDevice& d : slmp_devices()) {
        const std::string name = d.name;
        if(s.compare(0, name.size(), name) != 0) continue;
        std::string rest = s.substr(name.size());
        if(rest.empty() || !std::isxdigit(static_cast<unsigned char>(rest[0]))) continue;     // "SM" in "SMX" is not it
        SlmpAddress a;
        a.dev = &d;
        size_t dot = rest.find('.');
        std::string num = rest.substr(0, dot);
        if(!parse_number(num, d.hex, a.number)) {
            fail(err, "\"" + text + "\": the number of " + name + (d.hex ? " is hexadecimal (0-9, A-F)" : " is decimal") + " and at most " + (d.hex ? "FFFFFF" : "16777215"));
            return std::nullopt;
        }
        if(dot != std::string::npos) {
            if(d.bit) { fail(err, "\"" + text + "\": a bit device has no bit number"); return std::nullopt; }
            std::string b = rest.substr(dot + 1);
            uint32_t bit = 0;
            if(!parse_number(b, true, bit) || bit > 15) { fail(err, "\"" + text + "\": the bit number is 0 to F"); return std::nullopt; }
            a.bit = static_cast<int>(bit);
        }
        return a;
    }
    fail(err, "\"" + text + "\": unknown device (D, W, R, ZR, M, X, Y, B, L, F, V, S, TS, TC, TN, SS, SC, SN, CS, CC, CN, SM, SD, SB, SW, DX, DY, Z)");
    return std::nullopt;
}

std::string slmp_format_address(const SlmpAddress& a)
{
    if(!a.dev) return "";
    char buf[48];
    if(a.dev->hex) std::snprintf(buf, sizeof(buf), "%s%X", a.dev->name, a.number);
    else std::snprintf(buf, sizeof(buf), "%s%u", a.dev->name, a.number);
    std::string s = buf;
    if(a.bit >= 0) { std::snprintf(buf, sizeof(buf), ".%X", a.bit); s += buf; }
    return s;
}

std::vector<uint8_t> slmp_request(bool frame4e, uint16_t serial, const SlmpRoute& route, uint16_t timer_250ms,
                                  uint16_t command, uint16_t subcommand, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> f;
    f.reserve(21 + data.size());
    if(frame4e) { f = { 0x54, 0x00, static_cast<uint8_t>(serial), static_cast<uint8_t>(serial >> 8), 0x00, 0x00 }; }
    else f = { 0x50, 0x00 };
    const size_t len = 6 + data.size();            // timer + command + subcommand + data
    f.push_back(route.network);
    f.push_back(route.pc);
    f.push_back(static_cast<uint8_t>(route.io)); f.push_back(static_cast<uint8_t>(route.io >> 8));
    f.push_back(route.station);
    f.push_back(static_cast<uint8_t>(len)); f.push_back(static_cast<uint8_t>(len >> 8));
    f.push_back(static_cast<uint8_t>(timer_250ms)); f.push_back(static_cast<uint8_t>(timer_250ms >> 8));
    f.push_back(static_cast<uint8_t>(command)); f.push_back(static_cast<uint8_t>(command >> 8));
    f.push_back(static_cast<uint8_t>(subcommand)); f.push_back(static_cast<uint8_t>(subcommand >> 8));
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

std::vector<uint8_t> slmp_batch_data(const SlmpDevice& dev, uint32_t number, uint16_t points)
{
    return { static_cast<uint8_t>(number), static_cast<uint8_t>(number >> 8), static_cast<uint8_t>(number >> 16), dev.code,
             static_cast<uint8_t>(points), static_cast<uint8_t>(points >> 8) };
}

int slmp_response_header_len(bool frame4e) { return frame4e ? 13 : 9; }

int slmp_response_body_len(const uint8_t* h, bool frame4e, int max_body)
{
    if(frame4e) { if(h[0] != 0xD4 || h[1] != 0x00 || h[4] != 0x00 || h[5] != 0x00) return -1; }
    else if(h[0] != 0xD0 || h[1] != 0x00) return -1;
    const int at = frame4e ? 11 : 7;
    const int len = h[at] | (h[at + 1] << 8);
    if(len < 2 || len > max_body) return -1;        // at least the end code
    return len;
}

bool slmp_parse_response(const uint8_t* buf, int len, bool frame4e, SlmpResponse& out, std::string* err)
{
    const int hl = slmp_response_header_len(frame4e);
    if(len < hl + 2) return fail(err, "response too short");
    const int body = slmp_response_body_len(buf, frame4e, len - hl);
    if(body < 0 || hl + body != len) return fail(err, "not a valid response frame");
    out.serial = frame4e ? static_cast<uint16_t>(buf[2] | (buf[3] << 8)) : 0;
    out.end_code = static_cast<uint16_t>(buf[hl] | (buf[hl + 1] << 8));
    out.data.assign(buf + hl + 2, buf + len);
    return true;
}

std::vector<uint8_t> slmp_pack_bits(const std::vector<bool>& bits)
{
    std::vector<uint8_t> out((bits.size() + 1) / 2, 0);
    for(size_t i = 0; i < bits.size(); i++) if(bits[i]) out[i / 2] |= (i % 2 == 0) ? 0x10 : 0x01;
    return out;
}

std::vector<bool> slmp_unpack_bits(const uint8_t* data, size_t bytes, size_t count)
{
    std::vector<bool> out;
    for(size_t i = 0; i < count && i / 2 < bytes; i++) out.push_back(((i % 2 == 0) ? (data[i / 2] >> 4) : data[i / 2]) & 0x01);
    return out;
}

std::string slmp_end_code_text(uint16_t code)
{
    const char* what = "error";
    switch(code) {
        case 0xC050: what = "data that cannot be converted"; break;
        case 0xC051: case 0xC052: case 0xC053: case 0xC054: what = "number of points out of range"; break;
        case 0xC056: what = "beyond the last device number"; break;
        case 0xC058: what = "request data length does not match"; break;
        case 0xC059: what = "command / subcommand not supported"; break;
        case 0xC05B: what = "this device cannot be read or written"; break;
        case 0xC05C: what = "wrong request"; break;
        case 0xC05F: what = "cannot be done on this station"; break;
        case 0xC061: what = "request data length does not match"; break;
        default: break;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%04X: %s", code, what);
    return buf;
}

} // namespace hml
