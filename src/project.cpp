#include "project.hpp"
#include "driver.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
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
    std::string path = local_overlay_path();
    std::ifstream probe(path, std::ios::binary);
    if(!probe) return;
    probe.close();
    json overlay = parse_json_file(path);
    if(!overlay.is_object()) return;
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
    lv_theme_t* th = lv_theme_default_init(disp, info_.primary, info_.secondary, info_.dark, font ? font_from_json(font) : LV_FONT_DEFAULT);
    lv_display_set_theme(disp, th);
}

int Project::start_drivers()
{
    return drivers().load(jget(root_, "drivers"));
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
            if(ui().build_screen(*def, info_.dir)) {
                if(first.empty()) first = jstr(*def, "name", "");
                built++;
            }
        }
    }
    // system settings screens (not counted: they are not part of the project)
    for(const json& s : system_settings().screens(info_.width, info_.height)) {
        if(!ui().build_screen(s, info_.dir)) HML_WARN("system screen '%s' could not be built", jstr(s, "name", "?").c_str());
    }

    if(built == 0) {
        HML_ERROR("no screens could be built");
        lv_obj_t* l = lv_label_create(lv_screen_active());   // keep something visible
        lv_label_set_text(l, "HML: no screens loaded\n(check the console log, press F5 to retry)");
        lv_obj_center(l);
        return 0;
    }

    const std::string& show = !current.empty() ? current : (!info_.start_screen.empty() ? info_.start_screen : first);
    if(!ui().show_screen(show, "none")) ui().show_screen(first, "none");
    ui().set_history(history);
    tags().dispatch(true);
    HML_INFO("%d screens built, showing '%s'", built, ui().current_screen().value_or("").c_str());
    return built;
}

void Project::unload()
{
    root_ = nullptr;
}

Project& project()
{
    static Project instance;
    return instance;
}

} // namespace hml
