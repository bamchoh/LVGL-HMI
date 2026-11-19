// User management: users and levels, PIN login with lock, auto logout, the "level" of widgets and
// screens, the login screen, the system settings gate, editing users on the device.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir()
    {
        dir = fs::temp_directory_path() / ("hml_users_test_" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this))));
        fs::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string path(const char* name) const { return (dir / name).generic_string(); }
    std::string write(const char* name, const std::string& text) const
    {
        std::ofstream f(dir / name, std::ios::binary);
        f << text;
        return path(name);
    }
};

json read_json(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return json::parse(ss.str(), nullptr, false, true);
}

const char* kUsers = R"json({
    "levels": ["Operator", "Maintenance", "Admin"],
    "auto_logout_s": 60, "max_failures": 3, "lock_s": 30,
    "list": [ { "name": "op", "level": 1, "pin": "1111" },
              { "name": "maint", "level": 2, "pin": 2222 },
              { "name": "admin", "level": 3, "pin": "0000" } ] })json";

// a clock the test moves by hand
struct Clock {
    uint32_t now = 1000, inactive = 0;
    void install() { users().set_clock([this] { return now; }, [this] { return inactive; }); }
};

std::string tagstr(const std::string& name) { Tag* t = tags().find(name); return t ? t->get().format() : std::string("<none>"); }
double tagnum(const std::string& name) { Tag* t = tags().find(name); return t ? t->get_number() : -999; }

// type a PIN on the login screen's keypad and press OK
void keypad(const std::string& pin)
{
    for(char c : pin) ui().call_action("user.key", std::string(1, c));
    ui().call_action("user.key", "OK");
}

const char* kProject = R"json({
    "start_screen": "main",
    "tags": [ { "name": "x", "type": "int" }, { "name": "run", "type": "bool" } ],
    "users": { "max_failures": 3, "lock_s": 30, "auto_logout_s": 60,
               "list": [ { "name": "op", "level": 1, "pin": "1111" }, { "name": "maint", "level": 2, "pin": "2222" },
                         { "name": "admin", "level": 3, "pin": "0000" } ] },
    "operation_log": { "path": "op.db" },
    "screens": [
        { "name": "main", "children": [
            { "type": "button", "text": "inc", "level": 1, "on_click": { "action": "inc", "tag": "x" } },
            { "type": "button", "text": "hidden", "level": 2, "level_deny": "hide", "on_click": { "action": "inc", "tag": "x", "by": 10 } },
            { "type": "button", "text": "ask", "level": 2, "level_deny": "login", "on_click": { "action": "inc", "tag": "x", "by": 100 } },
            { "type": "switch", "bind": "run", "level": 1 },
            { "type": "button", "text": "off", "disabled": true, "level": 1 },
            { "type": "button", "text": "login", "on_click": { "action": "login" } },
            { "type": "button", "text": "logout", "on_click": { "action": "logout" } } ] },
        { "name": "setup", "level": 2, "children": [ { "type": "label", "text": "setup" } ] },
        { "name": "free", "children": [ { "type": "label", "text": "free" } ] } ] })json";

} // namespace

TEST_CASE("users: PINs and names are validated, hashes are salted")
{
    CHECK(Users::valid_pin("1234"));
    CHECK(Users::valid_pin("12345678"));
    CHECK_FALSE(Users::valid_pin("123"));
    CHECK_FALSE(Users::valid_pin("123456789"));
    CHECK_FALSE(Users::valid_pin("12a4"));
    CHECK(Users::valid_name("op 1"));
    CHECK(Users::valid_name("a.b_c-d"));
    CHECK_FALSE(Users::valid_name(""));
    CHECK_FALSE(Users::valid_name(" op"));
    CHECK_FALSE(Users::valid_name("12345678901234567"));
    CHECK_FALSE(Users::valid_name("op$"));
    CHECK(Users::hash_pin("s1", "1234") != Users::hash_pin("s2", "1234"));
    CHECK(Users::hash_pin("s1", "1234") == Users::hash_pin("s1", "1234"));
    CHECK(Users::hash_pin("s1", "1234").size() == 64);
}

TEST_CASE("users: without a \"users\" section nothing is restricted")
{
    Runtime rt;
    users().load(nullptr, nullptr, "");
    CHECK_FALSE(users().enabled());
    CHECK(users().level() == 0);
    CHECK(users().allowed(3));                                   // every level is allowed
    CHECK(users().system_level() == 0);
    CHECK(users().login("op", "1111") == Users::LoginResult::UnknownUser);
    CHECK(tagnum("sys.user.level") == 0);                        // the tags exist all the same

    json off = json::parse(R"({ "enabled": false, "list": [ { "name": "op", "level": 1, "pin": "1111" } ] })");
    users().load(&off, nullptr, "");
    CHECK_FALSE(users().enabled());
    CHECK(users().allowed(9));
}

TEST_CASE("users: login, wrong PIN, lock after too many failures, logout")
{
    Runtime rt;
    Clock clock;
    clock.install();
    json cfg = json::parse(kUsers);
    users().load(&cfg, nullptr, "");
    REQUIRE(users().enabled());
    CHECK(users().list().size() == 3);
    CHECK(users().max_level() == 3);
    CHECK(users().system_level() == 3);                          // default: the highest level
    CHECK(users().level_name(2) == "Maintenance");
    CHECK(users().level_name(0) == "");
    CHECK_FALSE(users().allowed(1));
    CHECK(users().allowed(0));
    CHECK(tagstr("sys.user.name") == "");
    CHECK(tagnum("sys.user.logged_in") == 0);

    CHECK(users().login("nobody", "1111") == Users::LoginResult::UnknownUser);
    CHECK(users().login("op", "9999") == Users::LoginResult::WrongPin);
    CHECK(users().failures_left("op") == 2);
    CHECK(users().level() == 0);
    CHECK(users().login("OP", "1111") == Users::LoginResult::Ok);   // names are not case sensitive
    CHECK(users().name() == "op");
    CHECK(users().level() == 1);
    CHECK(users().failures_left("op") == 3);                     // a success clears the count
    CHECK(tagstr("sys.user.name") == "op");
    CHECK(tagnum("sys.user.level") == 1);
    CHECK(tagstr("sys.user.level_name") == "Operator");
    CHECK(tagnum("sys.user.logged_in") == 1);
    CHECK(users().allowed(1));
    CHECK_FALSE(users().allowed(2));

    CHECK(users().login("maint", "2222") == Users::LoginResult::Ok);   // a numeric "pin" in the file works too
    CHECK(users().level() == 2);

    // three wrong PINs lock that user (not the others) for lock_s
    int wait = 0;
    CHECK(users().login("admin", "1") == Users::LoginResult::WrongPin);
    CHECK(users().login("admin", "2") == Users::LoginResult::WrongPin);
    CHECK(users().login("admin", "3", &wait) == Users::LoginResult::Locked);
    CHECK(wait == 30);
    CHECK(users().login("admin", "0000", &wait) == Users::LoginResult::Locked);   // even the right PIN
    CHECK(users().level() == 2);                                 // whoever was logged in stays
    CHECK(users().login("op", "1111") == Users::LoginResult::Ok);
    clock.now += 29000;
    CHECK(users().login("admin", "0000", &wait) == Users::LoginResult::Locked);
    CHECK(wait == 1);
    clock.now += 1500;
    CHECK(users().login("admin", "0000") == Users::LoginResult::Ok);
    CHECK(users().level() == 3);

    users().logout();
    CHECK(users().level() == 0);
    CHECK(users().name() == "");
    CHECK(tagnum("sys.user.logged_in") == 0);
    users().logout();                                            // nothing to do
}

TEST_CASE("users: auto logout after a time without input")
{
    Runtime rt;
    Clock clock;
    clock.install();
    json cfg = json::parse(kUsers);
    users().load(&cfg, nullptr, "");
    REQUIRE(users().login("op", "1111") == Users::LoginResult::Ok);
    clock.now += 59000; clock.inactive = 59000;
    users().poll();
    CHECK(users().level() == 1);
    clock.inactive = 100;                                        // a touch: the time starts again
    clock.now += 30000;
    users().poll();
    CHECK(users().level() == 1);
    clock.now += 60000; clock.inactive = 60100;
    users().poll();
    CHECK(users().level() == 0);

    json never = json::parse(R"({ "auto_logout_s": 0, "list": [ { "name": "op", "level": 1, "pin": "1111" } ] })");
    users().load(&never, nullptr, "");
    REQUIRE(users().login("op", "1111") == Users::LoginResult::Ok);
    clock.now += 86400000u; clock.inactive = 86400000u;
    users().poll();
    CHECK(users().level() == 1);
}

TEST_CASE("users: malformed entries are skipped, hashed PINs are read")
{
    Runtime rt;
    std::string hash = Users::hash_pin("abcd", "4321");
    json cfg = json::parse(R"({ "levels": ["A", "B"], "list": [
        { "name": "ok", "level": 2, "pin_hash": "abcd$)" + hash + R"(" },
        { "name": "ok", "level": 1, "pin": "1111" },
        { "name": "bad pin", "level": 1, "pin": "12" },
        { "name": "", "level": 1, "pin": "1111" },
        { "name": "high", "level": 9, "pin": "1111" },
        "nonsense" ] })");
    users().load(&cfg, nullptr, "");
    REQUIRE(users().list().size() == 2);
    CHECK(users().list()[0].name == "ok");
    CHECK(users().list()[1].name == "high");
    CHECK(users().list()[1].level == 2);                         // clamped to the highest level
    CHECK(users().login("ok", "4321") == Users::LoginResult::Ok);
    CHECK(users().login("bad pin", "12") == Users::LoginResult::UnknownUser);
}

TEST_CASE("users: the level of widgets (disable / hide / login) and of screens")
{
    Runtime rt;
    TempDir tmp;
    Clock clock;
    Project p;
    REQUIRE(p.load(tmp.write("project.json", kProject)));
    clock.install();
    p.start_logs();
    REQUIRE(p.build_screens() == 3);
    lv_obj_t* scr = lv_screen_active();
    lv_obj_t* inc = lv_obj_get_child(scr, 0);
    lv_obj_t* hidden = lv_obj_get_child(scr, 1);
    lv_obj_t* ask = lv_obj_get_child(scr, 2);
    lv_obj_t* sw = lv_obj_get_child(scr, 3);
    lv_obj_t* off = lv_obj_get_child(scr, 4);
    Tag* x = tags().find("x");
    Tag* run = tags().find("run");
    CHECK(ui().current_screen().value_or("") == "main");
    CHECK(ui().screen_level("setup") == 2);
    CHECK(ui().screen_level("main") == 0);
    CHECK(ui().screen_level("__system") == 3);                   // the system settings: the highest level
    CHECK(ui().screen_level("__login") == 0);

    // ---- nobody logged in
    CHECK(lv_obj_has_state(inc, LV_STATE_DISABLED));
    CHECK(lv_obj_is_hidden(hidden));
    CHECK_FALSE(lv_obj_has_state(ask, LV_STATE_DISABLED));       // "login": looks normal
    CHECK(lv_obj_has_state(sw, LV_STATE_DISABLED));
    lv_obj_send_event(inc, LV_EVENT_CLICKED, nullptr);           // even if an event gets through: no action
    lv_obj_send_event(hidden, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(ask, LV_EVENT_CLICKED, nullptr);
    CHECK(x->get_number() == 0);
    lv_obj_add_state(sw, LV_STATE_CHECKED);                      // a switch that was moved anyway is put back
    lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(run->get_number() == 0);
    CHECK_FALSE(lv_obj_has_state(sw, LV_STATE_CHECKED));

    // ---- a touch on the "login" widget opens the login screen; afterwards back to where we were
    lv_obj_send_event(ask, LV_EVENT_PRESSED, nullptr);
    CHECK(ui().current_screen().value_or("") == "__login");
    CHECK(users().required() == 2);
    CHECK(tagstr("sys.login.message") == "Maintenance or higher is needed.");
    CHECK(tagstr("sys.login.u1") == "op");
    CHECK(tagnum("sys.login.u3_used") == 1);
    CHECK(tagnum("sys.login.u4_used") == 0);
    keypad("1111");                                              // no user picked
    CHECK(tagstr("sys.login.message") == "Pick a user first.");
    ui().call_action("user.select", "1");
    CHECK(tagstr("sys.login.user") == "op");
    ui().call_action("user.key", "1"); ui().call_action("user.key", "2");
    CHECK(tagstr("sys.login.pin") == "* *");
    ui().call_action("user.key", "BS");
    CHECK(tagstr("sys.login.pin") == "*");
    ui().call_action("user.key", "C");
    CHECK(tagstr("sys.login.pin") == "PIN");
    keypad("9999");
    CHECK(tagstr("sys.login.message") == "Wrong PIN (2 more tries).");
    keypad("1111");                                              // right, but the level is too low for what was asked
    CHECK(users().level() == 1);
    CHECK(ui().current_screen().value_or("") == "__login");
    CHECK(tagstr("sys.login.message").find("Maintenance or higher") != std::string::npos);
    ui().call_action("user.select", "2");
    keypad("2222");
    CHECK(users().name() == "maint");
    CHECK(ui().current_screen().value_or("") == "main");
    CHECK(users().required() == 0);

    // ---- level 2: everything on this screen works
    CHECK_FALSE(lv_obj_has_state(inc, LV_STATE_DISABLED));
    CHECK_FALSE(lv_obj_is_hidden(hidden));
    CHECK(lv_obj_has_state(off, LV_STATE_DISABLED));             // "disabled": true in the file stays
    lv_obj_send_event(inc, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(hidden, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(ask, LV_EVENT_PRESSED, nullptr);
    lv_obj_send_event(ask, LV_EVENT_CLICKED, nullptr);
    CHECK(x->get_number() == 111);
    CHECK(ui().current_screen().value_or("") == "main");
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
    CHECK(run->get_number() == 1);

    // ---- a protected screen: allowed now, closed again when the user logs out
    CHECK(ui().show_screen("setup", "none"));
    CHECK(ui().current_screen().value_or("") == "setup");
    lv_obj_send_event(lv_obj_get_child(scr, 6), LV_EVENT_CLICKED, nullptr);    // { "action": "logout" }
    CHECK(users().level() == 0);
    CHECK(ui().current_screen().value_or("") == "main");         // sent home
    CHECK_FALSE(ui().back());                                    // and "back" does not lead into it again
    CHECK(lv_obj_has_state(inc, LV_STATE_DISABLED));

    // ---- going to it without the level: login first, then on to the screen
    CHECK_FALSE(ui().show_screen("setup", "none"));
    CHECK(ui().current_screen().value_or("") == "__login");
    ui().call_action("user.cancel", "");                         // changed my mind
    CHECK(ui().current_screen().value_or("") == "main");
    CHECK_FALSE(ui().show_screen("setup", "none"));
    ui().call_action("user.select", "3");
    keypad("0000");
    CHECK(ui().current_screen().value_or("") == "setup");
    CHECK(ui().back());                                          // the login screen is not in the history
    CHECK(ui().current_screen().value_or("") == "main");

    // ---- auto logout while a protected screen is shown
    CHECK(ui().show_screen("setup", "none"));
    clock.now += 61000; clock.inactive = 61000;
    users().poll();
    CHECK(users().level() == 0);
    CHECK(ui().current_screen().value_or("") == "main");

    // ---- { "action": "login" } just opens the screen; back returns
    lv_obj_send_event(lv_obj_get_child(scr, 5), LV_EVENT_CLICKED, nullptr);
    CHECK(ui().current_screen().value_or("") == "__login");
    CHECK(tagstr("sys.login.message") == "");
    ui().call_action("user.select", "1");
    keypad("1111");
    CHECK(ui().current_screen().value_or("") == "main");
    CHECK(users().name() == "op");

    // ---- the operation log names the user and the logins, and never the PIN keys
    std::vector<Operation> ops = operation_log().query(0, false);
    bool login_row = false, failed_row = false, auto_row = false, key_row = false, write_by_maint = false;
    for(const Operation& o : ops) {
        if(o.kind == "user" && o.target == "maint" && o.new_value == "login") login_row = true;
        if(o.kind == "user" && o.target == "op" && o.new_value == "login failed") failed_row = true;
        if(o.kind == "user" && o.new_value == "auto logout") auto_row = true;
        if(o.kind == "call" && o.target.rfind("user.", 0) == 0) key_row = true;
        if(o.kind == "write" && o.target == "x" && o.user == "maint") write_by_maint = true;
    }
    CHECK(login_row);
    CHECK(failed_row);
    CHECK(auto_row);
    CHECK_FALSE(key_row);
    CHECK(write_by_maint);
    p.stop_logs();
}

TEST_CASE("users: a start screen that needs a level shows the login screen first")
{
    Runtime rt;
    TempDir tmp;
    Project p;
    REQUIRE(p.load(tmp.write("project.json", R"json({
        "start_screen": "main", "tags": [ { "name": "x" } ],
        "users": { "list": [ { "name": "op", "level": 1, "pin": "1111" } ] },
        "screens": [ { "name": "main", "level": 1, "children": [ { "type": "label", "text": "main" } ] },
                     { "name": "other", "children": [ { "type": "label", "text": "other" } ] } ] })json")));
    REQUIRE(p.build_screens() == 2);
    CHECK(ui().current_screen().value_or("") == "__login");
    CHECK(tagstr("sys.login.user") == "op");                     // the only user is picked already
    keypad("1111");
    CHECK(ui().current_screen().value_or("") == "main");
    users().logout();                                            // home itself is protected: back to the login screen
    CHECK(ui().current_screen().value_or("") == "__login");
}

TEST_CASE("users: the system settings need system_level; users can be edited on the device")
{
    Runtime rt;
    TempDir tmp;
    Project p;
    REQUIRE(p.load(tmp.write("project.json", kProject)));
    REQUIRE(p.build_screens() == 3);
    SystemSettings& sys = system_settings();
    REQUIRE(sys.page("users") != nullptr);
    const std::string local = tmp.path("project.local.json");

    CHECK_FALSE(ui().show_screen("__system", "none"));           // nobody logged in
    CHECK(ui().current_screen().value_or("") == "__login");
    ui().call_action("user.select", "2");
    keypad("2222");                                              // Maintenance is not enough
    CHECK(ui().current_screen().value_or("") == "__login");
    ui().call_action("user.select", "3");
    keypad("0000");
    CHECK(ui().current_screen().value_or("") == "__system");
    CHECK(ui().show_screen("__system_users", "none"));
    CHECK(tagstr("sys.users.k1") == "op  (Operator)");
    CHECK(tagstr("sys.users.count").find("as defined in the project") != std::string::npos);
    CHECK_FALSE(fs::exists(local));

    auto form = [](const char* name, int level, const char* pin) {
        tags().find("sys.users.name")->set_string(name, nullptr);
        tags().find("sys.users.level")->set_number(level - 1, nullptr);      // the dropdown's index
        tags().find("sys.users.pin")->set_string(pin, nullptr);
    };

    // ---- add
    form("nina", 2, "12");
    sys.command("users", "save");
    CHECK(tagstr("sys.users.status") == "PIN: 4 to 8 digits");
    CHECK(tagstr("sys.users.name") == "nina");                   // what was typed stays for a second try
    CHECK(users().list().size() == 3);
    form("nina", 2, "");
    sys.command("users", "save");
    CHECK(tagstr("sys.users.status").find("needs a PIN") != std::string::npos);
    form("ni$na", 2, "4444");
    sys.command("users", "save");
    CHECK(tagstr("sys.users.status").find("name:") == 0);
    form("nina", 2, "4444");
    sys.command("users", "save");
    CHECK(tagstr("sys.users.status") == "'nina' added.");
    CHECK(tagstr("sys.users.name") == "");                       // the form is empty again
    CHECK(tagstr("sys.users.pin") == "");
    REQUIRE(users().list().size() == 4);
    CHECK(tagstr("sys.users.k4") == "nina  (Maintenance)");
    CHECK(tagstr("sys.users.count").find("edited on this device") != std::string::npos);
    CHECK(tagstr("sys.login.u4") == "nina");                     // the login screen knows her at once

    // stored in project.local.json, hashed
    json stored = read_json(local);
    REQUIRE(stored["users"]["list"].size() == 4);
    CHECK(stored["users"]["list"][3]["name"] == "nina");
    CHECK(stored["users"]["list"][3]["level"] == 2);
    CHECK(stored["users"]["list"][3].contains("pin") == false);
    CHECK(stored["users"]["list"][3]["pin_hash"].get<std::string>().find('$') != std::string::npos);
    // nothing but the hash is kept of the PIN. (Not checked by searching the file for "4444":
    // the random salt and the hash are hex digits and contain such a run now and then.)
    for(const json& u : stored["users"]["list"]) {
        CHECK(u.size() == 3);                                    // name, level, pin_hash
        CHECK(u["pin_hash"].get<std::string>().size() == 16 + 1 + 64);
    }

    // ---- change: Edit fills the form; an empty PIN keeps the old one
    sys.command("users", "edit", "4");
    CHECK(tagstr("sys.users.name") == "nina");
    CHECK(tagnum("sys.users.level") == 1);
    form("nina", 1, "");
    sys.command("users", "save");
    CHECK(tagstr("sys.users.status") == "'nina' changed.");
    CHECK(users().list()[3].level == 1);
    form("NINA", 1, "5555");                                     // the same user, a new PIN
    sys.command("users", "save");
    CHECK(users().list().size() == 4);
    users().logout();
    CHECK(users().login("nina", "4444") == Users::LoginResult::WrongPin);
    CHECK(users().login("nina", "5555") == Users::LoginResult::Ok);
    CHECK(users().level() == 1);
    REQUIRE(users().login("admin", "0000") == Users::LoginResult::Ok);

    // ---- delete asks first; the last user who can open the system settings stays
    sys.command("users", "delete", "4");
    CHECK(tagnum("sys.users.confirm") == 1);
    CHECK(tagstr("sys.users.confirm_text") == "Delete the user 'nina'?");
    sys.command("users", "delete_no");
    CHECK(users().list().size() == 4);
    sys.command("users", "delete", "4");
    sys.command("users", "delete_yes");
    CHECK(tagstr("sys.users.status") == "'nina' deleted.");
    CHECK(users().list().size() == 3);
    sys.command("users", "delete", "3");                         // admin, the only level 3
    sys.command("users", "delete_yes");
    CHECK(tagstr("sys.users.status").find("last user who can open the system settings") != std::string::npos);
    CHECK(users().list().size() == 3);
    form("admin", 1, "");                                        // nor may he be lowered
    sys.command("users", "save");
    CHECK(tagstr("sys.users.status").find("last user") != std::string::npos);
    CHECK(users().list()[2].level == 3);

    // ---- deleting the user who is logged in logs him out (after a second admin exists)
    form("boss", 3, "7777");
    sys.command("users", "save");
    sys.command("users", "delete", "3");
    sys.command("users", "delete_yes");
    CHECK(users().level() == 0);
    CHECK(ui().current_screen().value_or("") != "__system_users");

    // ---- a fresh load uses the device's list, not the project's
    Project again;
    REQUIRE(again.load(tmp.path("project.json")));
    CHECK(users().local_list());
    REQUIRE(users().list().size() == 3);
    CHECK(users().list()[2].name == "boss");
    CHECK(users().login("boss", "7777") == Users::LoginResult::Ok);
    CHECK(users().login("admin", "0000") == Users::LoginResult::UnknownUser);
}
