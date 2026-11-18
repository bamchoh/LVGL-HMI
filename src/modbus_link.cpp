#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
using sock_t = SOCKET;
static const sock_t kInvalidSock = INVALID_SOCKET;
static void sock_close(sock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <termios.h>
#  include <unistd.h>
using sock_t = int;
static const sock_t kInvalidSock = -1;
static void sock_close(sock_t s) { close(s); }
#endif

#include "modbus_link.hpp"
#include "util.hpp"

#include <cstring>

namespace hml {

// ================================================================ frames

uint16_t modbus_crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
    }
    return crc;
}

uint8_t modbus_lrc(const uint8_t* data, size_t len)
{
    uint8_t sum = 0;
    for(size_t i = 0; i < len; i++) sum = static_cast<uint8_t>(sum + data[i]);
    return static_cast<uint8_t>(-static_cast<int8_t>(sum));
}

std::vector<uint8_t> modbus_rtu_frame(const uint8_t* adu, size_t len)
{
    std::vector<uint8_t> f(adu, adu + len);
    uint16_t crc = modbus_crc16(adu, len);
    f.push_back(static_cast<uint8_t>(crc & 0xFF));
    f.push_back(static_cast<uint8_t>(crc >> 8));
    return f;
}

std::string modbus_ascii_frame(const uint8_t* adu, size_t len)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string f = ":";
    auto put = [&](uint8_t b) { f += hex[b >> 4]; f += hex[b & 15]; };
    for(size_t i = 0; i < len; i++) put(adu[i]);
    put(modbus_lrc(adu, len));
    f += "\r\n";
    return f;
}

int modbus_rtu_response_len(const uint8_t* buf, int have)
{
    if(have < 2) return 0;
    uint8_t fc = buf[1];
    if(fc & 0x80) return 5;                                     // unit, fc, exception code, crc
    switch(fc) {
        case 0x01: case 0x02: case 0x03: case 0x04:
            return have < 3 ? 0 : 3 + buf[2] + 2;               // unit, fc, byte count, data, crc
        case 0x05: case 0x06: case 0x0F: case 0x10:
            return 8;                                           // unit, fc, address, value / quantity, crc
        default:
            return -1;
    }
}

int modbus_rtu_check(const uint8_t* frame, int len)
{
    if(len < 4) return -1;
    uint16_t crc = modbus_crc16(frame, static_cast<size_t>(len - 2));
    if(frame[len - 2] != (crc & 0xFF) || frame[len - 1] != (crc >> 8)) return -1;
    return len - 2;
}

bool modbus_ascii_decode(const std::string& line, std::vector<uint8_t>& adu)
{
    adu.clear();
    size_t start = line.rfind(':');
    if(start == std::string::npos) return false;
    size_t end = line.size();
    while(end > start + 1 && (line[end - 1] == '\r' || line[end - 1] == '\n')) end--;
    size_t digits = end - start - 1;
    if(digits < 6 || digits % 2 != 0) return false;             // unit, fc, lrc at least
    auto val = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::vector<uint8_t> bytes;
    for(size_t i = start + 1; i < end; i += 2) {
        int hi = val(line[i]), lo = val(line[i + 1]);
        if(hi < 0 || lo < 0) return false;
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    if(modbus_lrc(bytes.data(), bytes.size() - 1) != bytes.back()) return false;
    bytes.pop_back();
    adu = std::move(bytes);
    return true;
}

// ================================================================ serial settings

std::string SerialSettings::describe() const
{
    return device + " " + std::to_string(baud) + " " + std::to_string(data_bits) + std::string(1, parity) + std::to_string(stop_bits);
}

uint32_t SerialSettings::char_time_us(double chars) const
{
    const int bits = 1 + data_bits + (parity == 'N' ? 0 : 1) + stop_bits;
    return static_cast<uint32_t>(chars * bits * 1e6 / (baud > 0 ? baud : 9600));
}

// ================================================================ TCP

namespace {

class TcpStream final : public ModbusStream {
public:
    TcpStream(std::string host, uint16_t port, uint32_t timeout_ms) : host_(std::move(host)), port_(port), timeout_ms_(timeout_ms) {}
    ~TcpStream() override { close(); }

    bool open() override
    {
        close();
#ifdef _WIN32
        static bool started = false;
        if(!started) {
            WSADATA wsa;
            if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
            started = true;
        }
#endif
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if(getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res) != 0 || !res) { error_ = "cannot resolve " + host_; return false; }
        sock_t s = kInvalidSock;
        for(addrinfo* ai = res; ai; ai = ai->ai_next) {
            s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if(s == kInvalidSock) continue;
            set_timeouts(s);
            if(::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
            sock_close(s);
            s = kInvalidSock;
        }
        freeaddrinfo(res);
        if(s == kInvalidSock) { error_ = "cannot connect"; return false; }
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
        sock_ = s;
        return true;
    }
    void close() override { if(sock_ != kInvalidSock) { sock_close(sock_); sock_ = kInvalidSock; } }
    bool is_open() const override { return sock_ != kInvalidSock; }

    int read(uint8_t* buf, int max, uint32_t timeout_ms) override
    {
        if(sock_ == kInvalidSock) return -1;
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(sock_, &rd);
        timeval tv{ static_cast<long>(timeout_ms / 1000), static_cast<long>((timeout_ms % 1000) * 1000) };
        int r = select(static_cast<int>(sock_) + 1, &rd, nullptr, nullptr, &tv);
        if(r == 0) return 0;
        if(r < 0) { error_ = "select failed"; return -1; }
        int n = static_cast<int>(recv(sock_, reinterpret_cast<char*>(buf), max, 0));
        if(n <= 0) { error_ = "connection closed"; return -1; }
        return n;
    }
    bool write(const uint8_t* buf, int len) override
    {
        while(len > 0) {
            int n = static_cast<int>(send(sock_, reinterpret_cast<const char*>(buf), len, 0));
            if(n <= 0) { error_ = "send failed"; return false; }
            buf += n; len -= n;
        }
        return true;
    }
    void discard_input() override
    {
        uint8_t tmp[256];
        while(sock_ != kInvalidSock && read(tmp, sizeof(tmp), 0) > 0) {}
    }
    std::string describe() const override { return host_ + ":" + std::to_string(port_); }
    std::string last_error() const override { return error_; }

private:
    void set_timeouts(sock_t s)
    {
#ifdef _WIN32
        DWORD tv = timeout_ms_;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
        timeval tv;
        tv.tv_sec = timeout_ms_ / 1000;
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms_ % 1000) * 1000);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    }

    std::string host_;
    uint16_t    port_;
    uint32_t    timeout_ms_;
    sock_t      sock_ = kInvalidSock;
    std::string error_;
};

// ================================================================ serial port

class SerialStream final : public ModbusStream {
public:
    explicit SerialStream(SerialSettings s) : s_(std::move(s)) {}
    ~SerialStream() override { close(); }

    std::string describe() const override { return s_.describe(); }
    std::string last_error() const override { return error_; }

#ifdef _WIN32
    bool open() override
    {
        close();
        // "\\.\COM10" is needed above COM9 and works for all of them
        std::string name = s_.device.rfind("\\\\.\\", 0) == 0 ? s_.device : "\\\\.\\" + s_.device;
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if(h == INVALID_HANDLE_VALUE) { error_ = "cannot open " + s_.device + " (error " + std::to_string(GetLastError()) + ")"; return false; }
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if(!GetCommState(h, &dcb)) { error_ = "not a serial port: " + s_.device; CloseHandle(h); return false; }
        dcb.BaudRate = static_cast<DWORD>(s_.baud);
        dcb.ByteSize = static_cast<BYTE>(s_.data_bits);
        dcb.Parity = s_.parity == 'E' ? EVENPARITY : s_.parity == 'O' ? ODDPARITY : NOPARITY;
        dcb.fParity = s_.parity != 'N';
        dcb.StopBits = s_.stop_bits == 2 ? TWOSTOPBITS : ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutX = dcb.fInX = FALSE;
        dcb.fNull = FALSE;
        dcb.fAbortOnError = FALSE;
        if(!SetCommState(h, &dcb)) { error_ = "the port does not accept " + s_.describe(); CloseHandle(h); return false; }
        h_ = h;
        discard_input();
        return true;
    }
    void close() override { if(h_ != INVALID_HANDLE_VALUE) { CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; } }
    bool is_open() const override { return h_ != INVALID_HANDLE_VALUE; }
    int read(uint8_t* buf, int max, uint32_t timeout_ms) override
    {
        if(h_ == INVALID_HANDLE_VALUE) return -1;
        // return as soon as something is there, or after timeout_ms with nothing
        COMMTIMEOUTS to{};
        to.ReadIntervalTimeout = MAXDWORD;
        to.ReadTotalTimeoutMultiplier = timeout_ms ? MAXDWORD : 0;
        to.ReadTotalTimeoutConstant = timeout_ms;
        to.WriteTotalTimeoutConstant = 1000;
        SetCommTimeouts(h_, &to);
        DWORD n = 0;
        if(!ReadFile(h_, buf, static_cast<DWORD>(max), &n, nullptr)) { error_ = "read failed (error " + std::to_string(GetLastError()) + ")"; return -1; }
        return static_cast<int>(n);
    }
    bool write(const uint8_t* buf, int len) override
    {
        DWORD n = 0;
        if(h_ == INVALID_HANDLE_VALUE || !WriteFile(h_, buf, static_cast<DWORD>(len), &n, nullptr) || n != static_cast<DWORD>(len)) {
            error_ = "write failed (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
    }
    void discard_input() override { if(h_ != INVALID_HANDLE_VALUE) PurgeComm(h_, PURGE_RXCLEAR); }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
#else
    bool open() override
    {
        close();
        int fd = ::open(s_.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if(fd < 0) { error_ = "cannot open " + s_.device + ": " + std::strerror(errno); return false; }
        termios t{};
        if(tcgetattr(fd, &t) != 0) { error_ = "not a serial port: " + s_.device; ::close(fd); return false; }
        cfmakeraw(&t);
        speed_t sp = speed_of(s_.baud);
        if(sp == B0) { error_ = "unsupported baud rate " + std::to_string(s_.baud); ::close(fd); return false; }
        cfsetispeed(&t, sp);
        cfsetospeed(&t, sp);
        t.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
        t.c_cflag |= (s_.data_bits == 7 ? CS7 : CS8) | CLOCAL | CREAD;
        if(s_.parity != 'N') t.c_cflag |= PARENB;
        if(s_.parity == 'O') t.c_cflag |= PARODD;
        if(s_.stop_bits == 2) t.c_cflag |= CSTOPB;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        if(tcsetattr(fd, TCSANOW, &t) != 0) { error_ = "the port does not accept " + s_.describe(); ::close(fd); return false; }
        fd_ = fd;
        discard_input();
        return true;
    }
    void close() override { if(fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    bool is_open() const override { return fd_ >= 0; }
    int read(uint8_t* buf, int max, uint32_t timeout_ms) override
    {
        if(fd_ < 0) return -1;
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fd_, &rd);
        timeval tv{ static_cast<time_t>(timeout_ms / 1000), static_cast<suseconds_t>((timeout_ms % 1000) * 1000) };
        int r = select(fd_ + 1, &rd, nullptr, nullptr, &tv);
        if(r == 0) return 0;
        if(r < 0) { if(errno == EINTR) return 0; error_ = std::string("select: ") + std::strerror(errno); return -1; }
        ssize_t n = ::read(fd_, buf, static_cast<size_t>(max));
        if(n < 0) { if(errno == EAGAIN || errno == EINTR) return 0; error_ = std::string("read: ") + std::strerror(errno); return -1; }
        if(n == 0) { error_ = "the port is gone (unplugged?)"; return -1; }     // readable but empty: hang-up
        return static_cast<int>(n);
    }
    bool write(const uint8_t* buf, int len) override
    {
        if(fd_ < 0) return false;
        while(len > 0) {
            ssize_t n = ::write(fd_, buf, static_cast<size_t>(len));
            if(n < 0) {
                if(errno == EAGAIN || errno == EINTR) { usleep(1000); continue; }
                error_ = std::string("write: ") + std::strerror(errno);
                return false;
            }
            buf += n; len -= static_cast<int>(n);
        }
        return true;
    }
    void discard_input() override { if(fd_ >= 0) tcflush(fd_, TCIFLUSH); }

private:
    static speed_t speed_of(int baud)
    {
        switch(baud) {
            case 1200: return B1200;   case 2400: return B2400;   case 4800: return B4800;
            case 9600: return B9600;   case 19200: return B19200; case 38400: return B38400;
            case 57600: return B57600; case 115200: return B115200; case 230400: return B230400;
            default: return B0;
        }
    }
    int fd_ = -1;
#endif
    SerialSettings s_;
    std::string    error_;
};

} // namespace

std::unique_ptr<ModbusStream> make_tcp_stream(const std::string& host, uint16_t port, uint32_t connect_timeout_ms)
{
    return std::make_unique<TcpStream>(host, port, connect_timeout_ms);
}

std::unique_ptr<ModbusStream> make_serial_stream(const SerialSettings& s) { return std::make_unique<SerialStream>(s); }

} // namespace hml
