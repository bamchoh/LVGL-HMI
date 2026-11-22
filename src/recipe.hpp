// Recipes (レシピ): named sets of values for a group of tags, written to the PLC in one go.
//
//   "recipes": {
//     "level": 2,                         // user level needed to write / save / delete (0 = anybody)
//     "verify_ms": 2000,                  // after a write: compare the tags with what was written
//     "books": [
//       { "name": "product",
//         "items": ["temp_sp", "speed_sp", "time_s"],      // the tags of this kind of recipe; an item can be
//                                                          // { "tag": "temp_sp", "label": "Temperature", "unit": "°C" }
//         "recipes": [ { "name": "A", "values": { "temp_sp": 80, "speed_sp": 1200, "time_s": 30 } },
//                      { "name": "B", "values": { "temp_sp": 95, "speed_sp": 900, "time_s": 45 } } ] } ] }
//
// The recipes of the project are where a book starts. What is saved on the device goes to
// "recipes/<book>.json" next to project.json ({ "version": 1, "recipes": [...] }), written
// atomically; a deploy does not touch it (like logs/). Once that file exists it is the book.
//
// Writing a recipe checks every value first (type, the tag's min / max): if one is wrong,
// nothing is written. After "verify_ms" the tags are compared with what was written (a driver
// writes them to the PLC and reads them back); differences are reported in sys.recipe.status.
// Every write / save / delete / import is recorded in the operation log.
//
// Tags: sys.recipe.status (text of the last action), sys.recipe.<book>.selected (the recipe picked
// in the list), sys.recipe.<book>.written (the recipe written last; "" until one is written).
// Actions (call fn, arg): recipe.select "book,name" | recipe.write "book[,name]" |
// recipe.save "book,name" (the tags' current values) | recipe.delete "book[,name]" |
// recipe.export "book[,file.csv]" | recipe.import "book,file.csv". See README.md ("レシピ").
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "jsonutil.hpp"
#include "lvgl.h"
#include "tag.hpp"

namespace hml {

struct Recipe {
    std::string name;
    std::map<std::string, Value> values;          // tag name -> value (an item without a value is not written)
};

struct RecipeBook {
    std::string name;
    std::vector<std::string> items;
    std::map<std::string, std::string> labels, units;   // item -> what the table shows ("@ID" kept), its unit
    std::string written;                          // the recipe written last (sys.recipe.<book>.written)
    std::vector<Recipe> recipes;
    std::string file;                             // the device's copy
    std::string selected;
};

class Recipes {
public:
    static constexpr size_t kMaxRecipes = 1000;
    static constexpr size_t kMaxItems = 500;
    static constexpr size_t kMaxFileBytes = 4 * 1024 * 1024;

    int  load(const json* section, const std::string& base_dir);
    void clear();

    int level() const { return level_; }
    std::vector<std::string> books() const;
    const RecipeBook* book(const std::string& name) const;
    const Recipe* recipe(const std::string& book, const std::string& name) const;
    uint64_t generation() const { return gen_; }     // changes with every change

    // Each returns "" on success, else what is wrong (also put in sys.recipe.status).
    std::string select(const std::string& book, const std::string& name);
    std::string write(const std::string& book, const std::string& name);       // to the tags (and so to the PLC)
    std::string save(const std::string& book, const std::string& name, const std::map<std::string, Value>& values);
    std::string save_current(const std::string& book, const std::string& name); // the tags' values now
    std::string set_value(const std::string& book, const std::string& name, const std::string& item, const std::string& text);
    std::string remove(const std::string& book, const std::string& name);

    // Check a value for an item: "" if fine. text is what an operator typed.
    std::string check_value(const std::string& item, const Value& v) const;
    static bool valid_name(const std::string& name);    // 1..32 printable characters

    // After write(): compare when verify_ms has passed. Call regularly (main loop).
    void poll();
    void set_clock(std::function<uint32_t()> now_ms) { now_fn_ = std::move(now_ms); }
    bool verifying() const { return verify_.active; }

    // CSV: one row per recipe: name,<item>,<item>... ; UTF-8 with BOM. Cells that a spreadsheet
    // would take as a formula (= + - @ at the start, text only) are written with a leading '.
    int64_t     export_csv(const std::string& book, std::string path = "");
    // Replaces the recipes of the book with those of the file (all or nothing). Returns the number
    // of recipes, -1 on error (*err).
    int64_t     import_csv(const std::string& book, const std::string& path, std::string* err = nullptr);
    static std::string csv_cell(const std::string& s);

private:
    RecipeBook* book_mut(const std::string& name);
    std::string fail(const std::string& msg);
    std::string ok(const std::string& msg);
    bool persist(RecipeBook& b, std::string* err);
    void update_tags(const RecipeBook& b);
    uint32_t now() const;

    std::vector<RecipeBook> books_;
    std::string base_dir_, dir_;
    int level_ = 0;
    uint32_t verify_ms_ = 2000;
    uint64_t gen_ = 1;
    struct Verify {
        bool active = false;
        uint32_t due = 0;
        std::string book, name;
        std::map<std::string, Value> expected;
    } verify_;
    std::function<uint32_t()> now_fn_;
};

Recipes& recipes();
void register_recipe_actions();
// The user may change recipes (the "level"); if not, the login screen is asked for.
bool recipe_allowed();

// { "type": "recipe_table", "book": "product", "labels": { ... } } - see recipe_view.cpp
lv_obj_t* build_recipe_table(lv_obj_t* parent, const json& w);

} // namespace hml
