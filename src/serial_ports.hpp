// The serial ports of the machine the runtime runs on, for people who have to type a port name
// into a driver ("device" of modbus_rtu / modbus_ascii).
//
// The names differ per operating system and per machine:
//   Linux    /dev/ttyUSB0, /dev/ttyACM0, /dev/ttyAMA0 ...; a USB adapter also has a name that
//            does not depend on the order of plugging: /dev/serial/by-id/usb-FTDI_...
//   Windows  COM4 (the number is given by that PC)
//
// hml_runtime --list-serial-ports prints them; the system settings driver pages offer them.
#pragma once
#include <string>
#include <vector>

namespace hml {

struct SerialPortInfo {
    std::string device;        // what to write into "device": /dev/ttyUSB0, COM4
    std::string stable;        // Linux, USB adapters: the /dev/serial/by-id/... name ("" if none)
    std::string description;   // "FTDI USB HS SERIAL CONVERTER", "USB Serial Port (COM4)" ("" if unknown)
};

std::vector<SerialPortInfo> list_serial_ports();

// "" if the name can be a port of this operating system; otherwise a sentence that says what
// is wrong (a Linux name on Windows or the other way round) and which ports there are here.
std::string serial_name_problem(const std::string& device);

} // namespace hml
