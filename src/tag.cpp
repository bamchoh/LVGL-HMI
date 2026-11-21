#include "tag.hpp"
#include "util.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hml {

// ---------------------------------------------------------------- types / values

TagType type_from_name(const std::string& n, TagType dflt)
{
    if(iequals(n, "bool") || iequals(n, "boolean")) return TagType::Bool;
    if(iequals(n, "int") || iequals(n, "integer")) return TagType::Int;
    if(iequals(n, "float") || iequals(n, "real") || iequals(n, "number")) return TagType::Float;
    if(iequals(n, "string") || iequals(n, "text")) return TagType::String;
    return dflt;
}

const char* type_name(TagType t)
{
    switch(t) {
        case TagType::Bool: return "bool";
        case TagType::Int: return "int";
        case TagType::Float: return "float";
        default: return "string";
    }
}

double Value::as_number() const
{
    return is_string() ? std::atof(str.c_str()) : num;
}

bool Value::truthy() const
{
    return is_string() ? !str.empty() : num != 0;
}

namespace {

// A format of a project ("%.1f C", "%d pcs", "%s") as a format that is safe to hand to printf
// with exactly one argument: the first conversion is kept (with its flags; width at most 64,
// precision at most 20; the length modifiers replaced by ours), every other '%' becomes a
// literal one. A conversion we do not format (%n, %p, %*d ...) is shown as text. Returns the
// conversion character, 0 when there is none.
char safe_format(const std::string& fmt, std::string& out)
{
    char conv = 0;
    out.clear();
    for(size_t i = 0; i < fmt.size(); i++) {
        const char c = fmt[i];
        if(c != '%') { out += c; continue; }
        if(i + 1 < fmt.size() && fmt[i + 1] == '%') { out += "%%"; i++; continue; }
        if(conv) { out += "%%"; continue; }                      // a second conversion: there is one value only
        size_t j = i + 1;
        std::string spec = "%";
        while(j < fmt.size() && std::strchr("-+ #0", fmt[j])) spec += fmt[j++];
        auto digits = [&](int most) {
            long v = 0;
            bool any = false;
            while(j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') { if(v < 100000) v = v * 10 + (fmt[j] - '0'); j++; any = true; }
            if(any) spec += std::to_string(std::min<long>(v, most));
        };
        digits(64);
        if(j < fmt.size() && fmt[j] == '.') { spec += '.'; j++; const size_t before = spec.size(); digits(20); if(spec.size() == before) spec += '0'; }
        while(j < fmt.size() && std::strchr("lhjztL", fmt[j])) j++;
        if(j < fmt.size() && std::strchr("diuxXofFeEgGsc", fmt[j])) {
            conv = fmt[j];
            if(std::strchr("diuxXo", conv)) spec += "ll";
            out += spec + conv;
            i = j;
        }
        else out += "%%";                                        // (the rest follows as text)
    }
    return conv;
}

} // namespace

std::string Value::format(const std::string& fmt) const
{
    if(fmt.empty()) {
        switch(type) {
            case TagType::Bool: return num != 0 ? "true" : "false";
            case TagType::Int: return vformat("%lld", static_cast<long long>(num));
            case TagType::Float: return vformat("%g", num);
            default: return str;
        }
    }
    std::string f2;
    const char conv = safe_format(fmt, f2);
    double n = as_number();
    // (a whole-number conversion of what is no number, or of what no integer holds, shows 0)
    if(std::strchr("diuxXoc", conv ? conv : ' ') && !(std::fabs(n) < 9.0e18)) n = 0;
    switch(conv) {
        case 'd': case 'i':
            return vformat(f2.c_str(), static_cast<long long>(std::llround(n)));
        case 'u': case 'x': case 'X': case 'o':
            return vformat(f2.c_str(), static_cast<unsigned long long>(std::llround(n)));
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            return vformat(f2.c_str(), n);
        case 's':
            return vformat(f2.c_str(), (is_string() ? str : format()).c_str());
        case 'c':
            return vformat(f2.c_str(), static_cast<int>(n));
        default:   // no conversion: literal text ("%%" is one '%')
        {
            std::string text;                                // (not through printf: there is nothing to format)
            for(size_t i = 0; i < f2.size(); i++) {
                text += f2[i];
                if(f2[i] == '%' && i + 1 < f2.size() && f2[i + 1] == '%') i++;
            }
            return text;
        }
    }
}

std::optional<Value> Value::parse(TagType type, const std::string& text)
{
    switch(type) {
        case TagType::String:
            return Value::string(text);
        case TagType::Bool:
            if(iequals(text, "true") || iequals(text, "on") || iequals(text, "1")) return Value::number(1, type);
            if(iequals(text, "false") || iequals(text, "off") || iequals(text, "0")) return Value::number(0, type);
            return std::nullopt;
        default: {
            char* end = nullptr;
            double d = std::strtod(text.c_str(), &end);
            if(end == text.c_str()) return std::nullopt;
            return Value::number(d, type);
        }
    }
}

// ---------------------------------------------------------------- Tag

Tag::Tag(TagStore& store, std::string name, TagType type, bool has_limits, double min, double max)
    : store_(store), name_(std::move(name)), type_(type), has_limits_(has_limits), min_(min), max_(max)
{
    value_.type = type;
}

Value Tag::coerce(const Value& in, bool clamp) const
{
    Value out;
    out.type = type_;
    switch(type_) {
        case TagType::String:
            out.str = in.is_string() ? in.str : in.format();
            break;
        case TagType::Bool:
            if(in.is_string()) out.num = (iequals(in.str, "true") || std::atof(in.str.c_str()) != 0) ? 1 : 0;
            else out.num = in.num != 0 ? 1 : 0;
            break;
        case TagType::Int:
        case TagType::Float: {
            double n = in.as_number();
            if(has_limits_ && clamp) {
                if(n < min_) n = min_;
                if(n > max_) n = max_;
            }
            if(type_ == TagType::Int) n = static_cast<double>(std::llround(n));
            out.num = n;
            break;
        }
    }
    return out;
}

Value Tag::get() const
{
    std::lock_guard<std::mutex> lock(store_.mutex_);
    return value_;
}

double Tag::get_number() const
{
    std::lock_guard<std::mutex> lock(store_.mutex_);
    return value_.as_number();
}

bool Tag::set(const Value& v, const void* source) { return store(coerce(v, true), source); }

bool Tag::set_from_device(const Value& v, const void* source)
{
    Value nv = coerce(v, false);
    bool numeric = type_ == TagType::Int || type_ == TagType::Float;
    if(numeric && has_limits_ && (nv.num < min_ || nv.num > max_) && !range_reported_.exchange(true)) {
        HML_WARN("tag '%s': the device holds %s, outside the tag's limits %s..%s (shown as it is; the limits only apply to "
                 "values written from the HMI)",
                 name_.c_str(), nv.format().c_str(), Value::number(min_).format().c_str(), Value::number(max_).format().c_str());
    }
    return store(std::move(nv), source);
}

bool Tag::store(Value nv, const void* source)
{
    std::lock_guard<std::mutex> lock(store_.mutex_);
    bool changed = (type_ == TagType::String) ? value_.str != nv.str : value_.num != nv.num;
    if(changed) {
        value_ = std::move(nv);
        dirty_ = true;
        last_source_ = source;
    }
    return changed;
}

void Tag::subscribe(SubGroup group, Callback cb)
{
    std::lock_guard<std::mutex> lock(store_.mutex_);
    subs_.push_back({ group, std::move(cb) });
}

// ---------------------------------------------------------------- TagStore

Tag* TagStore::create(const std::string& name, TagType type, const Value* init, bool has_limits, double min, double max)
{
    if(name.empty()) return nullptr;
    if(Tag* existing = find(name)) {
        HML_WARN("tag '%s' defined twice, keeping the first", name.c_str());
        return existing;
    }
    std::unique_ptr<Tag> t(new Tag(*this, name, type, has_limits, min, max));
    if(init) t->value_ = t->coerce(*init);
    std::lock_guard<std::mutex> lock(mutex_);
    tags_.push_back(std::move(t));
    index_[name] = tags_.back().get();
    return tags_.back().get();
}

Tag* TagStore::find(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(name);
    return it == index_.end() ? nullptr : it->second;
}

size_t TagStore::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tags_.size();
}

std::vector<Tag*> TagStore::all() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Tag*> out;
    out.reserve(tags_.size());
    for(const auto& t : tags_) out.push_back(t.get());
    return out;
}

void TagStore::unsubscribe_group(SubGroup group)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& t : tags_) {
        auto& subs = t->subs_;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
                                  [group](const Tag::Subscriber& s) { return s.group == group; }),
                   subs.end());
    }
}

void TagStore::dispatch(bool force_ui)
{
    // Snapshot under the lock, deliver outside of it (callbacks may write tags).
    size_t n;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        n = tags_.size();
    }
    for(size_t i = 0; i < n; i++) {
        Tag* t = nullptr;
        Value v;
        const void* src;
        std::vector<Tag::Subscriber> subs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(i >= tags_.size()) break;
            t = tags_[i].get();
            if(!t->dirty_ && !force_ui) continue;
            if(!force_ui) t->dirty_ = false;     // a forced refresh keeps pending driver notifications
            v = t->value_;
            src = force_ui ? nullptr : t->last_source_;
            subs = t->subs_;
        }
        for(const auto& s : subs) {
            // a forced refresh is for the UI only: drivers must not re-write unchanged values
            if(force_ui && s.group != SubGroup::UI) continue;
            s.cb(*t, v, src);
        }
    }
}

void TagStore::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    tags_.clear();
    index_.clear();
}

TagStore& tags()
{
    static TagStore store;
    return store;
}

} // namespace hml
