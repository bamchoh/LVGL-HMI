// One file handed to a server: HTTP(S) PUT / POST, or FTP in passive mode (upload.hpp uses it).
// The conversations are written against streams that are given in, so that they are tested line
// by line without a network.
//
//   https://host[:port]/dir/    PUT  https://host/dir/<file name>            (the URL ends in "/")
//   https://host[:port]/path    POST to that URL, the file as the body, its name in Content-Disposition
//   http://...                  the same, nothing secured (a server of one's own network)
//   ftp://host[:port]/dir/      STOR <file name> in that directory, passive mode; nothing secured
//
// A user name and a password go as HTTP Basic / FTP USER and PASS. Over http:// and ftp:// they
// can be read on the network: that is said when the settings are read (upload.hpp, --check).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace hml {

struct NetUrl {
    std::string scheme;                 // "http" | "https" | "ftp"
    std::string host, path;             // path begins with "/"
    uint16_t    port = 0;
    bool secure() const { return scheme == "https"; }
    std::string shown() const;          // scheme://host[:port]/path - never a user name or a password
};
// false and *err for what is no URL we can send to (another scheme, "user:password@" in it, spaces, line ends ...)
bool net_url_parse(const std::string& text, NetUrl& out, std::string* err = nullptr);
// a file name as it may go into a URL, a header and an FTP command: [A-Za-z0-9._-], at most 120
std::string net_file_name(const std::string& name);

class NetStream {
public:
    virtual ~NetStream() = default;
    virtual bool write(const char* data, size_t n) = 0;
    // bytes read (>= 1), 0 = nothing within timeout_ms, -1 = closed / error
    virtual int  read(char* buf, size_t max, uint32_t timeout_ms) = 0;
    bool write(const std::string& s) { return write(s.data(), s.size()); }
};
// Opens a connection (tls: secured before it is handed out). nullptr and *err when it cannot.
using NetConnect = std::function<std::unique_ptr<NetStream>(const std::string& host, uint16_t port, bool tls, std::string* err)>;

struct NetResult {
    bool ok = false;
    bool permanent = false;             // the server refused for good: trying again is of no use
    std::string error;                  // one line, for the operator
};

struct NetUpload {
    NetUrl      url;
    bool        post = false;           // HTTP: POST to the URL as it is (else PUT to URL + file name)
    std::string username, password;
    std::string file;                   // the local file
    std::string name;                   // the name it gets at the server
    uint32_t    timeout_ms = 30000;
};

// The conversations (the connection for HTTP is open already; FTP opens its own).
NetResult http_upload(NetStream& s, const NetUpload& u);
NetResult ftp_upload(const NetConnect& connect, const NetUpload& u);

// Connect, send, close. ca_file empty: the certificates the system trusts.
struct NetTls { bool verify = true; std::string ca_file; };
NetResult net_upload(const NetUpload& u, const NetTls& tls);

} // namespace hml
