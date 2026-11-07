// OPC UA server driver (open62541): exposes tags as OPC UA variables.
//
// {
//   "type": "opcua_server",
//   "port": 4840,
//   "namespace": "urn:hml:runtime",     // namespace URI of the tag nodes (index is assigned at runtime, usually 2)
//   "folder": "HML",                    // object folder under Objects/ that holds the variables
//   "tags": [ "temp", { "tag": "setpoint", "access": "rw" }, { "tag": "alarm", "access": "ro" } ]
//            // optional; omitted = every tag, read/write
// }
//
// Every tag becomes a variable node  ns=<namespace>;s=<tag name>  under Objects/<folder>:
//   bool -> Boolean, int -> Int64, float -> Double, string -> String.
// Reads are served live from the tag store; client writes are coerced to the tag type.
// The server runs on its own thread; the tag store is thread-safe, so no queueing is needed.
#include "driver.hpp"
#include "tag.hpp"
#include "util.hpp"

#if defined(HML_WITH_OPCUA)

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
#include <atomic>
#include <cstring>
#include <thread>

namespace hml {
namespace {

class OpcUaServerDriver final : public Driver {
public:
    explicit OpcUaServerDriver(const json& cfg);
    ~OpcUaServerDriver() override { stop(); }
    bool ok() const { return ok_; }

    const char* type() const override { return "opcua_server"; }
    bool start() override;
    void stop() override;

private:
    struct Node {
        Tag* tag;
        bool writable;
    };

    static UA_StatusCode on_read(UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* ctx,
                                 UA_Boolean with_timestamp, const UA_NumericRange*, UA_DataValue* out);
    static UA_StatusCode on_write(UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* ctx,
                                  const UA_NumericRange*, const UA_DataValue* in);
    bool add_nodes();
    void thread_main();

    uint16_t    port_ = 4840;
    std::string ns_uri_ = "urn:hml:runtime";
    std::string folder_ = "HML";
    std::vector<std::unique_ptr<Node>> nodes_;   // stable addresses: used as nodeContext
    UA_Server*        server_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> running_{ false };
    bool              ok_ = false;
};

OpcUaServerDriver::OpcUaServerDriver(const json& cfg)
{
    port_ = static_cast<uint16_t>(jnum(cfg, "port", 4840));
    ns_uri_ = jstr(cfg, "namespace", ns_uri_);
    folder_ = jstr(cfg, "folder", folder_);

    const json* list = jget(cfg, "tags");
    if(list && list->is_array()) {
        for(const json& e : *list) {
            std::string name = e.is_string() ? e.get<std::string>() : jstr(e, "tag", "");
            Tag* tag = tags().find(name);
            if(!tag) { HML_WARN("opcua: unknown tag '%s'", name.c_str()); continue; }
            bool writable = !iequals(e.is_object() ? jstr(e, "access", "rw") : "rw", "ro");
            nodes_.push_back(std::make_unique<Node>(Node{ tag, writable }));
        }
    }
    else {
        for(Tag* tag : tags().all()) nodes_.push_back(std::make_unique<Node>(Node{ tag, true }));
    }
    ok_ = true;
}

// ---------------------------------------------------------------- data source callbacks (server thread)

UA_StatusCode OpcUaServerDriver::on_read(UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* ctx,
                                         UA_Boolean with_timestamp, const UA_NumericRange*, UA_DataValue* out)
{
    const Node* node = static_cast<const Node*>(ctx);
    Value v = node->tag->get();
    UA_StatusCode rc;
    switch(node->tag->type()) {
        case TagType::Bool: { UA_Boolean b = v.num != 0; rc = UA_Variant_setScalarCopy(&out->value, &b, &UA_TYPES[UA_TYPES_BOOLEAN]); break; }
        case TagType::Int: { UA_Int64 i = static_cast<UA_Int64>(v.num); rc = UA_Variant_setScalarCopy(&out->value, &i, &UA_TYPES[UA_TYPES_INT64]); break; }
        case TagType::Float: { UA_Double d = v.num; rc = UA_Variant_setScalarCopy(&out->value, &d, &UA_TYPES[UA_TYPES_DOUBLE]); break; }
        default: {
            UA_String s;
            s.length = v.str.size();
            s.data = reinterpret_cast<UA_Byte*>(const_cast<char*>(v.str.data()));
            rc = UA_Variant_setScalarCopy(&out->value, &s, &UA_TYPES[UA_TYPES_STRING]);   // copies the bytes
            break;
        }
    }
    if(rc != UA_STATUSCODE_GOOD) return rc;
    out->hasValue = true;
    if(with_timestamp) { out->hasSourceTimestamp = true; out->sourceTimestamp = UA_DateTime_now(); }
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode OpcUaServerDriver::on_write(UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* ctx,
                                          const UA_NumericRange*, const UA_DataValue* in)
{
    Node* node = static_cast<Node*>(ctx);
    if(!node->writable) return UA_STATUSCODE_BADNOTWRITABLE;
    if(!in->hasValue || !UA_Variant_isScalar(&in->value) || !in->value.type) return UA_STATUSCODE_BADTYPEMISMATCH;

    const UA_Variant& var = in->value;
    const void* p = var.data;
    Value v;
    switch(var.type->typeKind) {
        case UA_DATATYPEKIND_BOOLEAN: v = Value::number(*static_cast<const UA_Boolean*>(p) ? 1 : 0, TagType::Bool); break;
        case UA_DATATYPEKIND_SBYTE:   v = Value::number(*static_cast<const UA_SByte*>(p)); break;
        case UA_DATATYPEKIND_BYTE:    v = Value::number(*static_cast<const UA_Byte*>(p)); break;
        case UA_DATATYPEKIND_INT16:   v = Value::number(*static_cast<const UA_Int16*>(p)); break;
        case UA_DATATYPEKIND_UINT16:  v = Value::number(*static_cast<const UA_UInt16*>(p)); break;
        case UA_DATATYPEKIND_INT32:   v = Value::number(*static_cast<const UA_Int32*>(p)); break;
        case UA_DATATYPEKIND_UINT32:  v = Value::number(*static_cast<const UA_UInt32*>(p)); break;
        case UA_DATATYPEKIND_INT64:   v = Value::number(static_cast<double>(*static_cast<const UA_Int64*>(p))); break;
        case UA_DATATYPEKIND_UINT64:  v = Value::number(static_cast<double>(*static_cast<const UA_UInt64*>(p))); break;
        case UA_DATATYPEKIND_FLOAT:   v = Value::number(*static_cast<const UA_Float*>(p)); break;
        case UA_DATATYPEKIND_DOUBLE:  v = Value::number(*static_cast<const UA_Double*>(p)); break;
        case UA_DATATYPEKIND_STRING: {
            const UA_String* s = static_cast<const UA_String*>(p);
            v = Value::string(std::string(reinterpret_cast<const char*>(s->data), s->length));
            break;
        }
        default: return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    // `this` is not available in a static callback; the node pointer identifies the driver well enough
    // as a write source (the driver never subscribes to tags, so echo suppression is not needed).
    node->tag->set(v, node);
    return UA_STATUSCODE_GOOD;
}

// ---------------------------------------------------------------- server lifecycle (UI thread)

bool OpcUaServerDriver::add_nodes()
{
    UA_UInt16 ns = UA_Server_addNamespace(server_, ns_uri_.c_str());

    UA_ObjectAttributes oattr = UA_ObjectAttributes_default;
    oattr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(folder_.c_str()));
    UA_NodeId folder_id = UA_NODEID_STRING(ns, const_cast<char*>(folder_.c_str()));
    UA_StatusCode rc = UA_Server_addObjectNode(server_, folder_id,
                                               UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                               UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                               UA_QUALIFIEDNAME(ns, const_cast<char*>(folder_.c_str())),
                                               UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                                               oattr, nullptr, nullptr);
    if(rc != UA_STATUSCODE_GOOD) { HML_ERROR("opcua: cannot create folder '%s': %s", folder_.c_str(), UA_StatusCode_name(rc)); return false; }

    UA_DataSource ds;
    ds.read = &OpcUaServerDriver::on_read;
    ds.write = &OpcUaServerDriver::on_write;

    for(auto& node : nodes_) {
        Tag* tag = node->tag;
        char* name = const_cast<char*>(tag->name().c_str());
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), name);
        attr.description = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(type_name(tag->type())));
        // Clients such as UaExpert and asyncua send a SourceTimestamp with every write: allow it.
        attr.accessLevel = UA_ACCESSLEVELMASK_READ;
        if(node->writable) attr.accessLevel |= UA_ACCESSLEVELMASK_WRITE | UA_ACCESSLEVELMASK_TIMESTAMPWRITE | UA_ACCESSLEVELMASK_STATUSWRITE;
        attr.valueRank = UA_VALUERANK_SCALAR;
        switch(tag->type()) {
            case TagType::Bool:  attr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId; break;
            case TagType::Int:   attr.dataType = UA_TYPES[UA_TYPES_INT64].typeId; break;
            case TagType::Float: attr.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId; break;
            default:             attr.dataType = UA_TYPES[UA_TYPES_STRING].typeId; break;
        }
        rc = UA_Server_addDataSourceVariableNode(server_, UA_NODEID_STRING(ns, name), folder_id,
                                                 UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                                 UA_QUALIFIEDNAME(ns, name),
                                                 UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                                                 attr, ds, node.get(), nullptr);
        if(rc != UA_STATUSCODE_GOOD) HML_WARN("opcua: cannot add node '%s': %s", name, UA_StatusCode_name(rc));
    }
    HML_INFO("opcua: %d tags published as ns=%u (%s) under Objects/%s", static_cast<int>(nodes_.size()), ns, ns_uri_.c_str(), folder_.c_str());
    return true;
}

bool OpcUaServerDriver::start()
{
    UA_ServerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_WARNING);
    UA_StatusCode rc = UA_ServerConfig_setMinimal(&config, port_, nullptr);
    if(rc != UA_STATUSCODE_GOOD) { HML_ERROR("opcua: config failed: %s", UA_StatusCode_name(rc)); UA_ServerConfig_clean(&config); return false; }
    UA_LocalizedText_clear(&config.applicationDescription.applicationName);
    config.applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC("en-US", "HML Runtime");

    server_ = UA_Server_newWithConfig(&config);
    if(!server_) { HML_ERROR("opcua: cannot create server"); return false; }
    if(!add_nodes()) { UA_Server_delete(server_); server_ = nullptr; return false; }

    rc = UA_Server_run_startup(server_);
    if(rc != UA_STATUSCODE_GOOD) {
        HML_ERROR("opcua: cannot listen on port %u: %s", port_, UA_StatusCode_name(rc));
        UA_Server_delete(server_);
        server_ = nullptr;
        return false;
    }
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
    HML_INFO("opcua: listening on opc.tcp://0.0.0.0:%u", port_);
    return true;
}

void OpcUaServerDriver::thread_main()
{
    while(running_) UA_Server_run_iterate(server_, true);
}

void OpcUaServerDriver::stop()
{
    if(!running_) return;
    running_ = false;
    if(thread_.joinable()) thread_.join();
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    server_ = nullptr;
}

} // namespace

std::unique_ptr<Driver> make_opcua_server_driver(const json& cfg)
{
    auto d = std::make_unique<OpcUaServerDriver>(cfg);
    if(!d->ok()) return nullptr;
    return d;
}

} // namespace hml

#else // !HML_WITH_OPCUA

namespace hml {
std::unique_ptr<Driver> make_opcua_server_driver(const json&)
{
    HML_WARN("opcua_server: this build has no OPC UA support (configure with -DHML_OPCUA=ON)");
    return nullptr;
}
} // namespace hml

#endif
