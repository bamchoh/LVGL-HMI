#include "texts.hpp"
#include <algorithm>
#include <cctype>
#include "lvgl.h"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {

namespace {
void set_tag(const std::string& name, const std::string& v)
{
    Tag* t = tags().find(name);
    if(!t) t = tags().create(name, TagType::String, nullptr, false, 0, 0);
    if(t) t->set(Value::string(v), nullptr);
}
bool valid_lang(const std::string& s)
{
    if(s.empty() || s.size() > 16) return false;
    for(char c : s) if(!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    return true;
}
} // namespace

void Texts::clear()
{
    tags().unsubscribe_group(SubGroup::Texts);
    langs_.clear();
    lang_.clear();
    texts_.clear();
    gen_++;
}

void Texts::load(const json& project)
{
    clear();
    if(const json* l = jget(project, "languages"); l && l->is_array()) {
        for(const json& x : *l) {
            if(!x.is_string() || !valid_lang(x.get<std::string>())) { HML_WARN("languages: '%s' skipped (letters, digits, - and _)", x.dump().c_str()); continue; }
            if(std::find(langs_.begin(), langs_.end(), x.get<std::string>()) == langs_.end()) langs_.push_back(x.get<std::string>());
        }
    }
    if(const json* t = jget(project, "texts"); t && t->is_object()) {
        for(auto it = t->begin(); it != t->end(); ++it) {
            if(!it.value().is_object()) { HML_WARN("texts: '%s' is not { \"<language>\": \"text\" }", it.key().c_str()); continue; }
            auto& m = texts_[it.key()];
            for(auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
                if(!jt.value().is_string()) continue;
                m[jt.key()] = jt.value().get<std::string>();
                // a language used in the texts but not listed: still usable
                if(langs_.empty() || std::find(langs_.begin(), langs_.end(), jt.key()) == langs_.end()) {
                    if(valid_lang(jt.key()) && jget(project, "languages") == nullptr) langs_.push_back(jt.key());
                }
            }
        }
    }
    std::string start = jstr(project, "language", langs_.empty() ? "" : langs_.front());
    if(!langs_.empty() && std::find(langs_.begin(), langs_.end(), start) == langs_.end()) {
        HML_WARN("language '%s' is not in \"languages\", using '%s'", start.c_str(), langs_.front().c_str());
        start = langs_.front();
    }
    lang_ = start;
    std::string all;
    for(const auto& l : langs_) all += (all.empty() ? "" : ",") + l;
    updating_ = true;
    set_tag("sys.languages", all);
    set_tag("sys.lang", lang_);
    updating_ = false;
    // writing sys.lang switches (a dropdown bound to it, Lua, a "set" action)
    if(Tag* t = tags().find("sys.lang")) {
        t->subscribe(SubGroup::Texts, [this](Tag&, const Value& v, const void*) {
            if(updating_) return;
            if(!set_language(v.format())) { updating_ = true; set_tag("sys.lang", lang_); updating_ = false; }    // (not a language: put it back)
        });
    }
    const auto miss = missing();
    if(!miss.empty()) HML_WARN("texts: %d translation(s) missing, e.g. %s (the first language is used)", static_cast<int>(miss.size()), miss.front().c_str());
}

bool Texts::set_language(const std::string& lang)
{
    if(std::find(langs_.begin(), langs_.end(), lang) == langs_.end()) {
        if(!lang.empty()) HML_WARN("language '%s' is not one of the project's languages", lang.c_str());
        return false;
    }
    if(lang == lang_) return true;
    lang_ = lang;
    gen_++;
    updating_ = true;
    set_tag("sys.lang", lang_);
    updating_ = false;
    HML_INFO("language: %s", lang_.c_str());
    // (later: this may run inside an event of a widget that the rebuild deletes)
    if(on_change_ && lv_is_initialized() && !pending_) {
        pending_ = true;
        lv_async_call([](void*) { texts().pending_ = false; auto fn = texts().on_change_; if(fn) fn(); }, nullptr);
    }
    return true;
}

std::string Texts::get(const std::string& id, const std::string& lang) const
{
    auto it = texts_.find(id);
    if(it == texts_.end()) return "";
    auto jt = it->second.find(lang);
    return jt == it->second.end() ? "" : jt->second;
}

std::string Texts::resolve(const std::string& s) const
{
    if(s.size() < 2 || s[0] != '@') return s;
    if(s[1] == '@') return s.substr(1);                      // "@@" = a literal "@"
    const std::string id = s.substr(1);
    auto it = texts_.find(id);
    if(it == texts_.end()) return s;                         // not an id: the text itself
    auto jt = it->second.find(lang_);
    if(jt != it->second.end() && !jt->second.empty()) return jt->second;
    if(!langs_.empty()) { jt = it->second.find(langs_.front()); if(jt != it->second.end()) return jt->second; }
    return it->second.empty() ? s : it->second.begin()->second;
}

std::vector<std::string> Texts::missing() const
{
    std::vector<std::string> out;
    for(const auto& [id, m] : texts_) for(const auto& l : langs_) {
        auto it = m.find(l);
        if(it == m.end() || it->second.empty()) out.push_back(id + " (" + l + ")");
    }
    return out;
}

void register_language_actions()
{
    ui().register_action("lang.set", [](const std::string& arg) {
        const auto& langs = texts().languages();
        if(arg != "next") { texts().set_language(arg); return; }
        if(langs.empty()) return;
        auto it = std::find(langs.begin(), langs.end(), texts().language());
        texts().set_language(it == langs.end() || it + 1 == langs.end() ? langs.front() : *(it + 1));
    });
}

Texts& texts()
{
    static Texts instance;
    return instance;
}

} // namespace hml
