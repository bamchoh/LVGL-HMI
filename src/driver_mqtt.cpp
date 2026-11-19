// MQTT client driver (MQTT 3.1.1; TLS by default): tag values to a broker, commands from it.
//
// {
//   "type": "mqtt", "host": "broker.example.com", "port": 8883,
//   "tls": true,                    // default; false = plaintext (port 1883), only on a closed network
//   "ca_file": "certs/ca.pem",      // the broker's CA (Linux default: the system's CA bundle)
//   "verify": true,                 // check the broker's certificate and host name (false: warned)
//   "cert_file": "", "key_file": "",          // client certificate (optional)
//   "username": "", "password": "",           // better in project.local.json (System > Driver)
//   "client_id": "hml-pump1", "keepalive_s": 30, "timeout_ms": 5000, "reconnect_ms": 1000,
//   "topic_prefix": "plant/pump1/",
//   "status_topic": "status",       // "online" (retained) on connect, "offline" as the last will
//   "status_tag": "mqtt_online",
//   "qos": 0, "retain": false, "payload": "value",          // defaults of "publish"; payload "json": {"v":..,"ts":..}
//   "publish": [ { "tag": "temp", "topic": "temp", "mode": "change" },        // or "interval" + "interval_ms"
//                { "tag": "state", "qos": 1, "retain": true } ],           // topic default: the tag name
//   "subscribe": [ { "tag": "speed_sp", "topic": "cmd/speed_sp" } ]          // only these tags can be written
// }
//
// A value that arrives for a subscribed tag is checked (a number for numeric tags, true / false /
// 1 / 0 for bools, at most 256 bytes of text, the tag's min / max) and written; anything else is
// refused and logged. While the broker cannot be reached the newest value of each "change"
// topic is kept and sent when the connection is back (older ones are dropped). QoS 1 messages
// that were not acknowledged are sent again after a reconnect.
#include "driver.hpp"
#include "logdb.hpp"
#include "mqtt.hpp"
#include "project.hpp"
#include "tag.hpp"
#include "util.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

namespace hml {
namespace {

struct Pub {
    Tag* tag = nullptr;
    std::string topic;
    bool interval = false;
    uint32_t interval_ms = 1000, last_ms = 0;
    uint8_t qos = 0;
    bool retain = false;
};
struct Sub { Tag* tag = nullptr; std::string topic; };

class MqttDriver final : public Driver {
public:
    explicit MqttDriver(const json& cfg);
    ~MqttDriver() override { stop(); }
    bool ok() const { return ok_; }
    const char* type() const override { return "mqtt"; }
    bool start() override;
    void stop() override;

private:
    std::string payload_of(const Value& v) const;
    bool apply_command(const Sub& s, const std::string& payload);
    bool session();                 // one connection, until it breaks or stop()
    bool send(const MqttBytes& b) { return link_.write(b); }
    void set_online(bool on);
    void thread_main();

    std::string host_;
    uint16_t port_ = 8883;
    MqttTls tls_;
    mqtt::Connect connect_;
    std::string prefix_, status_topic_;
    bool json_payload_ = false;
    uint32_t timeout_ms_ = 5000;
    uint32_t reconnect_ms_ = 1000;              // first wait before connecting again (doubles up to 30 s)
    Tag* status_tag_ = nullptr;
    std::vector<Pub> pubs_;
    std::vector<Sub> subs_;

    MqttLink link_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    int online_ = -1;

    std::mutex mu_;
    std::map<size_t, Value> pending_;                // pub index -> newest value not sent yet
    std::map<uint16_t, MqttBytes> inflight_;         // QoS 1 not acknowledged (worker thread only)
    uint16_t next_id_ = 1;
    bool ok_ = false;
};

std::string resolve(const std::string& p)
{
    if(p.empty()) return p;
    std::filesystem::path f = std::filesystem::u8path(p);
    if(f.is_absolute()) return p;
    return (std::filesystem::u8path(project().info().dir) / f).generic_string();
}

MqttDriver::MqttDriver(const json& cfg)
{
    host_ = jstr(cfg, "host", "");
    if(host_.empty()) { HML_ERROR("mqtt: \"host\" is needed"); return; }
    tls_.enabled = jbool(cfg, "tls", true);
    tls_.verify = jbool(cfg, "verify", true);
    port_ = static_cast<uint16_t>(jnum(cfg, "port", tls_.enabled ? 8883 : 1883));
    tls_.ca_file = resolve(jstr(cfg, "ca_file", ""));
    tls_.cert_file = resolve(jstr(cfg, "cert_file", ""));
    tls_.key_file = resolve(jstr(cfg, "key_file", ""));
#ifndef _WIN32
    if(tls_.enabled && tls_.verify && tls_.ca_file.empty()) {
        for(const char* c : { "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt" })
            if(std::filesystem::exists(c)) { tls_.ca_file = c; break; }
    }
#endif
    if(!tls_.enabled) HML_WARN("mqtt %s: plaintext (\"tls\": false): user name, password and values can be read on the network", host_.c_str());
    else if(!tls_.verify) HML_WARN("mqtt %s: the broker's certificate is not checked (\"verify\": false): anybody on the way can pretend to be the broker", host_.c_str());
    if(!tls_.cert_file.empty() && tls_.key_file.empty()) { HML_ERROR("mqtt: cert_file needs key_file"); return; }

    connect_.client_id = jstr(cfg, "client_id", "hml-" + std::to_string(static_cast<unsigned>(std::hash<std::string>()(project().info().title) % 100000)));
    connect_.keepalive_s = static_cast<uint16_t>(std::clamp(jnum(cfg, "keepalive_s", 30), 5.0, 3600.0));
    if(auto u = jstr_opt(cfg, "username"); u && !u->empty()) { connect_.username = *u; connect_.has_username = true; }
    if(auto pw = jstr_opt(cfg, "password"); pw && !pw->empty()) { connect_.password = *pw; connect_.has_password = true; }
    if(connect_.has_password && !connect_.has_username) HML_WARN("mqtt: a password without a user name is not sent (MQTT 3.1.1)");
    timeout_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "timeout_ms", 5000), 200.0, 60000.0));
    reconnect_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "reconnect_ms", 1000), 100.0, 30000.0));
    prefix_ = jstr(cfg, "topic_prefix", "");
    json_payload_ = iequals(jstr(cfg, "payload", "value"), "json");
    status_topic_ = jstr(cfg, "status_topic", "");
    if(!status_topic_.empty()) {
        status_topic_ = prefix_ + status_topic_;
        if(!mqtt::valid_topic(status_topic_)) { HML_WARN("mqtt: status_topic '%s' is not a valid topic", status_topic_.c_str()); status_topic_.clear(); }
        else { connect_.will_topic = status_topic_; connect_.will_payload = "offline"; connect_.will_retain = true; }
    }
    if(auto st = jstr_opt(cfg, "status_tag"); st && !st->empty()) {
        status_tag_ = tags().find(*st);
        if(!status_tag_) HML_WARN("mqtt: unknown status_tag '%s'", st->c_str());
    }
    const uint8_t qos = static_cast<uint8_t>(std::clamp(jnum(cfg, "qos", 0), 0.0, 1.0));
    const bool retain = jbool(cfg, "retain", false);
    if(const json* pl = jget(cfg, "publish"); pl && pl->is_array()) {
        for(const json& p : *pl) {
            Pub pub;
            std::string tn = p.is_string() ? p.get<std::string>() : jstr(p, "tag", "");
            pub.tag = tags().find(tn);
            if(!pub.tag) { HML_WARN("mqtt: publish: unknown tag '%s'", tn.c_str()); continue; }
            pub.topic = prefix_ + (p.is_object() ? jstr(p, "topic", tn) : tn);
            if(!mqtt::valid_topic(pub.topic)) { HML_WARN("mqtt: publish: '%s' is not a topic (no + #, UTF-8)", pub.topic.c_str()); continue; }
            pub.interval = p.is_object() && iequals(jstr(p, "mode", "change"), "interval");
            pub.interval_ms = static_cast<uint32_t>(std::clamp(p.is_object() ? jnum(p, "interval_ms", 1000) : 1000.0, 100.0, 86400000.0));
            pub.qos = static_cast<uint8_t>(std::clamp(p.is_object() ? jnum(p, "qos", qos) : qos, 0.0, 1.0));
            pub.retain = p.is_object() ? jbool(p, "retain", retain) : retain;
            pubs_.push_back(pub);
        }
    }
    if(const json* sl = jget(cfg, "subscribe"); sl && sl->is_array()) {
        for(const json& sj : *sl) {
            Sub s;
            std::string tn = sj.is_string() ? sj.get<std::string>() : jstr(sj, "tag", "");
            s.tag = tags().find(tn);
            if(!s.tag) { HML_WARN("mqtt: subscribe: unknown tag '%s'", tn.c_str()); continue; }
            s.topic = prefix_ + (sj.is_object() ? jstr(sj, "topic", tn) : tn);
            // exact topics only: every tag that can be written from outside is listed by name
            if(!mqtt::valid_topic(s.topic)) { HML_WARN("mqtt: subscribe: '%s' is not an exact topic (wildcards are not allowed)", s.topic.c_str()); continue; }
            subs_.push_back(s);
        }
    }
    ok_ = true;
}

bool MqttDriver::start()
{
    for(size_t i = 0; i < pubs_.size(); i++) {
        if(pubs_[i].interval) continue;
        pubs_[i].tag->subscribe(SubGroup::Driver, [this, i](Tag&, const Value& v, const void* src) {
            if(src == this) return;                      // (a command we wrote: not sent back)
            std::lock_guard<std::mutex> l(mu_);
            pending_[i] = v;                             // only the newest value per topic
        });
        std::lock_guard<std::mutex> l(mu_);
        pending_[i] = pubs_[i].tag->get();               // the value at start
    }
    running_ = true;
    link_.set_keep_going([this] { return running_.load(); });
    thread_ = std::thread([this] { thread_main(); });
    return true;
}

void MqttDriver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
}

std::string MqttDriver::payload_of(const Value& v) const
{
    std::string val = v.type == TagType::Bool ? (v.num != 0 ? "true" : "false") : v.format();
    if(!json_payload_) return val;
    json o = { { "ts", LogDb::now_ms() } };
    if(v.is_string()) o["v"] = v.str;
    else if(v.type == TagType::Bool) o["v"] = v.num != 0;
    else o["v"] = v.num;
    return o.dump();
}

bool MqttDriver::apply_command(const Sub& s, const std::string& raw)
{
    std::string payload = raw;
    // a JSON object {"v": ...} is taken too (what payload "json" sends)
    if(!payload.empty() && payload.front() == '{') {
        json j = json::parse(payload, nullptr, false);
        if(j.is_object() && j.contains("v")) payload = j["v"].is_string() ? j["v"].get<std::string>() : j["v"].dump();
    }
    Tag* t = s.tag;
    std::string why;
    if(payload.size() > 256) why = "longer than 256 bytes";
    else if(!mqtt::valid_utf8(payload)) why = "not UTF-8 text";
    else if(t->type() == TagType::String) { t->set(Value::string(payload), this); }
    else {
        auto v = Value::parse(t->type(), payload);
        double mn, mx;
        if(t->type() == TagType::Bool && !v) {
            if(iequals(payload, "true") || iequals(payload, "on")) v = Value::number(1, TagType::Bool);
            else if(iequals(payload, "false") || iequals(payload, "off")) v = Value::number(0, TagType::Bool);
        }
        if(!v || !std::isfinite(v->num)) why = "not a " + std::string(t->type() == TagType::Bool ? "bool" : "number");
        else if(t->limits(mn, mx) && (v->num < mn || v->num > mx)) why = v->format() + " is outside " + Value::number(mn).format() + ".." + Value::number(mx).format();
        else t->set(*v, this);
    }
    if(!why.empty()) { HML_WARN("mqtt: '%s' -> %s refused: %s", s.topic.c_str(), t->name().c_str(), why.c_str()); return false; }
    HML_INFO("mqtt: %s = %s (from '%s')", t->name().c_str(), t->get().format().c_str(), s.topic.c_str());
    return true;
}

void MqttDriver::set_online(bool on)
{
    if(online_ == (on ? 1 : 0)) return;
    if(on || online_ == 1) HML_INFO("mqtt %s %s", link_.describe().c_str(), on ? "connected" : "disconnected");
    online_ = on ? 1 : 0;
    if(status_tag_) status_tag_->set_number(on ? 1 : 0, this);
}

bool MqttDriver::session()
{
    if(!link_.open(host_, port_, tls_, timeout_ms_)) { HML_WARN("mqtt: %s", link_.error().c_str()); return false; }
    if(!send(mqtt::connect(connect_))) return false;
    // read packets: a small buffer of what came in, cut into whole packets
    std::vector<uint8_t> in;
    auto next_packet = [&](uint32_t wait_ms, std::optional<mqtt::Packet>& out) -> int {   // 1 packet, 0 none, -1 broken
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
        for(;;) {
            if(in.size() >= 2) {
                size_t len = 0;
                const int lb = mqtt::remaining_length(in.data() + 1, in.size() - 1, &len);
                if(lb < 0) { HML_WARN("mqtt: the broker sent a packet that is too large or broken"); return -1; }
                if(lb > 0 && in.size() >= 1 + static_cast<size_t>(lb) + len) {
                    const size_t total = 1 + static_cast<size_t>(lb) + len;
                    std::string err;
                    out = mqtt::parse(in.data(), total, &err);
                    in.erase(in.begin(), in.begin() + static_cast<long>(total));
                    if(!out) { HML_WARN("mqtt: broken packet from the broker (%s)", err.c_str()); return -1; }
                    return 1;
                }
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
            if(left <= 0 || !running_) return 0;
            uint8_t buf[2048];
            const int n = link_.read(buf, sizeof(buf), static_cast<uint32_t>(std::min<long long>(left, 50)));
            if(n < 0) { HML_WARN("mqtt: %s", link_.error().c_str()); return -1; }
            in.insert(in.end(), buf, buf + n);
            if(in.size() > mqtt::kMaxIncoming + 8) { HML_WARN("mqtt: too much data from the broker"); return -1; }
        }
    };
    std::optional<mqtt::Packet> p;
    int r = next_packet(timeout_ms_, p);
    if(r <= 0 || p->type != mqtt::CONNACK) { HML_WARN("mqtt %s: no CONNACK", link_.describe().c_str()); return false; }
    if(p->return_code != 0) { HML_WARN("mqtt %s: connection refused: %s", link_.describe().c_str(), mqtt::connack_text(p->return_code).c_str()); return false; }
    set_online(true);
    if(!status_topic_.empty()) send(mqtt::publish(status_topic_, "online", 0, true, 0));
    uint16_t sub_id = 0;
    if(!subs_.empty()) {
        std::vector<std::pair<std::string, uint8_t>> topics;
        for(const Sub& s : subs_) topics.emplace_back(s.topic, 1);
        sub_id = next_id_++;
        if(!next_id_) next_id_ = 1;
        if(!send(mqtt::subscribe(sub_id, topics))) return false;
    }
    for(auto& [id, b] : inflight_) {                      // QoS 1 not acknowledged before: again, as duplicates
        b[0] |= 0x08;
        if(!send(b)) return false;
    }
    auto last_send = std::chrono::steady_clock::now(), last_in = last_send;
    bool ping_out = false;
    while(running_) {
        // values to send: changes (the newest of each), then intervals that are due
        std::map<size_t, Value> todo;
        { std::lock_guard<std::mutex> l(mu_); todo.swap(pending_); }
        const uint32_t now_ms = millis();
        for(size_t i = 0; i < pubs_.size(); i++) if(pubs_[i].interval && now_ms - pubs_[i].last_ms >= pubs_[i].interval_ms) { pubs_[i].last_ms = now_ms; todo[i] = pubs_[i].tag->get(); }
        for(auto& [i, v] : todo) {
            const Pub& pb = pubs_[i];
            uint16_t id = 0;
            if(pb.qos) { id = next_id_++; if(!next_id_) next_id_ = 1; }
            MqttBytes b = mqtt::publish(pb.topic, payload_of(v), pb.qos, pb.retain, id);
            if(pb.qos) {
                if(inflight_.size() >= 100) inflight_.erase(inflight_.begin());   // (bounded: the oldest goes)
                inflight_[id] = b;
            }
            if(!send(b)) {
                // not sent: keep the newest value for the next connection
                std::lock_guard<std::mutex> l(mu_);
                for(auto& [k, val] : todo) if(!pending_.count(k)) pending_[k] = val;
                return false;
            }
            last_send = std::chrono::steady_clock::now();
        }
        // what the broker sends
        r = next_packet(50, p);
        if(r < 0) return false;
        const auto t = std::chrono::steady_clock::now();
        if(r > 0) {
            last_in = t;
            if(p->type == mqtt::PUBLISH) {
                for(const Sub& s : subs_) if(s.topic == p->topic) apply_command(s, p->payload);
                if(p->qos == 1 && !send(mqtt::puback(p->packet_id))) return false;
            }
            else if(p->type == mqtt::PUBACK) inflight_.erase(p->packet_id);
            else if(p->type == mqtt::SUBACK && p->packet_id == sub_id) {
                for(size_t i = 0; i < p->granted.size() && i < subs_.size(); i++)
                    if(p->granted[i] == 0x80) HML_WARN("mqtt: the broker refused the subscription to '%s'", subs_[i].topic.c_str());
            }
            else if(p->type == mqtt::PINGRESP) ping_out = false;
        }
        // keep alive: a ping when nothing was sent for half the time; no answer for a whole period = gone
        const auto keep = std::chrono::seconds(connect_.keepalive_s);
        if(t - last_send >= keep / 2 && !ping_out) { if(!send(mqtt::pingreq())) return false; ping_out = true; last_send = t; }
        if(ping_out && t - last_in >= keep) { HML_WARN("mqtt %s: no answer to the keep-alive", link_.describe().c_str()); return false; }
    }
    if(!status_topic_.empty()) send(mqtt::publish(status_topic_, "offline", 0, true, 0));
    send(mqtt::disconnect());
    return true;
}

void MqttDriver::thread_main()
{
    uint32_t backoff = reconnect_ms_;
    while(running_) {
        const bool clean = session();
        const bool was_online = online_ == 1;
        link_.close();
        set_online(false);
        if(!running_ || clean) break;
        if(was_online) backoff = reconnect_ms_;           // it worked: start again from the short wait
        for(uint32_t t = 0; t < backoff && running_; t += 50) sleep_ms(50);
        backoff = std::min<uint32_t>(backoff * 2, 30000);
    }
    set_online(false);
}

} // namespace

std::unique_ptr<Driver> make_mqtt_driver(const json& cfg)
{
    auto d = std::make_unique<MqttDriver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml
