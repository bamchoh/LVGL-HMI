// Receiving a project from the designer without SSH (deploy_link.hpp has the protocol).
//
// The device decides everything, and nothing here can be turned on by a project:
//   - Off unless the operator has switched it on (System > Deploy). The port is closed then.
//   - A designer is known by the fingerprint of its certificate. A new one is only offered to
//     the operator while "Add a designer" is on (a few minutes), and waits until the operator
//     has compared the fingerprint and pressed Accept. Until then it is told nothing about
//     the device and nothing is taken from it.
//   - An accepted designer may send a project file. It is written next to the keys, compared
//     with the checksum it came with, and handed to the UI thread, which checks and installs
//     it like one from a USB memory (transfer.hpp) and restarts.
//
// What the device keeps for this lies in its data folder (never in a project, never in
// ~/.ssh): the folder given with --data-dir, else "keys" next to the executable.
//   device.crt, device.key     the device's own certificate and private key
//   deploy.json                { "enabled", "port", "designers": [ { "fingerprint", "name", "added" } ] }
//   incoming.hmlproj           a project that was received and is being installed
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "deploy_link.hpp"

namespace hml {

// The data folder of this device: `given` (--data-dir) or <folder of the executable>/keys.
std::string device_data_dir(const std::string& given = "");

struct DeployDesigner {
    std::string fingerprint, name, added;      // added: "YYYY-MM-DD HH:MM"
};

class DeployService {
public:
    static constexpr uint64_t kMaxBytes = 512ull * 1024 * 1024;

    DeployService();
    ~DeployService();
    DeployService(const DeployService&) = delete;
    DeployService& operator=(const DeployService&) = delete;

    // Where the device keeps its keys and settings; reads deploy.json. "" = not available
    // (the page says so). Does not start anything.
    void open(const std::string& data_dir);
    const std::string& data_dir() const { return dir_; }
    bool available() const { return !dir_.empty(); }

    // ---- receiving projects: the setting is kept (deploy.json)
    bool enabled() const { return enabled_; }
    int  configured_port() const { return port_cfg_; }
    std::string set_enabled(bool on);                // "" or why not; starts / stops listening
    std::string set_port(int port);
    bool start(std::string* error = nullptr);        // listen if enabled (called once at startup)
    void stop();
    bool listening() const { return listening_; }
    int  port() const { return port_; }              // the port it listens on (0 = it does not)
    std::string fingerprint();                       // of the device's certificate (made when first asked for)

    // ---- the designers that may send
    std::vector<DeployDesigner> designers() const;
    std::string remove(const std::string& fingerprint);
    void set_adding(bool on) { adding_ = on; }       // new designers are offered to the operator
    bool adding() const { return adding_; }

    struct Offer { std::string fingerprint, name, from; };
    bool pending(Offer& out) const;                  // a new designer waits for the operator
    std::string accept();                            // "" or why it could not be kept
    void reject();
    void set_offer_timeout(int seconds) { offer_timeout_s_ = seconds; }

    // ---- a project that arrived (UI thread)
    struct Received { std::string file, name, from, designer; };
    bool received(Received& out) const;              // waits to be installed
    // Tell the sender how it went ("" = installed) and forget the file.
    void finish(const std::string& error);
    void set_install_timeout(int seconds) { install_timeout_s_ = seconds; }

private:
    void run();
    void serve(std::intptr_t client, const std::string& from);
    std::string save();
    bool known(const std::string& fingerprint) const;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::string dir_;
    bool enabled_ = false;
    int  port_cfg_ = kDeployDefaultPort;
    std::vector<DeployDesigner> designers_;
    DeployIdentity identity_;
    bool has_identity_ = false;

    std::thread thread_;
    std::atomic<bool> listening_{ false };
    std::atomic<bool> stop_{ false };
    std::atomic<bool> adding_{ false };
    std::atomic<int>  port_{ 0 };
    std::atomic<int>  offer_timeout_s_{ 120 };
    std::atomic<int>  install_timeout_s_{ 90 };
    std::intptr_t listen_sock_ = -1;

    bool  has_offer_ = false;
    Offer offer_;
    enum class Decision { None, Accept, Reject } decision_ = Decision::None;
    std::string accept_error_;

    bool     has_received_ = false, finished_ = false;
    Received received_;
    std::string finish_error_;
};

DeployService& deploy_service();

} // namespace hml
