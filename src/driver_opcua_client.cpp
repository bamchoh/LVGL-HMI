// OPC UA client driver (open62541): nodes of another OPC UA server become tags.
//
// {
//   "type": "opcua_client",
//   "endpoint": "opc.tcp://192.168.0.20:4840",
//   "security": "sign_encrypt",        // default; "sign", or "none" (plaintext: warned)
//   "policy": "",                      // empty = the best one both sides have (e.g. Basic256Sha256), or a name / URI
//   "server_cert": "certs/plc.der",    // the server's certificate (DER / PEM), trusted; more in pki/trusted/
//   "verify": true,                    // check the server's certificate (false: warned, anybody can pretend to be it)
//   "cert_file": "", "key_file": "",   // this client's certificate; empty = pki/own/opcua_client.der (made once)
//   "application_uri": "urn:hml:opcua_client",     // must be the URI in cert_file
//   "username": "", "password": "",    // better in project.local.json (System > Driver)
//   "publish_ms": 500, "timeout_ms": 5000, "reconnect_ms": 1000, "status_tag": "plc_online",
//   "map": [ { "tag": "temp", "node": "ns=2;s=Temp" },
//            { "tag": "sp", "node": "nsu=urn:plc:vars;s=Setpoint", "access": "rw" } ]
// }
//
// Values come by subscription (data changes, sampled every publish_ms). A tag is written to the
// server only when its row says "access": "rw" (default "ro"); the value is converted to the
// node's data type and refused (logged) when it does not fit (an Int16 node and 40000, text for
// a number). The client runs on its own thread; after a lost connection everything is built
// again (session, subscription) after reconnect_ms, doubling up to 30 s.
#include "driver.hpp"
#include "project.hpp"
#include "tag.hpp"
#include "util.hpp"

#if defined(HML_WITH_OPCUA)

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/create_certificate.h>
#include <open62541/plugin/pki_default.h>
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>

namespace hml {
namespace {

namespace fs = std::filesystem;

// open62541's log into the runtime's log: warnings and errors (HML_OPCUA_LOG=info / debug: more,
// for finding out why a connection fails)
UA_LogLevel ua_log_level()
{
    static const UA_LogLevel lvl = [] {
        const char* e = std::getenv("HML_OPCUA_LOG");
        if(e && iequals(e, "debug")) return UA_LOGLEVEL_DEBUG;
        if(e && iequals(e, "info")) return UA_LOGLEVEL_INFO;
        return UA_LOGLEVEL_WARNING;
    }();
    return lvl;
}
// While this thread closes a session on purpose, what the server still answers to the requests
// that were waiting (a ServiceFault for each Publish: "Session no longer valid", "Request with
// unknown RequestId") is no news: a server that answers them, as the specification asks, filled
// the log with warnings at every shutdown. Errors are still shown.
thread_local bool t_closing = false;

void ua_log(void*, UA_LogLevel level, UA_LogCategory, const char* msg, va_list args)
{
    if(level < ua_log_level()) return;
    if(t_closing && level == UA_LOGLEVEL_WARNING && ua_log_level() == UA_LOGLEVEL_WARNING) return;
    char buf[512];
    std::vsnprintf(buf, sizeof(buf), msg, args);
    if(level >= UA_LOGLEVEL_ERROR) HML_ERROR("opcua client: %s", buf);
    else if(level >= UA_LOGLEVEL_WARNING) HML_WARN("opcua client: %s", buf);
    else HML_INFO("opcua client: %s", buf);
}
UA_Logger g_logger{ ua_log, nullptr, nullptr };

// an encrypted private key would make open62541 ask for its password on stdin
UA_StatusCode no_key_password(UA_ClientConfig*, UA_ByteString*) { return UA_STATUSCODE_BADSECURITYCHECKSFAILED; }

struct Bytes {
    UA_ByteString b = UA_BYTESTRING_NULL;
    Bytes() = default;
    Bytes(const Bytes&) = delete;
    Bytes& operator=(const Bytes&) = delete;
    Bytes(Bytes&& o) noexcept : b(o.b) { o.b = UA_BYTESTRING_NULL; }
    ~Bytes() { UA_ByteString_clear(&b); }
    bool load(const std::string& path, std::string* err)
    {
        auto data = read_file(path, 1 << 20, err);
        if(!data) return false;
        UA_ByteString_clear(&b);
        if(UA_ByteString_allocBuffer(&b, data->size()) != UA_STATUSCODE_GOOD) return false;
        std::memcpy(b.data, data->data(), data->size());
        return true;
    }
};

enum class Security { None, Sign, SignEncrypt };

struct Item {
    Tag* tag = nullptr;
    std::string node_text;
    bool writable = false;
    UA_NodeId node = UA_NODEID_NULL;     // resolved at each connection
    const UA_DataType* type = nullptr;   // the node's data type (for writes)
    bool reported = false;               // a problem with the value was logged
};

struct Write { size_t item; Value value; };

class OpcUaClientDriver final : public Driver {
public:
    explicit OpcUaClientDriver(const json& cfg);
    ~OpcUaClientDriver() override { stop(); }
    bool ok() const { return ok_; }
    const char* type() const override { return "opcua_client"; }
    bool start() override;
    void stop() override;

private:
    UA_Client* make_client();
    bool own_certificate(Bytes& cert, Bytes& key);
    bool session(UA_Client* c);
    bool resolve(UA_Client* c, Item& it);
    void on_value(size_t i, const UA_DataValue* v);
    void do_write(UA_Client* c, const Write& w);
    void set_online(bool on);
    void thread_main();
    static void data_change(UA_Client*, UA_UInt32, void*, UA_UInt32, void* ctx, UA_DataValue* v);

    std::string endpoint_, policy_, app_uri_, cert_file_, key_file_, user_, pass_;
    std::vector<std::string> trusted_;
    Security security_ = Security::SignEncrypt;
    bool verify_ = true;
    uint32_t publish_ms_ = 500, timeout_ms_ = 5000, reconnect_ms_ = 1000;
    Tag* status_tag_ = nullptr;
    std::vector<Item> items_;
    struct Ctx { OpcUaClientDriver* self; size_t index; };
    std::vector<Ctx> ctx_;

    std::thread thread_;
    std::atomic<bool> running_{ false };
    int online_ = -1;
    std::mutex mu_;
    std::deque<Write> writes_;
    bool ok_ = false;
};

std::string resolve_path(const std::string& p)
{
    if(p.empty()) return p;
    fs::path f = fs::u8path(p);
    if(f.is_absolute()) return p;
    return (fs::u8path(project().info().dir) / f).generic_string();
}

OpcUaClientDriver::OpcUaClientDriver(const json& cfg)
{
    link_set(false);
    endpoint_ = jstr(cfg, "endpoint", "");
    if(endpoint_.rfind("opc.tcp://", 0) != 0) { HML_ERROR("opcua_client: \"endpoint\" must be opc.tcp://host:port (is '%s')", endpoint_.c_str()); return; }
    const std::string sec = jstr(cfg, "security", "sign_encrypt");
    if(iequals(sec, "none")) security_ = Security::None;
    else if(iequals(sec, "sign")) security_ = Security::Sign;
    else if(iequals(sec, "sign_encrypt") || iequals(sec, "signandencrypt")) security_ = Security::SignEncrypt;
    else { HML_ERROR("opcua_client: security '%s' (none, sign, sign_encrypt)", sec.c_str()); return; }
    policy_ = jstr(cfg, "policy", "");
    if(!policy_.empty() && policy_.find("://") == std::string::npos) policy_ = "http://opcfoundation.org/UA/SecurityPolicy#" + policy_;
    verify_ = jbool(cfg, "verify", true);
    app_uri_ = jstr(cfg, "application_uri", "urn:hml:opcua_client");
    cert_file_ = resolve_path(jstr(cfg, "cert_file", ""));
    key_file_ = resolve_path(jstr(cfg, "key_file", ""));
    if(cert_file_.empty() != key_file_.empty()) { HML_ERROR("opcua_client: cert_file and key_file go together"); return; }
    user_ = jstr(cfg, "username", "");
    pass_ = jstr(cfg, "password", "");
    publish_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "publish_ms", 500), 50.0, 60000.0));
    timeout_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "timeout_ms", 5000), 500.0, 60000.0));
    reconnect_ms_ = static_cast<uint32_t>(std::clamp(jnum(cfg, "reconnect_ms", 1000), 100.0, 30000.0));
    if(auto st = jstr_opt(cfg, "status_tag"); st && !st->empty()) {
        status_tag_ = tags().find(*st);
        if(!status_tag_) HML_WARN("opcua_client: unknown status_tag '%s'", st->c_str());
    }

    if(security_ == Security::None) {
        HML_WARN("opcua_client %s: no security (\"security\": \"none\"): values%s can be read and changed on the network",
                 endpoint_.c_str(), user_.empty() ? "" : ", user name and password");
    }
    else {
        // the certificates the server may show: server_cert (one or a list) and pki/trusted/*
        const json* sc = jget(cfg, "server_cert");
        if(sc && sc->is_string() && !sc->get<std::string>().empty()) trusted_.push_back(resolve_path(sc->get<std::string>()));
        if(sc && sc->is_array()) for(const json& s : *sc) if(s.is_string() && !s.get<std::string>().empty()) trusted_.push_back(resolve_path(s.get<std::string>()));
        std::error_code ec;
        const fs::path dir = fs::u8path(project().info().dir) / "pki" / "trusted";
        for(auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if(it->is_regular_file(ec)) trusted_.push_back(it->path().generic_string());
        if(!verify_) HML_WARN("opcua_client %s: the server's certificate is not checked (\"verify\": false): anybody on the way can pretend to be the server", endpoint_.c_str());
        else if(trusted_.empty()) {
            HML_ERROR("opcua_client %s: no server certificate to trust (\"server_cert\", or a file in pki/trusted/); without one any server would be accepted", endpoint_.c_str());
            return;
        }
    }

    if(const json* map = jget(cfg, "map"); map && map->is_array()) {
        for(const json& row : *map) {
            Item it;
            const std::string tn = jstr(row, "tag", "");
            it.tag = tags().find(tn);
            if(!it.tag) { HML_WARN("opcua_client: unknown tag '%s'", tn.c_str()); continue; }
            it.node_text = jstr(row, "node", "");
            if(it.node_text.empty()) { HML_WARN("opcua_client: '%s' has no \"node\"", tn.c_str()); continue; }
            const std::string acc = jstr(row, "access", "ro");
            it.writable = iequals(acc, "rw") || iequals(acc, "wo");
            items_.push_back(std::move(it));
        }
    }
    ok_ = true;
}

bool OpcUaClientDriver::start()
{
    ctx_.clear();
    for(size_t i = 0; i < items_.size(); i++) ctx_.push_back({ this, i });
    for(size_t i = 0; i < items_.size(); i++) {
        if(!items_[i].writable) continue;
        items_[i].tag->subscribe(SubGroup::Driver, [this, i](Tag&, const Value& v, const void* src) {
            if(src == this) return;                        // (a value that came from the server)
            std::lock_guard<std::mutex> l(mu_);
            if(writes_.size() >= 1000) { HML_WARN("opcua_client: write queue full, '%s' dropped", items_[i].tag->name().c_str()); return; }
            writes_.push_back({ i, v });
        });
    }
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
    return true;
}

void OpcUaClientDriver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
}

void OpcUaClientDriver::set_online(bool on)
{
    link_set(on);
    if(online_ == (on ? 1 : 0)) return;
    if(on || online_ == 1) HML_INFO("opcua_client %s %s", endpoint_.c_str(), on ? "connected" : "disconnected");
    online_ = on ? 1 : 0;
    if(status_tag_) status_tag_->set_number(on ? 1 : 0, this);
}

// This client's application certificate: cert_file / key_file, or one made at the first start and
// kept in pki/own/ (a deploy keeps pki/), so that the server can trust it once.
bool OpcUaClientDriver::own_certificate(Bytes& cert, Bytes& key)
{
    std::string err;
    std::string cf = cert_file_, kf = key_file_;
    if(cf.empty()) {
        const fs::path own = fs::u8path(project().info().dir) / "pki" / "own";
        cf = (own / "opcua_client.der").generic_string();
        kf = (own / "opcua_client.key").generic_string();
        std::error_code ec;
        if(!fs::exists(fs::u8path(cf), ec)) {
            HML_INFO("opcua_client: making this device's certificate (%s)", app_uri_.c_str());
            UA_String subject[] = { UA_STRING_STATIC("O=HML"), UA_STRING_STATIC("CN=HML OPC UA client") };
            const std::string san_uri = "URI:" + app_uri_;
            UA_String san[] = { UA_STRING(const_cast<char*>(san_uri.c_str())) };
            UA_KeyValueMap params = UA_KEYVALUEMAP_NULL;
            UA_UInt16 bits = 2048, days = 3650 * 2;
            UA_KeyValueMap_setScalar(&params, UA_QUALIFIEDNAME(0, const_cast<char*>("key-size-bits")), &bits, &UA_TYPES[UA_TYPES_UINT16]);
            UA_KeyValueMap_setScalar(&params, UA_QUALIFIEDNAME(0, const_cast<char*>("expires-in-days")), &days, &UA_TYPES[UA_TYPES_UINT16]);
            UA_ByteString k = UA_BYTESTRING_NULL, c = UA_BYTESTRING_NULL;
            UA_StatusCode rc = UA_CreateCertificate(&g_logger, subject, 2, san, 1, UA_CERTIFICATEFORMAT_DER, &params, &k, &c);
            UA_KeyValueMap_clear(&params);
            if(rc != UA_STATUSCODE_GOOD) { HML_ERROR("opcua_client: cannot make a certificate: %s", UA_StatusCode_name(rc)); return false; }
            const bool saved = write_file_atomic(cf, std::string(reinterpret_cast<char*>(c.data), c.length), &err)
                            && write_file_atomic(kf, std::string(reinterpret_cast<char*>(k.data), k.length), &err);
            UA_ByteString_clear(&c);
            UA_ByteString_memZero(&k);
            UA_ByteString_clear(&k);
            if(!saved) { HML_ERROR("opcua_client: cannot save the certificate: %s", err.c_str()); return false; }
            HML_INFO("opcua_client: certificate saved to %s; the server has to trust it", cf.c_str());
        }
    }
    if(!cert.load(cf, &err) || !key.load(kf, &err)) { HML_ERROR("opcua_client: %s", err.c_str()); return false; }
    return true;
}

UA_Client* OpcUaClientDriver::make_client()
{
    // the configuration first, then the client from it (the event loop takes the logger then)
    UA_ClientConfig config;
    std::memset(&config, 0, sizeof(config));
    UA_ClientConfig* cc = &config;
    cc->logging = &g_logger;
    cc->privateKeyPasswordCallback = no_key_password;
    UA_StatusCode rc;
    if(security_ == Security::None) {
        rc = UA_ClientConfig_setDefault(cc);
        cc->securityMode = UA_MESSAGESECURITYMODE_NONE;
        cc->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#None");
    }
    else {
        Bytes cert, key;
        if(!own_certificate(cert, key)) { UA_ClientConfig_clear(cc); return nullptr; }
        std::vector<Bytes> trust;
        for(const std::string& t : trusted_) {
            Bytes b;
            std::string err;
            if(!b.load(t, &err)) { HML_WARN("opcua_client: trusted certificate: %s", err.c_str()); continue; }
            trust.push_back(std::move(b));
        }
        if(verify_ && trust.empty()) { HML_ERROR("opcua_client: none of the trusted server certificates could be read"); UA_ClientConfig_clear(cc); return nullptr; }
        std::vector<UA_ByteString> list;
        for(auto& b : trust) list.push_back(b.b);
        rc = UA_ClientConfig_setDefaultEncryption(cc, cert.b, key.b, verify_ ? list.data() : nullptr, verify_ ? list.size() : 0, nullptr, 0);
        if(!verify_) {
            cc->certificateVerification.clear(&cc->certificateVerification);
            UA_CertificateVerification_AcceptAll(&cc->certificateVerification);
        }
        cc->securityMode = security_ == Security::Sign ? UA_MESSAGESECURITYMODE_SIGN : UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        if(!policy_.empty()) cc->securityPolicyUri = UA_STRING_ALLOC(policy_.c_str());
    }
    if(rc != UA_STATUSCODE_GOOD) { HML_ERROR("opcua_client: configuration: %s", UA_StatusCode_name(rc)); UA_ClientConfig_clear(cc); return nullptr; }
    UA_String_clear(&cc->clientDescription.applicationUri);
    cc->clientDescription.applicationUri = UA_STRING_ALLOC(app_uri_.c_str());
    UA_LocalizedText_clear(&cc->clientDescription.applicationName);
    cc->clientDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC("en-US", "HML Runtime");
    if(!user_.empty()) UA_ClientConfig_setAuthenticationUsername(cc, user_.c_str(), pass_.c_str());
    cc->timeout = timeout_ms_;
    // (noReconnect stays off: open62541 also uses its reconnect to move from the None channel it
    // finds the endpoints with to the secure one. A lost session is handled in session().)
    UA_Client* c = UA_Client_newWithConfig(cc);
    if(!c) { HML_ERROR("opcua_client: cannot create the client"); UA_ClientConfig_clear(cc); }
    return c;
}

bool OpcUaClientDriver::resolve(UA_Client* c, Item& it)
{
    UA_NodeId_clear(&it.node);
    it.type = nullptr;
    UA_StatusCode rc;
    if(it.node_text.rfind("nsu=", 0) == 0) {
        UA_ExpandedNodeId en;
        rc = UA_ExpandedNodeId_parse(&en, UA_STRING(const_cast<char*>(it.node_text.c_str())));
        if(rc != UA_STATUSCODE_GOOD) { HML_WARN("opcua_client: '%s' is not a node id", it.node_text.c_str()); return false; }
        UA_UInt16 ns = 0;
        rc = UA_Client_NamespaceGetIndex(c, &en.namespaceUri, &ns);
        if(rc != UA_STATUSCODE_GOOD) { HML_WARN("opcua_client: the server has no namespace for '%s'", it.node_text.c_str()); UA_ExpandedNodeId_clear(&en); return false; }
        UA_NodeId_copy(&en.nodeId, &it.node);
        it.node.namespaceIndex = ns;
        UA_ExpandedNodeId_clear(&en);
    }
    else {
        rc = UA_NodeId_parse(&it.node, UA_STRING(const_cast<char*>(it.node_text.c_str())));
        if(rc != UA_STATUSCODE_GOOD) { HML_WARN("opcua_client: '%s' is not a node id", it.node_text.c_str()); return false; }
    }
    if(it.writable) {
        UA_NodeId dt;
        rc = UA_Client_readDataTypeAttribute(c, it.node, &dt);
        if(rc != UA_STATUSCODE_GOOD) { HML_WARN("opcua_client: '%s': %s", it.node_text.c_str(), UA_StatusCode_name(rc)); return false; }
        it.type = UA_findDataType(&dt);
        UA_NodeId_clear(&dt);
    }
    return true;
}

void OpcUaClientDriver::data_change(UA_Client*, UA_UInt32, void*, UA_UInt32, void* ctx, UA_DataValue* v)
{
    auto* c = static_cast<Ctx*>(ctx);
    c->self->on_value(c->index, v);
}

void OpcUaClientDriver::on_value(size_t i, const UA_DataValue* dv)
{
    Item& it = items_[i];
    auto report = [&](const std::string& why) {
        if(!it.reported) HML_WARN("opcua_client: '%s' -> %s: %s", it.node_text.c_str(), it.tag->name().c_str(), why.c_str());
        it.reported = true;
    };
    if(dv->hasStatus && UA_StatusCode_isBad(dv->status)) { report(UA_StatusCode_name(dv->status)); return; }
    if(!dv->hasValue || !UA_Variant_isScalar(&dv->value)) { report("not a single value (arrays are not supported)"); return; }
    const UA_Variant& v = dv->value;
    const void* d = v.data;
    double n = 0;
    bool num = true;
    switch(v.type->typeKind) {
        case UA_DATATYPEKIND_BOOLEAN: n = *static_cast<const UA_Boolean*>(d) ? 1 : 0; break;
        case UA_DATATYPEKIND_SBYTE:  n = *static_cast<const UA_SByte*>(d); break;
        case UA_DATATYPEKIND_BYTE:   n = *static_cast<const UA_Byte*>(d); break;
        case UA_DATATYPEKIND_INT16:  n = *static_cast<const UA_Int16*>(d); break;
        case UA_DATATYPEKIND_UINT16: n = *static_cast<const UA_UInt16*>(d); break;
        case UA_DATATYPEKIND_INT32:  n = *static_cast<const UA_Int32*>(d); break;
        case UA_DATATYPEKIND_UINT32: n = *static_cast<const UA_UInt32*>(d); break;
        case UA_DATATYPEKIND_INT64:  n = static_cast<double>(*static_cast<const UA_Int64*>(d)); break;
        case UA_DATATYPEKIND_UINT64: n = static_cast<double>(*static_cast<const UA_UInt64*>(d)); break;
        case UA_DATATYPEKIND_FLOAT:  n = *static_cast<const UA_Float*>(d); break;
        case UA_DATATYPEKIND_DOUBLE: n = *static_cast<const UA_Double*>(d); break;
        case UA_DATATYPEKIND_ENUM:   n = *static_cast<const UA_Int32*>(d); break;
        default: num = false;
    }
    std::string text;
    if(!num) {
        if(v.type == &UA_TYPES[UA_TYPES_STRING]) { auto* s = static_cast<const UA_String*>(d); text.assign(reinterpret_cast<const char*>(s->data), s->length); }
        else if(v.type == &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) { auto* s = static_cast<const UA_LocalizedText*>(d); text.assign(reinterpret_cast<const char*>(s->text.data), s->text.length); }
        else { report(std::string("data type ") + v.type->typeName + " is not supported"); return; }
    }
    it.reported = false;
    if(it.tag->type() == TagType::String) it.tag->set_from_device(Value::string(num ? Value::number(n).format() : text), this);
    else if(num) it.tag->set_from_device(Value::number(n), this);
    else if(auto p = Value::parse(it.tag->type(), text)) it.tag->set_from_device(*p, this);
    else report("'" + text + "' is not a number");
}

void OpcUaClientDriver::do_write(UA_Client* c, const Write& w)
{
    Item& it = items_[w.item];
    const UA_DataType* t = it.type;
    std::string why;
    UA_Variant var;
    UA_Variant_init(&var);
    const Value& v = w.value;
    const double n = v.num;
    auto in_range = [&](double lo, double hi) {
        if(v.is_string() || !std::isfinite(n) || n < lo || n > hi || n != std::floor(n)) { why = (v.is_string() ? "'" + v.str + "'" : v.format()) + " does not fit " + t->typeName; return false; }
        return true;
    };
    union { UA_Boolean b; UA_SByte i8; UA_Byte u8; UA_Int16 i16; UA_UInt16 u16; UA_Int32 i32; UA_UInt32 u32; UA_Int64 i64; UA_UInt64 u64; UA_Float f; UA_Double d; } u{};
    UA_String s = UA_STRING_NULL;
    if(!t) why = "unknown data type";
    else switch(t->typeKind) {
        case UA_DATATYPEKIND_BOOLEAN: if(v.is_string()) why = "text for a Boolean"; else { u.b = n != 0; UA_Variant_setScalar(&var, &u.b, t); } break;
        case UA_DATATYPEKIND_SBYTE:  if(in_range(-128, 127)) { u.i8 = static_cast<UA_SByte>(n); UA_Variant_setScalar(&var, &u.i8, t); } break;
        case UA_DATATYPEKIND_BYTE:   if(in_range(0, 255)) { u.u8 = static_cast<UA_Byte>(n); UA_Variant_setScalar(&var, &u.u8, t); } break;
        case UA_DATATYPEKIND_INT16:  if(in_range(-32768, 32767)) { u.i16 = static_cast<UA_Int16>(n); UA_Variant_setScalar(&var, &u.i16, t); } break;
        case UA_DATATYPEKIND_UINT16: if(in_range(0, 65535)) { u.u16 = static_cast<UA_UInt16>(n); UA_Variant_setScalar(&var, &u.u16, t); } break;
        case UA_DATATYPEKIND_INT32:  if(in_range(-2147483648.0, 2147483647.0)) { u.i32 = static_cast<UA_Int32>(n); UA_Variant_setScalar(&var, &u.i32, t); } break;
        case UA_DATATYPEKIND_UINT32: if(in_range(0, 4294967295.0)) { u.u32 = static_cast<UA_UInt32>(n); UA_Variant_setScalar(&var, &u.u32, t); } break;
        case UA_DATATYPEKIND_INT64:  if(in_range(-9.2e18, 9.2e18)) { u.i64 = static_cast<UA_Int64>(n); UA_Variant_setScalar(&var, &u.i64, t); } break;
        case UA_DATATYPEKIND_UINT64: if(in_range(0, 1.8e19)) { u.u64 = static_cast<UA_UInt64>(n); UA_Variant_setScalar(&var, &u.u64, t); } break;
        case UA_DATATYPEKIND_FLOAT:
            if(v.is_string() || !std::isfinite(n) || std::fabs(n) > std::numeric_limits<float>::max()) why = v.format() + " does not fit Float";
            else { u.f = static_cast<UA_Float>(n); UA_Variant_setScalar(&var, &u.f, t); }
            break;
        case UA_DATATYPEKIND_DOUBLE: if(v.is_string()) why = "text for a Double"; else { u.d = n; UA_Variant_setScalar(&var, &u.d, t); } break;
        case UA_DATATYPEKIND_STRING: {
            const std::string text = v.is_string() ? v.str : v.format();
            s = UA_STRING(const_cast<char*>(text.c_str()));
            UA_String copy;
            UA_String_copy(&s, &copy);
            s = copy;
            UA_Variant_setScalar(&var, &s, t);
            break;
        }
        default: why = std::string("data type ") + t->typeName + " cannot be written";
    }
    if(why.empty()) {
        const UA_StatusCode rc = UA_Client_writeValueAttribute(c, it.node, &var);
        if(rc != UA_STATUSCODE_GOOD) why = std::string("the server answered ") + UA_StatusCode_name(rc);
    }
    UA_String_clear(&s);
    if(!why.empty()) HML_WARN("opcua_client: %s -> '%s' not written: %s", it.tag->name().c_str(), it.node_text.c_str(), why.c_str());
}

bool OpcUaClientDriver::session(UA_Client* c)
{
    UA_StatusCode rc = UA_Client_connectAsync(c, endpoint_.c_str());
    const uint32_t t0 = millis();
    UA_SecureChannelState ch;
    UA_SessionState ss;
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    while(rc == UA_STATUSCODE_GOOD && running_) {
        UA_Client_run_iterate(c, 20);
        UA_Client_getState(c, &ch, &ss, &status);
        if(ss == UA_SESSIONSTATE_ACTIVATED) break;
        if(status != UA_STATUSCODE_GOOD) { rc = status; break; }
        if(millis() - t0 > timeout_ms_) { rc = UA_STATUSCODE_BADTIMEOUT; break; }
    }
    if(!running_) return true;
    if(rc != UA_STATUSCODE_GOOD) { HML_WARN("opcua_client %s: cannot connect: %s", endpoint_.c_str(), UA_StatusCode_name(rc)); return false; }

    for(Item& it : items_) resolve(c, it);
    UA_CreateSubscriptionRequest req = UA_CreateSubscriptionRequest_default();
    req.requestedPublishingInterval = publish_ms_;
    UA_CreateSubscriptionResponse resp = UA_Client_Subscriptions_create(c, req, nullptr, nullptr, nullptr);
    if(resp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        HML_WARN("opcua_client %s: no subscription: %s", endpoint_.c_str(), UA_StatusCode_name(resp.responseHeader.serviceResult));
        return false;
    }
    const UA_UInt32 sub = resp.subscriptionId;
    for(size_t i = 0; i < items_.size(); i++) {
        if(UA_NodeId_isNull(&items_[i].node)) continue;
        UA_MonitoredItemCreateRequest mr = UA_MonitoredItemCreateRequest_default(items_[i].node);
        mr.requestedParameters.samplingInterval = publish_ms_;
        UA_MonitoredItemCreateResult r = UA_Client_MonitoredItems_createDataChange(c, sub, UA_TIMESTAMPSTORETURN_SOURCE, mr, &ctx_[i], data_change, nullptr);
        if(r.statusCode != UA_STATUSCODE_GOOD) HML_WARN("opcua_client: '%s': %s", items_[i].node_text.c_str(), UA_StatusCode_name(r.statusCode));
    }
    set_online(true);
    { std::lock_guard<std::mutex> l(mu_); writes_.clear(); }      // (changes made while offline are not sent late)
    while(running_) {
        rc = UA_Client_run_iterate(c, 20);
        UA_Client_getState(c, &ch, &ss, &status);
        if(rc != UA_STATUSCODE_GOOD || ss != UA_SESSIONSTATE_ACTIVATED) {
            HML_WARN("opcua_client %s: connection lost: %s", endpoint_.c_str(), UA_StatusCode_name(rc != UA_STATUSCODE_GOOD ? rc : status));
            return false;
        }
        std::deque<Write> todo;
        { std::lock_guard<std::mutex> l(mu_); todo.swap(writes_); }
        for(const Write& w : todo) do_write(c, w);
    }
    return true;
}

void OpcUaClientDriver::thread_main()
{
    uint32_t backoff = reconnect_ms_;
    while(running_) {
        UA_Client* c = make_client();
        bool clean = false;
        if(c) {
            clean = session(c);
            t_closing = clean || !running_;                  // (not when the connection was lost: that is news)
            UA_Client_disconnect(c);
            UA_Client_delete(c);
            t_closing = false;
        }
        const bool was_online = online_ == 1;
        set_online(false);
        if(!running_ || clean) break;
        if(was_online) backoff = reconnect_ms_;
        for(uint32_t t = 0; t < backoff && running_; t += 50) sleep_ms(50);
        backoff = std::min<uint32_t>(backoff * 2, 30000);
    }
    set_online(false);
}

} // namespace

std::unique_ptr<Driver> make_opcua_client_driver(const json& cfg)
{
    auto d = std::make_unique<OpcUaClientDriver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml

#else

namespace hml {
std::unique_ptr<Driver> make_opcua_client_driver(const json&)
{
    HML_ERROR("opcua_client: this runtime was built without OPC UA (HML_OPCUA=OFF)");
    return nullptr;
}
} // namespace hml

#endif
