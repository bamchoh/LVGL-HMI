// OPC UA server driver tests: start the driver on a local port and talk to it with the
// open62541 client API that ships in the same library.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "util.hpp"

#if defined(HML_WITH_OPCUA)

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include "project.hpp"
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <mbedtls/net_sockets.h>

using namespace hml;
using hmltest::Runtime;

namespace {

// Ports are picked from a private range and advanced when one is busy. (Below 32768: on Linux the
// ports 32768 .. 60999 are given to clients, so a connection of another test could hold one.)
uint16_t s_next_port = static_cast<uint16_t>(24000 + (millis() % 500));

// Start an opcua_server driver, trying a few ports. Returns the port or 0.
uint16_t start_opcua(Runtime& rt, json cfg)
{
    for(int attempt = 0; attempt < 5; attempt++) {
        uint16_t port = s_next_port++;
        cfg["type"] = "opcua_server";
        if(!cfg.contains("security") && !cfg.contains("cert_file")) cfg["security"] = "none";   // (a plain client; the secure ones: below)
        cfg["port"] = port;
        json arr = json::array({ cfg });
        if(rt.drivers.load(&arr) == 1) return port;
    }
    return 0;
}

// A client that only logs warnings (the default INFO logger prints every state change).
UA_Client* quiet_client()
{
    UA_Client* c = UA_Client_new();
    UA_ClientConfig* cfg = UA_Client_getConfig(c);
    cfg->logging = UA_Log_Stdout_new(UA_LOGLEVEL_WARNING);   // set before setDefault(), which keeps an existing logger
    UA_ClientConfig_setDefault(cfg);
    return c;
}

struct Client {
    UA_Client* c = nullptr;
    UA_UInt16  ns = 0;

    explicit Client(uint16_t port, const char* ns_uri = "urn:hml:runtime")
    {
        c = quiet_client();
        std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port);
        UA_StatusCode rc = UA_Client_connect(c, url.c_str());
        REQUIRE_MESSAGE(rc == UA_STATUSCODE_GOOD, UA_StatusCode_name(rc));
        UA_String uri = UA_STRING(const_cast<char*>(ns_uri));
        rc = UA_Client_NamespaceGetIndex(c, &uri, &ns);
        REQUIRE_MESSAGE(rc == UA_STATUSCODE_GOOD, UA_StatusCode_name(rc));
    }
    ~Client()
    {
        UA_Client_disconnect(c);
        UA_Client_delete(c);
    }

    UA_NodeId node(const char* name) const { return UA_NODEID_STRING(ns, const_cast<char*>(name)); }

    // Read a scalar; returns the status, and the value through the out-params when it matches the type.
    UA_StatusCode read(const char* name, UA_Variant* out) { UA_Variant_init(out); return UA_Client_readValueAttribute(c, node(name), out); }

    template <class T>
    UA_StatusCode write(const char* name, T value, const UA_DataType* type)
    {
        UA_Variant v;
        UA_Variant_init(&v);
        UA_Variant_setScalar(&v, &value, type);
        return UA_Client_writeValueAttribute(c, node(name), &v);
    }

    UA_StatusCode write_string(const char* name, const char* text)
    {
        UA_String s = UA_STRING(const_cast<char*>(text));
        return write(name, s, &UA_TYPES[UA_TYPES_STRING]);
    }

    std::string display_name(const char* name)
    {
        UA_LocalizedText t;
        UA_LocalizedText_init(&t);
        if(UA_Client_readDisplayNameAttribute(c, node(name), &t) != UA_STATUSCODE_GOOD) return "<error>";
        std::string out(reinterpret_cast<const char*>(t.text.data), t.text.length);
        UA_LocalizedText_clear(&t);
        return out;
    }
    std::string description(const char* name)
    {
        UA_LocalizedText t;
        UA_LocalizedText_init(&t);
        if(UA_Client_readDescriptionAttribute(c, node(name), &t) != UA_STATUSCODE_GOOD) return "<error>";
        std::string out(reinterpret_cast<const char*>(t.text.data), t.text.length);
        UA_LocalizedText_clear(&t);
        return out;
    }
};

template <class T>
bool scalar_is(const UA_Variant& v, const UA_DataType* type, T expected)
{
    if(!UA_Variant_isScalar(&v) || v.type != type) return false;
    return *static_cast<const T*>(v.data) == expected;
}

} // namespace

TEST_CASE("opcua: tags are published with the matching data types and read live")
{
    Runtime rt;
    Tag* b = rt.tag("b", TagType::Bool, 1);
    Tag* i = rt.tag("i", TagType::Int, 42);
    Tag* f = rt.tag("f", TagType::Float, 2.5);
    Tag* s = rt.tag("s", std::string("hello"));
    uint16_t port = start_opcua(rt, json::object());
    REQUIRE(port != 0);

    Client cl(port);
    CHECK(cl.ns >= 2);
    UA_Variant v;

    REQUIRE(cl.read("b", &v) == UA_STATUSCODE_GOOD);
    CHECK(scalar_is<UA_Boolean>(v, &UA_TYPES[UA_TYPES_BOOLEAN], true));
    UA_Variant_clear(&v);

    REQUIRE(cl.read("i", &v) == UA_STATUSCODE_GOOD);
    CHECK(scalar_is<UA_Int64>(v, &UA_TYPES[UA_TYPES_INT64], 42));
    UA_Variant_clear(&v);

    REQUIRE(cl.read("f", &v) == UA_STATUSCODE_GOOD);
    CHECK(scalar_is<UA_Double>(v, &UA_TYPES[UA_TYPES_DOUBLE], 2.5));
    UA_Variant_clear(&v);

    REQUIRE(cl.read("s", &v) == UA_STATUSCODE_GOOD);
    REQUIRE(v.type == &UA_TYPES[UA_TYPES_STRING]);
    {
        const UA_String* str = static_cast<const UA_String*>(v.data);
        CHECK(std::string(reinterpret_cast<const char*>(str->data), str->length) == "hello");
    }
    UA_Variant_clear(&v);

    SUBCASE("reads follow the tag store") {
        b->set_number(0, nullptr);
        i->set_number(-7, nullptr);
        f->set_number(-0.25, nullptr);
        s->set_string("", nullptr);
        REQUIRE(cl.read("i", &v) == UA_STATUSCODE_GOOD);
        CHECK(scalar_is<UA_Int64>(v, &UA_TYPES[UA_TYPES_INT64], -7));
        UA_Variant_clear(&v);
        REQUIRE(cl.read("f", &v) == UA_STATUSCODE_GOOD);
        CHECK(scalar_is<UA_Double>(v, &UA_TYPES[UA_TYPES_DOUBLE], -0.25));
        UA_Variant_clear(&v);
        REQUIRE(cl.read("b", &v) == UA_STATUSCODE_GOOD);
        CHECK(scalar_is<UA_Boolean>(v, &UA_TYPES[UA_TYPES_BOOLEAN], false));
        UA_Variant_clear(&v);
        REQUIRE(cl.read("s", &v) == UA_STATUSCODE_GOOD);
        CHECK(static_cast<const UA_String*>(v.data)->length == 0);
        UA_Variant_clear(&v);
    }
    SUBCASE("display name, description and the folder node") {
        CHECK(cl.display_name("f") == "f");
        CHECK(cl.description("f") == "float");
        CHECK(cl.description("i") == "int");
        CHECK(cl.description("b") == "bool");
        CHECK(cl.description("s") == "string");
        CHECK(cl.display_name("HML") == "HML");               // default folder
    }
    SUBCASE("unknown nodes") {
        CHECK(cl.read("nope", &v) == UA_STATUSCODE_BADNODEIDUNKNOWN);
        UA_Variant_clear(&v);
    }
}

TEST_CASE("opcua: client writes are coerced to the tag type")
{
    Runtime rt;
    Tag* b = rt.tag("b", TagType::Bool, 0);
    Tag* i = rt.tag("i", TagType::Int, 0, 0, 100);
    Tag* f = rt.tag("f", TagType::Float, 0);
    Tag* s = rt.tag("s", std::string(""));
    uint16_t port = start_opcua(rt, json::object());
    REQUIRE(port != 0);
    Client cl(port);

    CHECK(cl.write<UA_Boolean>("b", true, &UA_TYPES[UA_TYPES_BOOLEAN]) == UA_STATUSCODE_GOOD);
    CHECK(b->get_number() == doctest::Approx(1));

    CHECK(cl.write<UA_Int64>("i", 77, &UA_TYPES[UA_TYPES_INT64]) == UA_STATUSCODE_GOOD);
    CHECK(i->get_number() == doctest::Approx(77));
    CHECK(cl.write<UA_Int64>("i", 500, &UA_TYPES[UA_TYPES_INT64]) == UA_STATUSCODE_GOOD);
    CHECK(i->get_number() == doctest::Approx(100));            // clamped to the tag limits

    CHECK(cl.write<UA_Double>("f", 12.25, &UA_TYPES[UA_TYPES_DOUBLE]) == UA_STATUSCODE_GOOD);
    CHECK(f->get_number() == doctest::Approx(12.25));

    CHECK(cl.write_string("s", "from client") == UA_STATUSCODE_GOOD);
    CHECK(s->get().str == "from client");

    SUBCASE("only the node's exact data type is accepted, even among numeric types") {
        // open62541 validates the variant against the declared DataType before the data source
        // callback runs; Int32 -> Double, Float -> Int64 and friends are refused by the server.
        CHECK(cl.write<UA_Int32>("f", -3, &UA_TYPES[UA_TYPES_INT32]) == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(f->get_number() == doctest::Approx(12.25));
        CHECK(cl.write<UA_UInt16>("i", 9, &UA_TYPES[UA_TYPES_UINT16]) == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(cl.write<UA_Float>("i", 2.6f, &UA_TYPES[UA_TYPES_FLOAT]) == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(i->get_number() == doctest::Approx(100));
        CHECK(cl.write<UA_Byte>("b", 0, &UA_TYPES[UA_TYPES_BYTE]) == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(cl.write<UA_Double>("b", 0.5, &UA_TYPES[UA_TYPES_DOUBLE]) == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(b->get_number() == doctest::Approx(1));
    }
    SUBCASE("the server rejects values whose type does not match the node's data type") {
        // open62541 checks the variant against the declared DataType before calling the
        // data source, so text cannot be written to numeric nodes (and vice versa).
        CHECK(cl.write_string("f", "12.5") == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(f->get_number() == doctest::Approx(12.25));
        CHECK(cl.write_string("b", "true") == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(b->get_number() == doctest::Approx(1));
        CHECK(cl.write<UA_Double>("s", 2.5, &UA_TYPES[UA_TYPES_DOUBLE]) == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(s->get().str == "from client");
    }
    SUBCASE("unsupported value types are rejected") {
        UA_DateTime now = UA_DateTime_now();
        CHECK(cl.write<UA_DateTime>("f", now, &UA_TYPES[UA_TYPES_DATETIME]) == UA_STATUSCODE_BADTYPEMISMATCH);
        CHECK(f->get_number() == doctest::Approx(12.25));
    }
    SUBCASE("writes are visible to tag subscribers with a non-null source") {
        int hits = 0;
        const void* src = nullptr;
        f->subscribe(SubGroup::UI, [&](Tag&, const Value& v, const void* s_) { hits++; src = s_; CHECK(v.num == doctest::Approx(1)); });
        CHECK(cl.write<UA_Double>("f", 1, &UA_TYPES[UA_TYPES_DOUBLE]) == UA_STATUSCODE_GOOD);
        rt.pump();
        CHECK(hits == 1);
        CHECK(src != nullptr);
    }
}

TEST_CASE("opcua: an explicit tag list limits what is published and allows read-only nodes")
{
    Runtime rt;
    Tag* rw = rt.tag("rw", TagType::Int, 1);
    Tag* ro = rt.tag("ro", TagType::Int, 2);
    rt.tag("hidden", TagType::Int, 3);
    json cfg = json::parse(R"({ "namespace": "urn:test:ns", "folder": "Plant",
                                "tags": [ "rw", { "tag": "ro", "access": "ro" }, { "tag": "ghost" }, 42 ] })");
    uint16_t port = start_opcua(rt, cfg);
    REQUIRE(port != 0);
    Client cl(port, "urn:test:ns");
    UA_Variant v;

    REQUIRE(cl.read("rw", &v) == UA_STATUSCODE_GOOD);
    UA_Variant_clear(&v);
    REQUIRE(cl.read("ro", &v) == UA_STATUSCODE_GOOD);
    CHECK(scalar_is<UA_Int64>(v, &UA_TYPES[UA_TYPES_INT64], 2));
    UA_Variant_clear(&v);
    CHECK(cl.read("hidden", &v) == UA_STATUSCODE_BADNODEIDUNKNOWN);
    UA_Variant_clear(&v);
    CHECK(cl.display_name("Plant") == "Plant");
    CHECK(cl.display_name("HML") == "<error>");

    CHECK(cl.write<UA_Int64>("rw", 10, &UA_TYPES[UA_TYPES_INT64]) == UA_STATUSCODE_GOOD);
    CHECK(rw->get_number() == doctest::Approx(10));
    CHECK(cl.write<UA_Int64>("ro", 10, &UA_TYPES[UA_TYPES_INT64]) != UA_STATUSCODE_GOOD);
    CHECK(ro->get_number() == doctest::Approx(2));
}

TEST_CASE("opcua: the server stops cleanly and the port can be reused")
{
    Runtime rt;
    rt.tag("x", TagType::Int, 1);
    uint16_t port = start_opcua(rt, json::object());
    REQUIRE(port != 0);
    { Client cl(port); }
    rt.drivers.stop_all();

    UA_Client* c = quiet_client();
    std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port);
    CHECK(UA_Client_connect(c, url.c_str()) != UA_STATUSCODE_GOOD);
    UA_Client_delete(c);

    json cfg = json{ { "type", "opcua_server" }, { "port", port }, { "security", "none" } };
    json arr = json::array({ cfg });
    CHECK(rt.drivers.load(&arr) == 1);
    { Client cl(port); UA_Variant v; CHECK(cl.read("x", &v) == UA_STATUSCODE_GOOD); UA_Variant_clear(&v); }
}

TEST_CASE("opcua: a port that another program holds is not taken (the server does not pretend to listen)")
{
    Runtime rt;
    rt.tag("x", TagType::Int, 1);
    // another program: a listening socket on IPv4 (open62541 would still bind IPv6 and call that a start)
    mbedtls_net_context other;
    mbedtls_net_init(&other);
    uint16_t port = 0;
    for(int attempt = 0; attempt < 10 && !port; attempt++) {
        const uint16_t p = s_next_port++;
        if(mbedtls_net_bind(&other, "127.0.0.1", std::to_string(p).c_str(), MBEDTLS_NET_PROTO_TCP) == 0) port = p;
    }
    REQUIRE(port != 0);
    json arr = json::array({ json{ { "type", "opcua_server" }, { "port", port }, { "security", "none" } } });
    CHECK(rt.drivers.load(&arr) == 0);
    mbedtls_net_free(&other);
    // free again: it starts
    CHECK(rt.drivers.load(&arr) == 1);
    { Client cl(port); UA_Variant v; CHECK(cl.read("x", &v) == UA_STATUSCODE_GOOD); UA_Variant_clear(&v); }
}

TEST_CASE("the 'opcua' alias creates the same driver")
{
    Runtime rt;
    rt.tag("x", TagType::Int, 1);
    json cfg = json{ { "type", "opcua" }, { "port", s_next_port++ }, { "security", "none" } };
    json arr = json::array({ cfg });
    CHECK(rt.drivers.load(&arr) == 1);
}

namespace {
std::mutex g_sec_mu;
std::vector<std::string> g_sec_log;
void sec_capture(LogLevel, const std::string& m) { std::lock_guard<std::mutex> l(g_sec_mu); g_sec_log.push_back(m); }
struct LogCapture {
    LogCapture() { { std::lock_guard<std::mutex> l(g_sec_mu); g_sec_log.clear(); } set_log_sink(sec_capture); }
    ~LogCapture() { set_log_sink(nullptr); }
    bool has(const std::string& part) const
    {
        std::lock_guard<std::mutex> l(g_sec_mu);
        for(const auto& m : g_sec_log) if(m.find(part) != std::string::npos) return true;
        return false;
    }
};
} // namespace

// ---------------------------------------------------------------- security (Sign & Encrypt, trusted clients, login)

namespace {

std::string ua_asset(const char* name)
{
    const char* env = std::getenv("HML_TEST_SOURCE_DIR");
    return std::string(env && *env ? env : HML_TEST_SOURCE_DIR) + "/tests/assets/opcua/" + name;
}

UA_ByteString ua_load(const std::string& path)
{
    UA_ByteString b = UA_BYTESTRING_NULL;
    auto data = read_file(path, 1 << 20);
    REQUIRE(data);
    UA_ByteString_allocBuffer(&b, data->size());
    std::memcpy(b.data, data->data(), data->size());
    return b;
}

// the secure server of these tests: the test certificates, the test client trusted
json secure_cfg(const char* security = "sign_encrypt")
{
    return json{ { "security", security }, { "cert_file", ua_asset("server.der") }, { "key_file", ua_asset("server.key") },
                 { "application_uri", "urn:hml:test:server" }, { "client_certs", json::array({ ua_asset("client.der") }) } };
}

// A client: mode None / Sign / SignAndEncrypt (with the test client's certificate), maybe a login.
// Returns the status of the connection; reads x when connected.
UA_StatusCode connect_as(uint16_t port, UA_MessageSecurityMode mode, const char* user = nullptr, const char* pass = nullptr, bool* read_ok = nullptr,
                         const std::string& server_cert = "")
{
    UA_ClientConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.logging = UA_Log_Stdout_new(UA_LOGLEVEL_FATAL);
    if(mode == UA_MESSAGESECURITYMODE_NONE) {
        UA_ClientConfig_setDefault(&cfg);
        cfg.securityMode = UA_MESSAGESECURITYMODE_NONE;
        cfg.securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#None");
    }
    else {
        UA_ByteString cert = ua_load(ua_asset("client.der")), key = ua_load(ua_asset("client.key")), server = ua_load(server_cert.empty() ? ua_asset("server.der") : server_cert);
        UA_ClientConfig_setDefaultEncryption(&cfg, cert, key, &server, 1, nullptr, 0);
        UA_ByteString_clear(&cert); UA_ByteString_clear(&key); UA_ByteString_clear(&server);
        cfg.securityMode = mode;
        UA_String_clear(&cfg.clientDescription.applicationUri);
        cfg.clientDescription.applicationUri = UA_STRING_ALLOC("urn:hml:test:client");
    }
    if(user) UA_ClientConfig_setAuthenticationUsername(&cfg, user, pass);
    cfg.timeout = 3000;
    UA_Client* c = UA_Client_newWithConfig(&cfg);
    const std::string url = "opc.tcp://localhost:" + std::to_string(port);
    UA_StatusCode rc = UA_Client_connect(c, url.c_str());
    if(rc == UA_STATUSCODE_GOOD && read_ok) {
        UA_UInt16 ns = 0;
        UA_String uri = UA_STRING_STATIC("urn:hml:runtime");
        UA_Variant v;
        UA_Variant_init(&v);
        *read_ok = UA_Client_NamespaceGetIndex(c, &uri, &ns) == UA_STATUSCODE_GOOD
                && UA_Client_readValueAttribute(c, UA_NODEID_STRING(ns, const_cast<char*>("x")), &v) == UA_STATUSCODE_GOOD;
        UA_Variant_clear(&v);
    }
    UA_Client_disconnect(c);
    UA_Client_delete(c);
    return rc;
}

} // namespace

TEST_SUITE("security") {
TEST_CASE("opcua server: Sign & Encrypt by default; None only finds the endpoints; signed-only refused")
{
    Runtime rt;
    rt.tag("x", TagType::Int, 7);
    json cfg = secure_cfg();
    cfg.erase("security");                                       // (the default)
    const uint16_t port = start_opcua(rt, cfg);
    REQUIRE(port != 0);
    bool read = false;
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT, nullptr, nullptr, &read) == UA_STATUSCODE_GOOD);
    CHECK(read);
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_NONE) != UA_STATUSCODE_GOOD);
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGN) != UA_STATUSCODE_GOOD);
}

TEST_CASE("opcua server: \"sign\" takes signed-only clients too")
{
    Runtime rt;
    rt.tag("x", TagType::Int, 7);
    const uint16_t port = start_opcua(rt, secure_cfg("sign"));
    REQUIRE(port != 0);
    bool read = false;
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGN, nullptr, nullptr, &read) == UA_STATUSCODE_GOOD);
    CHECK(read);
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_NONE) != UA_STATUSCODE_GOOD);
}

TEST_CASE("opcua server: a client that is not trusted is refused; with nobody trusted, everybody is")
{
    Runtime rt;
    rt.tag("x", TagType::Int, 7);
    json other = secure_cfg();
    other["client_certs"] = json::array({ ua_asset("other_server.der") });   // somebody else
    uint16_t port = start_opcua(rt, other);
    REQUIRE(port != 0);
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) != UA_STATUSCODE_GOOD);
    rt.drivers.stop_all();

    json nobody = secure_cfg();
    nobody.erase("client_certs");
    port = start_opcua(rt, nobody);
    REQUIRE(port != 0);
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) != UA_STATUSCODE_GOOD);
}

TEST_CASE("opcua server: a login instead of anonymous; the password is not logged; none without a password")
{
    LogCapture log;
    Runtime rt;
    rt.tag("x", TagType::Int, 7);
    json cfg = secure_cfg();
    cfg["username"] = "op";
    cfg["password"] = "pw-secret-123";                           // (from project.local.json on a device)
    const uint16_t port = start_opcua(rt, cfg);
    REQUIRE(port != 0);
    bool read = false;
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT, "op", "pw-secret-123", &read) == UA_STATUSCODE_GOOD);
    CHECK(read);
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT, "op", "wrong") != UA_STATUSCODE_GOOD);
    CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) != UA_STATUSCODE_GOOD);       // anonymous
    CHECK_FALSE(log.has("pw-secret-123"));
    rt.drivers.stop_all();

    json nopass = secure_cfg();
    nopass["username"] = "op";
    nopass["type"] = "opcua_server";
    nopass["port"] = s_next_port++;
    json arr = json::array({ nopass });
    CHECK(rt.drivers.load(&arr) == 0);
    CHECK(log.has("has no password yet"));
}
}

TEST_SUITE("security") {
TEST_CASE("opcua server: without cert_file the device makes its certificate once, in pki/own; a client that trusts it gets in")
{
    Runtime rt;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("hml_opcua_server_" + std::to_string(millis()));
    fs::create_directories(dir);
    const uint16_t port = s_next_port++;
    json pj = { { "tags", json::array({ json{ { "name", "x" }, { "type", "int" }, { "init", 7 } } }) },
                { "key_management", json{ { "enabled", false } } },
                { "drivers", json::array({ json{ { "type", "opcua_server" }, { "port", port }, { "client_certs", json::array({ ua_asset("client.der") }) } } }) } };
    { std::ofstream f(dir / "project.json"); f << pj.dump(); }
    static const bool registered = [] { drivers().register_builtins(); return true; }();
    (void)registered;
    std::optional<std::string> first;
    for(int run = 0; run < 2; run++) {
        Project& p = project();                                  // (the driver finds pki/ through project())
        REQUIRE(p.load((dir / "project.json").generic_string()));
        REQUIRE(p.start_drivers() == 1);
        const std::string cert = (dir / "pki" / "own" / "opcua_server.der").generic_string();
        REQUIRE(fs::exists(cert));
        CHECK(fs::exists(dir / "pki" / "own" / "opcua_server.key"));
        bool read = false;
        CHECK(connect_as(port, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT, nullptr, nullptr, &read, cert) == UA_STATUSCODE_GOOD);
        CHECK(read);
        if(run == 0) first = read_file(cert, 1 << 20);
        else CHECK(read_file(cert, 1 << 20) == first);           // the same one again
        drivers().stop_all();
        p.unload();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
}

#else

using namespace hml;
using hmltest::Runtime;

TEST_CASE("opcua: driver is unavailable in this build")
{
    Runtime rt;
    json arr = json::array({ json{ { "type", "opcua_server" } } });
    CHECK(rt.drivers.load(&arr) == 0);
}

#endif
