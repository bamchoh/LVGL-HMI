// Texts in several languages, and the font they are drawn with.
//
//   "languages": ["ja", "en"],          // the first is the default (and what a missing translation falls back to)
//   "language": "ja",                   // at start (default: the first)
//   "texts": { "title": { "ja": "ポンプ場 1", "en": "Pump station 1" },
//              "start": { "ja": "運転", "en": "Start" } },
//   "font": { "file": "assets/NotoSansJP-subset.otf" }    // TTF / OTF for every text (TinyTTF)
//
// A text "@title" (widget "text", "placeholder", the items of "options", the "labels" of the
// alarm / recipe / trend widgets, alarm "message") is the text with that id in the current
// language. An id that is not in "texts" stays as it is (so "@home" in an old project keeps
// working), "@@" is a literal "@".
//
// The language is the tag sys.lang: writing it (a widget bound to it, Lua, the action
// { "action": "language", "lang": "en" }) builds the screens again in that language; the
// screen shown and the history stay. sys.languages holds the list ("ja,en").
//
// With "font": { "file": ... } every font size comes from that file (any size, not only the
// built-in ones); the built-in Montserrat stays as a fallback for the symbols (LV_SYMBOL_*).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

class Texts {
public:
    // Read "languages", "language", "texts" of the project (and create the sys.lang tags).
    void load(const json& project);
    void clear();

    const std::vector<std::string>& languages() const { return langs_; }
    const std::string& language() const { return lang_; }
    // Switch; false for a language the project does not have. on_change runs (later, from the
    // main loop) when it really changed.
    bool set_language(const std::string& lang);
    void set_on_change(std::function<void()> fn) { on_change_ = std::move(fn); }

    // "@id" -> the text in the current language; anything else is returned as it is.
    std::string resolve(const std::string& s) const;
    bool has(const std::string& id) const { return texts_.count(id) > 0; }
    // the text of an id in a language ("" if there is none)
    std::string get(const std::string& id, const std::string& lang) const;
    // ids without a text in some language: "id (en)"
    std::vector<std::string> missing() const;
    uint64_t generation() const { return gen_; }

private:
    std::vector<std::string> langs_;
    std::string lang_;
    std::map<std::string, std::map<std::string, std::string>> texts_;
    std::function<void()> on_change_;
    uint64_t gen_ = 1;
    bool updating_ = false;
    bool pending_ = false;          // a rebuild is queued (several switches in a row: one rebuild)
};

Texts& texts();
// { "action": "language", "lang": "en" | "next" } = call "lang.set"
void register_language_actions();

} // namespace hml
