// SLMP / MC protocol (Mitsubishi Electric MELSEC), binary code, 3E and 4E frames: the pieces the
// client driver (driver_slmp.cpp) is built from. Nothing here does I/O, so every byte can be tested.
//
// Request (3E):  50 00 | network | pc | io (LE16) | station | length (LE16) | timer (LE16) | command (LE16) | subcommand (LE16) | data
// Request (4E):  54 00 | serial (LE16) | 00 00 | ... as 3E from "network"
// Response:      D0 00 (D4 00 serial 00 00) | network | pc | io | station | length (LE16) | end code (LE16) | data
//                "length" counts from the timer (request) / the end code (response) to the end.
//
// Devices are written as in GX Works: "D100", "M0", "X1F" (X Y B W SB SW DX DY ZR take hexadecimal
// numbers), and "D100.3" for bit 3 of a word device.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hml {

struct SlmpDevice {
    const char* name;        // "D", "SM"
    uint8_t     code;        // binary device code (MELSEC-Q / L)
    bool        bit;         // a bit device (M, X ...) - read in words of 16 points
    bool        hex;         // the number is written in hexadecimal
    bool        system;      // SM / SD: the CPU's own settings (not written unless allowed)
};

const std::vector<SlmpDevice>& slmp_devices();

struct SlmpAddress {
    const SlmpDevice* dev = nullptr;
    uint32_t number = 0;      // device number
    int      bit = -1;        // "D100.3": bit 3 of the word (word devices only), -1 = none
};

constexpr uint32_t kSlmpMaxNumber = 0xFFFFFF;     // 3 bytes in the request
constexpr int      kSlmpMaxWords = 960;           // batch read / write, word units
constexpr int      kSlmpMaxBits = 7168;           // batch read / write, bit units

// "D100", "x1f", "ZR0A", "D10.F". nullopt (and *err) when it is not an address.
std::optional<SlmpAddress> slmp_parse_address(const std::string& text, std::string* err = nullptr);
std::string slmp_format_address(const SlmpAddress& a);

struct SlmpRoute {
    uint8_t  network = 0;
    uint8_t  pc = 0xFF;
    uint16_t io = 0x03FF;          // the CPU of the station
    uint8_t  station = 0;
};

// A whole request frame.
std::vector<uint8_t> slmp_request(bool frame4e, uint16_t serial, const SlmpRoute& route, uint16_t timer_250ms,
                                  uint16_t command, uint16_t subcommand, const std::vector<uint8_t>& data);

// Request data of a batch read / write: device number (3 bytes LE), device code, points (LE16) [, data].
std::vector<uint8_t> slmp_batch_data(const SlmpDevice& dev, uint32_t number, uint16_t points);

// Bytes of the response header (up to and including the length field).
int slmp_response_header_len(bool frame4e);

struct SlmpResponse {
    uint16_t serial = 0;
    uint16_t end_code = 0;
    std::vector<uint8_t> data;     // after the end code
};

// Check the header of a response: the number of bytes that follow it (end code + data), or -1
// when it is not a response of this frame type, or the length is impossible (above max_body).
int slmp_response_body_len(const uint8_t* header, bool frame4e, int max_body);
// Check a complete response (header + body).
bool slmp_parse_response(const uint8_t* buf, int len, bool frame4e, SlmpResponse& out, std::string* err = nullptr);

// Bit units: two points per byte, the first in the upper 4 bits.
std::vector<uint8_t> slmp_pack_bits(const std::vector<bool>& bits);
std::vector<bool>    slmp_unpack_bits(const uint8_t* data, size_t bytes, size_t count);

std::string slmp_end_code_text(uint16_t code);   // "C051: number of points out of range"

} // namespace hml
