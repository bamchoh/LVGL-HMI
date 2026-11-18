// SSH key management for the target the runtime runs on.
//
// Deploying from the designer logs in with an SSH key. This service lets the operator manage
// the keys that may log in (the user's authorized_keys file) from the HMI itself:
//
//   - While "key management mode" is on, a TCP port is open on which a new public key can be
//     offered (the designer does that). Nothing is accepted by the network side alone: the
//     offer waits until the operator accepts or rejects it on the screen, where the sender's
//     address and the key's fingerprint are shown. With the mode off the port is closed.
//   - The registered keys can be listed and removed.
//
// Offer protocol (plain text, one line each; a public key is not a secret):
//   client: "HML-KEY 1"            server: "PENDING <fingerprint>"   (waiting for the operator)
//           "<type> <base64> [comment]"    then "ACCEPTED" | "REJECTED" | "TIMEOUT"
//                                  or at once "ALREADY <fingerprint>" | "ERROR <message>"
//
// The system settings page "Keys" (system_keys.cpp) is the user interface of this class.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hml {

struct PublicKey {
    std::string type;          // "ssh-ed25519", "ssh-rsa", ...
    std::string base64;        // the key blob as written in the file
    std::string comment;       // printable ASCII, may be empty
    std::string fingerprint;   // "SHA256:..." as printed by ssh-keygen -l
    std::string line() const { return type + " " + base64 + (comment.empty() ? "" : " " + comment); }
    std::string short_type() const;                 // "ed25519", "rsa", "ecdsa"
};

// Parse "<type> <base64> [comment]". Only well-formed keys of a known type are accepted; the
// comment is reduced to printable ASCII. With allow_options a leading options field
// (authorized_keys syntax: 'command="..." ssh-ed25519 AAAA...') is skipped.
bool parse_public_key(const std::string& line, PublicKey& out, std::string* error = nullptr, bool allow_options = false);

std::string sha256_hex(const std::string& data);
std::string key_fingerprint(const std::string& blob);          // blob = the decoded base64

class KeyService {
public:
    static constexpr int kDefaultPort = 4855;

    KeyService();
    ~KeyService();
    KeyService(const KeyService&) = delete;
    KeyService& operator=(const KeyService&) = delete;

    // ---- the authorized_keys file
    // "" = the default: ~/.ssh/authorized_keys of the user the runtime runs as.
    void set_file(const std::string& path);
    std::string file() const;
    std::vector<PublicKey> list() const;            // keys in file order; other lines are ignored
    bool add(const PublicKey& key, std::string* error = nullptr);      // no-op if already there
    bool remove(const std::string& fingerprint, std::string* error = nullptr);

    // ---- key management mode: accept offers on a TCP port
    bool start(int port, std::string* error = nullptr);
    void stop();                                    // a pending offer is answered "REJECTED"
    bool listening() const { return listening_; }
    int  port() const { return port_; }

    struct Offer {
        PublicKey   key;
        std::string from;                           // sender's address
    };
    bool pending(Offer& out) const;                 // an offer is waiting for the operator
    bool accept(std::string* error = nullptr);      // register the pending key and tell the sender
    void reject();
    void set_offer_timeout(int seconds) { offer_timeout_s_ = seconds; }

private:
    void run();
    void serve(std::intptr_t client, const std::string& from);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::string file_;
    std::thread thread_;
    std::atomic<bool> listening_{ false };
    std::atomic<bool> stop_{ false };
    std::atomic<int>  port_{ 0 };
    std::atomic<int>  offer_timeout_s_{ 120 };
    std::intptr_t listen_sock_ = -1;
    bool  has_offer_ = false;
    Offer offer_;
    enum class Decision { None, Accept, Reject } decision_ = Decision::None;
    std::string accept_error_;                      // why an accepted key could not be written
};

KeyService& key_service();

} // namespace hml
