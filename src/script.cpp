#include "script.hpp"
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <vector>
#include "lvgl.h"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

// Lua is compiled as C++ (see CMakeLists.txt): errors are C++ exceptions, so the C functions
// below may hold std::string etc. while calling luaL_error.
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

namespace hml {
namespace {

enum class Trigger { Manual, Startup, Interval, Tag, ScreenShow, ScreenHide };

constexpr int kMaxDepth = 8;            // script -> goto_screen -> screen_show script -> ...
constexpr int kHookCount = 10000;       // VM instructions between two timeout checks

struct Event {
    const char*  trigger = "manual";
    std::string  tag;
    const Value* value = nullptr;
    std::string  screen;
};

void push_value(lua_State* L, const Value& v)
{
    switch(v.type) {
        case TagType::Bool:   lua_pushboolean(L, v.num != 0); break;
        case TagType::Int:    lua_pushinteger(L, static_cast<lua_Integer>(v.num)); break;
        case TagType::Float:  lua_pushnumber(L, v.num); break;
        case TagType::String: lua_pushlstring(L, v.str.data(), v.str.size()); break;
    }
}

std::string join_args(lua_State* L)
{
    std::string out;
    int n = lua_gettop(L);
    for(int i = 1; i <= n; i++) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if(i > 1) out += ' ';
        out.append(s, len);
        lua_pop(L, 1);
    }
    return out;
}

} // namespace

struct ScriptEngine::Impl {
    struct Script {
        std::string name;
        Trigger     trigger = Trigger::Manual;
        int         ref = LUA_NOREF;        // compiled chunk in the Lua registry
        std::string screen;                 // screen_show / screen_hide: "" = any screen
        lv_timer_t* timer = nullptr;
        Impl*       owner = nullptr;
    };

    lua_State* L = nullptr;
    std::vector<std::unique_ptr<Script>> scripts;
    std::map<std::string, int> inline_cache;    // action code -> registry ref
    int         depth = 0;
    uint32_t    t_start = 0;
    uint32_t    timeout_ms = 500;
    const void* source = nullptr;               // "source" of tag writes made by the running script
    int         errors = 0;
    std::string last_error;

    static Impl* self(lua_State* L) { return *static_cast<Impl**>(lua_getextraspace(L)); }

    void report(const std::string& label, const std::string& msg)
    {
        errors++;
        last_error = msg;
        HML_ERROR("script '%s': %s", label.c_str(), msg.c_str());
    }

    // ---- Lua API ----
    static Tag* check_tag(lua_State* L)
    {
        const char* name = luaL_checkstring(L, 2);
        Tag* t = tags().find(name);
        if(!t) luaL_error(L, "unknown tag '%s'", name);
        return t;
    }
    static int l_tag_get(lua_State* L)
    {
        push_value(L, check_tag(L)->get());
        return 1;
    }
    static int l_tag_set(lua_State* L)
    {
        Tag* t = check_tag(L);
        const void* src = self(L)->source;
        switch(lua_type(L, 3)) {
            case LUA_TBOOLEAN: t->set(Value::number(lua_toboolean(L, 3), TagType::Bool), src); break;
            case LUA_TNUMBER:  t->set_number(lua_tonumber(L, 3), src); break;
            case LUA_TSTRING: {
                size_t len = 0;
                const char* s = lua_tolstring(L, 3, &len);
                t->set_string(std::string(s, len), src);
                break;
            }
            default: luaL_error(L, "tag '%s': cannot assign a %s value", t->name().c_str(), luaL_typename(L, 3));
        }
        return 0;
    }
    static int l_print(lua_State* L)
    {
        HML_INFO("[script] %s", join_args(L).c_str());
        return 0;
    }
    static int l_warn(lua_State* L)
    {
        HML_WARN("[script] %s", join_args(L).c_str());
        return 0;
    }
    static int l_goto_screen(lua_State* L)
    {
        std::string name = luaL_checkstring(L, 1), anim = luaL_optstring(L, 2, "fade");
        lua_pushboolean(L, ui().show_screen(name, anim));
        return 1;
    }
    static int l_back(lua_State* L)
    {
        lua_pushboolean(L, ui().back());
        return 1;
    }
    static int l_current_screen(lua_State* L)
    {
        auto name = ui().current_screen();
        if(name) lua_pushlstring(L, name->data(), name->size());
        else lua_pushnil(L);
        return 1;
    }
    static int l_call(lua_State* L)
    {
        std::string fn = luaL_checkstring(L, 1), arg = luaL_optstring(L, 2, "");
        if(!ui().call_action(fn, arg)) luaL_error(L, "call: unknown action '%s'", fn.c_str());
        return 0;
    }
    static int l_run(lua_State* L)
    {
        std::string name = luaL_checkstring(L, 1);
        Impl* m = self(L);
        Script* s = m->find(name);
        if(!s) luaL_error(L, "run: unknown script '%s'", name.c_str());
        lua_pushboolean(L, m->call(s->ref, s->name, Event{}, s));
        return 1;
    }
    static int l_millis(lua_State* L)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(millis()));
        return 1;
    }
    static int l_os_time(lua_State* L)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(std::time(nullptr)));
        return 1;
    }
    static int l_os_clock(lua_State* L)
    {
        lua_pushnumber(L, static_cast<lua_Number>(std::clock()) / CLOCKS_PER_SEC);
        return 1;
    }
    static int l_os_date(lua_State* L)
    {
        const char* fmt = luaL_optstring(L, 1, "%Y-%m-%d %H:%M:%S");
        std::time_t t = static_cast<std::time_t>(luaL_optinteger(L, 2, static_cast<lua_Integer>(std::time(nullptr))));
        std::tm* tm = std::localtime(&t);
        char buf[128] = "";
        if(tm) std::strftime(buf, sizeof(buf), *fmt ? fmt : "%c", tm);
        lua_pushstring(L, buf);
        return 1;
    }
    static void l_hook(lua_State* L, lua_Debug*)
    {
        Impl* m = self(L);
        if(m->depth > 0 && millis() - m->t_start > m->timeout_ms) {
            luaL_error(L, "timed out (ran longer than %d ms)", static_cast<int>(m->timeout_ms));
        }
    }

    void ensure_state()
    {
        if(L) return;
        L = luaL_newstate();
        *static_cast<Impl**>(lua_getextraspace(L)) = this;

        static const luaL_Reg libs[] = {
            { LUA_GNAME, luaopen_base }, { LUA_STRLIBNAME, luaopen_string }, { LUA_TABLIBNAME, luaopen_table },
            { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 }, { LUA_COLIBNAME, luaopen_coroutine },
        };
        for(const luaL_Reg& lib : libs) {
            luaL_requiref(L, lib.name, lib.func, 1);
            lua_pop(L, 1);
        }
        // no file access, no loading of (binary) chunks
        for(const char* name : { "dofile", "loadfile", "load" }) {
            lua_pushnil(L);
            lua_setglobal(L, name);
        }

        static const luaL_Reg funcs[] = {
            { "print", l_print }, { "log", l_print }, { "warn", l_warn }, { "goto_screen", l_goto_screen }, { "back", l_back },
            { "current_screen", l_current_screen }, { "call", l_call }, { "run", l_run }, { "millis", l_millis },
        };
        for(const luaL_Reg& f : funcs) lua_register(L, f.name, f.func);

        static const luaL_Reg os_funcs[] = { { "time", l_os_time }, { "date", l_os_date }, { "clock", l_os_clock }, { nullptr, nullptr } };
        luaL_newlib(L, os_funcs);
        lua_setglobal(L, "os");

        // tag.name / tag["name"]
        lua_newtable(L);
        lua_newtable(L);
        lua_pushcfunction(L, l_tag_get);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, l_tag_set);
        lua_setfield(L, -2, "__newindex");
        lua_setmetatable(L, -2);
        lua_setglobal(L, "tag");

        lua_newtable(L);
        lua_setglobal(L, "event");
        lua_sethook(L, l_hook, LUA_MASKCOUNT, kHookCount);
    }

    Script* find(const std::string& name)
    {
        for(auto& s : scripts) if(s->name == name) return s.get();
        return nullptr;
    }

    int compile(const std::string& code, const std::string& label)
    {
        ensure_state();
        std::string chunk = "=" + label;
        if(luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            report(label, msg ? msg : "syntax error");
            lua_pop(L, 1);
            return LUA_NOREF;
        }
        return luaL_ref(L, LUA_REGISTRYINDEX);
    }

    bool call(int ref, const std::string& label, const Event& ev, const void* src)
    {
        if(!L || ref == LUA_NOREF) return false;
        if(depth >= kMaxDepth) { report(label, "scripts nested too deeply (a script triggers itself?)"); return false; }

        int top = lua_gettop(L);
        lua_getglobal(L, "event");                 // restored afterwards (nested runs)
        lua_createtable(L, 0, 4);
        lua_pushstring(L, ev.trigger);
        lua_setfield(L, -2, "trigger");
        if(!ev.tag.empty()) { lua_pushstring(L, ev.tag.c_str()); lua_setfield(L, -2, "tag"); }
        if(ev.value) { push_value(L, *ev.value); lua_setfield(L, -2, "value"); }
        if(!ev.screen.empty()) { lua_pushstring(L, ev.screen.c_str()); lua_setfield(L, -2, "screen"); }
        lua_setglobal(L, "event");

        const void* saved_source = source;
        source = src;
        if(depth == 0) t_start = millis();
        depth++;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        bool ok = lua_pcall(L, 0, 0, 0) == LUA_OK;
        depth--;
        source = saved_source;
        if(!ok) {
            const char* msg = lua_tostring(L, -1);
            report(label, msg ? msg : "error");
        }
        lua_settop(L, top + 1);
        lua_setglobal(L, "event");
        return ok;
    }

    static void on_timer(lv_timer_t* t)
    {
        Script* s = static_cast<Script*>(lv_timer_get_user_data(t));
        Event ev;
        ev.trigger = "interval";
        s->owner->call(s->ref, s->name, ev, s);
    }
};

namespace {

std::optional<std::string> read_text(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if(!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if(text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    return text;
}

// "code": "..." or ["line 1", "line 2"] (JSON has no multi-line strings), or "file": "scripts/x.lua"
std::optional<std::string> script_source(const json& j, const std::string& base_dir, const std::string& name)
{
    if(const json* code = jget(j, "code")) {
        if(code->is_string()) return code->get<std::string>();
        if(code->is_array()) {
            std::string out;
            for(const json& line : *code) if(line.is_string()) { out += line.get<std::string>(); out += '\n'; }
            return out;
        }
    }
    std::string file = jstr(j, "file", "");
    if(file.empty()) { HML_WARN("script '%s' has neither \"code\" nor \"file\"", name.c_str()); return std::nullopt; }
    bool absolute = file[0] == '/' || file[0] == '\\' || (file.size() > 1 && file[1] == ':');
    std::string path = absolute ? file : base_dir + "/" + file;
    auto text = read_text(path);
    if(!text) HML_ERROR("script '%s': cannot read '%s'", name.c_str(), path.c_str());
    return text;
}

} // namespace

ScriptEngine::ScriptEngine() : impl_(std::make_unique<Impl>()) {}
ScriptEngine::~ScriptEngine()
{
    if(impl_->L) lua_close(impl_->L);       // timers / subscriptions belong to LVGL and the tag store: see clear()
}

void ScriptEngine::clear()
{
    Impl& m = *impl_;
    if(m.depth > 0) { HML_ERROR("scripts cannot be unloaded while one is running"); return; }
    tags().unsubscribe_group(SubGroup::Script);
    for(auto& s : m.scripts) if(s->timer) lv_timer_delete(s->timer);
    m.scripts.clear();
    m.inline_cache.clear();
    if(m.L) { lua_close(m.L); m.L = nullptr; }
    m.errors = 0;
    m.last_error.clear();
}

int ScriptEngine::load(const json* list, const std::string& base_dir)
{
    Impl& m = *impl_;
    clear();
    if(m.depth > 0 || !list || !list->is_array()) return 0;

    int index = 0;
    for(const json& j : *list) {
        index++;
        if(!j.is_object() || !jbool(j, "enabled", true)) continue;
        auto s = std::make_unique<Impl::Script>();
        s->owner = &m;
        s->name = jstr(j, "name", "script" + std::to_string(index));
        if(m.find(s->name)) { HML_WARN("script '%s' defined twice, keeping the first", s->name.c_str()); continue; }

        auto code = script_source(j, base_dir, s->name);
        if(!code) continue;
        s->ref = m.compile(*code, s->name);
        if(s->ref == LUA_NOREF) continue;

        std::string trig = jstr(j, "trigger", "manual");
        Impl::Script* raw = s.get();
        if(iequals(trig, "startup")) s->trigger = Trigger::Startup;
        else if(iequals(trig, "interval")) {
            s->trigger = Trigger::Interval;
            double ms = jnum(j, "interval_ms", 1000);
            if(ms < 10) ms = 10;
            s->timer = lv_timer_create(&Impl::on_timer, static_cast<uint32_t>(ms), raw);
        }
        else if(iequals(trig, "tag")) {
            s->trigger = Trigger::Tag;
            std::vector<std::string> names;
            const json* tl = jget(j, "tags");
            if(tl && tl->is_array()) { for(const json& t : *tl) if(t.is_string()) names.push_back(t.get<std::string>()); }
            else if(tl && tl->is_string()) names.push_back(tl->get<std::string>());
            if(auto one = jstr_opt(j, "tag")) names.push_back(*one);
            if(names.empty()) HML_WARN("script '%s': trigger \"tag\" without \"tags\"", s->name.c_str());
            for(const std::string& n : names) {
                Tag* t = tags().find(n);
                if(!t) { HML_WARN("script '%s': unknown tag '%s'", s->name.c_str(), n.c_str()); continue; }
                Impl* mp = &m;
                t->subscribe(SubGroup::Script, [mp, raw](Tag& tag, const Value& v, const void* src) {
                    if(src == raw) return;              // the script's own write: no self-retrigger
                    Event ev;
                    ev.trigger = "tag";
                    ev.tag = tag.name();
                    ev.value = &v;
                    mp->call(raw->ref, raw->name, ev, raw);
                });
            }
        }
        else if(iequals(trig, "screen_show") || iequals(trig, "screen_hide")) {
            s->trigger = iequals(trig, "screen_show") ? Trigger::ScreenShow : Trigger::ScreenHide;
            s->screen = jstr(j, "screen", "");
        }
        else if(!iequals(trig, "manual") && !iequals(trig, "none")) {
            HML_WARN("script '%s': unknown trigger '%s' (startup | interval | tag | screen_show | screen_hide | manual)", s->name.c_str(), trig.c_str());
        }
        m.scripts.push_back(std::move(s));
    }
    if(!m.scripts.empty()) HML_INFO("%d scripts loaded", static_cast<int>(m.scripts.size()));
    return static_cast<int>(m.scripts.size());
}

void ScriptEngine::run_startup()
{
    Impl& m = *impl_;
    for(size_t i = 0; i < m.scripts.size(); i++) {
        Impl::Script* s = m.scripts[i].get();
        if(s->trigger != Trigger::Startup) continue;
        Event ev;
        ev.trigger = "startup";
        m.call(s->ref, s->name, ev, s);
    }
}

bool ScriptEngine::run(const std::string& name)
{
    Impl& m = *impl_;
    Impl::Script* s = m.find(name);
    if(!s) { HML_WARN("script: unknown script '%s'", name.c_str()); return false; }
    Event ev;
    ev.trigger = "action";
    return m.call(s->ref, s->name, ev, s);
}

bool ScriptEngine::has(const std::string& name) const
{
    return impl_->find(name) != nullptr;
}

bool ScriptEngine::check(const std::string& code, const std::string& label)
{
    Impl& m = *impl_;
    auto it = m.inline_cache.find(code);
    if(it != m.inline_cache.end()) return true;
    int ref = m.compile(code, label);
    if(ref == LUA_NOREF) return false;
    m.inline_cache[code] = ref;
    return true;
}

bool ScriptEngine::exec(const std::string& code, const std::string& label)
{
    Impl& m = *impl_;
    if(!check(code, label)) return false;
    Event ev;
    ev.trigger = "action";
    return m.call(m.inline_cache[code], label, ev, &m);
}

void ScriptEngine::on_screen(const std::string& name, bool shown)
{
    Impl& m = *impl_;
    Trigger want = shown ? Trigger::ScreenShow : Trigger::ScreenHide;
    for(size_t i = 0; i < m.scripts.size(); i++) {
        Impl::Script* s = m.scripts[i].get();
        if(s->trigger != want || (!s->screen.empty() && s->screen != name)) continue;
        Event ev;
        ev.trigger = shown ? "screen_show" : "screen_hide";
        ev.screen = name;
        m.call(s->ref, s->name, ev, s);
    }
}

size_t ScriptEngine::count() const { return impl_->scripts.size(); }
int ScriptEngine::error_count() const { return impl_->errors; }
const std::string& ScriptEngine::last_error() const { return impl_->last_error; }
void ScriptEngine::set_timeout_ms(uint32_t ms) { impl_->timeout_ms = ms; }

ScriptEngine& scripts()
{
    static ScriptEngine instance;
    return instance;
}

} // namespace hml
