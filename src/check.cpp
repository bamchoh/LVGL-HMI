#include "check.hpp"
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include "jsonutil.hpp"
#include "src/misc/lv_text_private.h"
#include "util.hpp"

namespace hml {
namespace {

namespace fs = std::filesystem;

// What the designer knows (widget types, their keys, style keys, actions ...), generated from its
// schema.js so that both agree.
const json& schema()
{
    static const json s = json::parse(
#include "check_schema.inc"
    );
    return s;
}

std::set<std::string> str_set(const json& arr)
{
    std::set<std::string> out;
    if(arr.is_array()) for(const json& v : arr) if(v.is_string()) out.insert(v.get<std::string>());
    return out;
}

struct Checker {
    std::vector<CheckProblem> out;
    fs::path dir;
    std::set<std::string> tags, screens, popups, scripts, texts, languages, books;
    std::map<std::string, std::set<std::string>> groups;      // data log group -> its tags (empty = all)
    bool has_texts = false, has_alarms = false;
    std::set<std::string> log_sections;                       // "system" / "operation": the project records that log
    std::string file;                                          // the file being read

    void add(const std::string& where, const std::string& text) { out.push_back({ file, where, text }); }

    // The JSON of a file, or null (and a problem).
    json read(const std::string& rel)
    {
        std::string err;
        auto text = read_file((dir / fs::u8path(rel)).generic_u8string(), 16u << 20, &err);
        if(!text) { out.push_back({ rel, "", "cannot be read (" + err + ")" }); return nullptr; }
        try {
            return json::parse(*text, nullptr, true, true);
        }
        catch(const json::exception& e) {
            out.push_back({ rel, "", std::string("JSON syntax error: ") + e.what() });
            return nullptr;
        }
    }

    bool tag_ok(const std::string& t) const { return tags.count(t) || t.rfind("sys.", 0) == 0; }
    void need_tag(const std::string& where, const std::string& what, const json& v)
    {
        if(!v.is_string()) return;
        const std::string t = v.get<std::string>();
        if(t.empty()) add(where, what + ": no tag");
        else if(!tag_ok(t)) add(where, what + ": unknown tag \"" + t + "\"");
    }
    // "@id" must be an id of "texts" (when the project has texts)
    void text_id(const std::string& where, const std::string& key, const json& v)
    {
        if(!has_texts || !v.is_string()) return;
        const std::string s = v.get<std::string>();
        if(s.size() < 2 || s[0] != '@' || s[1] == '@') return;
        if(!texts.count(s.substr(1))) add(where, key + ": \"" + s + "\" is not an id of \"texts\" (it is shown as it is)");
    }

    // an image of the project: a file in its folder (the runtime shows nothing else)
    void need_image(const std::string& where, const std::string& what, const std::string& src)
    {
        if(src.empty()) return;
        std::string n = src;
        for(char& ch : n) if(ch == '\\') ch = '/';
        std::error_code ec;
        if(n[0] == '/' || (n.size() > 1 && n[1] == ':') || n == ".." || n.rfind("../", 0) == 0 || n.find("/../") != std::string::npos)
            add(where, what + ": \"" + src + "\" is outside the project folder (not shown)");
        else if(!fs::is_regular_file(dir / fs::u8path(n), ec)) add(where, what + ": no file \"" + src + "\"");
        else {
            std::string ext = fs::u8path(n).extension().generic_u8string();
            for(char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if(ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".gif") add(where, what + ": \"" + src + "\" is not a PNG, JPEG, BMP or GIF");
        }
    }

    void check_style(const std::string& where, const std::string& sel, const json& st)
    {
        static const std::set<std::string> known = str_set(schema()["style"]);
        if(!st.is_object()) { add(where, sel + " is not an object"); return; }
        for(auto it = st.begin(); it != st.end(); ++it) {
            if(!known.count(it.key())) add(where, sel + ": unknown style property \"" + it.key() + "\"");
            else if(it.key() == "bg_image" && it.value().is_string()) need_image(where, sel + ".bg_image", it.value().get<std::string>());
        }
    }

    void check_bind(const std::string& where, const json& b)
    {
        static const std::set<std::string> kinds = str_set(schema()["bind"]);
        static const std::set<std::string> conds = str_set(schema()["conditions"]);
        static const std::set<std::string> spec_keys{ "tag", "format", "scale", "offset", "invert", "map", "part", "two_way" };
        if(b.is_string()) { need_tag(where, "bind", b); return; }
        if(!b.is_object()) { add(where, "bind is neither a tag name nor an object"); return; }
        for(auto it = b.begin(); it != b.end(); ++it) {
            const std::string what = "bind." + it.key();
            if(!kinds.count(it.key())) { add(where, "unknown bind \"" + it.key() + "\""); continue; }
            const json& spec = it.value();
            if(spec.is_string()) { need_tag(where, what, spec); continue; }
            if(!spec.is_object()) { add(where, what + " is neither a tag name nor an object"); continue; }
            if(!spec.contains("tag")) add(where, what + ": no tag");
            for(auto k = spec.begin(); k != spec.end(); ++k) {
                if(k.key() == "tag") need_tag(where, what, k.value());
                else if(conds.count(k.key())) { if(k.value().is_object()) need_tag(where, what + "." + k.key(), k.value().value("tag", json(""))); }
                else if(!spec_keys.count(k.key())) add(where, what + ": unknown key \"" + k.key() + "\"");
            }
        }
    }

    void check_actions(const std::string& where, const std::string& ev, const json& acts)
    {
        static const std::set<std::string> anims = str_set(schema()["anims"]);
        const json& known = schema()["actions"];
        for(const json& a : acts.is_array() ? acts : json::array({ acts })) {
            if(!a.is_object()) { add(where, ev + ": an action is an object { \"action\": ... }"); continue; }
            const std::string name = jstr(a, "action", "");
            if(!known.contains(name)) { add(where, ev + ": unknown action \"" + name + "\""); continue; }
            const std::string what = ev + " (" + name + ")";
            std::set<std::string> keys = str_set(known[name]);
            keys.insert("action");
            if(name == "popup") keys.insert("set");
            for(auto k = a.begin(); k != a.end(); ++k) if(!keys.count(k.key())) add(where, what + ": unknown key \"" + k.key() + "\"");
            if(name == "set" || name == "toggle" || name == "inc" || name == "dec") need_tag(where, what, a.value("tag", json("")));
            if(name == "goto" || name == "popup" || (name == "close" && a.contains("screen"))) {
                const std::string s = jstr(a, "screen", "");
                if(s.empty()) { if(name != "close") add(where, what + ": no screen"); }
                else if(!screens.count(s)) add(where, what + ": unknown screen \"" + s + "\"");
                else if(name == "popup" && !popups.count(s)) add(where, what + ": screen \"" + s + "\" is not a popup (\"popup\": true)");
            }
            if(name == "popup") if(const json* set = jget(a, "set"); set && set->is_object())
                for(auto k = set->begin(); k != set->end(); ++k) if(!tag_ok(k.key())) add(where, what + ": set: unknown tag \"" + k.key() + "\"");
            if(const json* an = jget(a, "anim"); an && an->is_string() && !an->get<std::string>().empty() && !anims.count(an->get<std::string>()))
                add(where, what + ": unknown anim \"" + an->get<std::string>() + "\"");
            if(name == "script") {
                const std::string s = jstr(a, "name", "");
                if(s.empty() && !a.contains("code")) add(where, what + ": neither \"name\" nor \"code\"");
                else if(!s.empty() && !scripts.count(s)) add(where, what + ": unknown script \"" + s + "\"");
            }
            if(name == "language") {
                const std::string l = jstr(a, "lang", "");
                if(!l.empty() && l != "next" && !languages.count(l)) add(where, what + ": \"" + l + "\" is not a language of the project");
            }
        }
    }

    void check_animate(const std::string& where, const std::string& type, const json& list)
    {
        const json& A = schema()["animate"];
        static const std::set<std::string> keys = str_set(A["keys"]), props = str_set(A["props"]), presets = str_set(A["presets"]), paths = str_set(A["paths"]);
        for(json a : list.is_array() ? list : json::array({ list })) {
            if(a.is_string()) a = json{ { "preset", a } };
            if(!a.is_object()) { add(where, "animate: an animation is an object { \"prop\": ... }"); continue; }
            for(auto k = a.begin(); k != a.end(); ++k) if(!keys.count(k.key())) add(where, "animate: unknown key \"" + k.key() + "\"");
            const std::string preset = jstr(a, "preset", "");
            if(a.contains("preset") && !presets.count(preset)) { add(where, "animate: unknown preset \"" + preset + "\""); continue; }
            const std::string prop = jstr(a, "prop", preset == "spin" ? "rotation" : preset.empty() ? "" : "opa");
            if(prop.empty()) { add(where, "animate: no \"prop\" (or \"preset\")"); continue; }
            if(!props.count(prop)) { add(where, "animate: unknown prop \"" + prop + "\""); continue; }
            if(preset.empty() && prop != "frames" && !a.contains("to")) add(where, "animate: \"" + prop + "\" needs \"to\"");
            if(prop == "frames") {
                if(type != "image") add(where, "animate: \"frames\" is for image widgets");
                const json* fl = jget(a, "files");
                if(!fl || !fl->is_array() || fl->size() < 2) add(where, "animate: \"frames\" needs \"files\" with two pictures or more");
                else for(const json& f : *fl) if(f.is_string()) need_image(where, "animate.files", f.get<std::string>());
            }
            if(const json* p = jget(a, "path"); p && p->is_string() && !paths.count(p->get<std::string>())) add(where, "animate: unknown path \"" + p->get<std::string>() + "\"");
            const bool flash = prop == "opa" || prop == "bg_color" || prop == "text_color";
            const double time = jnum(a, "time", preset == "blink" ? 500 : preset == "pulse" ? 700 : 1000);
            const bool playback = jbool(a, "playback", preset == "blink" || preset == "pulse");
            if(flash && time < (playback ? 167 : 334))
                add(where, "animate: " + std::to_string(static_cast<int>(time)) + " ms flashes faster than 3 times a second (the runtime slows it down)");
            if(const json* wh = jget(a, "when")) {
                if(wh->is_string()) need_tag(where, "animate.when", *wh);
                else if(wh->is_object()) {
                    need_tag(where, "animate.when", wh->value("tag", json("")));
                    static const std::set<std::string> conds = str_set(schema()["conditions"]);
                    for(auto k = wh->begin(); k != wh->end(); ++k) {
                        if(k.key() == "tag" || k.key() == "invert") continue;
                        if(!conds.count(k.key())) add(where, "animate.when: unknown key \"" + k.key() + "\"");
                        else if(k.value().is_object()) need_tag(where, "animate.when." + k.key(), k.value().value("tag", json("")));
                    }
                }
                else add(where, "animate.when is neither a tag name nor a condition");
            }
        }
    }

    void check_widget(const json& w, const std::string& path, bool screen)
    {
        const json& S = schema();
        static const std::set<std::string> common = str_set(S["common"]), selectors = str_set(S["selectors"]), events = str_set(S["events"]);
        static const std::set<std::string> any{ "type", "name", "children", "bind", "labels", "checked", "checkable", "animate" };
        static const std::set<std::string> screen_keys{ "name", "children", "popup", "dim" };
        if(!w.is_object()) { add(path, "a widget is an object { \"type\": ... }"); return; }
        std::string type = screen ? "" : jstr(w, "type", "");
        if(type == "container") type = "panel";
        const std::string where = screen ? path : path + " (" + (type.empty() ? "?" : type) + ")";
        std::set<std::string> own;
        if(!screen) {
            if(type.empty()) { add(where, "no \"type\""); return; }
            if(!S["types"].contains(type)) { add(where, "unknown widget type \"" + type + "\""); return; }
            own = str_set(S["types"][type]);
            if(type == "log_view") own.insert("widths");                         // (an object: edited as JSON)
            if(type == "meter") own.insert("zones");
            if(type == "table") { own.insert("columns"); own.insert("rows"); }
        }
        for(auto it = w.begin(); it != w.end(); ++it) {
            const std::string& k = it.key();
            const json& v = it.value();
            if(k.empty() || k[0] == '_' || k[0] == '/') continue;                // "_note", "//": comments
            if(selectors.count(k)) check_style(where, k, v);
            else if(events.count(k)) { if(screen) add(where, "unknown key \"" + k + "\""); else check_actions(where, k, v); }
            else if(k == "bind") { if(screen) add(where, "unknown key \"bind\""); else check_bind(where, v); }
            else if(!(common.count(k) || own.count(k) || (screen ? screen_keys.count(k) : any.count(k))))
                add(where, "unknown key \"" + k + "\" (ignored by the runtime)");
        }
        if(screen) return;

        if(const json* an = jget(w, "animate")) check_animate(where, type, *an);
        for(const char* k : { "text", "placeholder" }) if(const json* v = jget(w, k)) text_id(where, k, *v);
        if(const json* o = jget(w, "options"); o && o->is_array()) for(const json& v : *o) text_id(where, "options", v);
        if(const json* l = jget(w, "labels"); l && l->is_object()) for(auto it = l->begin(); it != l->end(); ++it) text_id(where, "labels." + it.key(), it.value());
        if(type == "image") {
            need_image(where, "src", jstr(w, "src", ""));
            // JPEG / BMP: their own size only
            auto own_size = [](const std::string& f) {
                std::string ext = fs::u8path(f).extension().generic_u8string();
                for(char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
            };
            std::string plain = own_size(jstr(w, "src", "")) ? jstr(w, "src", "") : "";
            if(const json* b = jget(w, "bind"); b && b->is_object())
                if(const json* sb = jget(*b, "src"); sb && sb->is_object())
                    if(const json* map = jget(*sb, "map"); map && map->is_object())
                        for(auto it = map->begin(); it != map->end(); ++it) if(it.value().is_string() && own_size(it.value().get<std::string>())) plain = it.value().get<std::string>();
            if(!plain.empty())
                for(const char* k : { "fit", "zoom", "rotation" })
                    if(w.contains(k)) add(where, std::string(k) + ": only for PNG (\"" + plain + "\" is shown in its own size; make the file the size it is shown in, or use a PNG)");
            // pictures by value: "bind": { "src": { "tag": ..., "map": { "0": "a.png", "default": "b.png" } } }
            if(const json* b = jget(w, "bind"); b && b->is_object())
                if(const json* sb = jget(*b, "src"); sb && sb->is_object())
                    if(const json* map = jget(*sb, "map"); map && map->is_object())
                        for(auto it = map->begin(); it != map->end(); ++it) if(it.value().is_string()) need_image(where, "bind.src.map." + it.key(), it.value().get<std::string>());
        }
        if(type == "chart") if(const json* ser = jget(w, "series"); ser && ser->is_array())
            for(const json& s : *ser) need_tag(where, "series", s.is_object() ? s.value("tag", json("")) : s);
        if(type == "trend") {
            const std::string g = jstr(w, "group", "");
            auto it = groups.find(g);
            if(it == groups.end()) add(where, g.empty() ? "no \"group\" (a data log group)" : "unknown data log group \"" + g + "\"");
            else if(const json* tl = jget(w, "tags"); tl && tl->is_array()) {
                for(const json& t : *tl) {
                    const std::string n = t.is_string() ? t.get<std::string>() : t.is_object() ? jstr(t, "tag", "") : "";
                    if(!it->second.empty() && !it->second.count(n)) add(where, "data log group \"" + g + "\" does not record \"" + n + "\"");
                }
            }
        }
        if(type == "recipe_table") {
            const std::string b = jstr(w, "book", "");
            if(!books.count(b) && !(b.empty() && books.size() == 1)) add(where, b.empty() ? "no \"book\" (a recipe book)" : "unknown recipe book \"" + b + "\"");
        }
        if((type == "alarm_list" || type == "alarm_banner") && !has_alarms) add(where, "the project has no \"alarms\"");
        if(type == "table") {
            if(const json* cl = jget(w, "columns"); cl && !cl->is_array()) add(where, "columns: a list is needed");
            if(const json* rw = jget(w, "rows")) {
                if(!rw->is_array()) add(where, "rows: a list of rows is needed");
                else for(size_t r = 0; r < rw->size(); r++) {
                    const json& row = (*rw)[r];
                    const std::string at = "rows[" + std::to_string(r) + "]";
                    if(!row.is_array()) { add(where, at + ": a list of cells is needed"); continue; }
                    for(size_t i = 0; i < row.size(); i++) {
                        const json& cell = row[i];
                        if(!cell.is_object()) continue;
                        const std::string cat = at + "[" + std::to_string(i) + "]";
                        if(!cell.contains("tag")) { add(where, cat + ": a cell is a text or { \"tag\": ... }"); continue; }
                        need_tag(where, cat, cell["tag"]);
                        for(auto k = cell.begin(); k != cell.end(); ++k)
                            if(k.key() != "tag" && k.key() != "format" && k.key() != "map") add(where, cat + ": unknown key \"" + k.key() + "\"");
                    }
                }
            }
        }
        if(type == "log_view") {
            const std::string src = jstr(w, "source", "system");
            if(src == "data") {
                const std::string g = jstr(w, "group", "");
                auto it = groups.find(g);
                if(it == groups.end()) add(where, g.empty() ? "no \"group\" (a data log group)" : "unknown data log group \"" + g + "\"");
                else if(const json* cl = jget(w, "columns"); cl && cl->is_array() && !it->second.empty())
                    for(const json& c : *cl) {
                        const std::string n = c.is_string() ? c.get<std::string>() : "";
                        if(n != "time" && !it->second.count(n)) add(where, "data log group \"" + g + "\" does not record \"" + n + "\"");
                    }
            }
            else if(src == "system" || src == "operation") {
                if(!log_sections.count(src)) add(where, "the project has no \"" + src + "_log\" (nothing is recorded to show)");
                static const std::set<std::string> sys{ "time", "level", "source", "message", "count" }, op{ "time", "user", "screen", "kind", "target", "old", "new" };
                if(const json* cl = jget(w, "columns"); cl && cl->is_array())
                    for(const json& c : *cl) {
                        const std::string n = c.is_string() ? c.get<std::string>() : "";
                        if(!(src == "system" ? sys : op).count(n)) add(where, "the " + src + " log has no column \"" + n + "\"");
                    }
            }
            else add(where, "source: \"" + src + "\" is not system, operation or data");
            if(w.contains("filter_tag")) need_tag(where, "filter_tag", w["filter_tag"]);
        }
    }

    void walk(const json& w, const std::string& path, bool screen)
    {
        check_widget(w, path, screen);
        if(!w.is_object()) return;
        const json* ch = jget(w, "children");
        if(!ch) return;
        if(!ch->is_array()) { add(path, "\"children\" is not an array"); return; }
        for(size_t i = 0; i < ch->size(); i++) walk((*ch)[i], (path.empty() ? std::string() : path + ".") + "children[" + std::to_string(i) + "]", false);
    }
};

} // namespace

std::vector<CheckProblem> check_project(const std::string& path, std::vector<std::string>* screen_names)
{
    Checker c;
    const fs::path file = fs::u8path(path);
    c.dir = file.parent_path();
    const std::string pj = file.filename().generic_u8string();
    json root = c.read(pj);
    if(root.is_null()) return c.out;
    c.file = pj;
    if(!root.is_object()) { c.add("", "not an object"); return c.out; }

    // ---- what the project defines
    if(const json* tl = jget(root, "tags"); tl && tl->is_array()) {
        for(const json& t : *tl) {
            const std::string n = t.is_string() ? t.get<std::string>() : jstr(t, "name", "");
            if(n.empty()) c.add("tags", "a tag without a name");
            else if(!c.tags.insert(n).second) c.add("tags", "tag \"" + n + "\" is defined twice");
        }
    }
    c.languages = str_set(root.value("languages", json::array()));
    if(const json* tx = jget(root, "texts"); tx && tx->is_object()) {
        c.has_texts = true;
        for(auto it = tx->begin(); it != tx->end(); ++it) c.texts.insert(it.key());
    }
    if(const json* sc = jget(root, "scripts"); sc && sc->is_array()) for(const json& s : *sc) c.scripts.insert(jstr(s, "name", ""));
    if(const json* dl = jget(root, "data_log")) if(const json* gs = jget(*dl, "groups"); gs && gs->is_array())
        for(const json& g : *gs) c.groups[jstr(g, "name", "")] = str_set(g.value("tags", json::array()));
    if(const json* rc = jget(root, "recipes")) if(const json* bs = jget(*rc, "books"); bs && bs->is_array())
        for(const json& b : *bs) c.books.insert(jstr(b, "name", ""));
    if(const json* al = jget(root, "alarms")) c.has_alarms = al->is_object() && jget(*al, "items");
    for(const char* n : { "system", "operation" })
        if(const json* sec = jget(root, (std::string(n) + "_log").c_str()); sec && sec->is_object() && jbool(*sec, "enabled", true)) c.log_sections.insert(n);
    if(const json* f = jget(root, "font")) {
        const std::string ff = jstr(*f, "file", "");
        std::error_code ec;
        if(!ff.empty() && !fs::is_regular_file(c.dir / fs::u8path(ff), ec)) c.add("font", "no file \"" + ff + "\"");
    }

    // ---- drivers: the tags of their tables ("map", "items", "publish" ...) and the status tag
    if(const json* dl = jget(root, "drivers"); dl && dl->is_array()) {
        for(size_t i = 0; i < dl->size(); i++) {
            const json& d = (*dl)[i];
            if(!d.is_object()) continue;
            const std::string where = "drivers[" + std::to_string(i) + "] (" + jstr(d, "name", jstr(d, "type", "?")) + ")";
            if(const json* st = jget(d, "status_tag"); st && st->is_string() && !st->get<std::string>().empty()) c.need_tag(where, "status_tag", *st);
            for(auto it = d.begin(); it != d.end(); ++it) {
                if(!it.value().is_array()) continue;
                for(const json& row : it.value()) {
                    if(row.is_object() && row.contains("tag")) c.need_tag(where, it.key(), row["tag"]);
                    else if(row.is_string() && (it.key() == "tags" || it.key() == "publish" || it.key() == "subscribe")) c.need_tag(where, it.key(), row);
                }
            }
        }
    }

    // ---- the screens: read them all first (their names are needed for goto / popup)
    struct Screen { std::string file; json def; };
    std::vector<Screen> screens;
    const json* sl = jget(root, "screens");
    if(!sl || !sl->is_array() || sl->empty()) c.add("screens", "the project has no screens");
    else for(size_t i = 0; i < sl->size(); i++) {
        const json& s = (*sl)[i];
        if(s.is_string()) {
            json def = c.read(s.get<std::string>());
            if(!def.is_null()) screens.push_back({ s.get<std::string>(), std::move(def) });
        }
        else if(s.is_object()) screens.push_back({ pj, s });
        else c.add("screens[" + std::to_string(i) + "]", "neither a file name nor a screen object");
    }
    for(const Screen& s : screens) {
        c.file = s.file;
        if(!s.def.is_object()) { c.add("", "a screen is an object { \"name\": ..., \"children\": [...] }"); continue; }
        const std::string n = jstr(s.def, "name", "");
        if(n.empty()) c.add("", "the screen has no \"name\"");
        else if(!c.screens.insert(n).second) c.add("", "screen name \"" + n + "\" is used twice");
        else if(screen_names) screen_names->push_back(n);
        if(jbool(s.def, "popup", false)) c.popups.insert(n);
    }
    c.file = pj;
    const std::string start = jstr(root, "start_screen", "");
    if(!start.empty() && !c.screens.count(start)) c.add("start_screen", "unknown screen \"" + start + "\"");
    else if(c.popups.count(start)) c.add("start_screen", "\"" + start + "\" is a popup");

    for(const Screen& s : screens) {
        if(!s.def.is_object()) continue;
        c.file = s.file;
        c.walk(s.def, s.file == pj ? "screen \"" + jstr(s.def, "name", "") + "\"" : "", true);
    }
    return c.out;
}

std::string missing_glyphs(const lv_font_t* font, const std::string& text)
{
    std::string out;
    std::set<uint32_t> seen;
    uint32_t i = 0;
    while(i < text.size()) {
        const uint32_t start = i;
        const uint32_t c = lv_text_encoded_next(text.c_str(), &i);
        if(i <= start) break;
        if(c < 0x20 || c == 0x7F || !seen.insert(c).second) continue;
        lv_font_glyph_dsc_t g;
        if(lv_font_get_glyph_dsc(font, &g, c, 0) && !g.is_placeholder) continue;
        out += (out.empty() ? "" : " ") + text.substr(start, i - start);
    }
    return out;
}

std::vector<std::string> missing_glyphs_on(lv_obj_t* root)
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto look = [&](lv_obj_t* o, const char* text, lv_part_t part) {
        if(!text || !*text) return;
        const std::string miss = missing_glyphs(lv_obj_get_style_text_font(o, part), text);
        if(miss.empty()) return;
        std::string shown = text;
        for(char& ch : shown) if(ch == '\n') ch = ' ';
        if(shown.size() > 40) { size_t n = 40; while(n > 0 && (static_cast<unsigned char>(shown[n]) & 0xC0) == 0x80) n--; shown = shown.substr(0, n) + "..."; }
        const std::string line = "\"" + shown + "\": no glyph for: " + miss;
        if(seen.insert(line).second) out.push_back(line);
    };
    std::vector<lv_obj_t*> todo{ root };
    while(!todo.empty()) {
        lv_obj_t* o = todo.back();
        todo.pop_back();
        if(!o || lv_obj_is_hidden(o)) continue;          // (what is hidden now is not drawn)
        if(lv_obj_check_type(o, &lv_label_class)) look(o, lv_label_get_text(o), LV_PART_MAIN);
        else if(lv_obj_check_type(o, &lv_checkbox_class)) look(o, lv_checkbox_get_text(o), LV_PART_MAIN);
        else if(lv_obj_check_type(o, &lv_dropdown_class)) look(o, lv_dropdown_get_options(o), LV_PART_MAIN);
        else if(lv_obj_check_type(o, &lv_textarea_class)) look(o, lv_textarea_get_placeholder_text(o), LV_PART_MAIN);
        for(uint32_t i = 0; i < lv_obj_get_child_count(o); i++) todo.push_back(lv_obj_get_child(o, static_cast<int32_t>(i)));
    }
    return out;
}

} // namespace hml
