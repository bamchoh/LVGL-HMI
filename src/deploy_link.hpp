// Deploying without SSH: the designer sends a project file straight to the runtime over TLS.
// This file is what both ends share - who they are (a certificate each), one encrypted
// connection, and the sending side. The receiving side is deploy_service.hpp.
//
// Who is who:
//   - Each end has a certificate of its own (EC P-256, self-signed), made the first time it is
//     needed and kept as <dir>/<base>.crt + <base>.key. Nobody signs them: an end is known by
//     the fingerprint of its certificate ("SHA256:..."), the way SSH knows host keys.
//   - The designer remembers the fingerprint of a device after the user has confirmed it once.
//   - The device accepts a designer only after its operator has confirmed the fingerprint on
//     the screen (deploy_service.hpp).
//   TLS proves that the other end holds the private key of the certificate it shows.
//
// The conversation, inside TLS (text lines, then the bytes of the file):
//   designer: "HML-DEPLOY 1 <name of the designer's PC>"
//   device:   "PENDING <fingerprint of the designer>"      waiting for the operator (new designers only)
//             "READY"  |  "REJECTED <why>"  |  "TIMEOUT"  |  "ERROR <why>"
//   designer: "PUT <bytes> <sha256 hex> <file name>"  and the bytes
//   device:   "RECEIVED"                                    complete and unchanged
//             "INSTALLED"  |  "ERROR <why>"                 checked and put in place; the runtime restarts
//
// No dependency on the rest of the runtime (the designer host uses this file too): mbedTLS only.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace hml {

constexpr int kDeployDefaultPort = 4856;

struct DeployIdentity {
    std::string cert_pem, key_pem;
    std::string fingerprint;             // of the certificate: "SHA256:<base64 without padding>"
};

// "SHA256:..." of a certificate in DER.
std::string deploy_fingerprint(const unsigned char* der, size_t len);
// SHA-256 of a file as 64 hex digits ("" = cannot be read).
std::string deploy_file_sha256(const std::filesystem::path& file);

// A new certificate and key (in memory). common_name: letters, digits, '-', '_', '.' are kept.
bool deploy_make_identity(const std::string& common_name, DeployIdentity& out, std::string* error = nullptr);
// <dir>/<base>.crt and <base>.key: read, or made and written when they are not there (the key
// readable by its owner only). An unreadable or half pair is an error, never replaced silently.
bool deploy_identity(const std::filesystem::path& dir, const std::string& base, const std::string& common_name, DeployIdentity& out, std::string* error = nullptr);

// One TLS connection. Every wait ends when *cancel becomes true.
class DeployConn {
public:
    DeployConn();
    ~DeployConn();
    DeployConn(const DeployConn&) = delete;
    DeployConn& operator=(const DeployConn&) = delete;

    void set_cancel(const std::atomic<bool>* cancel);
    // The device's side: sock is a connected socket (stays the caller's to close). The other
    // end must show a certificate.
    bool accept(std::intptr_t sock, const DeployIdentity& me, int timeout_ms, std::string* error = nullptr);
    // The designer's side: connects to host:port.
    bool connect(const std::string& host, int port, const DeployIdentity& me, int timeout_ms, std::string* error = nullptr);
    void close();

    const std::string& peer_fingerprint() const;     // after accept() / connect()

    bool write(const void* data, size_t len);
    bool write_line(const std::string& line);        // '\n' is added
    // One line without its end, at most max bytes. False: closed, too long, timeout, cancelled.
    bool read_line(std::string& out, size_t max, int timeout_ms);
    // Up to max bytes. > 0: that many; 0: nothing within the time; < 0: closed or broken.
    int  read_some(void* buf, size_t max, int timeout_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------- the designer's side

struct DeploySend {
    std::string host;
    int         port = kDeployDefaultPort;
    std::string expect_fingerprint;      // the device's, as remembered. "" with probe: just ask
    bool        probe = false;           // only find out the fingerprint of the device; nothing is sent
    std::string file;                    // the project file (.hmlproj)
    std::string client_name;             // shown on the device next to the fingerprint
    int         connect_timeout_ms = 8000;
    int         approve_timeout_ms = 150000;   // the operator has to press a button
    int         install_timeout_ms = 120000;
    std::function<void(const std::string&)> on_state;   // "connected", "pending <fp>", "sending", "sent 50", "received" (may be empty)
    const std::atomic<bool>* cancel = nullptr;
};

struct DeployOutcome {
    bool        ok = false;
    std::string error;                   // why not
    std::string device_fingerprint;      // known as soon as TLS is up, also on failure
};

DeployOutcome deploy_send(const DeploySend& req, const DeployIdentity& me);

} // namespace hml
