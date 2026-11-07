// UI loader: builds LVGL screens from JSON and wires widgets to the tag store.
// See README.md for the screen file format.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

struct AppCallbacks {
    std::function<void()> request_reload;
    std::function<void()> request_exit;
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

    bool show_screen(const std::string& name, const std::string& anim);
    bool back();
    std::optional<std::string> current_screen() const;
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
