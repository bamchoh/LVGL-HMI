// FINS (OMRON CS / CJ / CP / NJ / NX) over FINS/TCP: the pieces the client driver
// (driver_fins.cpp) is built from. Nothing here does I/O, so every byte can be tested.
//
// FINS/TCP header (16 bytes, big-endian):  "FINS" | length (bytes after this field) | command | error code
//   command 0: the client asks for a node address   data: client node (0 = give me one)
//   command 1: the server's answer                  data: client node, server node
//   command 2: a FINS frame                         data: the frame
// FINS frame:  ICF RSV GCT | DNA DA1 DA2 | SNA SA1 SA2 | SID | MRC SRC | (response: end code, 2 bytes) | data
//   01 01 memory area read:   area code, address (2 bytes), bit, number of items (2 bytes)
//   01 02 memory area write:  the same, then the data (a word = 2 bytes, a bit = 1 byte)
//
// Addresses are written as in CX-Programmer: "D100" (DM), "CIO100" or "100", "W10", "H5", "A440",
// "E0_100" (EM bank 0; banks 0..18 hexadecimal), "T5" / "C5" (the present value), and "D100.03" for a bit.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hml {

struct FinsAddress {
    std::string area;         // "CIO" "W" "H" "A" "D" "E0".."E18" "T" "C"
    uint8_t  word_code = 0;   // memory area code for words
    uint8_t  bit_code = 0;    // ... for bits (0 = the area has none: T / C)
    uint16_t number = 0;      // as on the wire (a counter: its number + 0x8000)
    int      bit = -1;        // 0..15, -1 = the word
    bool     system = false;  // the Auxiliary Area (A): the CPU's own flags and settings
};

constexpr int kFinsMaxWords = 990;       // one memory area read / write (the limit is 999 for most CPUs)

// "D100", "cio1.15", "W0.00", "E1_200". nullopt (and *err) when it is not an address.
std::optional<FinsAddress> fins_parse_address(const std::string& text, std::string* err = nullptr);
std::string fins_format_address(const FinsAddress& a);

struct FinsNode { uint8_t network = 0, node = 0, unit = 0; };

// FINS/TCP
constexpr int kFinsTcpHeader = 16;
std::vector<uint8_t> fins_tcp_handshake(uint8_t client_node);               // command 0
std::vector<uint8_t> fins_tcp_wrap(const std::vector<uint8_t>& frame);       // command 2
// Check a header: the number of bytes that follow it, or -1 when it is not FINS/TCP or the
// length is impossible (above max_body). *command and *error are filled.
int fins_tcp_body_len(const uint8_t* header, int max_body, uint32_t* command, uint32_t* error);
// The body of the answer to the handshake: our node and the server's.
bool fins_tcp_parse_handshake(const uint8_t* body, int len, uint8_t* client_node, uint8_t* server_node);
std::string fins_tcp_error_text(uint32_t code);

// A command frame (ICF 0x80: an answer is wanted).
std::vector<uint8_t> fins_frame(const FinsNode& dst, const FinsNode& src, uint8_t sid, uint8_t mrc, uint8_t src_code,
                                const std::vector<uint8_t>& data);
// The data of a memory area read / write up to the number of items.
std::vector<uint8_t> fins_area_data(uint8_t area_code, uint16_t number, uint8_t bit, uint16_t count);

struct FinsResponse {
    uint8_t  sid = 0, mrc = 0, src = 0;
    uint16_t end_code = 0;             // main code << 8 | sub code, without the relay / CPU error flags
    std::vector<uint8_t> data;
};
bool fins_parse_response(const uint8_t* frame, int len, FinsResponse& out, std::string* err = nullptr);
std::string fins_end_code_text(uint16_t code);     // "1103: address out of range"

uint32_t fins_from_bcd(uint32_t bcd, bool* ok = nullptr);     // 0x1234 -> 1234
uint32_t fins_to_bcd(uint32_t value);                         // 1234 -> 0x1234

} // namespace hml
