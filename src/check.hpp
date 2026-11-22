// `hml_runtime --check`: what is wrong in a project, found by reading its files (nothing is
// started, connected or written). The checks are the ones a designer user sees as "problems":
// things the runtime would silently ignore or only complain about when they are first used.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "lvgl.h"

namespace hml {

struct CheckProblem {
    std::string file;     // "project.json", "screens/main.json"
    std::string where;    // "children[2] (button)" - empty for the file as a whole
    std::string text;
    std::string line() const { return file + (where.empty() ? "" : ": " + where) + ": " + text; }
};

// A tag of the runtime ("sys.") a project names. Which exist depends on the project (sound,
// users, recipe books ...): they are looked up once the project is loaded.
struct SysTagUse { std::string file, where, what, tag; };

// path: project.json (a folder project). screens: the names of its screens, in order.
// sys_uses: the "sys." tags named (check_project does not report them).
std::vector<CheckProblem> check_project(const std::string& path, std::vector<std::string>* screens = nullptr,
                                        std::vector<SysTagUse>* sys_uses = nullptr);
// The problems of those that do not exist (exists: the loaded project's tag store).
std::vector<CheckProblem> unknown_sys_tags(const std::vector<SysTagUse>& uses, const std::function<bool(const std::string&)>& exists);

// The characters of text the font (with its fallbacks) has no glyph for, each once, separated by
// spaces ("" = none). They would be drawn as an empty box.
std::string missing_glyphs(const lv_font_t* font, const std::string& text);
// The same for what a built screen shows now: labels, checkboxes, dropdowns, text boxes below
// root, each in its own font. One line per text: <text>: no glyph for: <characters>.
std::vector<std::string> missing_glyphs_on(lv_obj_t* root);

} // namespace hml
