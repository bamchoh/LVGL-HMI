// SMTP: one mail handed to a mail server (mail.hpp uses it). The conversation is written against
// a stream that is given in, so that it is tested line by line without a network.
//
//   security "starttls"  a plain connection that is secured before anything else is said (port 587)
//            "tls"       TLS from the first byte (port 465)
//            "none"      nothing is secured: the user name, the password and the mail can be read
//                        on the network (a relay of one's own, port 25)
//
// A user name and a password are sent only over TLS, unless the security is "none" by choice.
// What goes into a header (subject, addresses) cannot add a header: line ends are taken out, and
// an address with anything but the characters of an address is refused.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hml {

constexpr size_t kMailSubjectMax = 200;         // bytes
constexpr size_t kMailBodyMax = 64 * 1024;
constexpr size_t kMailRecipientsMax = 20;

struct SmtpMessage {
    std::string from;                   // an address; "Name <a@b>" is not taken
    std::vector<std::string> to;
    std::string subject, body;          // UTF-8
    int64_t     time_s = 0;             // for "Date:" (0 = now)
    std::string id;                     // for "Message-ID:" (empty = made up)
};

enum class SmtpSecurity { StartTls, Tls, None };

struct SmtpAccount {
    std::string host;
    uint16_t    port = 587;
    SmtpSecurity security = SmtpSecurity::StartTls;
    std::string username, password;
    std::string hello = "hml";          // what the client calls itself (EHLO)
    uint32_t    timeout_ms = 10000;
};

// The lines of a connection. start_tls() is asked for after the server agreed to STARTTLS.
class SmtpStream {
public:
    virtual ~SmtpStream() = default;
    virtual bool write(const std::string& bytes) = 0;
    // bytes read (>= 1), 0 = nothing within timeout_ms, -1 = closed / error
    virtual int  read(char* buf, size_t max, uint32_t timeout_ms) = 0;
    virtual bool start_tls(std::string* err) = 0;
    virtual bool secure() const = 0;    // what is said now cannot be read on the way
};

struct SmtpResult {
    bool ok = false;
    bool permanent = false;             // the server refused it for good (5xx, a wrong address): trying again is of no use
    std::string error;                  // one line, for the operator
    std::vector<std::string> refused;   // recipients the server did not take (the others got the mail)
};

// a@b: one "@", no spaces, no control characters, no < > , ; " - at most 254 bytes
bool smtp_address_ok(const std::string& addr);
// for a header: control characters (line ends!) become spaces, cut at max_bytes on a character boundary
std::string smtp_header_text(const std::string& s, size_t max_bytes = kMailSubjectMax);
std::string smtp_base64(const std::string& bytes);
// the mail as it goes after DATA (headers, an empty line, the body as base64), lines end in CRLF
std::string smtp_build_message(const SmtpMessage& m);
// The conversation on a connection that is open (and, for "tls", secured).
SmtpResult smtp_send(SmtpStream& s, const SmtpAccount& acc, const SmtpMessage& m);

// Connect, send, close. ca_file empty: the certificates the system trusts. verify false: any
// certificate is taken (said in the log).
struct SmtpTls { bool verify = true; std::string ca_file; };
SmtpResult smtp_deliver(const SmtpAccount& acc, const SmtpTls& tls, const SmtpMessage& m);

} // namespace hml
