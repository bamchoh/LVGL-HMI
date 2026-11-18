#include "users.hpp"
#include "key_service.hpp"      // sha256_hex
#include "operation_log.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"
#include "lvgl.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace hml {

namespace {

constexpr size_t kMaxUsers = 16;
constexpr size_t kMaxLevels = 9;
const char* kLoginScreen = "__login";

void set_tag(const std::string& name, TagType type, const Value& v)
{
    Tag* t = tags().find(name);
    if(!t) tags().create(name, type, &v, false, 0, 0);
    else t->set(v, nullptr);
}

std::string new_salt()
{
    static std::mt19937_64 rng{ std::random_device{}() ^ static_cast<uint64_t>(millis()) };
    return vformat("%016llx", static_cast<unsigned long long>(rng()));
}

bool same_name(const std::string& a, const std::string& b) { return iequals(a, b); }

} // namespace

// ================================================================ static helpers

std::string Users::hash_pin(const std::string& salt, const std::string& pin) { return sha256_hex(salt + ":" + pin); }

bool Users::valid_pin(const std::string& pin)
{
    if(pin.size() < 4 || pin.size() > 8) return false;
    return std::all_of(pin.begin(), pin.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool Users::valid_name(const std::string& name)
{
    if(name.empty() || name.size() > 16 || name.front() == ' ' || name.back() == ' ') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ' '; });
}

// ================================================================ loading

void Users::parse_list(const json* arr, std::vector<UserInfo>& out) const
{
    if(!arr || !arr->is_array()) return;
    for(const json& j : *arr) {
        if(!j.is_object()) continue;
        UserInfo u;
        u.name = jstr(j, "name", "");
        if(!valid_name(u.name)) { HML_WARN("users: invalid user name '%s' (1..16 of letters, digits, _ - . space), skipped", u.name.c_str()); continue; }
        if(std::any_of(out.begin(), out.end(), [&](const UserInfo& o) { return same_name(o.name, u.name); })) {
            HML_WARN("users: user '%s' is defined twice, the second one is skipped", u.name.c_str());
            continue;
        }
        u.level = static_cast<int>(jnum(j, "level", 1));
        if(u.level < 1 || u.level > max_level()) {
            HML_WARN("users: user '%s': level %d is outside 1..%d", u.name.c_str(), u.level, max_level());
            u.level = std::clamp(u.level, 1, max_level());
        }
        std::string stored = jstr(j, "pin_hash", "");
        size_t sep = stored.find('$');
        if(sep != std::string::npos && stored.size() - sep - 1 == 64) {
            u.salt = stored.substr(0, sep);
            u.hash = stored.substr(sep + 1);
        }
        else {
            // "pin" may be written as a number in a hand-made file
            const json* p = jget(j, "pin");
            std::string pin = p && p->is_string() ? p->get<std::string>() : p && p->is_number_integer() ? std::to_string(p->get<long long>()) : "";
            if(!valid_pin(pin)) { HML_WARN("users: user '%s' has no usable PIN (4..8 digits), skipped", u.name.c_str()); continue; }
            u.salt = new_salt();
            u.hash = hash_pin(u.salt, pin);
        }
        if(out.size() >= kMaxUsers) { HML_WARN("users: more than %d users, '%s' and the rest are skipped", static_cast<int>(kMaxUsers), u.name.c_str()); break; }
        out.push_back(std::move(u));
    }
}

void Users::load(const json* section, const json* local, const std::string& store)
{
    clear();
    store_ = store;
    enabled_ = section && section->is_object() && jbool(*section, "enabled", true);
    if(enabled_) {
        if(const json* names = jget(*section, "levels"); names && names->is_array()) {
            for(const json& n : *names) if(n.is_string() && level_names_.size() < kMaxLevels) level_names_.push_back(n.get<std::string>());
        }
        if(level_names_.empty()) level_names_ = { "Operator", "Maintenance", "Admin" };
        const json* local_list = local && local->is_object() ? jget(*local, "list") : nullptr;
        local_ = local_list && local_list->is_array();
        parse_list(local_ ? local_list : jget(*section, "list"), list_);
        auto_logout_s_ = std::max(0, static_cast<int>(jnum(*section, "auto_logout_s", 300)));
        max_failures_ = std::max(1, static_cast<int>(jnum(*section, "max_failures", 5)));
        lock_s_ = std::max(1, static_cast<int>(jnum(*section, "lock_s", 60)));
        system_level_ = std::clamp(static_cast<int>(jnum(*section, "system_level", max_level())), 0, max_level());
        if(list_.empty()) HML_WARN("users: no user is defined: nobody can log in");
        else if(system_level_ > 0 && std::none_of(list_.begin(), list_.end(), [&](const UserInfo& u) { return u.level >= system_level_; }))
            HML_WARN("users: no user has level %d: nobody can open the system settings", system_level_);
        HML_INFO("users: %d user(s)%s, %d level(s), auto logout %d s", static_cast<int>(list_.size()), local_ ? " from project.local.json" : "",
                 max_level(), auto_logout_s_);
    }
    update_tags();
}

void Users::clear()
{
    enabled_ = local_ = false;
    level_names_.clear();
    list_.clear();
    locks_.clear();
    name_.clear();
    level_ = 0;
    required_ = 0;
    resume_.clear();
    auto_logout_s_ = 0;
    system_level_ = 0;
}

std::string Users::level_name(int level) const
{
    return level >= 1 && level <= max_level() ? level_names_[static_cast<size_t>(level - 1)] : std::string();
}

void Users::update_tags()
{
    set_tag("sys.user.name", TagType::String, Value::string(name_));
    set_tag("sys.user.level", TagType::Int, Value::number(level_, TagType::Int));
    set_tag("sys.user.level_name", TagType::String, Value::string(level_name(level_)));
    set_tag("sys.user.logged_in", TagType::Bool, Value::number(level_ > 0 ? 1 : 0, TagType::Bool));
}

// ================================================================ login / logout

void Users::set_clock(std::function<uint32_t()> now_ms, std::function<uint32_t()> inactive_ms)
{
    now_fn_ = std::move(now_ms);
    inactive_fn_ = std::move(inactive_ms);
}

uint32_t Users::now() const { return now_fn_ ? now_fn_() : millis(); }
uint32_t Users::inactive() const { return inactive_fn_ ? inactive_fn_() : lv_display_get_inactive_time(nullptr); }

Users::Lock& Users::lock_of(const std::string& name)
{
    for(Lock& l : locks_) if(same_name(l.name, name)) return l;
    locks_.push_back(Lock{ name, 0, 0, false });
    return locks_.back();
}

int Users::failures_left(const std::string& name) const
{
    for(const Lock& l : locks_) if(same_name(l.name, name)) return std::max(0, max_failures_ - l.failures);
    return max_failures_;
}

void Users::set_current(const std::string& name, int level)
{
    name_ = name;
    level_ = level;
    login_at_ms_ = now();
    update_tags();
    ui().refresh_access();
}

Users::LoginResult Users::login(const std::string& name, const std::string& pin, int* wait_s)
{
    if(wait_s) *wait_s = 0;
    auto it = std::find_if(list_.begin(), list_.end(), [&](const UserInfo& u) { return same_name(u.name, name); });
    if(!enabled_ || it == list_.end()) return LoginResult::UnknownUser;
    Lock& lock = lock_of(it->name);
    if(lock.locked) {
        int32_t left = static_cast<int32_t>(lock.until_ms - now());
        if(left > 0) { if(wait_s) *wait_s = (left + 999) / 1000; return LoginResult::Locked; }
        lock.locked = false;                                   // the lock has run out: a fresh set of tries
        lock.failures = 0;
    }
    if(hash_pin(it->salt, pin) != it->hash) {
        lock.failures++;
        if(lock.failures >= max_failures_) {
            lock.locked = true;
            lock.until_ms = now() + static_cast<uint32_t>(lock_s_) * 1000u;
            if(wait_s) *wait_s = lock_s_;
            HML_WARN("users: '%s' is locked for %d s after %d wrong PINs", it->name.c_str(), lock_s_, lock.failures);
            operation_log().record("user", it->name, "", "locked");
            return LoginResult::Locked;
        }
        HML_WARN("users: wrong PIN for '%s' (%d of %d)", it->name.c_str(), lock.failures, max_failures_);
        operation_log().record("user", it->name, "", "login failed");
        return LoginResult::WrongPin;
    }
    lock.failures = 0;
    if(level_ > 0 && !same_name(name_, it->name)) logout("logout");
    set_current(it->name, it->level);
    HML_INFO("users: '%s' logged in (level %d %s)", name_.c_str(), level_, level_name(level_).c_str());
    operation_log().record("user", name_, "", "login");
    return LoginResult::Ok;
}

void Users::logout(const char* reason)
{
    if(level_ == 0) return;
    HML_INFO("users: '%s' logged out (%s)", name_.c_str(), reason);
    operation_log().record("user", name_, "", reason);        // (recorded under the user who leaves)
    set_current("", 0);
    // a screen the operator may no longer see is closed
    std::string cur = ui().current_screen().value_or("");
    if(!cur.empty() && cur != kLoginScreen && ui().screen_level(cur) > 0) {
        ui().clear_history();
        if(!home_.empty() && ui().screen_level(home_) == 0) ui().show_screen(home_, "none", true);
        else request_login(ui().screen_level(home_.empty() ? cur : home_), home_.empty() ? cur : home_, "none");
    }
}

void Users::request_login(int required, const std::string& resume_screen, const std::string& anim)
{
    required_ = required;
    resume_ = resume_screen;
    resume_anim_ = anim;
    refresh_login_tags();
    // (no animation when nothing is shown yet: at startup there is no screen to fade from)
    if(ui().current_screen().value_or("") != kLoginScreen) ui().show_screen(kLoginScreen, ui().current_screen() ? "fade" : "none");
}

void Users::finish_login(bool ok)
{
    int req = required_;
    std::string resume = resume_, anim = resume_anim_;
    required_ = 0;
    resume_.clear();
    if(ok && !resume.empty() && level_ >= req) { ui().show_screen(resume, anim, true); return; }
    if(ui().back()) return;
    if(!home_.empty() && (ui().screen_level(home_) == 0 || level_ >= ui().screen_level(home_))) ui().show_screen(home_, "none", true);
}

void Users::poll()
{
    if(!enabled_ || level_ == 0 || auto_logout_s_ <= 0) return;
    uint32_t limit = static_cast<uint32_t>(auto_logout_s_) * 1000u;
    if(inactive() >= limit && now() - login_at_ms_ >= limit) logout("auto logout");
}

// ================================================================ editing on the device

std::string Users::save_local()
{
    if(store_.empty()) return "no place to store the users";
    json overlay = json::object();
    {
        std::ifstream f(std::filesystem::u8path(store_), std::ios::binary);
        if(f) {
            std::stringstream ss;
            ss << f.rdbuf();
            try { overlay = json::parse(ss.str(), nullptr, true, true); } catch(const json::exception&) { overlay = json::object(); }
            if(!overlay.is_object()) overlay = json::object();
        }
    }
    json arr = json::array();
    for(const UserInfo& u : list_) arr.push_back({ { "name", u.name }, { "level", u.level }, { "pin_hash", u.salt + "$" + u.hash } });
    overlay["users"] = { { "list", arr } };
    std::ofstream f(std::filesystem::u8path(store_), std::ios::binary | std::ios::trunc);
    if(!f) return "cannot write " + store_;
    f << overlay.dump(2) << "\n";
    if(!f.good()) return "cannot write " + store_;
    local_ = true;
    return "";
}

std::string Users::set_user(const std::string& name, int level, const std::string& pin)
{
    if(!enabled_) return "user management is not enabled";
    if(!valid_name(name)) return "name: 1 to 16 letters, digits, _ - . or spaces";
    if(level < 1 || level > max_level()) return "level: 1 to " + std::to_string(max_level());
    if(!pin.empty() && !valid_pin(pin)) return "PIN: 4 to 8 digits";
    auto it = std::find_if(list_.begin(), list_.end(), [&](const UserInfo& u) { return same_name(u.name, name); });
    std::vector<UserInfo> before = list_;
    if(it == list_.end()) {
        if(pin.empty()) return "a new user needs a PIN (4 to 8 digits)";
        if(list_.size() >= kMaxUsers) return "no more than " + std::to_string(kMaxUsers) + " users";
        UserInfo u;
        u.name = name;
        u.level = level;
        u.salt = new_salt();
        u.hash = hash_pin(u.salt, pin);
        list_.push_back(std::move(u));
        HML_INFO("users: '%s' added (level %d)", name.c_str(), level);
        operation_log().record("user", name, "", "added, level " + std::to_string(level));
    }
    else {
        const bool was_manager = system_level_ > 0 && it->level >= system_level_;
        const bool others = std::any_of(list_.begin(), list_.end(), [&](const UserInfo& u) { return &u != &*it && u.level >= system_level_; });
        if(was_manager && level < system_level_ && !others) return "'" + it->name + "' is the last user who can open the system settings";
        std::string what;
        if(it->level != level) what = "level " + std::to_string(it->level) + " -> " + std::to_string(level);
        it->level = level;
        if(!pin.empty()) { it->salt = new_salt(); it->hash = hash_pin(it->salt, pin); what += what.empty() ? "PIN changed" : ", PIN changed"; lock_of(it->name) = Lock{ it->name, 0, 0, false }; }
        if(what.empty()) return "";                            // nothing to change
        HML_INFO("users: '%s' changed (%s)", it->name.c_str(), what.c_str());
        operation_log().record("user", it->name, "", what);
        if(same_name(name_, it->name) && level_ != level) set_current(it->name, level);
    }
    std::string err = save_local();
    if(!err.empty()) { list_ = std::move(before); return err; }
    refresh_login_tags();
    return "";
}

std::string Users::remove_user(const std::string& name)
{
    auto it = std::find_if(list_.begin(), list_.end(), [&](const UserInfo& u) { return same_name(u.name, name); });
    if(it == list_.end()) return "no such user";
    const bool others = std::any_of(list_.begin(), list_.end(), [&](const UserInfo& u) { return &u != &*it && u.level >= system_level_; });
    if(system_level_ > 0 && it->level >= system_level_ && !others) return "'" + it->name + "' is the last user who can open the system settings";
    std::vector<UserInfo> before = list_;
    std::string removed = it->name;
    list_.erase(it);
    std::string err = save_local();
    if(!err.empty()) { list_ = std::move(before); return err; }
    HML_INFO("users: '%s' removed", removed.c_str());
    operation_log().record("user", removed, "", "removed");
    if(same_name(name_, removed)) logout("removed");
    refresh_login_tags();
    return "";
}

Users& users()
{
    static Users inst;
    return inst;
}

} // namespace hml
