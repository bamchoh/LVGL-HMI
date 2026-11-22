// OPC UA client driver: against an open62541 server in the same process, without security and
// with Sign & Encrypt (test certificates in tests/assets/opcua). Grouped by ISO/IEC 25010
// characteristic.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "tag.hpp"
#include "util.hpp"

#if defined(HML_WITH_OPCUA)

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/pki_default.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <atomic>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef HML_TEST_SOURCE_DIR
#  error "HML_TEST_SOURCE_DIR must point at the repository root"
#endif

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

std::string asset(const char* name)
{
    const char* env = std::getenv("HML_TEST_SOURCE_DIR");
    return std::string(env && *env ? env : HML_TEST_SOURCE_DIR) + "/tests/assets/opcua/" + name;
}

UA_ByteString load(const std::string& path)
{
    auto d = read_file(path, 1 << 20);
    REQUIRE(d);
    UA_ByteString b;
    UA_ByteString_allocBuffer(&b, d->size());
    std::memcpy(b.data, d->data(), d->size());
    return b;
}

// ---------------------------------------------------------------- log capture
std::mutex g_mu;
std::vector<std::string> g_log;
void capture(LogLevel, const std::string& m) { std::lock_guard<std::mutex> l(g_mu); g_log.push_back(m); }
struct LogCapture {
    LogCapture() { { std::lock_guard<std::mutex> l(g_mu); g_log.clear(); } set_log_sink(capture); }
    ~LogCapture() { set_log_sink(nullptr); }
    bool has(const std::string& part) const
    {
        std::lock_guard<std::mutex> l(g_mu);
        for(const auto& m : g_log) if(m.find(part) != std::string::npos) return true;
        return false;
    }
};

// ---------------------------------------------------------------- the server
uint16_t s_port = static_cast<uint16_t>(25600 + (millis() % 300));     // (below the ports Linux gives to clients, 32768 ..)

struct ServerOptions {
    const char* cert = "server";       // nullptr: no security at all (None only)
    bool secure_only = true;           // None only for discovery: no session without security
    bool users = false;                // user "op" / "pw-123" only, no anonymous
    bool trust_any_client = false;     // false: only client.der (a real server trusts the clients it knows)
};

class TestServer {
public:
    explicit TestServer(ServerOptions o = {}) : opt_(o)
    {
        for(int attempt = 0; attempt < 10 && !server_; attempt++) {
            port = s_port++;
            if(!start()) { if(server_) { UA_Server_delete(server_); server_ = nullptr; } }
        }
        REQUIRE(server_);
        running_ = true;
        thread_ = std::thread([this] { while(running_) UA_Server_run_iterate(server_, true); });
    }
    ~TestServer() { shutdown(); }
    void shutdown()
    {
        if(!server_) return;
        running_ = false;
        thread_.join();
        UA_Server_run_shutdown(server_);
        UA_Server_delete(server_);
        server_ = nullptr;
    }
    std::string url() const { return "opc.tcp://localhost:" + std::to_string(port); }
    std::string node(const char* name) const { return "ns=" + std::to_string(ns) + ";s=" + name; }
    UA_NodeId id(const char* name) const { return UA_NODEID_STRING(ns, const_cast<char*>(name)); }
    double number(const char* name)
    {
        UA_Variant v;
        UA_Variant_init(&v);
        double r = -9999;
        if(UA_Server_readValue(server_, id(name), &v) == UA_STATUSCODE_GOOD && UA_Variant_isScalar(&v)) {
            if(v.type == &UA_TYPES[UA_TYPES_DOUBLE]) r = *static_cast<UA_Double*>(v.data);
            else if(v.type == &UA_TYPES[UA_TYPES_INT16]) r = *static_cast<UA_Int16*>(v.data);
            else if(v.type == &UA_TYPES[UA_TYPES_INT32]) r = *static_cast<UA_Int32*>(v.data);
            else if(v.type == &UA_TYPES[UA_TYPES_BOOLEAN]) r = *static_cast<UA_Boolean*>(v.data);
        }
        UA_Variant_clear(&v);
        return r;
    }
    std::string text(const char* name)
    {
        UA_Variant v;
        UA_Variant_init(&v);
        std::string r;
        if(UA_Server_readValue(server_, id(name), &v) == UA_STATUSCODE_GOOD && v.type == &UA_TYPES[UA_TYPES_STRING]) {
            auto* s = static_cast<UA_String*>(v.data);
            r.assign(reinterpret_cast<char*>(s->data), s->length);
        }
        UA_Variant_clear(&v);
        return r;
    }
    template <class T> void set(const char* name, T value, int type)
    {
        UA_Variant v;
        UA_Variant_setScalar(&v, &value, &UA_TYPES[type]);
        REQUIRE(UA_Server_writeValue(server_, id(name), v) == UA_STATUSCODE_GOOD);
    }

    uint16_t port = 0;
    UA_UInt16 ns = 0;

private:
    bool start()
    {
        UA_ServerConfig cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.logging = UA_Log_Stdout_new(std::getenv("HML_TEST_UA_SERVER_LOG") ? UA_LOGLEVEL_INFO : UA_LOGLEVEL_FATAL);
        UA_StatusCode rc;
        if(!opt_.cert) rc = UA_ServerConfig_setMinimal(&cfg, port, nullptr);
        else {
            UA_ByteString cert = load(asset((std::string(opt_.cert) + ".der").c_str()));
            UA_ByteString key = load(asset((std::string(opt_.cert) + ".key").c_str()));
            UA_ByteString client = load(asset("client.der"));
            rc = UA_ServerConfig_setDefaultWithSecurityPolicies(&cfg, port, &cert, &key, opt_.trust_any_client ? nullptr : &client,
                                                                opt_.trust_any_client ? 0 : 1, nullptr, 0, nullptr, 0);
            UA_ByteString_clear(&client);
            if(opt_.trust_any_client) {
                cfg.secureChannelPKI.clear(&cfg.secureChannelPKI);
                UA_CertificateVerification_AcceptAll(&cfg.secureChannelPKI);
                cfg.sessionPKI.clear(&cfg.sessionPKI);
                UA_CertificateVerification_AcceptAll(&cfg.sessionPKI);
            }
            // secure only: None is there for finding the endpoints (GetEndpoints), as on real servers
            cfg.securityPolicyNoneDiscoveryOnly = opt_.secure_only;
            UA_ByteString_clear(&cert);
            UA_ByteString_clear(&key);
            UA_String_clear(&cfg.applicationDescription.applicationUri);
            cfg.applicationDescription.applicationUri = UA_STRING_ALLOC("urn:hml:test:server");
            for(size_t i = 0; i < cfg.endpointsSize; i++) {
                UA_String_clear(&cfg.endpoints[i].server.applicationUri);
                cfg.endpoints[i].server.applicationUri = UA_STRING_ALLOC("urn:hml:test:server");
            }
        }
        if(rc != UA_STATUSCODE_GOOD) { UA_ServerConfig_clean(&cfg); return false; }
        if(opt_.users) {
            UA_UsernamePasswordLogin login{ UA_STRING_STATIC("op"), UA_STRING_STATIC("pw-123") };
            cfg.accessControl.clear(&cfg.accessControl);
            UA_AccessControl_default(&cfg, false, nullptr, 1, &login);
        }
        server_ = UA_Server_newWithConfig(&cfg);
        if(!server_) return false;
        ns = UA_Server_addNamespace(server_, "urn:hml:test");
        add("Temp", 21.5, UA_TYPES_DOUBLE);
        add("Count", static_cast<UA_Int16>(5), UA_TYPES_INT16);
        add("Run", static_cast<UA_Boolean>(false), UA_TYPES_BOOLEAN);
        add("Big", static_cast<UA_Int32>(1), UA_TYPES_INT32);
        UA_String name = UA_STRING_STATIC("a");
        add("Name", name, UA_TYPES_STRING);
        return UA_Server_run_startup(server_) == UA_STATUSCODE_GOOD;
    }
    template <class T> void add(const char* name, T value, int type)
    {
        UA_VariableAttributes a = UA_VariableAttributes_default;
        UA_Variant_setScalar(&a.value, &value, &UA_TYPES[type]);
        a.dataType = UA_TYPES[type].typeId;
        a.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
        a.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(name));
        UA_Server_addVariableNode(server_, id(name), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                  UA_QUALIFIEDNAME(ns, const_cast<char*>(name)), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), a, nullptr, nullptr);
    }

    ServerOptions opt_;
    UA_Server* server_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{ false };
};

int start(Runtime& rt, const json& cfg) { json arr = json::array({ cfg }); return rt.drivers.load(&arr); }

bool wait_for(Runtime& rt, const std::function<bool()>& cond, uint32_t ms = 8000)
{
    const uint32_t t0 = millis();
    while(millis() - t0 < ms) {
        rt.pump();
        if(cond()) return true;
        sleep_ms(10);
    }
    rt.pump();
    return cond();
}

json base_cfg(const TestServer& s)
{
    json map = json::array({
        json{ { "tag", "temp" }, { "node", s.node("Temp") } },
        json{ { "tag", "count" }, { "node", s.node("Count") }, { "access", "rw" } },
        json{ { "tag", "run" }, { "node", "nsu=urn:hml:test;s=Run" }, { "access", "rw" } },
        json{ { "tag", "name" }, { "node", s.node("Name") }, { "access", "rw" } },
        json{ { "tag", "big" }, { "node", s.node("Big") } },              // read only (the default)
    });
    return json{ { "type", "opcua_client" }, { "endpoint", s.url() }, { "status_tag", "online" }, { "publish_ms", 50 },
                 { "reconnect_ms", 100 }, { "timeout_ms", 3000 }, { "map", map } };
}

json secure_cfg(const TestServer& s, const char* trusted = "server.der")
{
    json c = base_cfg(s);
    c["server_cert"] = asset(trusted);
    c["cert_file"] = asset("client.der");
    c["key_file"] = asset("client.key");
    c["application_uri"] = "urn:hml:test:client";
    if(const char* pol = std::getenv("HML_TEST_UA_POLICY")) c["policy"] = pol;
    return c;
}

struct Tags {
    Tag *temp, *count, *run, *name, *big, *online;
    explicit Tags(Runtime& rt)
    {
        temp = rt.tag("temp", TagType::Float, 0);
        count = rt.tag("count", TagType::Int, 0);
        run = rt.tag("run", TagType::Bool, 0);
        name = rt.tag("name", "");
        big = rt.tag("big", TagType::Int, 0);
        online = rt.tag("online", TagType::Bool, 0);
    }
};

} // namespace

// ================================================================ functional suitability

TEST_CASE("opcua_client: nodes are read into tags, rw rows are written back, ro rows are not")
{
    LogCapture log;
    TestServer server({ nullptr, false, false });
    Runtime rt;
    Tags t(rt);
    json cfg = base_cfg(server);
    cfg["security"] = "none";
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return t.online->get_number() == 1 && t.temp->get_number() == 21.5 && t.name->get().str == "a"; }));
    CHECK(t.count->get_number() == 5);
    CHECK(log.has("no security"));

    // changes on the server arrive (subscription)
    server.set("Temp", 30.25, UA_TYPES_DOUBLE);
    CHECK(wait_for(rt, [&] { return t.temp->get_number() == 30.25; }));
    // writes: converted to the node's type
    t.count->set_number(-12, nullptr);
    t.run->set_number(1, nullptr);
    t.name->set_string("auto", nullptr);
    CHECK(wait_for(rt, [&] { return server.number("Count") == -12 && server.number("Run") == 1 && server.text("Name") == "auto"; }));
    // a read-only row is never written
    t.big->set_number(77, nullptr);
    rt.pump();
    sleep_ms(300);
    CHECK(server.number("Big") == 1);
}

// ================================================================ security

TEST_CASE("opcua_client: Sign & Encrypt with a trusted server certificate")
{
    LogCapture log;
    TestServer server;                                  // secure only: no None endpoint
    Runtime rt;
    Tags t(rt);
    REQUIRE(start(rt, secure_cfg(server)) == 1);
    REQUIRE(wait_for(rt, [&] { return t.online->get_number() == 1 && t.temp->get_number() == 21.5; }));
    t.count->set_number(9, nullptr);
    CHECK(wait_for(rt, [&] { return server.number("Count") == 9; }));
    CHECK_FALSE(log.has("no security"));
}

TEST_CASE("opcua_client: a server with another certificate is refused")
{
    LogCapture log;
    TestServer server({ "other_server", true, false });  // the same application URI, another key
    Runtime rt;
    Tags t(rt);
    REQUIRE(start(rt, secure_cfg(server)) == 1);
    CHECK(wait_for(rt, [&] { return log.has("cannot connect"); }));
    sleep_ms(300);
    rt.pump();
    CHECK(t.online->get_number() == 0);
    CHECK(t.temp->get_number() == 0);                    // nothing was read from it
}

TEST_CASE("opcua_client: verify false accepts it, with a warning; no trusted certificate is refused")
{
    LogCapture log;
    TestServer server({ "other_server", true, false });
    {
        Runtime rt;
        Tags t(rt);
        json c = secure_cfg(server);
        c["verify"] = false;
        REQUIRE(start(rt, c) == 1);
        CHECK(wait_for(rt, [&] { return t.online->get_number() == 1; }));
        CHECK(log.has("certificate is not checked"));
    }
    Runtime rt;
    Tags t(rt);
    json c = secure_cfg(server);
    c.erase("server_cert");
    CHECK(start(rt, c) == 0);                            // verify without anything to trust: not started
    CHECK(log.has("no server certificate to trust"));
}

TEST_CASE("opcua_client: no security against a secure-only server does not connect")
{
    LogCapture log;
    TestServer server;
    Runtime rt;
    Tags t(rt);
    json c = base_cfg(server);
    c["security"] = "none";
    REQUIRE(start(rt, c) == 1);
    CHECK(wait_for(rt, [&] { return log.has("cannot connect"); }));
    CHECK(t.online->get_number() == 0);
}

TEST_CASE("opcua_client: user name and password; a wrong password is refused and never logged")
{
    LogCapture log;
    TestServer server({ "server", true, true });
    {
        Runtime rt;
        Tags t(rt);
        json c = secure_cfg(server);
        c["username"] = "op";
        c["password"] = "pw-123";
        REQUIRE(start(rt, c) == 1);
        CHECK(wait_for(rt, [&] { return t.online->get_number() == 1 && t.temp->get_number() == 21.5; }));
    }
    Runtime rt;
    Tags t(rt);
    json c = secure_cfg(server);
    c["username"] = "op";
    c["password"] = "wrong-secret";
    REQUIRE(start(rt, c) == 1);
    CHECK(wait_for(rt, [&] { return log.has("cannot connect"); }));
    CHECK(t.online->get_number() == 0);
    CHECK_FALSE(log.has("wrong-secret"));
    CHECK_FALSE(log.has("pw-123"));
}

TEST_CASE("opcua_client: values that do not fit the node's type are not written")
{
    LogCapture log;
    TestServer server({ nullptr, false, false });
    Runtime rt;
    Tags t(rt);
    rt.tag("temp_text", "x");
    json cfg = base_cfg(server);
    cfg["security"] = "none";
    cfg["map"].push_back(json{ { "tag", "temp_text" }, { "node", server.node("Temp") }, { "access", "rw" } });
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return t.online->get_number() == 1; }));
    t.count->set_number(40000, nullptr);                // Int16
    CHECK(wait_for(rt, [&] { return log.has("40000 does not fit Int16"); }));
    CHECK(server.number("Count") == 5);
    tags().find("temp_text")->set_string("hot", nullptr);
    CHECK(wait_for(rt, [&] { return log.has("text for a Double"); }));
    CHECK(server.number("Temp") == 21.5);
}

// ================================================================ reliability

TEST_CASE("opcua_client: the server goes away and comes back")
{
    Runtime rt;
    Tags t(rt);
    auto server = std::make_unique<TestServer>(ServerOptions{ nullptr, false, false });
    const uint16_t port = server->port;
    json cfg = base_cfg(*server);
    cfg["security"] = "none";
    REQUIRE(start(rt, cfg) == 1);
    REQUIRE(wait_for(rt, [&] { return t.online->get_number() == 1; }));
    server.reset();
    CHECK(wait_for(rt, [&] { return t.online->get_number() == 0; }));
    // the same port again
    s_port = port;
    server = std::make_unique<TestServer>(ServerOptions{ nullptr, false, false });
    REQUIRE(server->port == port);
    server->set("Temp", 55.0, UA_TYPES_DOUBLE);
    CHECK(wait_for(rt, [&] { return t.online->get_number() == 1 && t.temp->get_number() == 55; }, 15000));
}

TEST_CASE("opcua_client: no server: the driver stops quickly; a bad configuration is refused")
{
    Runtime rt;
    rt.tag("online", TagType::Bool, 0);
    REQUIRE(start(rt, json{ { "type", "opcua_client" }, { "endpoint", "opc.tcp://127.0.0.1:1" }, { "security", "none" } }) == 1);
    sleep_ms(300);
    const uint32_t t0 = millis();
    rt.drivers.stop_all();
    CHECK(millis() - t0 < 1000);
    CHECK(start(rt, json{ { "type", "opcua_client" }, { "endpoint", "http://x" } }) == 0);
    CHECK(start(rt, json{ { "type", "opcua_client" }, { "endpoint", "opc.tcp://x:4840" }, { "security", "maybe" } }) == 0);
    CHECK(start(rt, json{ { "type", "opcua_client" }, { "endpoint", "opc.tcp://x:4840" }, { "server_cert", "a.der" }, { "cert_file", "c.der" } }) == 0);
}

TEST_CASE("opcua_client: without cert_file the device makes its certificate once, in pki/own")
{
    LogCapture log;
    TestServer server({ "server", true, false, true });   // (the device's new certificate is not known to it)
    Runtime rt;
    const fs::path dir = fs::temp_directory_path() / ("hml_opcua_client_" + std::to_string(millis()));
    fs::create_directories(dir);
    json pj = {
        { "tags", json::array({ json{ { "name", "temp" }, { "type", "float" } }, json{ { "name", "online" }, { "type", "bool" } } }) },
        { "key_management", json{ { "enabled", false } } },
        { "drivers", json::array({ json{ { "type", "opcua_client" }, { "endpoint", server.url() }, { "server_cert", asset("server.der") },
                                         { "status_tag", "online" }, { "publish_ms", 50 }, { "reconnect_ms", 100 },
                                         { "map", json::array({ json{ { "tag", "temp" }, { "node", server.node("Temp") } } }) } } }) } };
    { std::ofstream f(dir / "project.json"); f << pj.dump(); }
    static const bool registered = [] { drivers().register_builtins(); return true; }();   // (main() does this for the runtime)
    (void)registered;
    {
        Project& p = project();                          // (the driver finds pki/ through project())
        REQUIRE(p.load((dir / "project.json").generic_string()));
        REQUIRE(p.start_drivers() == 1);
        set_log_sink(capture);
        CHECK(wait_for(rt, [&] { return tags().find("online")->get_number() == 1 && tags().find("temp")->get_number() == 21.5; }, 30000));
        CHECK(tags().find("temp")->get_number() == 21.5);
        drivers().stop_all();
        p.unload();
    }
    CHECK(fs::exists(dir / "pki" / "own" / "opcua_client.der"));
    CHECK(fs::exists(dir / "pki" / "own" / "opcua_client.key"));
    CHECK(log.has("making this device's certificate"));
    const auto first = read_file((dir / "pki" / "own" / "opcua_client.der").generic_string(), 1 << 20);
    {   // the next start uses the same one
        Project& p = project();
        REQUIRE(p.load((dir / "project.json").generic_string()));
        REQUIRE(p.start_drivers() == 1);
        sleep_ms(200);
        drivers().stop_all();
        p.unload();
    }
    CHECK(read_file((dir / "pki" / "own" / "opcua_client.der").generic_string(), 1 << 20) == first);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

#endif
