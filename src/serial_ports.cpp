#include "serial_ports.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#ifdef _WIN32
#  include <windows.h>
#  include <initguid.h>
#  include <devguid.h>
#  include <setupapi.h>
#else
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace hml {

#ifdef _WIN32

namespace {
std::string utf8(const wchar_t* w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if(n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
int com_number(const std::string& name) { return name.size() > 3 ? std::atoi(name.c_str() + 3) : 0; }
} // namespace

std::vector<SerialPortInfo> list_serial_ports()
{
    std::vector<SerialPortInfo> out;
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if(set == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof(dev);
    for(DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); i++) {
        HKEY key = SetupDiOpenDevRegKey(set, &dev, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if(key == INVALID_HANDLE_VALUE) continue;
        wchar_t port[64] = {};
        DWORD size = sizeof(port) - sizeof(wchar_t), type = 0;
        LONG rc = RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<BYTE*>(port), &size);
        RegCloseKey(key);
        if(rc != ERROR_SUCCESS || type != REG_SZ) continue;
        SerialPortInfo p;
        p.device = utf8(port);
        if(p.device.rfind("COM", 0) != 0) continue;                 // not LPT1 and the like
        wchar_t friendly[256] = {};
        if(SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_FRIENDLYNAME, nullptr, reinterpret_cast<BYTE*>(friendly), sizeof(friendly) - sizeof(wchar_t), nullptr))
            p.description = utf8(friendly);
        out.push_back(std::move(p));
    }
    SetupDiDestroyDeviceInfoList(set);
    std::sort(out.begin(), out.end(), [](const SerialPortInfo& a, const SerialPortInfo& b) { return com_number(a.device) < com_number(b.device); });
    return out;
}

#else

std::vector<SerialPortInfo> list_serial_ports()
{
    std::vector<SerialPortInfo> out;
    std::error_code ec;
    // USB adapters: the name that stays the same whatever the order of plugging
    std::map<std::string, std::string> by_id;                       // /dev/ttyUSB0 -> /dev/serial/by-id/...
    for(const auto& e : fs::directory_iterator("/dev/serial/by-id", ec)) {
        fs::path real = fs::canonical(e.path(), ec);
        if(!ec) by_id[real.string()] = e.path().string();
    }
    ec.clear();
    for(const auto& e : fs::directory_iterator("/sys/class/tty", ec)) {
        const std::string name = e.path().filename().string();
        if(!fs::exists(e.path() / "device", ec)) continue;          // consoles, pseudo terminals: no hardware behind them
        std::string driver = fs::read_symlink(e.path() / "device" / "driver", ec).filename().string();
        ec.clear();
        SerialPortInfo p;
        p.device = "/dev/" + name;
        if(!fs::exists(p.device, ec)) continue;
        // Built-in ports: the 8250 driver lists ports that are not there (ttyS4 ... ttyS31).
        // Such a port can be opened but has no settings to read.
        if(name.rfind("ttyS", 0) == 0) {
            if(driver == "serial8250") continue;
            int fd = ::open(p.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if(fd >= 0) {
                termios t{};
                bool real = tcgetattr(fd, &t) == 0;
                ::close(fd);
                if(!real) continue;
            }
        }
        auto it = by_id.find(p.device);
        if(it != by_id.end()) {
            p.stable = it->second;
            // "usb-FTDI_USB_HS_SERIAL_CONVERTER_FTRTKAR0-if00-port0" -> "FTDI USB HS SERIAL CONVERTER FTRTKAR0"
            std::string d = fs::path(p.stable).filename().string();
            if(d.rfind("usb-", 0) == 0) d = d.substr(4);
            size_t cut = d.find("-if");
            if(cut != std::string::npos) d = d.substr(0, cut);
            std::replace(d.begin(), d.end(), '_', ' ');
            p.description = d;
        }
        else p.description = (driver.empty() || driver == "port") ? "built-in serial port" : driver;   // (newer kernels: always \"port\")
        out.push_back(std::move(p));
    }
    // adapters first (they are what gets plugged in for a PLC), then the built-in ports
    auto rank = [](const std::string& d) { return d.find("ttyUSB") != std::string::npos ? 0 : d.find("ttyACM") != std::string::npos ? 1 : 2; };
    std::sort(out.begin(), out.end(), [&](const SerialPortInfo& a, const SerialPortInfo& b) {
        return rank(a.device) != rank(b.device) ? rank(a.device) < rank(b.device) : a.device < b.device;
    });
    return out;
}

#endif

std::string serial_name_problem(const std::string& device)
{
    std::string name = device;
    if(name.rfind("\\\\.\\", 0) == 0) name = name.substr(4);
    const bool windows_name = name.size() > 3 && iequals(name.substr(0, 3), "COM") &&
                              std::all_of(name.begin() + 3, name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    const bool linux_name = !device.empty() && device[0] == '/';
#ifdef _WIN32
    if(!linux_name) return {};
    std::string msg = "'" + device + "' is a Linux port name; on Windows a port is called COM<n>.";
#else
    if(!windows_name) return {};
    std::string msg = "'" + device + "' is a Windows port name; on this system a port is called /dev/tty<...>.";
#endif
    (void)windows_name; (void)linux_name;
    std::string here;
    for(const SerialPortInfo& p : list_serial_ports()) here += (here.empty() ? "" : ", ") + p.device;
    msg += here.empty() ? " No serial port was found here." : " Ports here: " + here + ".";
    msg += " Set the port for this machine in its local settings (System > Driver page, or the designer's \"device on this PC\").";
    return msg;
}

} // namespace hml
