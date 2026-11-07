// Tag store: named, typed process variables shared between the UI and the data drivers.
//
//  - Tag::set_*() may be called from any thread.
//  - Subscribers run on the UI thread, from TagStore::dispatch().
//  - Every write carries a "source" pointer so a writer can ignore its own echo.
#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace hml {

enum class TagType { Bool, Int, Float, String };

TagType     type_from_name(const std::string& name, TagType dflt);
const char* type_name(TagType t);

struct Value {
    TagType     type = TagType::Float;
    double      num = 0;      // bool / int / float payload
    std::string str;          // string payload

    static Value number(double n, TagType t = TagType::Float) { Value v; v.type = t; v.num = n; return v; }
    static Value string(std::string s) { Value v; v.type = TagType::String; v.str = std::move(s); return v; }

    bool   is_string() const { return type == TagType::String; }
    double as_number() const;                              // strings are parsed with strtod
    bool   truthy() const;                                 // non-empty string / non-zero number
    // printf-style format with a single conversion (%d %x %.1f %s ...). Empty = sensible default.
    std::string format(const std::string& fmt = {}) const;
    // Parse text into a value of the given type ("true", "12.5", ...).
    static std::optional<Value> parse(TagType type, const std::string& text);
};

// Subscription groups let the UI drop all of its subscriptions on hot reload
// without touching the drivers'.
enum class SubGroup { Driver, UI };

class TagStore;

class Tag {
public:
    using Callback = std::function<void(Tag& tag, const Value& value, const void* source)>;

    const std::string& name() const { return name_; }
    TagType type() const { return type_; }
    bool limits(double& min, double& max) const { min = min_; max = max_; return has_limits_; }

    Value  get() const;
    double get_number() const;

    // Set and mark dirty. The value is coerced to the tag's type and clamped to its limits.
    // Returns true if the stored value changed.
    bool set(const Value& v, const void* source);
    bool set_number(double n, const void* source) { return set(Value::number(n), source); }
    bool set_string(const std::string& s, const void* source) { return set(Value::string(s), source); }

    void subscribe(SubGroup group, Callback cb);

private:
    friend class TagStore;
    struct Subscriber { SubGroup group; Callback cb; };

    Tag(TagStore& store, std::string name, TagType type, bool has_limits, double min, double max);
    Value coerce(const Value& in) const;

    TagStore&   store_;
    std::string name_;
    TagType     type_;
    Value       value_;
    bool        has_limits_;
    double      min_, max_;
    bool        dirty_ = false;          // initial values are pulled by the UI, never pushed to drivers
    const void* last_source_ = nullptr;
    std::vector<Subscriber> subs_;
};

class TagStore {
public:
    Tag* create(const std::string& name, TagType type, const Value* init, bool has_limits, double min, double max);
    Tag* find(const std::string& name) const;
    size_t count() const;
    std::vector<Tag*> all() const;      // in creation order

    void unsubscribe_group(SubGroup group);

    // UI thread: deliver pending changes. With force_ui every tag is delivered to UI
    // subscribers only (used right after screens are (re)built); pending driver
    // notifications are kept for the next normal dispatch.
    void dispatch(bool force_ui);

    void clear();

private:
    friend class Tag;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Tag>> tags_;
};

TagStore& tags();

} // namespace hml
