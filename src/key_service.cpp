#include "key_service.hpp"
#include "util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kInvalidSock = INVALID_SOCKET;
static void sock_close(sock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <pwd.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <unistd.h>
using sock_t = int;
static const sock_t kInvalidSock = -1;
static void sock_close(sock_t s) { close(s); }
#endif

namespace fs = std::filesystem;

namespace hml {

// ================================================================ SHA-256 / base64

namespace {

struct Sha256 {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t  buf[64] = {};
    size_t   fill = 0;
    uint64_t total = 0;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        uint32_t w[64];
        for(int i = 0; i < 16; i++) w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        for(int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for(int i = 0; i < 64; i++) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* p, size_t n)
    {
        total += n;
        while(n > 0) {
            size_t take = std::min(n, sizeof(buf) - fill);
            std::memcpy(buf + fill, p, take);
            fill += take; p += take; n -= take;
            if(fill == sizeof(buf)) { block(buf); fill = 0; }
        }
    }

    std::string finish()
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while(fill != 56) update(&zero, 1);
        uint8_t len[8];
        for(int i = 0; i < 8; i++) len[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        update(len, 8);
        std::string out(32, '\0');
        for(int i = 0; i < 8; i++) for(int j = 0; j < 4; j++) out[static_cast<size_t>(i * 4 + j)] = static_cast<char>(h[i] >> (24 - j * 8));
        return out;
    }
};

std::string sha256_raw(const std::string& data)
{
    Sha256 s;
    s.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return s.finish();
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string& in, bool pad)
{
    std::string out;
    size_t i = 0;
    for(; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if(i < in.size()) {
        uint32_t v = uint8_t(in[i]) << 16;
        if(i + 1 < in.size()) v |= uint8_t(in[i + 1]) << 8;
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63];
        if(i + 1 < in.size()) out += kB64[(v >> 6) & 63]; else if(pad) out += '=';
        if(pad) out += '=';
    }
    return out;
}

// Strict: the standard alphabet, padding only at the end, length a multiple of 4.
bool base64_decode(const std::string& in, std::string& out)
{
    out.clear();
    if(in.empty() || in.size() % 4 != 0) return false;
    size_t pad = 0;
    uint32_t v = 0;
    for(size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        int d;
        if(c == '=') { if(i < in.size() - 2) return false; pad++; d = 0; }
        else {
            if(pad) return false;
            const char* p = std::strchr(kB64, c);
            if(!p || c == '\0') return false;
            d = static_cast<int>(p - kB64);
        }
        v = (v << 6) | static_cast<uint32_t>(d);
        if(i % 4 == 3) {
            out += static_cast<char>((v >> 16) & 0xFF);
            if(pad < 2) out += static_cast<char>((v >> 8) & 0xFF);
            if(pad < 1) out += static_cast<char>(v & 0xFF);
            v = 0;
        }
    }
    return true;
}

const char* const kKeyTypes[] = {
    "ssh-ed25519", "ssh-rsa", "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384", "ecdsa-sha2-nistp521",
    "sk-ssh-ed25519@openssh.com", "sk-ecdsa-sha2-nistp256@openssh.com",
};

bool known_type(const std::string& t)
{
    for(const char* k : kKeyTypes) if(t == k) return true;
    return false;
}

bool fail(std::string* error, const std::string& msg)
{
    if(error) *error = msg;
    return false;
}

std::string default_file()
{
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
    if(!home || !*home) {
        if(struct passwd* pw = getpwuid(getuid())) home = pw->pw_dir;
    }
#endif
    return std::string(home && *home ? home : ".") + "/.ssh/authorized_keys";
}

std::vector<std::string> read_lines(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::string line;
    while(std::getline(f, line)) {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Write the whole file through a temporary one, private to the user (sshd ignores a key file
// that others may write).
bool write_lines(const std::string& path, const std::vector<std::string>& lines, std::string* error)
{
    std::error_code ec;
    fs::path p = fs::u8path(path), dir = p.parent_path();
    if(!dir.empty() && !fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
        if(ec) return fail(error, "cannot create " + dir.u8string());
#ifndef _WIN32
        chmod(dir.c_str(), 0700);
#endif
    }
    fs::path tmp = p;
    tmp += ".hml-tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if(!f) return fail(error, "cannot write " + tmp.u8string());
        for(const std::string& l : lines) f << l << "\n";
        f.flush();
        if(!f) return fail(error, "cannot write " + tmp.u8string());
    }
#ifndef _WIN32
    chmod(tmp.c_str(), 0600);
#endif
    fs::rename(tmp, p, ec);
    if(ec) { fs::remove(tmp, ec); return fail(error, "cannot replace " + path); }
    return true;
}

bool sock_init()
{
#ifdef _WIN32
    static bool done = false;
    if(!done) {
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        done = true;
    }
#endif
    return true;
}

// 1 = readable, 0 = timeout, -1 = error
int wait_readable(sock_t s, int timeout_ms)
{
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    return select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv);
}

void send_line(sock_t s, const std::string& text)
{
    std::string out = text + "\n";
    send(s, out.data(), static_cast<int>(out.size()), 0);
}

} // namespace

// ================================================================ keys

std::string sha256_hex(const std::string& data)
{
    std::string raw = sha256_raw(data), out;
    for(unsigned char c : raw) out += vformat("%02x", c);
    return out;
}

std::string key_fingerprint(const std::string& blob) { return "SHA256:" + base64_encode(sha256_raw(blob), false); }

std::string PublicKey::short_type() const
{
    std::string t = type;
    if(t.rfind("sk-", 0) == 0) t = t.substr(3);
    if(t.rfind("ssh-", 0) == 0) t = t.substr(4);
    if(t.rfind("ecdsa", 0) == 0) return "ecdsa";
    return t.substr(0, t.find('@'));
}

bool parse_public_key(const std::string& line_in, PublicKey& out, std::string* error, bool allow_options)
{
    std::string line = line_in;
    while(!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    size_t start = line.find_first_not_of(" \t");
    if(start == std::string::npos) return fail(error, "empty");
    line = line.substr(start);
    if(line.size() > 8192) return fail(error, "the key is too long");
    if(line.find_first_of("\r\n") != std::string::npos) return fail(error, "a key is a single line");

    // where the key type starts (after the options field of an authorized_keys line, if any)
    size_t at = std::string::npos;
    for(const char* t : kKeyTypes) {
        std::string needle = std::string(t) + " ";
        if(line.rfind(needle, 0) == 0) { at = 0; break; }
        if(!allow_options) continue;
        size_t p = line.find(" " + needle);
        if(p != std::string::npos && (at == std::string::npos || p + 1 < at)) at = p + 1;
    }
    if(at == std::string::npos) return fail(error, "not a public key of a supported type (ed25519, rsa, ecdsa)");

    std::istringstream ss(line.substr(at));
    PublicKey k;
    ss >> k.type >> k.base64;
    std::string rest;
    std::getline(ss, rest);
    if(!known_type(k.type)) return fail(error, "unsupported key type");
    std::string blob;
    if(!base64_decode(k.base64, blob)) return fail(error, "the key data is not valid base64");
    if(blob.size() < 4 + k.type.size() || blob.size() > 4096) return fail(error, "the key data has an unexpected size");
    uint32_t n = (uint8_t(blob[0]) << 24) | (uint8_t(blob[1]) << 16) | (uint8_t(blob[2]) << 8) | uint8_t(blob[3]);
    if(n != k.type.size() || blob.compare(4, n, k.type) != 0) return fail(error, "the key data does not match its type");
    if(k.type == "ssh-ed25519" && blob.size() != 4 + 11 + 4 + 32) return fail(error, "the key data has an unexpected size");
    for(char c : rest) {
        if(k.comment.size() >= 100) break;
        if(c >= 0x20 && c < 0x7f) k.comment += c;
    }
    size_t cs = k.comment.find_first_not_of(' ');
    k.comment = cs == std::string::npos ? "" : k.comment.substr(cs);
    k.fingerprint = key_fingerprint(blob);
    out = std::move(k);
    return true;
}

// ================================================================ KeyService: the file

KeyService::KeyService() = default;
KeyService::~KeyService() { stop(); }

void KeyService::set_file(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = path;
}

std::string KeyService::file() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return file_.empty() ? default_file() : file_;
}

std::vector<PublicKey> KeyService::list() const
{
    std::vector<PublicKey> out;
    for(const std::string& line : read_lines(file())) {
        size_t s = line.find_first_not_of(" \t");
        if(s == std::string::npos || line[s] == '#') continue;
        PublicKey k;
        if(parse_public_key(line, k, nullptr, true)) out.push_back(std::move(k));
    }
    return out;
}

bool KeyService::add(const PublicKey& key, std::string* error)
{
    PublicKey checked;
    if(!parse_public_key(key.line(), checked, error)) return false;        // never write what was not validated
    std::string path = file();
    std::vector<std::string> lines = read_lines(path);
    for(const std::string& line : lines) {
        PublicKey k;
        if(parse_public_key(line, k, nullptr, true) && k.fingerprint == checked.fingerprint) return true;
    }
    lines.push_back(checked.line());
    if(!write_lines(path, lines, error)) return false;
    HML_INFO("keys: registered %s %s%s%s", checked.short_type().c_str(), checked.fingerprint.c_str(),
             checked.comment.empty() ? "" : " ", checked.comment.c_str());
    return true;
}

bool KeyService::remove(const std::string& fingerprint, std::string* error)
{
    std::string path = file();
    std::vector<std::string> lines = read_lines(path), kept;
    bool found = false;
    for(const std::string& line : lines) {
        PublicKey k;
        size_t s = line.find_first_not_of(" \t");
        bool is_key = s != std::string::npos && line[s] != '#' && parse_public_key(line, k, nullptr, true);
        if(is_key && k.fingerprint == fingerprint) { found = true; continue; }
        kept.push_back(line);                                               // comments, options, unknown lines stay as they are
    }
    if(!found) return fail(error, "no such key");
    if(!write_lines(path, kept, error)) return false;
    HML_INFO("keys: removed %s", fingerprint.c_str());
    return true;
}

// ================================================================ KeyService: offers

bool KeyService::start(int port, std::string* error)
{
    if(listening_) return true;
    if(thread_.joinable()) thread_.join();
    if(port < 1 || port > 65535) return fail(error, "invalid port");
    if(!sock_init()) return fail(error, "sockets are not available");
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == kInvalidSock) return fail(error, "cannot create a socket");
#ifndef _WIN32
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if(bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 4) != 0) {
        sock_close(s);
        return fail(error, "port " + std::to_string(port) + " is in use");
    }
    listen_sock_ = static_cast<std::intptr_t>(s);
    port_ = port;
    stop_ = false;
    listening_ = true;
    thread_ = std::thread([this] { run(); });
    HML_INFO("keys: key management mode on, accepting key offers on port %d", port);
    return true;
}

void KeyService::stop()
{
    bool was = listening_;
    stop_ = true;
    cv_.notify_all();
    if(thread_.joinable()) thread_.join();
    if(listen_sock_ != -1) { sock_close(static_cast<sock_t>(listen_sock_)); listen_sock_ = -1; }
    listening_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        has_offer_ = false;
        decision_ = Decision::None;
    }
    if(was) HML_INFO("keys: key management mode off");
}

bool KeyService::pending(Offer& out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!has_offer_ || decision_ != Decision::None) return false;
    out = offer_;
    return true;
}

bool KeyService::accept(std::string* error)
{
    PublicKey key;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!has_offer_ || decision_ != Decision::None) return fail(error, "no key is waiting");
        key = offer_.key;
    }
    std::string err;
    bool ok = add(key, &err);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accept_error_ = ok ? "" : err;
        decision_ = Decision::Accept;
    }
    cv_.notify_all();
    if(!ok) return fail(error, err);
    return true;
}

void KeyService::reject()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!has_offer_ || decision_ != Decision::None) return;
        decision_ = Decision::Reject;
        HML_INFO("keys: rejected %s from %s", offer_.key.fingerprint.c_str(), offer_.from.c_str());
    }
    cv_.notify_all();
}

void KeyService::run()
{
    sock_t ls = static_cast<sock_t>(listen_sock_);
    while(!stop_) {
        if(wait_readable(ls, 200) <= 0) continue;
        sockaddr_in peer{};
#ifdef _WIN32
        int len = sizeof(peer);
#else
        socklen_t len = sizeof(peer);
#endif
        sock_t c = ::accept(ls, reinterpret_cast<sockaddr*>(&peer), &len);
        if(c == kInvalidSock) continue;
        char ip[64] = "?";
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        serve(static_cast<std::intptr_t>(c), ip);
        sock_close(c);
    }
}

// One offer at a time: further senders wait in the listen queue until this one is decided.
void KeyService::serve(std::intptr_t client, const std::string& from)
{
    sock_t c = static_cast<sock_t>(client);
    // ---- two lines within 5 s, 8 KB at most
    std::string in;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto lines_in = [&] { size_t n = 0; for(char ch : in) if(ch == '\n') n++; return n; };
    while(lines_in() < 2 && in.size() < 8192 && !stop_) {
        if(std::chrono::steady_clock::now() > deadline) return;
        if(wait_readable(c, 200) <= 0) continue;
        char buf[1024];
        int n = static_cast<int>(recv(c, buf, sizeof(buf), 0));
        if(n <= 0) return;
        in.append(buf, static_cast<size_t>(n));
    }
    if(lines_in() < 2) { send_line(c, "ERROR request too long"); return; }
    size_t nl = in.find('\n');
    std::string hello = in.substr(0, nl), keyline = in.substr(nl + 1, in.find('\n', nl + 1) - nl - 1);
    if(!hello.empty() && hello.back() == '\r') hello.pop_back();
    if(hello != "HML-KEY 1") { send_line(c, "ERROR unknown request"); return; }
    PublicKey key;
    std::string err;
    if(!parse_public_key(keyline, key, &err)) {
        HML_WARN("keys: offer from %s refused: %s", from.c_str(), err.c_str());
        send_line(c, "ERROR " + err);
        return;
    }
    for(const PublicKey& k : list()) {
        if(k.fingerprint == key.fingerprint) { send_line(c, "ALREADY " + key.fingerprint); return; }
    }

    // ---- wait for the operator
    {
        std::lock_guard<std::mutex> lock(mutex_);
        offer_ = Offer{ key, from };
        decision_ = Decision::None;
        accept_error_.clear();
        has_offer_ = true;
    }
    HML_INFO("keys: offer from %s: %s %s %s", from.c_str(), key.short_type().c_str(), key.fingerprint.c_str(), key.comment.c_str());
    send_line(c, "PENDING " + key.fingerprint);
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(offer_timeout_s_.load());
    std::string answer = "REJECTED";
    for(;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(150), [&] { return decision_ != Decision::None || stop_.load(); });
            if(decision_ == Decision::Accept) { answer = accept_error_.empty() ? "ACCEPTED" : "ERROR " + accept_error_; break; }
            if(decision_ == Decision::Reject || stop_) break;
        }
        if(std::chrono::steady_clock::now() > until) { answer = "TIMEOUT"; HML_INFO("keys: offer from %s timed out", from.c_str()); break; }
        if(wait_readable(c, 0) > 0) {                       // the sender gave up (or sent something unexpected)
            char buf[64];
            if(recv(c, buf, sizeof(buf), 0) <= 0) { answer.clear(); HML_INFO("keys: the sender %s closed the connection", from.c_str()); break; }
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        has_offer_ = false;
        decision_ = Decision::None;
    }
    if(!answer.empty()) send_line(c, answer);
}

KeyService& key_service()
{
    static KeyService inst;
    return inst;
}

} // namespace hml
