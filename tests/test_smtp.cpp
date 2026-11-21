// SMTP (smtp.hpp): the conversation with a mail server, line by line, against a server that is a
// script here. Grouped by ISO/IEC 25010 characteristic (issue #67).
#include <doctest.h>
#include "smtp.hpp"

#include <algorithm>
#include <cstring>

#include <deque>

using namespace hml;

namespace {

// A server that says its lines in turn: what the client sent since the last answer is kept, the
// next answer is handed out when the client reads.
class Script final : public SmtpStream {
public:
    std::deque<std::string> answers;        // one per read turn ("" = the server says nothing: a timeout; "<close>")
    std::vector<std::string> sent;          // what the client said, a turn per entry
    bool tls = false, tls_fails = false;
    int  tls_asked = 0;
    std::string pending_;

    explicit Script(std::initializer_list<const char*> a) { for(const char* s : a) answers.emplace_back(s); }
    bool write(const std::string& bytes) override
    {
        if(sent.empty() || read_since_write_) sent.emplace_back();
        sent.back() += bytes;
        read_since_write_ = false;
        return !broken;
    }
    int read(char* buf, size_t max, uint32_t) override
    {
        read_since_write_ = true;
        if(pending_.empty()) {
            if(answers.empty()) return 0;
            pending_ = answers.front();
            answers.pop_front();
            if(pending_ == "<close>") { pending_.clear(); return -1; }
            if(pending_.empty()) return 0;
        }
        const size_t n = std::min(max, pending_.size());
        std::memcpy(buf, pending_.data(), n);
        pending_.erase(0, n);
        return static_cast<int>(n);
    }
    bool start_tls(std::string* err) override
    {
        tls_asked++;
        if(tls_fails) { if(err) *err = "TLS handshake: the certificate is not trusted"; return false; }
        tls = true;
        return true;
    }
    bool secure() const override { return tls; }
    bool broken = false;
    // everything the client said, as one text
    std::string all() const { std::string s; for(const auto& x : sent) s += x; return s; }
    bool said(const std::string& what) const { return all().find(what) != std::string::npos; }

private:
    bool read_since_write_ = true;
};

SmtpMessage mail()
{
    SmtpMessage m;
    m.from = "hmi@plant.example";
    m.to = { "boss@plant.example", "shift@plant.example" };
    m.subject = "Alarm: tank high";
    m.body = "The tank is at 95 %.\r\n.\r\nSecond line.";
    m.time_s = 1795000000;
    m.id = "42";
    return m;
}
SmtpAccount account(SmtpSecurity sec = SmtpSecurity::StartTls)
{
    SmtpAccount a;
    a.host = "mail.plant.example";
    a.security = sec;
    a.username = "hmi";
    a.password = "s3cret";
    a.hello = "hml-line1";
    return a;
}
const char* kEhlo = "250-mail.plant.example\r\n250-SIZE 1000000\r\n250-STARTTLS\r\n250 AUTH LOGIN PLAIN\r\n";
const char* kEhloTls = "250-mail.plant.example\r\n250-AUTH=PLAIN LOGIN\r\n250 8BITMIME\r\n";

std::string unbase64(const std::string& in)
{
    std::string out;
    int val = 0, bits = -8;
    for(char c : in) {
        const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const char* p = std::strchr(t, c);
        if(!p || c == 0) continue;
        val = (val << 6) + static_cast<int>(p - t);
        bits += 6;
        if(bits >= 0) { out += static_cast<char>((val >> bits) & 0xFF); bits -= 8; }
    }
    return out;
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("smtp: a mail over STARTTLS with a user name - what is said, in which order, and nothing before the connection is secured")
{
    Script s{ "220 mail.plant.example ESMTP\r\n", kEhlo, "220 go ahead\r\n", kEhloTls, "235 ok\r\n", "250 sender ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 end with .\r\n", "250 queued as 77\r\n", "221 bye\r\n" };
    const SmtpResult r = smtp_send(s, account(), mail());
    CHECK_MESSAGE(r.ok, r.error);
    CHECK(r.refused.empty());
    REQUIRE(s.sent.size() >= 9);
    CHECK(s.sent[0] == "EHLO hml-line1\r\n");
    CHECK(s.sent[1] == "STARTTLS\r\n");
    CHECK(s.tls_asked == 1);
    CHECK(s.sent[2] == "EHLO hml-line1\r\n");                    // asked again: what was offered before the handshake does not count
    CHECK(s.sent[3] == "AUTH PLAIN " + smtp_base64(std::string("\0hmi\0s3cret", 11)) + "\r\n");
    CHECK(s.sent[4] == "MAIL FROM:<hmi@plant.example>\r\n");
    CHECK(s.sent[5] == "RCPT TO:<boss@plant.example>\r\n");
    CHECK(s.sent[6] == "RCPT TO:<shift@plant.example>\r\n");
    CHECK(s.sent[7] == "DATA\r\n");
    CHECK(s.sent[8].substr(s.sent[8].size() - 5) == "\r\n.\r\n");
    CHECK(s.sent.back() == "QUIT\r\n");
    // the password went out only after the handshake
    const std::string before = s.sent[0] + s.sent[1];
    CHECK(before.find("AUTH") == std::string::npos);

    // the mail itself
    const std::string& data = s.sent[8];
    CHECK(data.find("From: <hmi@plant.example>\r\n") != std::string::npos);
    CHECK(data.find("To: <boss@plant.example>,\r\n <shift@plant.example>\r\n") != std::string::npos);
    CHECK(data.find("Subject: Alarm: tank high\r\n") != std::string::npos);
    CHECK(data.find("Message-ID: <42@plant.example>\r\n") != std::string::npos);
    CHECK(data.find("Date: ") == 0);
    CHECK(data.find(" +0000\r\n") != std::string::npos);
    CHECK(data.find("Content-Type: text/plain; charset=utf-8\r\n") != std::string::npos);
    const size_t body_at = data.find("\r\n\r\n");
    REQUIRE(body_at != std::string::npos);
    const std::string body = data.substr(body_at + 4, data.size() - body_at - 4 - 3);
    CHECK(unbase64(body) == "The tank is at 95 %.\r\n.\r\nSecond line.");
    // no line of the mail is a lone "." (that would end it), none is longer than a mail line may be
    size_t from = 0, dots = 0;
    for(size_t nl; (nl = data.find("\r\n", from)) != std::string::npos; from = nl + 2) {
        CHECK(nl - from <= 78);
        if(data.substr(from, nl - from) == ".") dots++;
    }
    CHECK(dots == 1);                                            // the one that ends the mail
}

TEST_CASE("smtp: TLS from the start, AUTH LOGIN, no user name, a recipient the server does not take")
{
    {   // port 465: secured before a word is said; a server that only knows LOGIN
        Script s{ "220 hi\r\n", "250-x\r\n250 AUTH LOGIN\r\n", "334 VXNlcm5hbWU6\r\n", "334 UGFzc3dvcmQ6\r\n", "235 ok\r\n", "250 ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 go\r\n", "250 ok\r\n" };
        s.tls = true;
        const SmtpResult r = smtp_send(s, account(SmtpSecurity::Tls), mail());
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(s.tls_asked == 0);
        CHECK_FALSE(s.said("STARTTLS"));
        CHECK(s.sent[1] == "AUTH LOGIN\r\n");
        CHECK(s.sent[2] == smtp_base64("hmi") + "\r\n");
        CHECK(s.sent[3] == smtp_base64("s3cret") + "\r\n");
    }
    {   // a relay that asks for no login
        Script s{ "220 hi\r\n", "250 x\r\n", "250 ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 go\r\n", "250 ok\r\n" };
        SmtpAccount a = account(SmtpSecurity::None);
        a.username.clear();
        const SmtpResult r = smtp_send(s, a, mail());
        CHECK_MESSAGE(r.ok, r.error);
        CHECK_FALSE(s.said("AUTH"));
    }
    {   // one of two recipients is unknown: the other gets the mail, and it is said
        Script s{ "220 hi\r\n", "250 x\r\n", "250 ok\r\n", "550 5.1.1 no such user\r\n", "250 ok\r\n", "354 go\r\n", "250 ok\r\n" };
        SmtpAccount a = account(SmtpSecurity::None);
        a.username.clear();
        const SmtpResult r = smtp_send(s, a, mail());
        CHECK(r.ok);
        REQUIRE(r.refused.size() == 1);
        CHECK(r.refused[0] == "boss@plant.example (550 5.1.1 no such user)");
    }
    {   // none is taken: no DATA
        Script s{ "220 hi\r\n", "250 x\r\n", "250 ok\r\n", "550 no\r\n", "550 no\r\n" };
        SmtpAccount a = account(SmtpSecurity::None);
        a.username.clear();
        const SmtpResult r = smtp_send(s, a, mail());
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK(r.error.find("none of the recipients") != std::string::npos);
        CHECK_FALSE(s.said("DATA"));
    }
}

TEST_CASE("smtp: the subject in another script, long subjects, the parts of a mail")
{
    SmtpMessage m = mail();
    m.subject = "アラーム: タンクの液位が高い (95 %)";
    m.body = "液位 95 %\n確認してください";
    std::string data = smtp_build_message(m);
    const size_t at = data.find("Subject:");
    REQUIRE(at != std::string::npos);
    const size_t end = data.find("\r\nMessage-ID:");
    std::string decoded;
    const std::string subject = data.substr(at, end - at);
    for(size_t p = 0; (p = subject.find("=?UTF-8?B?", p)) != std::string::npos;) {
        const size_t q = subject.find("?=", p + 10);
        REQUIRE(q != std::string::npos);
        const std::string part = unbase64(subject.substr(p + 10, q - p - 10));
        // each encoded word is whole characters (a mail program decodes them one by one)
        CHECK((static_cast<unsigned char>(part[0]) & 0xC0) != 0x80);
        decoded += part;
        p = q + 2;
    }
    CHECK(decoded == m.subject);
    for(size_t from = 0, nl; (nl = data.find("\r\n", from)) != std::string::npos; from = nl + 2) CHECK(nl - from <= 78);
    CHECK(unbase64(data.substr(data.find("\r\n\r\n") + 4)) == m.body);
    // a subject of a thousand characters is cut, at a character
    m.subject = std::string(100, 'x');
    for(int i = 0; i < 300; i++) m.subject += "液";
    data = smtp_build_message(m);
    CHECK(data.size() < 2000);
    // an empty body, a body of a megabyte
    m.body.clear();
    CHECK(smtp_build_message(m).find("\r\n\r\n") != std::string::npos);
    m.body.assign(1024 * 1024, 'a');
    CHECK(smtp_build_message(m).size() < kMailBodyMax * 2);
    CHECK(smtp_base64("") == "");
    CHECK(smtp_base64("f") == "Zg==");
    CHECK(smtp_base64("fo") == "Zm8=");
    CHECK(smtp_base64("foobar") == "Zm9vYmFy");
}

// ================================================================ security

TEST_CASE("smtp: what goes into a header cannot add a header or a command")
{
    // a tag value in the subject that tries to add a recipient and a body
    SmtpMessage m = mail();
    m.subject = "Alarm\r\nBcc: thief@evil.example\r\n\r\nYou are hacked\r\n.\r\nMAIL FROM:<x@evil.example>";
    m.id = "1\r\nX-Evil: yes";
    const std::string data = smtp_build_message(m);
    const std::string head = data.substr(0, data.find("\r\n\r\n"));
    CHECK(head.find("\r\nBcc:") == std::string::npos);
    CHECK(head.find("\r\nX-Evil:") == std::string::npos);
    CHECK(head.find("\r\nMAIL FROM") == std::string::npos);
    CHECK(head.find("Subject: Alarm  Bcc: thief@evil.example") != std::string::npos);     // one line, as text
    int headers = 0;
    for(size_t from = 0, nl; (nl = head.find("\r\n", from)) != std::string::npos; from = nl + 2) if(head[nl + 2] != ' ') headers++;
    CHECK(headers == 7);                                         // Date From To Subject Message-ID MIME-Version Content-Type (+ the last: Content-Transfer-Encoding)

    for(const char* ok : { "a@b.example", "first.last+tag@sub.plant.example", "x@localhost", "A_b-c@1.2.3.4" }) { CAPTURE(ok); CHECK(smtp_address_ok(ok)); }
    for(const char* bad : { "", "plain", "@b.example", "a@", "a@@b", "a b@c.example", "a@b.example\r\nRCPT TO:<thief@evil.example>", "a@b.example>\r\nDATA", "<a@b.example>", "a@b.example,c@d.example",
                            "a@b.example;c@d.example", "Name <a@b.example>", "a\"b@c.example", "a@b..example", "a@.b", "a@b.", "a\tb@c.example", "\xE3\x81\x82@b.example", "a@b.example\n", "a:b@c.example" }) {
        CAPTURE(bad);
        CHECK_FALSE(smtp_address_ok(bad));
    }
    CHECK_FALSE(smtp_address_ok(std::string(250, 'a') + "@b.example"));
    // an address that is none is not offered to any server
    for(int which = 0; which < 2; which++) {
        Script s{ "220 hi\r\n", kEhlo };
        SmtpMessage x = mail();
        if(which == 0) x.to.push_back("boss@plant.example>\r\nRCPT TO:<thief@evil.example");
        else x.from = "hmi@plant.example>\r\nMAIL FROM:<ceo@plant.example";
        const SmtpResult r = smtp_send(s, account(), x);
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK(r.error.find("no mail address") != std::string::npos);
        CHECK(r.error.find('\n') == std::string::npos);
        CHECK(s.sent.empty());
    }
    {   // more recipients than a mail of a device should have; none at all
        Script s{ "220 hi\r\n" };
        SmtpMessage x = mail();
        x.to.assign(50, "a@b.example");
        CHECK_FALSE(smtp_send(s, account(), x).ok);
        x.to.clear();
        CHECK(smtp_send(s, account(), x).error == "no recipient");
        CHECK(s.sent.empty());
    }
    {   // a line end in the user name or the password
        Script s{ "220 hi\r\n", kEhlo, "220 go\r\n", kEhloTls };
        SmtpAccount a = account();
        a.password = "x\r\nMAIL FROM:<a@b>";
        const SmtpResult r = smtp_send(s, a, mail());
        CHECK_FALSE(r.ok);
        CHECK_FALSE(s.said("MAIL FROM"));
    }
}

TEST_CASE("smtp: the password is never sent over a connection that is not secured, unless that was chosen")
{
    {   // the server does not offer STARTTLS (or somebody on the way took the line out)
        Script s{ "220 hi\r\n", "250-x\r\n250 AUTH PLAIN LOGIN\r\n" };
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK(r.error.find("STARTTLS") != std::string::npos);
        CHECK_FALSE(s.said("AUTH"));
        CHECK_FALSE(s.said("MAIL FROM"));
        CHECK(s.tls_asked == 0);
    }
    {   // the handshake fails (a certificate that is not trusted): nothing more is said
        Script s{ "220 hi\r\n", kEhlo, "220 go\r\n" };
        s.tls_fails = true;
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("certificate") != std::string::npos);
        CHECK(s.sent.size() == 2);
    }
    {   // somebody on the way put a reply behind the answer to STARTTLS, to be read after the handshake as if it came through it
        Script s{ "220 hi\r\n", kEhlo, "220 go\r\n250-evil\r\n250 AUTH PLAIN\r\n" };
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("not trusted") != std::string::npos);
        CHECK(s.tls_asked == 0);
        CHECK_FALSE(s.said("AUTH"));
    }
    {   // the server refuses STARTTLS after offering it
        Script s{ "220 hi\r\n", kEhlo, "454 TLS not available\r\n" };
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.permanent);
        CHECK_FALSE(s.said("AUTH"));
    }
    {   // "tls" was asked for, but the stream is not secured (a mistake of the caller): nothing is sent
        Script s{ "220 hi\r\n", kEhloTls };
        const SmtpResult r = smtp_send(s, account(SmtpSecurity::Tls), mail());
        CHECK_FALSE(r.ok);
        CHECK_FALSE(s.said("AUTH"));
    }
    {   // "none" by choice: it goes in the clear
        Script s{ "220 hi\r\n", "250-x\r\n250 AUTH PLAIN\r\n", "235 ok\r\n", "250 ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 go\r\n", "250 ok\r\n" };
        const SmtpResult r = smtp_send(s, account(SmtpSecurity::None), mail());
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(s.said("AUTH PLAIN"));
    }
    {   // a wrong password: said in words, for good, and the password is not in the message
        Script s{ "220 hi\r\n", kEhlo, "220 go\r\n", kEhloTls, "535 5.7.8 authentication failed\r\n" };
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK(r.permanent);
        CHECK(r.error == "the user name or the password was not accepted: 535 5.7.8 authentication failed");
        CHECK(r.error.find("s3cret") == std::string::npos);
        CHECK_FALSE(s.said("MAIL FROM"));
    }
    {   // a server without a way to log in that is known here
        Script s{ "220 hi\r\n", kEhlo, "220 go\r\n", "250-x\r\n250 AUTH XOAUTH2 GSSAPI\r\n" };
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("PLAIN, LOGIN") != std::string::npos);
    }
}

// ================================================================ reliability

TEST_CASE("smtp: a server that is busy, silent, gone, or no mail server at all - said in words, and whether trying again makes sense")
{
    struct Case { std::initializer_list<const char*> script; const char* in_error; bool permanent; };
    const Case cases[] = {
        { { "" }, "no answer from the server", false },
        { { "<close>" }, "closed the connection", false },
        { { "421 too busy, try later\r\n" }, "does not take mail now: 421 too busy", false },
        { { "554 no service for you\r\n" }, "554 no service", true },
        { { "HTTP/1.1 400 Bad Request\r\n" }, "does not speak SMTP", false },
        { { "SSH-2.0-OpenSSH_9.6\r\n" }, "does not speak SMTP", false },
        { { "220 hi\r\n", "" }, "no answer", false },
        { { "220 hi\r\n", "500 what?\r\n" }, "EHLO: 500", true },
        { { "220 hi\r\n", kEhlo, "220 go\r\n", kEhloTls, "235 ok\r\n", "451 try again later\r\n" }, "451 try again later", false },
        { { "220 hi\r\n", kEhlo, "220 go\r\n", kEhloTls, "235 ok\r\n", "550 sender blocked\r\n" }, "does not take mail from this sender: 550", true },
        { { "220 hi\r\n", kEhlo, "220 go\r\n", kEhloTls, "235 ok\r\n", "250 ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 go\r\n", "452 mailbox full\r\n" }, "did not take the mail: 452", false },
        { { "220 hi\r\n", kEhlo, "220 go\r\n", kEhloTls, "235 ok\r\n", "250 ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 go\r\n", "552 too large\r\n" }, "did not take the mail: 552", true },
        { { "220 hi\r\n", kEhlo, "220 go\r\n", kEhloTls, "235 ok\r\n", "250 ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 go\r\n", "<close>" }, "closed the connection", false },
        { { "220 hi\r\n", "250-a\r\n251 b\r\n" }, "changes its code", false },
    };
    for(const Case& c : cases) {
        CAPTURE(c.in_error);
        Script s(c.script);
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK_MESSAGE(r.error.find(c.in_error) != std::string::npos, r.error);
        CHECK(r.permanent == c.permanent);
        CHECK(r.error.find('\n') == std::string::npos);
        CHECK(r.error.size() < 400);
    }
    {   // a server that never ends its line, one that never ends its reply, one that sends control characters
        std::string endless(100000, 'x');
        Script a{ endless.c_str() };
        CHECK(smtp_send(a, account(), mail()).error.find("without end") != std::string::npos);
        std::string many;
        for(int i = 0; i < 2000; i++) many += "220-more and more and more\r\n";
        Script b{ many.c_str() };
        CHECK(smtp_send(b, account(), mail()).error.find("too long") != std::string::npos);
        Script c{ "554 go\x1b[31m away\x07\x00\r\n" };
        const SmtpResult r = smtp_send(c, account(), mail());
        for(char ch : r.error) CHECK(static_cast<unsigned char>(ch) >= 0x20);
    }
    {   // the connection breaks while we write
        Script s{ "220 hi\r\n", kEhlo };
        s.broken = true;
        const SmtpResult r = smtp_send(s, account(), mail());
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.permanent);
    }
    // nothing to connect to
    SmtpAccount none = account();
    none.host.clear();
    CHECK(smtp_deliver(none, SmtpTls{}, mail()).error.find("no mail server") != std::string::npos);
    SmtpAccount closed = account();
    closed.host = "127.0.0.1";
    closed.port = 1;                                             // (nothing listens there)
    closed.timeout_ms = 1500;
    const SmtpResult r = smtp_deliver(closed, SmtpTls{}, mail());
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.permanent);
    CHECK(r.error.find("cannot connect") != std::string::npos);
}
