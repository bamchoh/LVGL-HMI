#include "check.hpp"
#include "data_transfer.hpp"
#include "mail.hpp"
#include "upload.hpp"
#include "schedule.hpp"
#include "sound.hpp"
#include <functional>
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

    // The runtime's own tags ("sys.") depend on what the project sets up: they are noted here and
    // looked up once the project is loaded (run_check), where they all exist.
    std::vector<SysTagUse> sys_uses;
    bool tag_ok(const std::string& t, const std::string& where, const std::string& what)
    {
        if(tags.count(t)) return true;
        if(t.rfind("sys.", 0) != 0) return false;
        sys_uses.push_back({ file, where, what, t });
        return true;
    }
    void need_tag(const std::string& where, const std::string& what, const json& v)
    {
        if(!v.is_string()) return;
        const std::string t = v.get<std::string>();
        if(t.empty()) add(where, what + ": no tag");
        else if(!tag_ok(t, where, what)) add(where, what + ": unknown tag \"" + t + "\"");
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
                for(auto k = set->begin(); k != set->end(); ++k) if(!tag_ok(k.key(), where, what + ": set")) add(where, what + ": set: unknown tag \"" + k.key() + "\"");
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
        static const std::set<std::string> screen_keys{ "name", "children", "popup", "dim", "template", "template_args" };
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
            if(type == "xy_chart") own.insert("series");
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
        // texts by value: "bind": { "text": { "tag": ..., "map": { "1": "@running", "default": "@stopped" } } }
        if(const json* b = jget(w, "bind"); b && b->is_object())
            if(const json* tb = jget(*b, "text"); tb && tb->is_object())
                if(const json* map = jget(*tb, "map"); map && map->is_object())
                    for(auto it = map->begin(); it != map->end(); ++it) text_id(where, "bind.text.map." + it.key(), it.value());
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
        if(type == "bar_chart") {
            const std::string g = jstr(w, "group", ""), t = jstr(w, "tag", "");
            auto it = groups.find(g);
            if(it == groups.end()) add(where, g.empty() ? "no \"group\" (a data log group)" : "unknown data log group \"" + g + "\"");
            else if(t.empty()) add(where, "no \"tag\" (a tag of the data log group)");
            else if(!it->second.empty() && !it->second.count(t)) add(where, "data log group \"" + g + "\" does not record \"" + t + "\"");
            const std::string mode = jstr(w, "mode", "increase");
            if(mode != "increase" && mode != "value") add(where, "mode: \"" + mode + "\" is not increase or value");
            const std::string start = jstr(w, "start", "now");
            if(start != "now" && start != "day") add(where, "start: \"" + start + "\" is not now or day");
            if(const json* tg = jget(w, "target"); tg && (tg->is_string() || tg->is_object())) need_tag(where, "target", tg->is_string() ? *tg : tg->value("tag", json("")));
        }
        if(type == "recipe_table") {
            const std::string b = jstr(w, "book", "");
            if(!books.count(b) && !(b.empty() && books.size() == 1)) add(where, b.empty() ? "no \"book\" (a recipe book)" : "unknown recipe book \"" + b + "\"");
        }
        if((type == "alarm_list" || type == "alarm_banner" || type == "alarm_summary") && !has_alarms) add(where, "the project has no \"alarms\"");
        if(type == "xy_chart") {
            for(const char* k : { "x_tag", "y_tag" }) { if(w.contains(k)) need_tag(where, k, w[k]); else add(where, std::string("no \"") + k + "\""); }
            if(w.contains("clear_tag")) need_tag(where, "clear_tag", w["clear_tag"]);
            if(const json* ser = jget(w, "series")) {
                if(!ser->is_array()) add(where, "series: a list is needed");
                else for(size_t i = 0; i < ser->size(); i++) {
                    const json& s = (*ser)[i];
                    const std::string at = "series[" + std::to_string(i) + "]";
                    if(!s.is_object()) { add(where, at + ": { \"x_tag\", \"y_tag\" } is needed"); continue; }
                    for(auto k = s.begin(); k != s.end(); ++k) {
                        if(k.key() == "x_tag" || k.key() == "y_tag") need_tag(where, at + "." + k.key(), k.value());
                        else if(k.key() != "color") add(where, at + ": unknown key \"" + k.key() + "\"");
                    }
                    if(!s.contains("x_tag") || !s.contains("y_tag")) add(where, at + ": x_tag and y_tag are needed");
                }
            }
            for(const char* a : { "x", "y" }) {
                const std::string lo = std::string(a) + "_min", hi = std::string(a) + "_max";
                if(w.contains(lo) && w.contains(hi) && w[lo].is_number() && w[hi].is_number() && !(w[hi].get<double>() > w[lo].get<double>()))
                    add(where, hi + " has to be above " + lo);
            }
        }
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

std::vector<CheckProblem> check_project(const std::string& path, std::vector<std::string>* screen_names, std::vector<SysTagUse>* sys_uses)
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

    // ---- simulated values: the "simulation" section and "sim" drivers (driver_sim.cpp)
    auto check_sim = [&](const std::string& where, const json* items) {
        if(!items || !items->is_array()) { c.add(where, "no \"items\""); return; }
        static const std::set<std::string> modes{ "sine", "triangle", "ramp", "sawtooth", "square", "toggle", "random", "counter", "cycle", "follow" };
        static const std::set<std::string> keys{ "tag", "mode", "min", "max", "period_ms", "values", "when", "noise", "target", "rate", "idle" };
        for(size_t i = 0; i < items->size(); i++) {
            const json& it = (*items)[i];
            const std::string w = where + ": items[" + std::to_string(i) + "]";
            if(!it.is_object()) { c.add(w, "not an object"); continue; }
            for(auto k = it.begin(); k != it.end(); ++k) if(!keys.count(k.key())) c.add(w, "unknown key \"" + k.key() + "\" (ignored by the runtime)");
            const std::string mode = jstr(it, "mode", "sine");
            if(!modes.count(mode)) c.add(w, "unknown mode \"" + mode + "\" (sine triangle ramp square random counter cycle follow)");
            if(const json* wh = jget(it, "when")) c.need_tag(w, "when", wh->is_object() ? wh->value("tag", json("")) : *wh);
            if(mode == "follow") {
                const json* t = jget(it, "target");
                if(!t || !(t->is_string() || t->is_number())) c.add(w, "mode \"follow\" needs \"target\" (a tag or a number)");
                else if(t->is_string()) c.need_tag(w, "target", *t);
            }
        }
    };
    if(const json* sim = jget(root, "simulation")) {
        if(!sim->is_object()) c.add("simulation", "not an object");
        else check_sim("simulation", jget(*sim, "items"));
    }
    if(const json* dl = jget(root, "drivers"); dl && dl->is_array())
        for(size_t i = 0; i < dl->size(); i++)
            if((*dl)[i].is_object() && jstr((*dl)[i], "type", "") == "sim") check_sim("drivers[" + std::to_string(i) + "] (sim)", jget((*dl)[i], "items"));

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

    // ---- mail: a server, addresses that are addresses; what should not be in a project file
    if(const json* ml = jget(root, "mail")) {
        MailConfig mc;
        for(const std::string& p : mail_parse(*ml, nullptr, mc)) c.add("mail", p);
        if(ml->is_object()) {
            static const std::set<std::string> known{ "host", "port", "security", "username", "password", "from", "to", "verify", "ca_file", "timeout_ms", "retry_s", "retries", "queue", "alarms", "enabled" };
            for(auto k = ml->begin(); k != ml->end(); ++k)
                if(!k.key().empty() && k.key()[0] != '_' && !known.count(k.key())) c.add("mail", "unknown key \"" + k.key() + "\"");
            if(!jstr(*ml, "password", "").empty()) c.add("mail", "the password is in the project file: everybody who gets the project can read it. Better: leave it out and set it on the device (System > Mail; kept in project.local.json)");
            if(jstr(*ml, "security", "starttls") == "none") c.add("mail", "\"security\": \"none\": the mails" + std::string(jstr(*ml, "username", "").empty() ? "" : ", the user name and the password") + " can be read on the network");
            if(!jbool(*ml, "verify", true)) c.add("mail", "\"verify\": false: the certificate of the server is not checked (anybody can pose as the server)");
            if(const std::string ca = jstr(*ml, "ca_file", ""); !ca.empty()) {
                std::error_code ec;
                if(!fs::is_regular_file(fs::u8path(ca).is_absolute() ? fs::u8path(ca) : c.dir / fs::u8path(ca), ec)) c.add("mail", "ca_file: no file \"" + ca + "\"");
            }
            if(const json* al = jget(*ml, "alarms"); al && al->is_object()) {
                static const std::set<std::string> ak{ "events", "severities", "min_interval_s", "subject", "body" };
                for(auto k = al->begin(); k != al->end(); ++k) if(!k.key().empty() && k.key()[0] != '_' && !ak.count(k.key())) c.add("mail", "alarms: unknown key \"" + k.key() + "\"");
                if(!c.has_alarms && !(al->contains("events") && (*al)["events"].is_array() && (*al)["events"].empty())) c.add("mail", "alarms are to be mailed, but the project has no \"alarms\"");
            }
        }
    }

    // ---- uploads: where to, which logs; what should not be in a project file
    if(const json* up = jget(root, "uploads")) {
        if(!up->is_array()) c.add("uploads", "a list is needed");
        else {
            std::set<std::string> names, groups;
            if(const json* dl = jget(root, "data_log"); dl && dl->is_object()) if(const json* gs = jget(*dl, "groups"); gs && gs->is_array())
                for(const json& g : *gs) groups.insert(jstr(g, "name", ""));
            if(up->size() > kUploadTargetsMax) c.add("uploads", "more than " + std::to_string(kUploadTargetsMax) + " uploads (the rest is not used)");
            for(size_t i = 0; i < up->size(); i++) {
                const json& u = (*up)[i];
                const std::string where = "uploads[" + std::to_string(i) + "]" + (u.is_object() && u.contains("name") && u["name"].is_string() ? " (" + u["name"].get<std::string>() + ")" : "");
                UploadTarget t;
                std::string err;
                if(!upload_parse(u, nullptr, t, &err)) { c.add(where, err); continue; }
                if(!names.insert(t.name).second) c.add(where, "the name is used twice");
                static const std::set<std::string> known{ "name", "url", "method", "username", "password", "logs", "verify", "ca_file", "timeout_ms", "retry_s", "retries", "keep", "enabled" };
                for(auto k = u.begin(); k != u.end(); ++k) if(!k.key().empty() && k.key()[0] != '_' && !known.count(k.key())) c.add(where, "unknown key \"" + k.key() + "\"");
                if(!t.password.empty()) c.add(where, "the password is in the project file: everybody who gets the project can read it. Better: leave it out and set it on the device (System > Upload; kept in project.local.json)");
                if(!t.url.secure()) c.add(where, t.url.scheme + ":// is not secured: the files" + std::string(t.username.empty() ? "" : ", the user name and the password") + " can be read on the network (https:// secures them)");
                else if(!t.tls.verify) c.add(where, "\"verify\": false: the certificate of the server is not checked (anybody can pose as the server)");
                if(!t.tls.ca_file.empty()) {
                    std::error_code ec;
                    if(!fs::is_regular_file(fs::u8path(t.tls.ca_file).is_absolute() ? fs::u8path(t.tls.ca_file) : c.dir / fs::u8path(t.tls.ca_file), ec)) c.add(where, "ca_file: no file \"" + t.tls.ca_file + "\"");
                }
                for(const std::string& l : t.logs) {
                    if(l.rfind("data:", 0) == 0) { if(!groups.count(l.substr(5))) c.add(where, "logs: the data log has no group \"" + l.substr(5) + "\""); }
                    else if(l == "alarms") { if(!c.has_alarms) c.add(where, "logs: the project has no \"alarms\""); }
                    else if(!c.log_sections.count(l)) c.add(where, "logs: the project has no \"" + l + "_log\" (nothing is recorded to send)");
                }
            }
        }
    }

    // ---- transfers: tags that exist, no rings
    if(const json* tr = jget(root, "transfers")) {
        if(!tr->is_array()) c.add("transfers", "a list is needed");
        else {
            std::set<std::string> names;
            std::vector<TransferItem> good;
            std::vector<std::string> where_of;
            if(tr->size() > kTransferMax) c.add("transfers", "more than " + std::to_string(kTransferMax) + " transfers (the rest is not run)");
            for(size_t i = 0; i < tr->size(); i++) {
                const json& t = (*tr)[i];
                const std::string where = "transfers[" + std::to_string(i) + "]" + (t.is_object() && t.contains("name") && t["name"].is_string() ? " (" + t["name"].get<std::string>() + ")" : "");
                TransferItem item;
                std::string err;
                if(!transfer_parse(t, item, &err)) { c.add(where, err); continue; }
                if(!names.insert(item.name).second) { c.add(where, "the name is used twice"); continue; }
                for(auto k = t.begin(); k != t.end(); ++k) {
                    static const std::set<std::string> known{ "name", "from", "to", "trigger", "every_ms", "scale", "offset", "out_of_range", "enabled", "when" };
                    if(!k.key().empty() && k.key()[0] != '_' && !known.count(k.key())) c.add(where, "unknown key \"" + k.key() + "\"");
                }
                c.need_tag(where, "from", t["from"]);
                c.need_tag(where, "to", t["to"]);
                if(item.when.op != TransferCond::Op::None) {
                    c.need_tag(where, "when", json(item.when.tag));
                    if(!item.when.other_tag.empty()) c.need_tag(where, "when", json(item.when.other_tag));
                }
                if(!item.cyclic && t.contains("every_ms")) c.add(where, "\"every_ms\" has no meaning without \"trigger\": \"cyclic\"");
                good.push_back(item);
                where_of.push_back(where);
            }
            const std::vector<std::string> rings = transfer_rings(good);
            for(size_t i = 0; i < good.size(); i++)
                if(std::find(rings.begin(), rings.end(), good[i].name) != rings.end()) c.add(where_of[i], "part of a ring of transfers (" + good[i].from + " -> " + good[i].to + " -> ... -> " + good[i].from + "): not run");
        }
    }

    // ---- sound: an output that exists, sounds that can be played
    if(const json* so = jget(root, "sound")) {
        if(!so->is_object()) c.add("sound", "an object is needed");
        else {
            static const std::set<std::string> known{ "output", "device", "gpio", "gpio_active_low", "gpio_chip", "volume", "mute", "touch", "alarm", "alarm_every_s" };
            for(auto k = so->begin(); k != so->end(); ++k)
                if(!k.key().empty() && k.key()[0] != '_' && !known.count(k.key())) c.add("sound", "unknown key \"" + k.key() + "\"");
            const std::string out = jstr(*so, "output", "auto");
            if(out != "auto" && out != "audio" && out != "gpio" && out != "none") c.add("sound", "output: \"" + out + "\" is not auto, audio, gpio or none");
            if(const json* g = jget(*so, "gpio"); g && (!g->is_number_integer() || g->get<double>() < 0 || g->get<double>() > 511)) c.add("sound", "gpio: the number of a GPIO line (0..511)");
            if(out == "gpio" && !jhas(*so, "gpio")) c.add("sound", "output \"gpio\" needs \"gpio\" (the line of the buzzer)");
            if(const json* v = jget(*so, "volume"); v && (!v->is_number() || v->get<double>() < 0 || v->get<double>() > 100)) c.add("sound", "volume: 0 to 100");
            if(const json* v = jget(*so, "alarm_every_s"); v && (!v->is_number() || v->get<double>() < 0)) c.add("sound", "alarm_every_s: seconds (0 = once)");
            for(const char* key : { "touch", "alarm" }) {
                const json* v = jget(*so, key);
                if(!v || v->is_boolean()) continue;
                if(!v->is_string()) { c.add("sound", std::string(key) + ": the name of a sound, or false"); continue; }
                const std::string n = v->get<std::string>();
                Pcm pcm;
                std::string err;
                if(n.empty() || sound_builtin(n, pcm)) continue;
                if(!sound_file_name_ok(n)) c.add("sound", std::string(key) + ": \"" + n + "\" is no built-in sound (click, beep, alarm) and no .wav file inside the project folder");
                else if(const auto bytes = read_file((c.dir / fs::u8path(n)).generic_u8string(), kSoundFileMax, &err); !bytes) c.add("sound", std::string(key) + ": no file \"" + n + "\" (or larger than 4 MB)");
                else if(!wav_parse(*bytes, pcm, &err)) c.add("sound", std::string(key) + ": \"" + n + "\": " + err);
            }
        }
    }

    // ---- schedules: when, and what they do (the actions of a screen)
    if(const json* sc = jget(root, "schedules")) {
        if(!sc->is_array()) c.add("schedules", "a list is needed");
        else {
            std::set<std::string> names;
            for(size_t i = 0; i < sc->size(); i++) {
                const json& s = (*sc)[i];
                const std::string where = "schedules[" + std::to_string(i) + "]" + (s.is_object() && s.contains("name") && s["name"].is_string() ? " (" + s["name"].get<std::string>() + ")" : "");
                ScheduleItem item;
                std::string err;
                if(!schedule_parse(s, item, &err)) { c.add(where, err); continue; }
                if(!names.insert(item.name).second) c.add(where, "the name is used twice");
                for(auto k = s.begin(); k != s.end(); ++k) {
                    static const std::set<std::string> known{ "name", "at", "days", "do", "enabled", "catch_up" };
                    if(!k.key().empty() && k.key()[0] != '_' && !known.count(k.key())) c.add(where, "unknown key \"" + k.key() + "\"");
                }
                if(item.once && s.contains("days")) c.add(where, "\"days\" has no meaning for a schedule with a date");
                c.check_actions(where, "do", item.actions);
            }
        }
    }

    // ---- templates: a screen built on another one
    std::map<std::string, const json*> by_name;
    for(const Screen& s : screens) if(s.def.is_object()) by_name.emplace(jstr(s.def, "name", ""), &s.def);
    // the "${name}" in the texts of a widget tree
    std::function<void(const json&, std::set<std::string>&)> placeholders = [&](const json& j, std::set<std::string>& out) {
        if(j.is_string()) {
            const std::string& t = j.get_ref<const std::string&>();
            for(size_t a = t.find("${"); a != std::string::npos; a = t.find("${", a + 2)) {
                const size_t b = t.find('}', a + 2);
                if(b == std::string::npos) break;
                out.insert(t.substr(a + 2, b - a - 2));
            }
        }
        else if(j.is_structured()) for(const json& x : j) placeholders(x, out);
    };
    for(const Screen& s : screens) {
        if(!s.def.is_object()) continue;
        c.file = s.file;
        const std::string name = jstr(s.def, "name", "");
        const json* t = jget(s.def, "template");
        const json* args = jget(s.def, "template_args");
        if(args && !t) c.add("", "template_args without \"template\"");
        if(args && !args->is_object()) c.add("template_args", "an object { \"name\": value }");
        if(!t) continue;
        if(!t->is_string() || t->get<std::string>().empty()) { c.add("template", "the name of a screen"); continue; }
        // up the chain: every template there is, until one is missing or met twice
        std::set<std::string> wanted, seen{ name };
        bool ok = true;
        for(std::string next = t->get<std::string>(); !next.empty();) {
            auto it = by_name.find(next);
            if(next == name && seen.size() == 1) { c.add("template", "a screen is not its own template"); ok = false; break; }
            if(it == by_name.end()) { c.add("template", "unknown screen \"" + next + "\""); ok = false; break; }
            if(!seen.insert(next).second) { c.add("template", "the templates form a ring (" + next + ")"); ok = false; break; }
            if(jbool(*it->second, "popup", false)) { c.add("template", "\"" + next + "\" is a popup"); ok = false; break; }
            if(seen.size() > 9) { c.add("template", "more than 8 templates on top of each other"); ok = false; break; }
            if(const json* ch = jget(*it->second, "children")) placeholders(*ch, wanted);
            const json* up = jget(*it->second, "template");
            next = up && up->is_string() ? up->get<std::string>() : "";
        }
        if(!ok) continue;
        wanted.erase("screen");
        if(args && args->is_object()) {
            for(auto a = args->begin(); a != args->end(); ++a) {
                if(a.key().empty() || a.key()[0] == '_') continue;
                if(!(a.value().is_string() || a.value().is_number() || a.value().is_boolean())) c.add("template_args", "\"" + a.key() + "\" is a text, a number or true / false");
                if(!wanted.count(a.key()) && a.key() != "screen") c.add("template_args", "\"" + a.key() + "\" is not used by the template");
                wanted.erase(a.key());
            }
        }
        for(const std::string& w : wanted) c.add("template", "\"" + t->get<std::string>() + "\" has ${" + w + "}, which \"template_args\" does not give (it is shown as nothing)");
    }

    for(const Screen& s : screens) {
        if(!s.def.is_object()) continue;
        c.file = s.file;
        c.walk(s.def, s.file == pj ? "screen \"" + jstr(s.def, "name", "") + "\"" : "", true);
    }
    if(sys_uses) *sys_uses = std::move(c.sys_uses);
    return c.out;
}

std::vector<CheckProblem> unknown_sys_tags(const std::vector<SysTagUse>& uses, const std::function<bool(const std::string&)>& exists)
{
    std::vector<CheckProblem> out;
    for(const SysTagUse& u : uses) {
        if(exists(u.tag)) continue;
        out.push_back({ u.file, u.where, u.what + ": unknown tag \"" + u.tag + "\" (the runtime has no such sys. tag; or the feature it belongs to is not set up in project.json)" });
    }
    return out;
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
