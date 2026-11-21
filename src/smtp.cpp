#include "smtp.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

#include "mqtt.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr size_t kReplyMax = 16 * 1024;         // a reply of the server longer than this ends the conversation
constexpr size_t kLineMax = 2000;

// what a server said, for one line of a message: no control characters, not too long
std::string plain_line(const std::string& s, size_t max = 160)
{
    std::string out;
    for(char c : s) out += (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) ? ' ' : c;
    while(!out.empty() && out.back() == ' ') out.pop_back();
    if(out.size() > max) out = out.substr(0, max) + "...";
    return out;
}

bool is_ascii(const std::string& s) { return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }); }

// one reply: its code and its lines ("250-a", "250 b" -> 250, { "a", "b" })
struct Reply {
    int code = 0;
    std::vector<std::string> lines;
    std::string text() const { return std::to_string(code) + (lines.empty() ? "" : " " + plain_line(lines.front())); }      // (the first line says what is wrong, the others where to read more)
};

class Talk {
public:
    Talk(SmtpStream& s, uint32_t timeout_ms) : s_(s), timeout_(timeout_ms) {}
    std::string error;

    bool say(const std::string& line)
    {
        if(s_.write(line + "\r\n")) return true;
        error = "the connection was lost";
        return false;
    }
    bool read(Reply& r)
    {
        r = Reply();
        size_t total = 0;
        for(;;) {
            std::string line;
            if(!read_line(line)) return false;
            total += line.size();
            if(total > kReplyMax) { error = "the server's reply is too long"; return false; }
            if(line.size() < 3 || !std::all_of(line.begin(), line.begin() + 3, [](char c) { return c >= '0' && c <= '9'; }) || (line.size() > 3 && line[3] != ' ' && line[3] != '-')) {
                error = "the server does not speak SMTP (\"" + plain_line(line, 60) + "\")";
                return false;
            }
            const int code = std::atoi(line.substr(0, 3).c_str());
            if(r.code && code != r.code) { error = "the server's reply changes its code half way"; return false; }
            r.code = code;
            r.lines.push_back(line.size() > 4 ? line.substr(4) : "");
            if(line.size() == 3 || line[3] == ' ') return true;
        }
    }
    // say something and expect a reply of the class given (2 = 2xx ...)
    bool ask(const std::string& line, int klass, Reply& r, const char* what)
    {
        if(!say(line) || !read(r)) return false;
        if(r.code / 100 == klass) return true;
        error = std::string(what) + ": " + r.text();
        return false;
    }
    void reset() { buf_.clear(); }      // (after STARTTLS nothing read before counts)
    bool pending() const { return !buf_.empty(); }

private:
    bool read_line(std::string& line)
    {
        for(;;) {
            const size_t nl = buf_.find('\n');
            if(nl != std::string::npos) {
                line = buf_.substr(0, nl);
                buf_.erase(0, nl + 1);
                if(!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            if(buf_.size() > kLineMax) { error = "the server sent a line without end"; return false; }
            char tmp[1024];
            const int n = s_.read(tmp, sizeof(tmp), timeout_);
            if(n == 0) { error = "no answer from the server within " + std::to_string(timeout_ / 1000) + " s"; return false; }
            if(n < 0) { error = "the server closed the connection"; return false; }
            buf_.append(tmp, static_cast<size_t>(n));
        }
    }
    SmtpStream& s_;
    uint32_t timeout_;
    std::string buf_;
};

bool has_word(const std::vector<std::string>& lines, const char* first, const char* word = nullptr)
{
    for(const std::string& l : lines) {
        std::string u = l;
        for(char& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if(u.rfind(first, 0) != 0 || (u.size() > std::strlen(first) && u[std::strlen(first)] != ' ' && u[std::strlen(first)] != '=')) continue;
        if(!word) return true;
        u += " ";
        if(u.find(std::string(" ") + word + " ") != std::string::npos || u.find(std::string("=") + word + " ") != std::string::npos) return true;
    }
    return false;
}

} // namespace

bool smtp_address_ok(const std::string& a)
{
    if(a.empty() || a.size() > 254) return false;
    size_t at = std::string::npos;
    for(size_t i = 0; i < a.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(a[i]);
        if(c <= 0x20 || c >= 0x7f) return false;                // (no spaces, no control characters, ASCII only)
        if(std::strchr("<>,;:\"()[]\\", static_cast<char>(c))) return false;
        if(c == '@') { if(at != std::string::npos) return false; at = i; }
    }
    if(at == std::string::npos || at == 0 || at + 1 >= a.size()) return false;
    if(a.size() - at - 1 > 253 || at > 64) return false;
    const std::string host = a.substr(at + 1);
    return host.front() != '.' && host.back() != '.' && host.find("..") == std::string::npos;
}

std::string smtp_header_text(const std::string& s, size_t max_bytes)
{
    std::string out;
    for(char c : s) out += (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) ? ' ' : c;
    if(out.size() > max_bytes) {
        size_t n = max_bytes;
        while(n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) n--;
        out = out.substr(0, n);
    }
    return out;
}

std::string smtp_base64(const std::string& in)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for(size_t i = 0; i < in.size(); i += 3) {
        const uint32_t a = static_cast<unsigned char>(in[i]), b = i + 1 < in.size() ? static_cast<unsigned char>(in[i + 1]) : 0, c = i + 2 < in.size() ? static_cast<unsigned char>(in[i + 2]) : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? t[v & 63] : '=';
    }
    return out;
}

std::string smtp_build_message(const SmtpMessage& m)
{
    std::string out;
    const std::time_t t = static_cast<std::time_t>(m.time_s > 0 ? m.time_s : std::time(nullptr));
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char date[64];
    static const char* days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    std::snprintf(date, sizeof(date), "%s, %02d %s %04d %02d:%02d:%02d +0000", days[tm.tm_wday % 7], tm.tm_mday, months[tm.tm_mon % 12], tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    out += std::string("Date: ") + date + "\r\n";
    out += "From: <" + m.from + ">\r\n";
    // (addresses were checked before: they hold nothing that ends a line)
    std::string to;
    for(const std::string& a : m.to) { if(!to.empty()) to += ",\r\n "; to += "<" + a + ">"; }
    out += "To: " + to + "\r\n";
    // a subject that is not plain ASCII goes as "encoded words" (RFC 2047), a few characters each
    const std::string subject = smtp_header_text(m.subject);
    if(is_ascii(subject)) out += "Subject: " + subject + "\r\n";
    else {
        out += "Subject:";
        for(size_t i = 0; i < subject.size();) {
            size_t n = std::min<size_t>(39, subject.size() - i);                // 39 bytes -> 52 of base64: a line under 76
            while(n > 0 && i + n < subject.size() && (static_cast<unsigned char>(subject[i + n]) & 0xC0) == 0x80) n--;
            if(n == 0) n = std::min<size_t>(4, subject.size() - i);
            out += (i ? "\r\n " : " ") + std::string("=?UTF-8?B?") + smtp_base64(subject.substr(i, n)) + "?=";
            i += n;
        }
        out += "\r\n";
    }
    std::string id = m.id;
    if(id.empty()) id = std::to_string(static_cast<long long>(t)) + "." + std::to_string(millis());
    std::string domain = m.from.substr(m.from.find('@') == std::string::npos ? 0 : m.from.find('@') + 1);
    out += "Message-ID: <" + smtp_header_text(id, 60) + "@" + (domain.empty() ? "hml" : domain) + ">\r\n";
    out += "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: base64\r\n\r\n";
    // the body as base64: whatever it holds, no line of it is a command to the server (no lone ".")
    const std::string body = smtp_base64(m.body.size() > kMailBodyMax ? m.body.substr(0, kMailBodyMax) : m.body);
    for(size_t i = 0; i < body.size(); i += 76) out += body.substr(i, 76) + "\r\n";
    return out;
}

SmtpResult smtp_send(SmtpStream& s, const SmtpAccount& acc, const SmtpMessage& m)
{
    SmtpResult res;
    auto fail = [&](const std::string& why, bool permanent) { res.ok = false; res.error = why; res.permanent = permanent; return res; };
    // ---- what cannot be sent is not offered to the server
    if(!smtp_address_ok(m.from)) return fail("the sender \"" + plain_line(m.from, 60) + "\" is no mail address", true);
    if(m.to.empty()) return fail("no recipient", true);
    if(m.to.size() > kMailRecipientsMax) return fail("more than " + std::to_string(kMailRecipientsMax) + " recipients", true);
    for(const std::string& a : m.to) if(!smtp_address_ok(a)) return fail("the recipient \"" + plain_line(a, 60) + "\" is no mail address", true);
    std::string hello;
    for(char c : acc.hello) if(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.') hello += c;
    if(hello.empty()) hello = "hml";

    Talk t(s, acc.timeout_ms);
    Reply r;
    auto lost = [&](const Reply& rr) { return fail(t.error, rr.code / 100 == 5); };
    if(!t.read(r)) return fail(t.error, false);
    if(r.code != 220) return fail("the server does not take mail now: " + r.text(), r.code / 100 == 5);
    if(!t.ask("EHLO " + hello, 2, r, "EHLO")) return lost(r);
    if(acc.security == SmtpSecurity::StartTls) {
        if(s.secure()) return fail("the connection is secured already", true);
        if(!has_word(r.lines, "STARTTLS")) return fail("the server does not offer STARTTLS: nothing is sent over a connection that is not secured (\"security\": \"tls\" or \"none\"?)", true);
        // (what the server says before the handshake is not trusted afterwards: a reply that
        // already waits behind the answer to STARTTLS was put there by somebody in between)
        if(!t.ask("STARTTLS", 2, r, "STARTTLS")) return lost(r);
        if(t.pending()) return fail("the server sent more than its answer to STARTTLS: the connection is not trusted", true);
        std::string err;
        if(!s.start_tls(&err)) return fail(err.empty() ? "the TLS handshake failed" : err, false);
        t.reset();
        if(!t.ask("EHLO " + hello, 2, r, "EHLO")) return lost(r);
    }
    if(acc.security != SmtpSecurity::None && !s.secure()) return fail("the connection is not secured", true);
    if(!acc.username.empty()) {
        // (never over a connection others can read, unless that was chosen: "security": "none")
        if(!s.secure() && acc.security != SmtpSecurity::None) return fail("the connection is not secured: the password is not sent", true);
        if(acc.username.find_first_of(std::string("\r\n\0", 3)) != std::string::npos || acc.password.find_first_of(std::string("\r\n\0", 3)) != std::string::npos)
            return fail("the user name or the password has a character that cannot be sent", true);
        Reply a;
        if(has_word(r.lines, "AUTH", "PLAIN")) {
            if(!t.ask("AUTH PLAIN " + smtp_base64(std::string(1, '\0') + acc.username + std::string(1, '\0') + acc.password), 2, a, "the user name or the password was not accepted")) return lost(a);
        }
        else if(has_word(r.lines, "AUTH", "LOGIN")) {
            if(!t.ask("AUTH LOGIN", 3, a, "AUTH LOGIN")) return lost(a);
            if(!t.ask(smtp_base64(acc.username), 3, a, "the user name was not accepted")) return lost(a);
            if(!t.ask(smtp_base64(acc.password), 2, a, "the user name or the password was not accepted")) return lost(a);
        }
        else return fail("the server offers no way to log in that is known here (PLAIN, LOGIN)", true);
    }
    if(!t.ask("MAIL FROM:<" + m.from + ">", 2, r, "the server does not take mail from this sender")) return lost(r);
    size_t taken = 0;
    for(const std::string& a : m.to) {
        Reply rr;
        if(!t.say("RCPT TO:<" + a + ">") || !t.read(rr)) return fail(t.error, false);
        if(rr.code / 100 == 2) taken++;
        else res.refused.push_back(a + " (" + rr.text() + ")");
    }
    if(taken == 0) return fail("the server takes none of the recipients: " + res.refused.front(), true);
    if(!t.ask("DATA", 3, r, "DATA")) return lost(r);
    if(!s.write(smtp_build_message(m) + ".\r\n")) return fail("the connection was lost", false);
    if(!t.read(r)) return fail(t.error, false);
    if(r.code / 100 != 2) return fail("the server did not take the mail: " + r.text(), r.code / 100 == 5);
    t.say("QUIT");                                              // (the mail is on its way whatever comes back)
    res.ok = true;
    return res;
}

// ================================================================ over a real connection

namespace {

class LinkStream final : public SmtpStream {
public:
    LinkStream(MqttLink& link, MqttTls tls, uint32_t timeout_ms, bool secure) : link_(link), tls_(std::move(tls)), timeout_(timeout_ms), secure_(secure) {}
    bool write(const std::string& b) override { return link_.write(MqttBytes(b.begin(), b.end())); }
    int  read(char* buf, size_t max, uint32_t timeout_ms) override { return link_.read(reinterpret_cast<uint8_t*>(buf), max, timeout_ms); }
    bool start_tls(std::string* err) override
    {
        if(link_.start_tls(tls_, timeout_)) { secure_ = true; return true; }
        if(err) *err = link_.error();
        return false;
    }
    bool secure() const override { return secure_; }

private:
    MqttLink& link_;
    MqttTls tls_;
    uint32_t timeout_;
    bool secure_;
};

} // namespace

SmtpResult smtp_deliver(const SmtpAccount& acc, const SmtpTls& tls, const SmtpMessage& m)
{
    SmtpResult res;
    if(acc.host.empty()) { res.error = "no mail server (\"host\")"; res.permanent = true; return res; }
    MqttTls t;
    t.enabled = acc.security == SmtpSecurity::Tls;
    t.verify = tls.verify;
    t.ca_file = tls.ca_file;
    t.system_ca = true;
    MqttLink link;
    if(!link.open(acc.host, acc.port, t, acc.timeout_ms)) { res.error = link.error(); return res; }
    t.enabled = true;
    LinkStream stream(link, t, acc.timeout_ms, acc.security == SmtpSecurity::Tls);
    res = smtp_send(stream, acc, m);
    link.close();
    return res;
}

} // namespace hml
