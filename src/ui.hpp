// UI loader: builds LVGL screens from JSON and wires widgets to the tag store.
// See README.md for the screen file format.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

// "restart" action: Default = reboot the machine on the Linux console backends, restart the
// application on the PC (SDL) backend; App / System force one or the other.
enum class RestartTarget { Default, App, System };

struct AppCallbacks {
    std::function<void()> request_reload;
    std::function<void()> request_exit;
    std::function<void(RestartTarget)> request_restart;
};

class Ui {
public:
    Ui();
    ~Ui();

    void init(lv_indev_t* keyboard_indev, AppCallbacks cb);

    // Build a screen from its JSON description. base_dir resolves relative asset paths.
    lv_obj_t* build_screen(const json& screen, const std::string& base_dir);
    // Register a screen without building its widgets yet: it is built when it is first shown,
    // or by build_pending(). A project with many screens can show its first one at once.
    bool add_screen(const json& screen, const std::string& base_dir);
    // Build up to `max` of the screens that are still waiting; returns how many are left.
    size_t build_pending(size_t max = 1);

    // Delete every screen built so far, free bindings, drop the UI tag subscriptions.
    void unload_all();
    // Delete every screen and register them again from their descriptions (built when shown,
    // like after add_screen): texts in another language. No screen is shown afterwards.
    void rebuild_all();

    // Named actions for { "action": "call", "fn": "<name>", "arg": "..." }. Registering a name again replaces it.
    void register_action(const std::string& name, std::function<void(const std::string& arg)> fn);
    bool call_action(const std::string& name, const std::string& arg);   // false if the name is unknown

    // A screen with "level" that the current user may not see is not shown: the login screen
    // comes up instead and continues to it after a login (false is returned).
    // replace: the screen being left is not added to the history ("back" skips it).
    bool show_screen(const std::string& name, const std::string& anim, bool replace = false);
    bool back();                                              // in a popup: closes it
    void clear_history();
    // Popups: screens with "popup": true, shown over the current screen until closed (or until
    // the screen changes). close_popup("") closes the top one. False if nothing happened.
    bool open_popup(const std::string& name);
    bool close_popup(const std::string& name = "");
    std::vector<std::string> open_popups() const;             // bottom first
    int  screen_level(const std::string& name) const;        // the screen's "level" (0 = none / unknown screen)
    // Apply the "level" of the widgets (disable / hide) for the current user; users.cpp calls
    // it whenever the user changes.
    void refresh_access();
    std::optional<std::string> current_screen() const;
    // Navigation history as screen names (oldest first), e.g. to survive a rebuild of the screens.
    std::vector<std::string> history() const;
    void set_history(const std::vector<std::string>& names);   // unknown names are skipped
    size_t screen_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Ui& ui();

// Helpers shared with the project loader
lv_color_t       parse_color(const std::string& s, lv_color_t dflt);   // "#rgb", "#rrggbb", palette names
const lv_font_t* font_from_json(const json* v);                        // 16 or "montserrat_16"
// The project's font file (TTF / OTF, see texts.hpp): every size is made from it. "" = built-in.
void             set_font_file(const std::string& path);
bool             has_font_file();
const lv_font_t* font_of_size(int size);                               // nullptr without a font file
// Does this text field of a screen take numbers (numeric_display)? Its keyboard is the number pad.
bool             ui_number_pad(lv_obj_t* textarea);

} // namespace hml
