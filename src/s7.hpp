// S7comm (Siemens S7-300 / 400 / 1200 / 1500, "PUT/GET" communication) over ISO-on-TCP: the
// pieces the client driver (driver_s7.cpp) is built from. Nothing here does I/O, so every byte
// can be tested.
//
// On the wire (all numbers big-endian), TCP port 102:
//   TPKT (RFC 1006)    03 00 | length of the whole packet (2 bytes)
//   COTP (ISO 8073)    connection request  11 E0 | dst ref | src ref | class | C0 01 0A | C1 02 src TSAP | C2 02 dst TSAP
//                      connection confirm  .. D0 ..
//                      data                02 F0 80   (80: the last part of a message)
//   S7 header          32 | ROSCTR (1 job, 3 answer with data) | 00 00 | PDU reference (2) | parameter length (2) |
//                      data length (2) | (answers: error class, error code)
//   F0 setup communication:  F0 00 | max jobs calling (2) | max jobs called (2) | PDU length (2)
//   04 read var:   04 | number of items | items                      answer data: per item  return code | transport size | length | data (| pad to even)
//   05 write var:  05 | number of items | items | data per item      answer data: per item  return code
//   an item (12 bytes):  12 0A 10 | transport size (1 bit, 2 byte) | count (2) | DB number (2) | area | address (3) = byte * 8 + bit
//
// Addresses are written as in STEP 7 / TIA Portal: "DB1.DBX0.0" (bit), "DB1.DBB2" (byte), "DB1.DBW4"
// (word), "DB1.DBD6" (double word); "M0.0" "MB1" "MW2" "MD4"; inputs "I0.0" "IB0" "IW0" "ID0"
// (German: E), outputs "Q0.0" "QB0" ... (German: A). A leading "%" is allowed.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hml {

constexpr uint8_t kS7AreaInput = 0x81, kS7AreaOutput = 0x82, kS7AreaMemory = 0x83, kS7AreaDb = 0x84;

struct S7Address {
    uint8_t  area = 0;        // kS7Area...
    uint16_t db = 0;          // the DB number (0 for I / Q / M)
    uint32_t byte = 0;        // the first byte
    int      bit = -1;        // 0..7, -1 = not a bit
    int      size = 1;        // bytes: 1 (a bit or a byte), 2 (word), 4 (double word)
};

constexpr uint32_t kS7MaxByte = 65535;       // the last byte of an area or a DB that can be named
constexpr int kS7PduAsk = 480;               // the PDU length we ask for ...
constexpr int kS7PduMin = 240;               // ... and the least a CPU gives
constexpr int kS7MaxItems = 20;              // items of one read (what S7-300 / 400 and Snap7 take)
constexpr int kS7MaxString = 254;            // characters of a STRING

// "DB1.DBW4", "m0.0", "%IW2", "AB0". nullopt (and *err) when it is not an address.
std::optional<S7Address> s7_parse_address(const std::string& text, std::string* err = nullptr);
std::string s7_format_address(const S7Address& a);          // with the international letters (I, Q)

// The data that fits into one read item / one write with this PDU length (the answer to a read
// carries 14 bytes of headers and 4 per item; a write 24 and 4).
constexpr int s7_max_read_bytes(int pdu) { return pdu - 18; }
constexpr int s7_max_write_bytes(int pdu) { return pdu - 28; }

// ---- the connection
// TPKT + COTP connection request for the CPU in this rack and slot (S7-1200 / 1500: 0 / 1, S7-300: 0 / 2).
std::vector<uint8_t> s7_connect_request(int rack, int slot);
// Check the 4 bytes of a TPKT header: the number of bytes that follow, or -1 when it is not TPKT
// or the length is impossible (above max_body).
int  s7_tpkt_body_len(const uint8_t* header, int max_body);
// The body of the answer to the connection request.
bool s7_parse_connect_confirm(const uint8_t* body, int len, std::string* err = nullptr);
// The body of a data packet: where the S7 bytes start in it, or -1 when it is not COTP data.
// *last: this is the last part of the message.
int  s7_cotp_data(const uint8_t* body, int len, bool* last);

// ---- requests (whole packets: TPKT, COTP, S7)
std::vector<uint8_t> s7_setup_request(uint16_t ref, uint16_t pdu_length);

struct S7Item {
    uint8_t  area = 0;
    uint16_t db = 0;
    uint32_t byte = 0;
    int      bit = -1;        // >= 0: one bit; -1: `count` bytes
    uint16_t count = 1;
};
std::vector<uint8_t> s7_read_request(uint16_t ref, const std::vector<S7Item>& items);
// One item: `len` bytes for a byte item, one byte (0 / 1) for a bit.
std::vector<uint8_t> s7_write_request(uint16_t ref, const S7Item& item, const uint8_t* data, size_t len);

// ---- answers (the S7 bytes of a message, without TPKT and COTP)
struct S7Response {
    uint8_t  rosctr = 0;                         // 3: an answer with data, 2: without
    uint16_t ref = 0;
    uint8_t  error_class = 0, error_code = 0;    // 0 / 0 = done
    std::vector<uint8_t> param, data;
};
bool s7_parse_response(const uint8_t* pdu, int len, S7Response& out, std::string* err = nullptr);
// The PDU length the CPU answered to "setup communication".
bool s7_parse_setup(const S7Response& r, uint16_t* pdu_length);

struct S7ItemResult {
    uint8_t code = 0;                            // 0xFF = read
    std::vector<uint8_t> data;
};
constexpr uint8_t kS7ItemOk = 0xFF;
// The items of the answer to a read of `items` items / the return codes of a write.
bool s7_parse_read_data(const S7Response& r, size_t items, std::vector<S7ItemResult>& out, std::string* err = nullptr);
bool s7_parse_write_data(const S7Response& r, size_t items, std::vector<uint8_t>& codes, std::string* err = nullptr);

// "function not available (class 0x81, code 0x04): ..." / "address out of range (code 5)". Where a
// refusal is the one a CPU gives that does not permit PUT/GET, the text says so.
std::string s7_error_text(uint8_t error_class, uint8_t error_code);
std::string s7_item_error_text(uint8_t code);

} // namespace hml
