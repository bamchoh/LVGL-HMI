// User management: who is logged in, and with which level.
//
//   "users": {
//     "levels": ["Operator", "Maintenance", "Admin"],     // names of level 1, 2, 3 (0 = nobody logged in)
//     "list": [ { "name": "op", "level": 1, "pin": "1111" },
//               { "name": "admin", "level": 3, "pin_hash": "<salt>$<sha256(salt:pin)>" } ],
//     "auto_logout_s": 300,      // log out after this long without input (0 = never)
//     "max_failures": 5,         // wrong PINs in a row before the user is locked ...
//     "lock_s": 60,              // ... for this long
//     "system_level": 3          // level needed for the system settings screens (default: the highest)
//   }
//
// A user has a name, a level and a PIN (digits). Screens and widgets ask for a minimum level
// ("level": 2 in their JSON, see ui.cpp); the system settings screens ask for system_level.
// Without a "users" section nothing is restricted.
//
// The list can also be edited on the device (System > Users). Such a change is stored in
// project.local.json ("users": { "list": [...] }) and then replaces the project's list on
// that device: a deploy does not undo it.
//
// Tags for screens: sys.user.name, sys.user.level, sys.user.level_name, sys.user.logged_in.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

struct UserInfo {
    std::string name;
    int         level = 1;
    std::string salt, hash;        // hash = sha256 hex of "<salt>:<pin>"
};

class Users {
public:
    enum class LoginResult { Ok, WrongPin, UnknownUser, Locked };

    // section = "users" of project.json (nullptr = feature off); local = "users" of
    // project.local.json (nullptr = none); store = path of project.local.json for device edits.
    void load(const json* section, const json* local, const std::string& store);
    void clear();

    bool enabled() const { return enabled_; }
    int  level() const { return level_; }                     // 0 = nobody logged in
    // May the current user do something that asks for `required`? (always, without user management)
    bool allowed(int required) const { return !enabled_ || required <= level_; }
    const std::string& name() const { return name_; }         // "" = nobody
    int  max_level() const { return static_cast<int>(level_names_.size()); }
    std::string level_name(int level) const;                  // "" for 0
    const std::vector<std::string>& level_names() const { return level_names_; }
    int  system_level() const { return enabled_ ? system_level_ : 0; }
    const std::vector<UserInfo>& list() const { return list_; }
    bool local_list() const { return local_; }                // the list comes from project.local.json

    // wait_s: seconds until the user may try again (Locked)
    LoginResult login(const std::string& name, const std::string& pin, int* wait_s = nullptr);
    void logout(const char* reason = "logout");
    int  failures_left(const std::string& name) const;        // tries before the lock

    // Something needs `required`: show the login screen; after a login that is high enough
    // continue to resume_screen ("" = back to where the operator was).
    void request_login(int required, const std::string& resume_screen = "", const std::string& anim = "fade");
    // The login screen is done (ok = a user logged in): go on to the pending screen or back.
    void finish_login(bool ok);
    int  required() const { return required_; }               // what the pending request needs (0 = none)

    // Auto logout; call regularly on the UI thread.
    void poll();
    void set_home(const std::string& screen) { home_ = screen; }   // where to go when a logout closes a screen

    // ---- editing on the device (stored in project.local.json). Return "" or a message.
    std::string set_user(const std::string& name, int level, const std::string& pin);   // add, or change (pin "" = keep)
    std::string remove_user(const std::string& name);

    static std::string hash_pin(const std::string& salt, const std::string& pin);
    static bool valid_pin(const std::string& pin);            // 4..8 digits
    static bool valid_name(const std::string& name);          // 1..16 of letters, digits, _ - . space

    // hml_runtime --check / --render: every screen can be shown (the highest level, no PIN asked).
    // Nothing is operated in that mode.
    void preview_as_highest() { if(enabled()) set_current("preview", max_level()); }
    // Tests: replace the clocks (milliseconds; inactive = since the last input).
    void set_clock(std::function<uint32_t()> now_ms, std::function<uint32_t()> inactive_ms);

private:
    struct Lock { std::string name; int failures = 0; uint32_t until_ms = 0; bool locked = false; };
    void parse_list(const json* arr, std::vector<UserInfo>& out) const;
    void set_current(const std::string& name, int level);
    void update_tags();
    std::string save_local();
    Lock& lock_of(const std::string& name);
    uint32_t now() const;
    uint32_t inactive() const;

    bool enabled_ = false, local_ = false;
    std::vector<std::string> level_names_;
    std::vector<UserInfo> list_;
    std::string store_, home_;
    int auto_logout_s_ = 0, max_failures_ = 5, lock_s_ = 60, system_level_ = 0;
    std::string name_;
    int level_ = 0;
    uint32_t login_at_ms_ = 0;
    std::vector<Lock> locks_;
    int required_ = 0;
    std::string resume_, resume_anim_;
    std::function<uint32_t()> now_fn_, inactive_fn_;
};

Users& users();

// The built-in login screen ("__login") and its actions (system_users.cpp).
std::vector<json> login_screens(int width, int height);
void register_user_actions();
void refresh_login_tags();           // after the user list or the login state changed

} // namespace hml
