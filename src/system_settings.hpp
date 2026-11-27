// Built-in system settings screens.
//
// A SettingsPage describes a group of settings as a list of fields plus an apply() step. The
// runtime turns every registered page into a screen ("__system_<id>") with the normal widgets
// (text / numeric / password displays, switches, dropdowns), a menu screen ("__system") that
// lists the pages, and tags "sys.<page>.<key>" that hold the edited values. Projects open the
// menu with { "action": "system" }.
//
// Adding a page = implementing SettingsPage and registering it in register_builtin_pages()
// (or from anywhere before Project::load() returns).
#pragma once
#include <functional>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

class Project;

struct SettingField {
    enum class Type { Text, Number, Bool, Select, Info };
    std::string key;                 // [A-Za-z0-9_], unique within the page
    std::string label;
    Type        type = Type::Text;
    std::string value;               // current value as text: "1"/"0" for Bool, the option text for Select
    std::vector<std::string> options;// Select
    bool        live_options = false;// Select: the options can change while the runtime runs (ports that get plugged in)
    bool        password = false;    // Text
    int         decimals = 0;        // Number
    std::string unit;                // Number
};

// A button of a page in the bar at the bottom of its screen: outside what scrolls, so that a
// finger that scrolls cannot press it. Pressing it calls command(<command>, "").
struct PageAction {
    enum class Kind { Neutral, Primary, Ok, Danger };
    std::string text, command;
    Kind        kind = Kind::Neutral;
    std::string visible;             // key of a Bool tag of the page: shown only while it is on ("" = always).
    std::string enabled;             // ... can be pressed only while it is on. With a dot in it: the whole name of any tag
};

// A question of a page ("Remove this designer?"): shown over the top of the page, not in what
// scrolls, while the Bool tag `visible` is on, with the text of the tag `text` and two buttons.
struct PageDialog {
    std::string visible, text;       // keys of tags of the page
    PageAction  yes, no;
    bool        warning = false;     // amber (something waits for an answer) instead of red (cannot be undone)
};

class SettingsPage {
public:
    virtual ~SettingsPage() = default;
    virtual std::string id() const = 0;      // [a-z0-9_]
    virtual std::string title() const = 0;
    // Current values. Called when the tags are (re)filled: at load, after apply, on "system.refresh".
    virtual std::vector<SettingField> fields() = 0;
    // values: key -> text as held by the tags (Bool "1"/"0", Select = option text, Number = "%g").
    // Return "" on success, otherwise a message shown on the page.
    virtual std::string apply(const std::map<std::string, std::string>& values) = 0;
    virtual bool has_apply() const { return true; }
    // More buttons under the form: { text, command } - a press calls command(<command>, "").
    virtual std::vector<std::pair<std::string, std::string>> buttons() const { return {}; }
    // The same with more to say (when it is shown, what kind it is). Default: buttons().
    virtual std::vector<PageAction> actions() const
    {
        std::vector<PageAction> out;
        for(const auto& [text, cmd] : buttons()) out.push_back(PageAction{ text, cmd, PageAction::Kind::Neutral, "", "" });
        return out;
    }
    // The questions the page can ask.
    virtual std::vector<PageDialog> dialogs() const { return {}; }

    // ---- pages that are more than a form (optional)
    // The rows of the page body, as widgets bound to the page's tags. null = one row per field.
    virtual json custom_rows(bool compact, int font) const { (void)compact; (void)font; return nullptr; }
    // { "action": "call", "fn": "system.do", "arg": "<page>:<command>[:<arg>]" } from a widget of
    // the page. The page's tags are refreshed afterwards.
    virtual void command(const std::string& cmd, const std::string& arg) { (void)cmd; (void)arg; }
    // Called a few times a second on the UI thread. Return true when fields() would now give
    // other values (the tags are refreshed then).
    virtual bool poll() { return false; }
    // One short line about the page for the menu, under its name ("192.168.0.8", "Off"). "" = none.
    virtual std::string summary() { return ""; }
};

class SystemSettings {
public:
    void add_page(std::unique_ptr<SettingsPage> page);
    void clear();
    size_t page_count() const { return pages_.size(); }
    SettingsPage* page(const std::string& id) const;
    std::vector<std::string> page_ids() const;

    static std::string tag_name(const std::string& page, const std::string& key) { return "sys." + page + "." + key; }
    static bool is_system_tag(const std::string& name) { return name.rfind("sys.", 0) == 0; }

    // Create (or update) the "sys.*" tags from the pages' current values.
    void create_tags();
    // Re-read one page (or all) into its tags.
    void refresh(const std::string& page_id = "");
    // Collect the page's tag values, call apply(), refresh and set the status tag. Returns the message.
    std::string apply(const std::string& page_id);

    // Screen JSON: the menu plus one screen per page, sized for the display.
    std::vector<json> screens(int width, int height) const;
    static const char* menu_screen() { return "__system"; }
    static std::string page_screen(const std::string& id) { return "__system_" + id; }

    // "system.apply" / "system.refresh" actions (arg = page id) and "system.do"
    // (arg = "<page>:<command>[:<arg>]") for ui().
    void register_actions();
    // Run a page command and refresh the page (what "system.do" does).
    void command(const std::string& page_id, const std::string& cmd, const std::string& arg = "");
    // Let the pages follow what happens outside the UI (see SettingsPage::poll). Called by a
    // timer that register_actions() starts.
    void poll();
    // The colour of what is chosen or pressed ("#rrggbb"): the project's primary colour.
    void set_accent(const std::string& color) { accent_ = color; }
    // A project with a light theme ("theme": { "dark": false }): the system screens are light too.
    void set_light(bool on) { light_ = on; }
    bool light() const { return light_; }

private:
    void update_summaries();
    void translate_status(const std::string& page_id);
    void set_status(const std::string& page_id, const std::string& english, bool problem);
    bool light_ = false;
    std::map<std::string, std::chrono::steady_clock::time_point> status_until_;   // page -> when its news is put away
    std::map<std::string, std::string> status_shown_;            // page -> the status line as it was last written here
    std::string accent_ = "#1e88e5";
    unsigned    summary_ticks_ = 0;
    std::vector<std::unique_ptr<SettingsPage>> pages_;
    std::map<std::string, std::vector<SettingField>> fields_;   // last snapshot per page (for apply)
};

SystemSettings& system_settings();

// Built-in pages. register_builtin_pages() is called by Project::load(); the pages read the
// driver configuration and the overlay path (project.local.json) from that Project.
std::unique_ptr<SettingsPage> make_about_page(Project& proj);
std::unique_ptr<SettingsPage> make_network_page();
// SSH key management (system_keys.cpp): "key_management": { "enabled", "port", "file" } in
// project.json. Shown by default on the Linux console backends only.
std::unique_ptr<SettingsPage> make_keys_page(Project& proj);
// Users of the device (system_users.cpp); only with "users" in project.json.
std::unique_ptr<SettingsPage> make_users_page();
std::unique_ptr<SettingsPage> make_deploy_page(Project& proj);   // System > Deploy: projects from the designer without SSH - on / off, the designers that may send
std::unique_ptr<SettingsPage> make_storage_page(Project& proj);  // System > Storage: backup, a project from a USB memory
std::unique_ptr<SettingsPage> make_monitor_page();             // System > Tags: values of all tags, write one
std::unique_ptr<SettingsPage> make_sound_page();               // System > Sound: mute, volume, a test (projects with "sound")
std::unique_ptr<SettingsPage> make_transfers_page();           // System > Transfers: what runs, what waits and why (projects with "transfers")
std::unique_ptr<SettingsPage> make_upload_page();              // System > Upload: the login of an upload, "send now", what waits (projects with "uploads")
std::unique_ptr<SettingsPage> make_remote_page();              // System > Remote: who may connect with a VNC viewer - on / off, the password, view only (projects with "remote")
std::unique_ptr<SettingsPage> make_mail_page();                // System > Mail: recipients, the password, a test mail (projects with "mail")
std::unique_ptr<SettingsPage> make_schedule_page();            // System > Schedule: time, days, on / off of the schedules (projects that have some)
// System > Device: an address of a PLC without a tag (system_device.cpp); for projects with a
// driver that can do it.
std::unique_ptr<SettingsPage> make_device_page();
bool has_device_driver(const json* drivers);
void add_driver_pages(SystemSettings& s, Project& proj);
void register_builtin_pages(SystemSettings& s, Project& proj);
// The language the system screens are shown in for this project (system_i18n.hpp): set before
// the pages are asked for their texts.
void choose_system_language(Project& proj);
// The colours of a system screen for a light theme. The screens are written with dark colours;
// this puts the light ones in their place (backgrounds, texts, what is pressed), in one spot.
void system_light_palette(json& screen);

// Overlay keys of the drivers array: "name" or "type", "#n" appended for duplicates.
std::vector<std::string> driver_keys(const json* drivers);

// Helpers shared by the pages
std::string run_command(const std::string& cmd, int* exit_code = nullptr);   // stdout+stderr
std::string local_ip_addresses();                                            // "192.168.0.8, 10.0.0.2"

} // namespace hml
