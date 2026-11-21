// MQTT 3.1.1 for the mqtt driver (driver_mqtt.cpp): the packets (no I/O, so they can be tested
// byte by byte) and the connection (TCP, or TLS through mbedTLS).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hml {

using MqttBytes = std::vector<uint8_t>;

namespace mqtt {

enum Type : uint8_t { CONNECT = 1, CONNACK = 2, PUBLISH = 3, PUBACK = 4, SUBSCRIBE = 8, SUBACK = 9, PINGREQ = 12, PINGRESP = 13, DISCONNECT = 14 };

constexpr size_t kMaxIncoming = 64 * 1024;          // a larger packet from the broker is refused
constexpr size_t kMaxTopic = 512;

struct Connect {
    std::string client_id, username, password;
    bool has_username = false, has_password = false;
    uint16_t keepalive_s = 30;
    bool clean_session = true;
    std::string will_topic, will_payload;            // empty topic = no will
    bool will_retain = true;
    uint8_t will_qos = 0;
};

MqttBytes connect(const Connect& c);
MqttBytes publish(const std::string& topic, const std::string& payload, uint8_t qos, bool retain, uint16_t packet_id, bool dup = false);
MqttBytes subscribe(uint16_t packet_id, const std::vector<std::pair<std::string, uint8_t>>& topics);
MqttBytes puback(uint16_t packet_id);
MqttBytes pingreq();
MqttBytes disconnect();

// The "remaining length" after the first byte: bytes used (1..4) and the value, 0 = more bytes
// needed, -1 = not valid (more than 4 bytes, or above kMaxIncoming).
int remaining_length(const uint8_t* p, size_t have, size_t* value);

struct Packet {
    uint8_t  type = 0, flags = 0;
    // CONNACK
    uint8_t  return_code = 0;
    bool     session_present = false;
    // PUBLISH
    std::string topic, payload;
    uint8_t  qos = 0;
    bool     retain = false;
    // PUBLISH (qos 1), PUBACK, SUBACK
    uint16_t packet_id = 0;
    std::vector<uint8_t> granted;                    // SUBACK
};

// A whole packet (fixed header + body). nullopt and *err when it is not a valid 3.1.1 packet a
// client can receive: unknown type, wrong flags, lengths that do not add up, a topic that is not
// UTF-8 or has wildcards / NUL, QoS 2 or 3.
std::optional<Packet> parse(const uint8_t* p, size_t n, std::string* err = nullptr);

bool valid_utf8(const std::string& s);
// a topic one publishes to (no wildcards, not empty, at most kMaxTopic)
bool valid_topic(const std::string& t);
std::string connack_text(uint8_t code);

} // namespace mqtt

// The connection to the broker.
struct MqttTls {
    bool enabled = true;
    bool verify = true;                    // the broker's certificate against ca_file
    std::string ca_file, cert_file, key_file;
    // without a ca_file: the certificates the operating system trusts (mail, uploads: public
    // servers). Off for MQTT, where the broker is one's own and its CA is named.
    bool system_ca = false;
};

class MqttLink {
public:
    MqttLink();
    ~MqttLink();
    // TCP connect, then the TLS handshake when tls.enabled. false and error() on failure.
    bool open(const std::string& host, uint16_t port, const MqttTls& tls, uint32_t timeout_ms);
    // The TLS handshake on a connection that was opened without TLS (SMTP: STARTTLS). On failure
    // the connection is closed.
    bool start_tls(const MqttTls& tls, uint32_t timeout_ms);
    void close();
    bool is_open() const;
    bool write(const MqttBytes& b);
    // bytes read (>= 1), 0 = nothing within timeout_ms, -1 = closed / error
    int  read(uint8_t* buf, size_t max, uint32_t timeout_ms);
    const std::string& error() const { return err_; }
    std::string describe() const;
    // Asked every 50 ms while connecting and during the TLS handshake: false = give up now
    // (the driver stops).
    void set_keep_going(std::function<bool()> f) { keep_going_ = std::move(f); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string err_;
    std::function<bool()> keep_going_;
};

} // namespace hml
