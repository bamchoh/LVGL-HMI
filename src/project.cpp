#include "project.hpp"
#include "driver.hpp"
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
    ui().unload_all();

    // Re-read the project file too so that screen list / start screen edits are picked up.
    json fresh = parse_json_file(info_.path);
    if(!fresh.is_null()) {
        root_ = std::move(fresh);
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
    if(built == 0) {
        HML_ERROR("no screens could be built");
        lv_obj_t* l = lv_label_create(lv_screen_active());   // keep something visible
        lv_label_set_text(l, "HML: no screens loaded\n(check the console log, press F5 to retry)");
        lv_obj_center(l);
        return 0;
    }

    const std::string& show = !current.empty() ? current : (!info_.start_screen.empty() ? info_.start_screen : first);
    if(!ui().show_screen(show, "none")) ui().show_screen(first, "none");
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
