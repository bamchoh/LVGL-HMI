// The screens of user management (users.hpp):
//   "__login"            pick a user, type the PIN on a keypad
//   System > Users       add / change / remove users on the device (a SettingsPage)
#include "system_i18n.hpp"
#include "project.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"

namespace hml {

namespace {

constexpr int kSlots = 16;              // users shown (the most there can be)

// ---- what the login screen shows; kept here, mirrored into the sys.login.* tags
struct LoginState {
    std::string user;                   // picked user
    std::string pin;                    // typed so far
    std::string message;
} s_login;

void set_tag(const std::string& name, TagType type, const Value& v)
{
    Tag* t = tags().find(name);
    if(!t) tags().create(name, type, &v, false, 0, 0);
    else t->set(v, nullptr);
}
void set_text(const std::string& name, const std::string& text) { set_tag(name, TagType::String, Value::string(text)); }
void set_flag(const std::string& name, bool on) { set_tag(name, TagType::Bool, Value::number(on ? 1 : 0, TagType::Bool)); }

std::string describe(const UserInfo& u) { return u.name + "  (" + users().level_name(u.level) + ")"; }

void login_key(const std::string& key)
{
    Users& us = users();
    if(key == "C") s_login.pin.clear();
    else if(key == "BS") { if(!s_login.pin.empty()) s_login.pin.pop_back(); }
    else if(key == "OK") {
        if(s_login.user.empty()) s_login.message = "Pick a user first.";
        else {
            int wait = 0;
            Users::LoginResult r = us.login(s_login.user, s_login.pin, &wait);
            s_login.pin.clear();
            if(r == Users::LoginResult::Ok) {
                if(us.level() < us.required()) s_login.message = "Logged in, but this needs " + us.level_name(us.required()) + " or higher.";
                else {
                    s_login.message.clear();
                    s_login.user.clear();
                    refresh_login_tags();
                    us.finish_login(true);
                    return;
                }
            }
            else if(r == Users::LoginResult::Locked) s_login.message = vformat("Locked. Try again in %d s.", wait);
            else if(r == Users::LoginResult::WrongPin) s_login.message = vformat("Wrong PIN (%d more tries).", us.failures_left(s_login.user));
            else s_login.message = "Unknown user.";
        }
    }
    else if(key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
        if(s_login.pin.size() < 8) s_login.pin += key;
        s_login.message.clear();
    }
    refresh_login_tags();
}

} // namespace

void refresh_login_tags()
{
    Users& us = users();
    const auto& list = us.list();
    if(!s_login.user.empty() && std::none_of(list.begin(), list.end(), [](const UserInfo& u) { return iequals(u.name, s_login.user); })) s_login.user.clear();
    if(s_login.user.empty() && list.size() == 1) s_login.user = list[0].name;       // nothing to pick
    set_text("sys.login.user", s_login.user.empty() ? sys_text("Pick a user") : s_login.user);
    std::string mask;
    for(size_t i = 0; i < s_login.pin.size(); i++) mask += i ? " *" : "*";
    set_text("sys.login.pin", mask.empty() ? "PIN" : mask);
    std::string msg = s_login.message;
    if(msg.empty() && us.required() > 0 && us.level() < us.required()) msg = us.level_name(us.required()) + " or higher is needed.";
    set_text("sys.login.message", sys_text(msg));
    for(int i = 0; i < kSlots; i++) {
        bool used = static_cast<size_t>(i) < list.size();
        set_flag("sys.login.u" + std::to_string(i + 1) + "_used", used);
        set_text("sys.login.u" + std::to_string(i + 1), used ? list[static_cast<size_t>(i)].name : "");
    }
}

void register_user_actions()
{
    ui().register_action("user.login", [](const std::string&) {
        if(!users().enabled()) { HML_WARN("login: the project has no \"users\""); return; }
        s_login = LoginState{};
        users().request_login(0);
    });
    ui().register_action("user.logout", [](const std::string&) { users().logout(); refresh_login_tags(); });
    ui().register_action("user.select", [](const std::string& arg) {
        int slot = std::atoi(arg.c_str());
        const auto& list = users().list();
        if(slot < 1 || static_cast<size_t>(slot) > list.size()) return;
        s_login.user = list[static_cast<size_t>(slot - 1)].name;
        s_login.pin.clear();
        s_login.message.clear();
        refresh_login_tags();
    });
    ui().register_action("user.key", [](const std::string& arg) { login_key(arg); });
    ui().register_action("user.cancel", [](const std::string&) {
        s_login = LoginState{};
        refresh_login_tags();
        users().finish_login(false);
    });
}

std::vector<json> login_screens(int width, int height)
{
    std::vector<json> out;
    if(!users().enabled()) return out;
    refresh_login_tags();
    const bool compact = width < 600;
    const int header_h = compact ? 40 : 56;
    const int font = compact ? 12 : 14;
    const int pad = 8, gap = compact ? 5 : 8;
    const int body_y = header_h + pad, body_h = height - header_h - 2 * pad;
    const int list_w = (width - 3 * pad) * 38 / 100, pad_w = width - 3 * pad - list_w;
    const int key_w = (pad_w - 2 * pad - 2 * gap) / 3, key_h = compact ? 34 : 50;
    const char* bg = "#141a22";
    const char* card = "#1b2430";
    auto call = [](const char* fn, const std::string& arg = "") { return json{ { "action", "call" }, { "fn", fn }, { "arg", arg } }; };

    json names = json::array();
    for(int i = 1; i <= kSlots; i++) {
        names.push_back(json{ { "type", "button" }, { "w", "100%" }, { "h", compact ? 34 : 44 },
                              { "bind", { { "text", "sys.login.u" + std::to_string(i) }, { "visible", "sys.login.u" + std::to_string(i) + "_used" } } },
                              { "style", { { "bg_color", "#2c3e50" } } }, { "style_label", { { "font", font + 2 } } },
                              { "on_click", call("user.select", std::to_string(i)) } });
    }
    json keys = json::array();
    for(const char* k : { "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "OK" }) {
        const std::string key = k;
        const char* color = key == "OK" ? "#2e7d32" : key == "C" ? "#455a64" : "#2c3e50";
        keys.push_back(json{ { "type", "button" }, { "text", key }, { "w", key_w }, { "h", key_h }, { "style", { { "bg_color", color } } },
                             { "style_label", { { "font", compact ? 18 : 22 } } }, { "on_click", call("user.key", key) } });
    }
    out.push_back(json{
        { "name", "__login" }, { "scrollable", false }, { "style", { { "bg_color", bg }, { "pad", 0 } } },
        { "children", json::array({
            json{ { "type", "panel" }, { "x", 0 }, { "y", 0 }, { "w", "100%" }, { "h", header_h }, { "scrollable", false },
                  { "style", { { "bg_color", "#1f2a38" }, { "radius", 0 }, { "border_width", 0 }, { "pad_left", 8 }, { "pad_right", 8 }, { "pad_top", 0 }, { "pad_bottom", 0 } } },
                  { "children", json::array({
                      json{ { "type", "button" }, { "align", "left_mid" }, { "text", sys_text("< Back") }, { "h", header_h - 10 }, { "style_label", { { "font", font } } },
                            { "on_click", call("user.cancel") } },
                      json{ { "type", "label" }, { "align", "center" }, { "text", sys_text("Login") }, { "style", { { "font", compact ? 16 : 20 }, { "text_color", "#ffffff" } } } },
                      json{ { "type", "button" }, { "align", "right_mid" }, { "text", sys_text("Logout") }, { "h", header_h - 10 }, { "style", { { "bg_color", "#c62828" } } },
                            { "style_label", { { "font", font } } }, { "bind", { { "visible", "sys.user.logged_in" } } }, { "on_click", call("user.logout") } },
                  }) } },
            json{ { "type", "panel" }, { "x", pad }, { "y", body_y }, { "w", list_w }, { "h", body_h }, { "flex", "column" }, { "scrollbar", "auto" },
                  { "style", { { "bg_color", card }, { "radius", 8 }, { "border_width", 0 }, { "pad", 6 }, { "pad_row", gap } } }, { "children", names } },
            json{ { "type", "panel" }, { "x", 2 * pad + list_w }, { "y", body_y }, { "w", pad_w }, { "h", body_h }, { "flex", "column" }, { "flex_cross", "center" },
                  { "scrollable", false }, { "style", { { "bg_color", card }, { "radius", 8 }, { "border_width", 0 }, { "pad", pad }, { "pad_row", compact ? 3 : 6 } } },
                  { "children", json::array({
                      json{ { "type", "label" }, { "bind", { { "text", "sys.login.user" } } }, { "style", { { "font", compact ? 16 : 20 }, { "text_color", "#ffffff" } } } },
                      json{ { "type", "label" }, { "bind", { { "text", "sys.login.pin" } } }, { "style", { { "font", compact ? 20 : 26 }, { "text_color", "#90caf9" } } } },
                      json{ { "type", "label" }, { "w", "100%" }, { "h", font + 4 }, { "long_mode", "dot" }, { "bind", { { "text", "sys.login.message" } } },
                            { "style", { { "font", font }, { "text_color", "#ffc107" }, { "text_align", "center" } } } },
                      json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row_wrap" }, { "scrollable", false },
                            { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", gap }, { "pad_row", gap } } }, { "children", keys } },
                  }) } },
        }) },
    });
    if(system_settings().light()) for(json& scr : out) system_light_palette(scr);
    return out;
}

// ================================================================ System > Users

namespace {

class UsersPage final : public SettingsPage {
public:
    std::string id() const override { return "users"; }
    std::string title() const override { return "Users"; }
    bool has_apply() const override { return false; }

    std::vector<SettingField> fields() override
    {
        Users& us = users();
        std::vector<SettingField> out;
        auto info = [&](const std::string& key, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Info; f.value = value; out.push_back(f);
        };
        auto flag = [&](const std::string& key, bool on) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Bool; f.value = on ? "1" : "0"; out.push_back(f);
        };
        // the form: its values are what the operator typed, not state of ours
        SettingField name; name.key = "name"; name.label = "name"; name.type = SettingField::Type::Text; name.value = form_name_; out.push_back(name);
        SettingField level; level.key = "level"; level.label = "level"; level.type = SettingField::Type::Select; level.options = us.level_names();
        level.value = us.level_name(form_level_ >= 1 ? form_level_ : 1); out.push_back(level);
        SettingField pin; pin.key = "pin"; pin.label = "pin"; pin.type = SettingField::Type::Text; pin.password = true; pin.value = ""; out.push_back(pin);

        const auto& list = us.list();
        const UserInfo* del = nullptr;
        for(const UserInfo& u : list) if(iequals(u.name, confirm_)) del = &u;
        if(!del) confirm_.clear();
        flag("confirm", del != nullptr);
        info("confirm_text", del ? "Delete the user '" + del->name + "'?" : "");
        info("count", vformat("%d user(s)%s", static_cast<int>(list.size()), us.local_list() ? ", edited on this device" : ", as defined in the project"));
        bool chosen = false;
        for(int i = 0; i < kSlots; i++) {
            bool used = static_cast<size_t>(i) < list.size();
            const bool sel = used && !selected_.empty() && iequals(list[static_cast<size_t>(i)].name, selected_);
            chosen = chosen || sel;
            flag("k" + std::to_string(i + 1) + "_used", used);
            flag("k" + std::to_string(i + 1) + "_sel", sel);
            info("k" + std::to_string(i + 1), used ? describe(list[static_cast<size_t>(i)]) : "");
        }
        if(!chosen) selected_.clear();
        flag("has_sel", chosen);                                  // the bar at the bottom: Delete
        return out;
    }

    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    void command(const std::string& cmd, const std::string& arg) override
    {
        Users& us = users();
        std::string msg;
        auto tag_text = [&](const char* key) { Tag* t = tags().find(SystemSettings::tag_name(id(), key)); return t ? t->get().format() : std::string(); };
        if(cmd == "save") {                                       // add a user, or change the one with this name
            std::string name = tag_text("name"), pin = tag_text("pin");
            Tag* lt = tags().find(SystemSettings::tag_name(id(), "level"));
            int level = lt ? static_cast<int>(lt->get_number()) + 1 : 1;      // the dropdown's index
            const bool existed = std::any_of(us.list().begin(), us.list().end(), [&](const UserInfo& u) { return iequals(u.name, name); });
            msg = us.set_user(name, level, pin);
            if(msg.empty()) { msg = existed ? "'" + name + "' changed." : "'" + name + "' added."; form_name_.clear(); form_level_ = 1; }
            else { form_name_ = name; form_level_ = level; }      // keep what was typed
        }
        else if(cmd == "edit") {                                  // a row was touched: put it into the form
            int slot = std::atoi(arg.c_str());
            if(slot >= 1 && static_cast<size_t>(slot) <= us.list().size()) {
                const UserInfo& u = us.list()[static_cast<size_t>(slot - 1)];
                form_name_ = u.name;
                form_level_ = u.level;
                selected_ = u.name;                               // (and it is the one Delete below is about)
                msg = "Change the level, or type a new PIN, then press Save. An empty PIN keeps the old one.";
            }
        }
        else if(cmd == "delete") {                                // the user chosen, or the row given
            int slot = std::atoi(arg.c_str());
            if(arg.empty()) confirm_ = selected_;
            else if(slot >= 1 && static_cast<size_t>(slot) <= us.list().size()) confirm_ = us.list()[static_cast<size_t>(slot - 1)].name;
        }
        else if(cmd == "delete_yes") {
            if(!confirm_.empty()) { msg = us.remove_user(confirm_); if(msg.empty()) msg = "'" + confirm_ + "' deleted."; }
            confirm_.clear();
        }
        else if(cmd == "delete_no") confirm_.clear();
        else HML_WARN("system: users: unknown command '%s'", cmd.c_str());
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string(msg, nullptr);
    }

    bool picks_row(const std::string& cmd) const override { return cmd == "edit"; }
    json custom_rows(bool compact, int font) const override
    {
        const char* muted = "#9fb3c8";
        auto tag = [&](const std::string& key) { return SystemSettings::tag_name(id(), key); };
        auto call = [&](const std::string& cmd) { return json{ { "action", "call" }, { "fn", "system.do" }, { "arg", id() + ":" + cmd } }; };
        auto label = [&](const char* text) {
            return json{ { "type", "label" }, { "text", text }, { "w", compact ? 44 : 60 }, { "style", { { "font", font }, { "text_color", muted } } } };
        };
        auto row = [&](json children) {
            return json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                         { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 6 } } }, { "children", std::move(children) } };
        };

        json rows = json::array();
        rows.push_back(row(json::array({
            label("Name"),
            json{ { "type", "text_display" }, { "input", true }, { "flex_grow", 1 }, { "max_length", 16 }, { "bind", tag("name") }, { "style", { { "font", font }, { "pad", 6 } } } },
            label("Level"),
            json{ { "type", "dropdown" }, { "options", users().level_names() }, { "w", compact ? 120 : 170 }, { "bind", tag("level") }, { "style", { { "font", font } } } },
        })));
        rows.push_back(row(json::array({
            label("PIN"),
            json{ { "type", "password_display" }, { "input", true }, { "flex_grow", 1 }, { "max_length", 8 }, { "accepted_chars", "0123456789" }, { "bind", tag("pin") },
                  { "style", { { "font", font }, { "pad", 6 } } } },
        })));
        rows.push_back(json{ { "type", "label" }, { "w", "100%" }, { "bind", { { "text", tag("count") } } }, { "style", { { "font", font }, { "text_color", muted } } } });
        // a user: press its row to put it into the form (and to choose it for Delete below)
        for(int i = 1; i <= kSlots; i++) {
            const std::string n = std::to_string(i);
            json r = row(json::array({
                json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "dot" }, { "bind", { { "text", tag("k" + n) } } },
                      { "style", { { "font", font + 2 }, { "text_color", "#ffffff" } } } },
            }));
            r["bind"] = { { "visible", tag("k" + n + "_used") }, { "bg_color", { { "tag", tag("k" + n + "_sel") }, { "map", { { "true", "#2f4a6b" }, { "default", "#222d3b" } } } } } };
            r["on_click"] = call("edit:" + n);
            rows.push_back(std::move(r));
        }
        return rows;
    }

    std::vector<PageAction> actions() const override
    {
        return { { "Save", "save", PageAction::Kind::Primary, "", "" }, { "Delete", "delete", PageAction::Kind::Danger, "", "has_sel" } };
    }
    std::vector<PageDialog> dialogs() const override
    {
        return { { "confirm", "confirm_text", { "Delete", "delete_yes", PageAction::Kind::Danger, "", "" }, { "Cancel", "delete_no", PageAction::Kind::Neutral, "", "" }, false } };
    }

private:
    std::string form_name_;
    int         form_level_ = 1;
    std::string selected_;          // the user chosen on the page (its row was pressed)
    std::string confirm_;           // user waiting for "Delete?" confirmation
};

} // namespace

std::unique_ptr<SettingsPage> make_users_page()
{
    if(!users().enabled()) return nullptr;
    return std::make_unique<UsersPage>();
}

} // namespace hml
