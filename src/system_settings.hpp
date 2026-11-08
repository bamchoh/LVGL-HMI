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
    bool        password = false;    // Text
    int         decimals = 0;        // Number
    std::string unit;                // Number
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

    // "system.apply" / "system.refresh" actions (arg = page id) for ui().
    void register_actions();

private:
    std::vector<std::unique_ptr<SettingsPage>> pages_;
    std::map<std::string, std::vector<SettingField>> fields_;   // last snapshot per page (for apply)
};

SystemSettings& system_settings();

// Built-in pages. register_builtin_pages() is called by Project::load(); the pages read the
// driver configuration and the overlay path (project.local.json) from that Project.
std::unique_ptr<SettingsPage> make_about_page(Project& proj);
std::unique_ptr<SettingsPage> make_network_page();
void add_driver_pages(SystemSettings& s, Project& proj);
void register_builtin_pages(SystemSettings& s, Project& proj);

// Overlay keys of the drivers array: "name" or "type", "#n" appended for duplicates.
std::vector<std::string> driver_keys(const json* drivers);

// Helpers shared by the pages
std::string run_command(const std::string& cmd, int* exit_code = nullptr);   // stdout+stderr
std::string local_ip_addresses();                                            // "192.168.0.8, 10.0.0.2"

} // namespace hml
