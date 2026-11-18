// What the Modbus client drivers (driver_modbus.cpp) send their frames through, and the frame
// formats themselves.
//
//   stream    a byte pipe with timeouts: a TCP connection or a serial port
//   framing   Modbus TCP (MBAP header), Modbus RTU (binary + CRC-16), Modbus ASCII (':' hex LRC CR LF)
//
// RTU and ASCII normally run over a serial line, but both also work through a TCP connection
// (serial-to-Ethernet gateways), which is how the tests reach them.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hml {

class ModbusStream {
public:
    virtual ~ModbusStream() = default;
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    // Bytes read (>= 1), 0 when nothing arrived within timeout_ms, -1 on error / closed.
    virtual int  read(uint8_t* buf, int max, uint32_t timeout_ms) = 0;
    virtual bool write(const uint8_t* buf, int len) = 0;
    virtual void discard_input() = 0;                  // drop what is waiting (a late answer)
    virtual std::string describe() const = 0;          // "192.168.0.10:502", "/dev/ttyUSB0 9600 8N1"
    virtual std::string last_error() const { return {}; }
};

struct SerialSettings {
    std::string device;                // "/dev/ttyUSB0", "COM4"
    int  baud = 9600;
    int  data_bits = 8;                // 7 | 8
    char parity = 'N';                 // 'N' | 'E' | 'O'
    int  stop_bits = 1;                // 1 | 2
    std::string describe() const;
    // time on the wire of `chars` characters, in microseconds (start + data + parity + stop bits)
    uint32_t char_time_us(double chars) const;
};

std::unique_ptr<ModbusStream> make_tcp_stream(const std::string& host, uint16_t port, uint32_t connect_timeout_ms);
std::unique_ptr<ModbusStream> make_serial_stream(const SerialSettings& s);

// ---- frames. "adu" below = unit id + PDU (function code and data), without any checksum.
uint16_t modbus_crc16(const uint8_t* data, size_t len);
uint8_t  modbus_lrc(const uint8_t* data, size_t len);

std::vector<uint8_t> modbus_rtu_frame(const uint8_t* adu, size_t len);     // adu + CRC (low byte first)
std::string          modbus_ascii_frame(const uint8_t* adu, size_t len);   // ":" hex LRC "\r\n"

// Length of the whole RTU response frame (with CRC) once the first bytes tell it:
// 0 = more bytes are needed to know, -1 = not a response this client can expect.
int modbus_rtu_response_len(const uint8_t* buf, int have);
// Check the CRC of a complete frame and return the adu length (frame length - 2), -1 if wrong.
int modbus_rtu_check(const uint8_t* frame, int len);
// Decode one ASCII frame (anything before ':' and the line end are ignored) into adu bytes.
// False on bad characters, an odd number of digits or a wrong LRC.
bool modbus_ascii_decode(const std::string& line, std::vector<uint8_t>& adu);

} // namespace hml
