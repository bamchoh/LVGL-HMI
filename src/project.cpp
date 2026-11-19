#include "project.hpp"
#include "alarm.hpp"
#include "data_log.hpp"
#include "driver.hpp"
#include "operation_log.hpp"
#include "script.hpp"
#include "system_settings.hpp"
#include "texts.hpp"
#include "screen_saver.hpp"
#include <algorithm>
#include "recipe.hpp"
#include "alarm_view.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"
#include <fstream>
#include <sstream>

namespace hml {
namespace {

std::optional<std::string> read_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if(!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if(text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);   // UTF-8 BOM
    }
    return text;
}

// Parse a JSON file; comments (// and /* */) are tolerated. Returns a null json on failure.
json parse_json_file(const std::string& path)
{
    auto text = read_file(path);
    if(!text) { HML_ERROR("cannot read '%s'", path.c_str()); return nullptr; }
    try {
        return json::parse(*text, nullptr, true, true);
    }
    catch(const json::parse_error& e) {
        HML_ERROR("JSON syntax error in '%s': %s", path.c_str(), e.what());
        return nullptr;
    }
}

} // namespace

// project.local.json: { "drivers": { "<key>": { "host": "...", "enabled": false } } } written by the
// system settings screens. Scalars override the matching driver object; the project file itself
// is never modified.
void Project::apply_local_overlay()
{
    base_drivers_ = root_.is_object() && root_.contains("drivers") ? root_["drivers"] : json();
    local_users_ = json();
    local_screen_saver_ = json();
    std::string path = local_overlay_path();
    std::ifstream probe(path, std::ios::binary);
    if(!probe) return;
    probe.close();
    json overlay = parse_json_file(path);
    if(!overlay.is_object()) return;
    if(const json* lu = jget(overlay, "users")) local_users_ = *lu;
    if(const json* ls = jget(overlay, "screen_saver")) local_screen_saver_ = *ls;
    const json* od = jget(overlay, "drivers");
    json* drivers = root_.is_object() && root_.contains("drivers") && root_["drivers"].is_array() ? &root_["drivers"] : nullptr;
    if(!od || !od->is_object() || !drivers) return;
    std::vector<std::string> keys = driver_keys(drivers);
    int applied = 0;
    for(size_t i = 0; i < keys.size(); i++) {
        const json* o = jget(*od, keys[i].c_str());
        if(!o || !o->is_object()) continue;
        json& cfg = (*drivers)[i];
        for(auto it = o->begin(); it != o->end(); ++it) {
            if(it.value().is_object() || it.value().is_array()) continue;
            cfg[it.key()] = it.value();
            applied++;
        }
    }
    if(applied) HML_INFO("project.local.json: %d driver setting(s) applied", applied);
}

const json* Project::driver_config(const std::string& key) const
{
    const json* drivers = jget(root_, "drivers");
    std::vector<std::string> keys = driver_keys(drivers);
    for(size_t i = 0; i < keys.size(); i++) if(keys[i] == key) return &(*drivers)[i];
    return nullptr;
}

const json* Project::driver_base_config(const std::string& key) const
{
    std::vector<std::string> keys = driver_keys(&base_drivers_);
    for(size_t i = 0; i < keys.size(); i++) if(keys[i] == key) return &base_drivers_[i];
    return nullptr;
}

int Project::restart_drivers()
{
    drivers().stop_all();
    json fresh = parse_json_file(info_.path);
    if(!fresh.is_null()) root_ = std::move(fresh);
    apply_local_overlay();
    // (the system pages are not refreshed here: unsaved edits on other pages must survive an Apply)
    return drivers().load(jget(root_, "drivers"));
}

std::string Project::resolve_path(const std::string& rel) const
{
    if(rel.empty()) return rel;
    bool absolute = rel[0] == '/' || rel[0] == '\\' || (rel.size() > 1 && rel[1] == ':');
    return absolute ? rel : info_.dir + "/" + rel;
}

bool Project::load(const std::string& path)
{
    info_ = ProjectInfo{};
    info_.path = path;
    size_t slash = path.find_last_of("/\\");
    info_.dir = slash == std::string::npos ? "." : path.substr(0, slash);

    root_ = parse_json_file(path);
    if(root_.is_null()) return false;
    apply_local_overlay();

    info_.title = jstr(root_, "title", "HML Runtime");
    const json* disp = jget(root_, "display");
    const json  none = json::object();
    const json& d = disp ? *disp : none;
    info_.width = static_cast<int>(jnum(d, "width", 800));
    info_.height = static_cast<int>(jnum(d, "height", 480));
    info_.zoom = jnum(d, "zoom", 1.0);
    info_.rotation = static_cast<int>(jnum(d, "rotation", 0));
    if(info_.rotation % 90 != 0 || info_.rotation < 0 || info_.rotation > 270) {
        HML_WARN("display.rotation must be 0, 90, 180 or 270 (got %d); using 0", info_.rotation);
        info_.rotation = 0;
    }
    // Linux fbdev/drm drivers derive the DPI from the (often wrong) physical size the kernel
    // reports; fix it so that the theme looks the same on every backend.
    info_.dpi = static_cast<int>(jnum(d, "dpi", 130));
    if(info_.dpi < 50 || info_.dpi > 600) {
        HML_WARN("display.dpi must be 50..600 (got %d); using 130", info_.dpi);
        info_.dpi = 130;
    }
    info_.fbdev = jstr(d, "fbdev", "");
    info_.drm = jstr(d, "drm", "");
    info_.input = jstr(d, "input", "");
    info_.console = jstr(d, "console", "/dev/tty1");
    info_.splash = jstr(d, "splash", "");
    if(!info_.splash.empty()) info_.splash = resolve_path(info_.splash);
    info_.splash_bg = parse_color(jstr(d, "splash_bg", ""), lv_color_black());
    if(const json* touch = jget(d, "touch")) {
        info_.touch_swap_axes = jbool(*touch, "swap_axes", false);
        const json* calib = jget(*touch, "calibration");
        if(calib && calib->is_array() && calib->size() == 4) {
            info_.touch_has_calib = true;
            for(size_t i = 0; i < 4; i++) info_.touch_calib[i] = static_cast<int>(jscalar_to_number((*calib)[i]));
        }
    }

    const json* theme = jget(root_, "theme");
    const json& t = theme ? *theme : none;
    info_.dark = jbool(t, "dark", true);
    info_.primary = parse_color(jstr(t, "primary", ""), lv_palette_main(LV_PALETTE_BLUE));
    info_.secondary = parse_color(jstr(t, "secondary", ""), lv_palette_main(LV_PALETTE_RED));
    info_.start_screen = jstr(root_, "start_screen", "");

    // system log ("system_log"): off unless the section exists
    if(const json* lg = jget(root_, "system_log"); lg && lg->is_object()) {
        info_.system_log.enabled = jbool(*lg, "enabled", true);
        info_.system_log.path = resolve_path(jstr(*lg, "path", "logs/system.db"));
        std::string level = jstr(*lg, "level", "info");
        info_.system_log.level = log_level_from_name(level, LogLevel::Info);
        if(!iequals(level, log_level_name(info_.system_log.level))) HML_WARN("system_log.level must be info, warn or error (got '%s'); using info", level.c_str());
        info_.system_log.retention_days = static_cast<int>(jnum(*lg, "retention_days", 30));
        info_.system_log.max_rows = static_cast<int>(jnum(*lg, "max_rows", 100000));
        if(info_.system_log.retention_days < 0) info_.system_log.retention_days = 0;
        if(info_.system_log.max_rows < 0) info_.system_log.max_rows = 0;
    }

    // tags
    const json* tag_list = jget(root_, "tags");
    if(!tag_list || !tag_list->is_array()) HML_WARN("project has no \"tags\" array");
    else {
        int n = 0;
        for(const json& tj : *tag_list) {
            auto name = jstr_opt(tj, "name");
            if(!name) { HML_WARN("tag without \"name\" skipped"); continue; }
            TagType type = type_from_name(jstr(tj, "type", ""), TagType::Float);
            Value init;
            const json* iv = jget(tj, "init");
            if(iv && iv->is_string()) init = Value::string(iv->get<std::string>());
            else if(iv && iv->is_boolean()) init = Value::number(iv->get<bool>(), TagType::Bool);
            else init = Value::number(iv ? jscalar_to_number(*iv) : 0);
            bool has_limits = jhas(tj, "min") || jhas(tj, "max");
            double min = jnum(tj, "min", -1e300), max = jnum(tj, "max", 1e300);
            if(tags().create(*name, type, &init, has_limits, min, max)) n++;
        }
        HML_INFO("%d tags created", n);
    }

    // users and levels ("users"); a list edited on the device comes from project.local.json
    users().load(jget(root_, "users"), local_users_.is_null() ? nullptr : &local_users_, local_overlay_path());

    // the screen goes dark after a while without input ("screen_saver")
    screen_saver().configure(jget(root_, "screen_saver"), local_screen_saver_.is_null() ? nullptr : &local_screen_saver_, local_overlay_path());

    // languages and texts ("@id"), and the font they are drawn with
    texts().load(root_);
    texts().set_on_change([] { project().rebuild_screens(); });
    {
        std::string font = jstr(jget(root_, "font") ? *jget(root_, "font") : json::object(), "file", "");
        set_font_file(font.empty() ? "" : resolve_path(font));
    }
    register_language_actions();

    // built-in system settings pages (About, Network, one per driver) and their "sys.*" tags
    register_builtin_pages(system_settings(), *this);
    system_settings().create_tags();

    HML_INFO("project '%s' loaded (%dx%d)", info_.title.c_str(), info_.width, info_.height);
    return true;
}

void Project::apply_theme(lv_display_t* disp)
{
    const json* theme = jget(root_, "theme");
    const json* font = jget(theme, "font");
    // (with a font file: its size 16 unless the theme says otherwise)
    const lv_font_t* f = font ? font_from_json(font) : has_font_file() && font_of_size(16) ? font_of_size(16) : LV_FONT_DEFAULT;
    lv_theme_t* th = lv_theme_default_init(disp, info_.primary, info_.secondary, info_.dark, f);
    lv_display_set_theme(disp, th);
}

int Project::start_drivers()
{
    return drivers().load(jget(root_, "drivers"));
}

void Project::start_logs(bool read_only)
{
    data_log().load(jget(root_, "data_log"), info_.dir, read_only);
    alarms().load(jget(root_, "alarms"), info_.dir, read_only);
    operation_log().load(jget(root_, "operation_log"), info_.dir, read_only);
    if(read_only) return;
    recipes().load(jget(root_, "recipes"), info_.dir);
    register_recipe_actions();

    // { "action": "call", "fn": "data_log.export", "arg": "<group>" } or "<group>,<file.csv>";
    // without a file the CSV goes next to the database. The other two take the file (or nothing).
    ui().register_action("data_log.export", [](const std::string& arg) {
        size_t comma = arg.find(',');
        std::string group = arg.substr(0, comma), path = comma == std::string::npos ? "" : arg.substr(comma + 1);
        std::vector<std::string> groups = group.empty() ? data_log().groups() : std::vector<std::string>{ group };
        for(const std::string& g : groups) {
            int64_t n = data_log().export_csv(g, path);
            if(n >= 0) HML_INFO("data log: %lld rows of group '%s' exported", static_cast<long long>(n), g.c_str());
        }
    });
    ui().register_action("data_log.sample", [](const std::string& arg) { data_log().sample(arg); });
    register_alarm_actions();
    ui().register_action("alarm_log.export", [](const std::string& arg) {
        int64_t n = alarms().export_csv(arg);
        if(n >= 0) HML_INFO("alarm log: %lld rows exported", static_cast<long long>(n));
    });
    ui().register_action("operation_log.export", [](const std::string& arg) {
        int64_t n = operation_log().export_csv(arg);
        if(n >= 0) HML_INFO("operation log: %lld rows exported", static_cast<long long>(n));
    });
}

void Project::stop_logs()
{
    data_log().clear();
    alarms().clear();
    operation_log().clear();
    recipes().clear();
}

// The screens again, as they are in memory (no file is read, the scripts keep their state):
// after a language switch. The screen shown and the history stay.
void Project::rebuild_screens()
{
    if(lv_display_t* d = lv_display_get_default()) apply_theme(d);
    std::string current = ui().current_screen().value_or("");
    std::vector<std::string> history = ui().history();
    ui().rebuild_all();
    if(!current.empty()) ui().show_screen(current, "none", true);
    ui().set_history(history);
    tags().dispatch(true);
}

int Project::build_screens()
{
    std::string current = ui().current_screen().value_or("");
    std::vector<std::string> history = ui().history();      // keep "Back" working across a reload
    ui().unload_all();

    // Re-read the project file too so that screen list / start screen edits are picked up.
    json fresh = parse_json_file(info_.path);
    if(!fresh.is_null()) {
        root_ = std::move(fresh);
        apply_local_overlay();
        info_.start_screen = jstr(root_, "start_screen", "");
    }
    // Scripts are reloaded with the screens (fresh Lua state). They come first so that
    // { "action": "script" } can be checked against them while the screens are built.
    scripts().load(jget(root_, "scripts"), info_.dir);

    int built = 0;
    std::string first;
    const json* screens = jget(root_, "screens");
    if(screens && screens->is_array()) {
        for(const json& s : *screens) {
            json owned;
            const json* def = &s;
            if(s.is_string()) {
                owned = parse_json_file(resolve_path(s.get<std::string>()));
                if(owned.is_null()) continue;
                def = &owned;
            }
            // registered now, built when first shown or shortly after startup (see below)
            if(ui().add_screen(*def, info_.dir)) {
                if(first.empty()) first = jstr(*def, "name", "");
                built++;
            }
        }
    }
    // system settings screens (not counted: they are not part of the project)
    for(const json& s : system_settings().screens(info_.width, info_.height)) {
        if(!ui().add_screen(s, info_.dir)) HML_WARN("system screen '%s' could not be built", jstr(s, "name", "?").c_str());
    }
    // the login screen (with "users")
    for(const json& s : login_screens(info_.width, info_.height)) {
        if(!ui().add_screen(s, info_.dir)) HML_WARN("screen '%s' could not be built", jstr(s, "name", "?").c_str());
    }
    users().set_home(!info_.start_screen.empty() ? info_.start_screen : first);
    ui().refresh_access();

    if(built == 0) {
        HML_ERROR("no screens could be built");
        lv_obj_t* l = lv_label_create(lv_screen_active());   // keep something visible
        lv_label_set_text(l, "HML: no screens loaded\n(check the console log, press F5 to retry)");
        lv_obj_center(l);
        return 0;
    }

    // Startup scripts run before the first screen is shown (so screen_show scripts can rely on
    // them); if one of them navigates, its choice wins.
    scripts().run_startup();
    if(!ui().current_screen()) {
        const std::string& show = !current.empty() ? current : (!info_.start_screen.empty() ? info_.start_screen : first);
        // (a start screen that needs a login leaves the login screen up: that is not a failure)
        if(!ui().show_screen(show, "none") && !ui().current_screen()) ui().show_screen(first, "none");
    }
    ui().set_history(history);
    tags().dispatch(true);
    HML_INFO("%d screens built, showing '%s'", built, ui().current_screen().value_or("").c_str());
    // Only the screen that is shown has its widgets so far. The others are built one at a time
    // while the application already runs: a large project starts at once, and a mistake in any
    // screen is still reported shortly after startup, not when somebody first opens it.
    if(ui().build_pending(0) > 0) {
        lv_timer_create([](lv_timer_t* t) {
            if(ui().build_pending(1) == 0) { lv_timer_delete(t); return; }
            // Building a screen can take longer than the period. Count the pause from now, or
            // LVGL would call this again at once, screen after screen, and nothing else would run.
            lv_timer_reset(t);
        }, 20, nullptr);
    }
    return built;
}

void Project::unload()
{
    screen_saver().clear();
    root_ = nullptr;
}

Project& project()
{
    static Project instance;
    return instance;
}

} // namespace hml
