// Reading and writing an address of a device that no tag is mapped to (System > Device):
// what the page asks a driver for, and the conversion between 16-bit words and text that the
// word-oriented drivers (Modbus, SLMP, FINS) share. Nothing here does I/O.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hml {

struct DeviceRequest {
    std::string address;      // as the driver writes addresses: "D100", "CIO10.03", "holding:100"
    std::string type;         // of a word address: int16 uint16 int32 uint32 float hex ("" = the driver's usual one)
    int         count = 1;    // read: this many values, one after the other
    bool        write = false;
    std::string value;        // write: one value, as text
};

struct DeviceResult {
    bool        ok = false;
    std::string error;                 // why not (what the device answered, or what is wrong with the request)
    std::vector<std::string> values;   // read: the values as text
};

constexpr int kDeviceMaxCount = 16;

// The types of a word address, in the order a list shows them.
const std::vector<std::string>& device_types();
// Words a value of this type takes (1 or 2), 0 = not a type.
int device_type_words(const std::string& type);
// w[0] is the low word of a 32-bit value.
std::string device_format(const std::string& type, const uint16_t* w);
bool device_parse(const std::string& type, const std::string& text, uint16_t* w, std::string* err = nullptr);
// "1" "0" "true" "false" "on" "off"
bool device_parse_bit(const std::string& text, bool& on);

} // namespace hml
