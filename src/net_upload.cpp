#include "net_upload.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "mqtt.hpp"
#include "smtp.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr size_t kReplyMax = 16 * 1024;
constexpr size_t kLineMax = 4000;
constexpr size_t kChunk = 16 * 1024;            // a file goes out in pieces of this size: never the whole of it in memory

std::string plain_line(const std::string& s, size_t max = 160)
{
    std::string out;
    for(char c : s) out += (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) ? ' ' : c;
    while(!out.empty() && out.back() == ' ') out.pop_back();
    if(out.size() > max) out = out.substr(0, max) + "...";
    return out;
}

bool has_ctl(const std::string& s) { return std::any_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; }); }

// lines of a server, read through a buffer
class Lines {
public:
    Lines(NetStream& s, uint32_t timeout_ms) : s_(s), timeout_(timeout_ms) {}
    std::string error;
    bool line(std::string& out)
    {
        for(;;) {
            const size_t nl = buf_.find('\n');
            if(nl != std::string::npos) {
                out = buf_.substr(0, nl);
                buf_.erase(0, nl + 1);
                if(!out.empty() && out.back() == '\r') out.pop_back();
                return true;
            }
            if(buf_.size() > kLineMax) { error = "the server sent a line without end"; return false; }
            char tmp[2048];
            const int n = s_.read(tmp, sizeof(tmp), timeout_);
            if(n == 0) { error = "no answer from the server within " + std::to_string(timeout_ / 1000) + " s"; return false; }
            if(n < 0) { error = "the server closed the connection"; return false; }
            buf_.append(tmp, static_cast<size_t>(n));
        }
    }

private:
    NetStream& s_;
    uint32_t timeout_;
    std::string buf_;
};

// the file, piece by piece, into a stream
bool send_file(NetStream& s, const std::string& path, uint64_t size, std::string& err)
{
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if(!f) { err = "the file to send cannot be read"; return false; }
    std::vector<char> buf(kChunk);
    uint64_t left = size;
    while(left > 0) {
        const size_t want = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
        f.read(buf.data(), static_cast<std::streamsize>(want));
        if(static_cast<size_t>(f.gcount()) != want) { err = "the file to send changed while it was sent"; return false; }
        if(!s.write(buf.data(), want)) { err = "the connection was lost while the file was sent"; return false; }
        left -= want;
    }
    return true;
}

bool file_size(const std::string& path, uint64_t& size)
{
    std::error_code ec;
    const auto n = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    if(ec) return false;
    size = static_cast<uint64_t>(n);
    return true;
}

} // namespace

std::string NetUrl::shown() const
{
    const uint16_t dflt = scheme == "https" ? 443 : scheme == "http" ? 80 : 21;
    return scheme + "://" + host + (port != dflt ? ":" + std::to_string(port) : "") + path;
}

bool net_url_parse(const std::string& text, NetUrl& out, std::string* err)
{
    auto fail = [&](const char* why) { if(err) *err = why; return false; };
    if(text.empty() || text.size() > 500) return fail("not a URL");
    for(char c : text) if(static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7f) return fail("a URL has no spaces, line ends or characters outside ASCII");
    const size_t sep = text.find("://");
    if(sep == std::string::npos) return fail("a URL begins with https://, http:// or ftp://");
    NetUrl u;
    u.scheme = text.substr(0, sep);
    for(char& c : u.scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(u.scheme != "http" && u.scheme != "https" && u.scheme != "ftp") return fail("a URL begins with https://, http:// or ftp://");
    std::string rest = text.substr(sep + 3);
    const size_t slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if(authority.find('@') != std::string::npos) return fail("no user name or password in the URL: they have their own settings");
    if(u.path.find_first_of("?#") != std::string::npos) return fail("a URL to send to has no \"?\" and no \"#\"");
    if(u.path.find("/../") != std::string::npos || (u.path.size() >= 3 && u.path.compare(u.path.size() - 3, 3, "/..") == 0)) return fail("no \"..\" in the path");
    std::string port;
    if(!authority.empty() && authority[0] == '[') {                     // [::1]:8080
        const size_t close = authority.find(']');
        if(close == std::string::npos) return fail("not a URL");
        u.host = authority.substr(1, close - 1);
        if(close + 1 < authority.size()) { if(authority[close + 1] != ':') return fail("not a URL"); port = authority.substr(close + 2); }
        if(u.host.find_first_not_of("0123456789abcdefABCDEF:.") != std::string::npos) return fail("not a URL");
    }
    else {
        const size_t colon = authority.find(':');
        u.host = authority.substr(0, colon);
        if(colon != std::string::npos) port = authority.substr(colon + 1);
        if(u.host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos) return fail("the name of the server has characters a name has not");
    }
    if(u.host.empty() || u.host.size() > 253) return fail("a URL names a server");
    if(port.empty()) u.port = u.scheme == "https" ? 443 : u.scheme == "http" ? 80 : 21;
    else {
        if(port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) return fail("the port is a number");
        const int p = std::atoi(port.c_str());
        if(p < 1 || p > 65535) return fail("the port is 1 to 65535");
        u.port = static_cast<uint16_t>(p);
    }
    out = std::move(u);
    return true;
}

std::string net_file_name(const std::string& name)
{
    std::string out;
    for(char c : name) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        out += !ok || (c == '.' && !out.empty() && out.back() == '.') ? '_' : c;         // (never two dots in a row)
    }
    while(!out.empty() && out[0] == '.') out.erase(0, 1);               // (no hidden files, no "..")
    if(out.size() > 120) out = out.substr(out.size() - 120);
    return out.empty() ? "file" : out;
}

// ================================================================ HTTP

NetResult http_upload(NetStream& s, const NetUpload& u)
{
    NetResult res;
    auto fail = [&](const std::string& why, bool permanent) { res.ok = false; res.error = why; res.permanent = permanent; return res; };
    const std::string name = net_file_name(u.name);
    uint64_t size = 0;
    if(!file_size(u.file, size)) return fail("the file to send is not there", true);
    if(has_ctl(u.username) || has_ctl(u.password) || u.username.find(':') != std::string::npos) return fail("the user name or the password has a character that cannot be sent", true);
    std::string path = u.url.path;
    if(!u.post) path += (path.empty() || path.back() != '/' ? "/" : "") + name;
    const bool dflt_port = u.url.port == (u.url.secure() ? 443 : 80);
    const std::string host = (u.url.host.find(':') != std::string::npos ? "[" + u.url.host + "]" : u.url.host) + (dflt_port ? "" : ":" + std::to_string(u.url.port));
    std::string head = std::string(u.post ? "POST " : "PUT ") + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: hml\r\nConnection: close\r\n"
                       "Content-Type: text/csv; charset=utf-8\r\nContent-Length: " + std::to_string(size) + "\r\n";
    if(u.post) head += "Content-Disposition: attachment; filename=\"" + name + "\"\r\n";
    if(!u.username.empty()) head += "Authorization: Basic " + smtp_base64(u.username + ":" + u.password) + "\r\n";
    head += "\r\n";
    if(!s.write(head)) return fail("the connection was lost", false);
    std::string err;
    if(!send_file(s, u.file, size, err)) {
        // (a server that refuses - wrong password, too large - may answer and close before it has
        // read the body: what it said is worth more than "the connection was lost")
        Lines early(s, 2000);
        std::string line;
        if(early.line(line) && line.rfind("HTTP/1.", 0) == 0 && line.size() >= 12) {
            const int code = std::atoi(line.substr(9, 3).c_str());
            if(code >= 400) return fail("the server refused the file: " + plain_line(line.substr(9)), code != 408 && code != 429 && code < 500);
        }
        return fail(err, false);
    }
    Lines in(s, u.timeout_ms);
    std::string status;
    if(!in.line(status)) return fail(in.error, false);
    if(status.rfind("HTTP/1.", 0) != 0 || status.size() < 12 || status[8] != ' ' || !std::isdigit(static_cast<unsigned char>(status[9]))) return fail("the server does not speak HTTP (\"" + plain_line(status, 60) + "\")", false);
    const int code = std::atoi(status.substr(9, 3).c_str());
    if(code >= 200 && code < 300) { res.ok = true; return res; }
    const std::string said = plain_line(status.substr(9));
    if(code >= 300 && code < 400) return fail("the server sends us elsewhere (" + said + "): give the URL the file is really taken at", true);
    if(code == 401 || code == 403) return fail("the server does not let us in (" + said + "): the user name or the password?", true);
    if(code == 408 || code == 429 || code >= 500) return fail("the server cannot take the file now: " + said, false);
    return fail("the server refused the file: " + said, true);
}

// ================================================================ FTP

namespace {

struct FtpReply { int code = 0; std::string text; };

// a reply of the control connection: "226 done", or "226-first" ... "226 last"
bool ftp_reply(Lines& in, FtpReply& r)
{
    r = FtpReply();
    size_t total = 0;
    std::string line;
    for(bool first = true;; first = false) {
        if(!in.line(line)) return false;
        total += line.size();
        if(total > kReplyMax) { in.error = "the server's reply is too long"; return false; }
        const bool coded = line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[0])) && std::isdigit(static_cast<unsigned char>(line[1])) && std::isdigit(static_cast<unsigned char>(line[2]));
        if(first) {
            if(!coded || (line.size() > 3 && line[3] != ' ' && line[3] != '-')) { in.error = "the server does not speak FTP (\"" + plain_line(line, 60) + "\")"; return false; }
            r.code = std::atoi(line.substr(0, 3).c_str());
            r.text = line;
            if(line.size() == 3 || line[3] == ' ') return true;
        }
        else if(coded && std::atoi(line.substr(0, 3).c_str()) == r.code && (line.size() == 3 || line[3] == ' ')) { r.text = line; return true; }
    }
}

} // namespace

NetResult ftp_upload(const NetConnect& connect, const NetUpload& u)
{
    NetResult res;
    auto fail = [&](const std::string& why, bool permanent) { res.ok = false; res.error = why; res.permanent = permanent; return res; };
    const std::string name = net_file_name(u.name);
    uint64_t size = 0;
    if(!file_size(u.file, size)) return fail("the file to send is not there", true);
    if(has_ctl(u.username) || has_ctl(u.password) || has_ctl(u.url.path)) return fail("the user name, the password or the directory has a character that cannot be sent", true);
    std::string err;
    std::unique_ptr<NetStream> ctl = connect(u.url.host, u.url.port, false, &err);
    if(!ctl) return fail(err.empty() ? "cannot connect" : err, false);
    Lines in(*ctl, u.timeout_ms);
    FtpReply r;
    // say something, expect a reply whose first digit is one of `classes` ("23": 2xx or 3xx)
    auto ask = [&](const std::string& cmd, const char* classes, const char* what) {
        if(!ctl->write(cmd + "\r\n")) { res.error = "the connection was lost"; return false; }
        if(!ftp_reply(in, r)) { res.error = in.error; return false; }
        if(std::strchr(classes, static_cast<char>('0' + r.code / 100))) return true;
        res.error = std::string(what) + ": " + plain_line(r.text);
        res.permanent = r.code / 100 == 5;
        return false;
    };
    if(!ftp_reply(in, r)) return fail(in.error, false);
    if(r.code != 220) return fail("the server does not take us now: " + plain_line(r.text), r.code / 100 == 5);
    if(!ask("USER " + (u.username.empty() ? "anonymous" : u.username), "23", "the user name was not accepted")) return res;
    if(r.code / 100 == 3 && !ask("PASS " + (u.username.empty() ? "hml@" : u.password), "2", "the user name or the password was not accepted")) return res;
    if(!ask("TYPE I", "2", "TYPE I")) return res;
    std::string dir = u.url.path;
    while(dir.size() > 1 && dir.back() == '/') dir.pop_back();
    if(dir != "/" && !dir.empty() && !ask("CWD " + dir, "2", "the directory is not there")) return res;
    // passive mode: the server names a port, we connect to it - at the server we already talk to,
    // whatever address its reply names (a reply must not be able to send our file elsewhere)
    uint16_t data_port = 0;
    if(ask("EPSV", "2", "EPSV") && r.code == 229) {
        const size_t a = r.text.find("(|||");
        const size_t b = a == std::string::npos ? a : r.text.find("|)", a + 4);
        if(b != std::string::npos) { const int p = std::atoi(r.text.substr(a + 4, b - a - 4).c_str()); if(p > 0 && p < 65536) data_port = static_cast<uint16_t>(p); }
    }
    if(data_port == 0) {
        res = NetResult();
        if(!ask("PASV", "2", "the server does not offer passive mode")) return res;
        const size_t a = r.text.find('(');
        int n[6] = { 0, 0, 0, 0, 0, 0 };
        if(a == std::string::npos || std::sscanf(r.text.c_str() + a, "(%d,%d,%d,%d,%d,%d)", &n[0], &n[1], &n[2], &n[3], &n[4], &n[5]) != 6 || n[4] < 0 || n[4] > 255 || n[5] < 0 || n[5] > 255 || n[4] * 256 + n[5] == 0)
            return fail("the server's answer to PASV is not understood: " + plain_line(r.text), false);
        data_port = static_cast<uint16_t>(n[4] * 256 + n[5]);
    }
    std::unique_ptr<NetStream> data = connect(u.url.host, data_port, false, &err);
    if(!data) return fail("the data connection: " + (err.empty() ? std::string("cannot connect") : err), false);
    if(!ask("STOR " + name, "1", "the server does not take the file")) return res;
    if(!send_file(*data, u.file, size, err)) return fail(err, false);
    data.reset();                                                       // closing it says: that was all
    if(!ftp_reply(in, r)) return fail(in.error, false);
    if(r.code / 100 != 2) return fail("the server did not keep the file: " + plain_line(r.text), r.code / 100 == 5);
    ctl->write("QUIT\r\n");
    res = NetResult();
    res.ok = true;
    return res;
}

// ================================================================ over real connections

namespace {

class LinkNetStream final : public NetStream {
public:
    bool open(const std::string& host, uint16_t port, const MqttTls& tls, uint32_t timeout_ms, std::string* err)
    {
        if(link_.open(host, port, tls, timeout_ms)) return true;
        if(err) *err = link_.error();
        return false;
    }
    ~LinkNetStream() override { link_.close(); }
    bool write(const char* data, size_t n) override { return link_.write(MqttBytes(data, data + n)); }
    int  read(char* buf, size_t max, uint32_t timeout_ms) override { return link_.read(reinterpret_cast<uint8_t*>(buf), max, timeout_ms); }

private:
    MqttLink link_;
};

} // namespace

NetResult net_upload(const NetUpload& u, const NetTls& tls)
{
    const uint32_t connect_ms = std::min<uint32_t>(u.timeout_ms, 15000);
    NetConnect connect = [&](const std::string& host, uint16_t port, bool secure, std::string* err) -> std::unique_ptr<NetStream> {
        MqttTls t;
        t.enabled = secure;
        t.verify = tls.verify;
        t.ca_file = tls.ca_file;
        t.system_ca = true;
        auto s = std::make_unique<LinkNetStream>();
        if(!s->open(host, port, t, connect_ms, err)) return nullptr;
        return s;
    };
    if(u.url.scheme == "ftp") return ftp_upload(connect, u);
    NetResult res;
    std::string err;
    std::unique_ptr<NetStream> s = connect(u.url.host, u.url.port, u.url.secure(), &err);
    if(!s) { res.error = err.empty() ? "cannot connect" : err; return res; }
    return http_upload(*s, u);
}

} // namespace hml
