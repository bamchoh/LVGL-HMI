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

    // Delete every screen built so far, free bindings, drop the UI tag subscriptions.
    void unload_all();

    // Named actions for { "action": "call", "fn": "<name>", "arg": "..." }. Registering a name again replaces it.
    void register_action(const std::string& name, std::function<void(const std::string& arg)> fn);

    bool show_screen(const std::string& name, const std::string& anim);
    bool back();
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

} // namespace hml
