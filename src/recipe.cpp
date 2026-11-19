#include "recipe.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include "logdb.hpp"
#include "operation_log.hpp"
#include "ui.hpp"
#include "users.hpp"
#include "util.hpp"

namespace hml {
namespace {

void set_sys_tag(const std::string& name, const std::string& value)
{
    Tag* t = tags().find(name);
    if(!t) t = tags().create(name, TagType::String, nullptr, false, 0, 0);
    if(t) t->set(Value::string(value), nullptr);
}

json value_json(const Value& v)
{
    if(v.is_string()) return v.str;
    if(v.type == TagType::Bool) return v.num != 0;
    if(v.type == TagType::Int) return static_cast<long long>(std::llround(v.num));
    return v.num;
}

// a value from the project / the device file, as the tag's type
std::optional<Value> value_from_json(const json& j, TagType type)
{
    if(type == TagType::String) {
        if(j.is_string()) return Value::string(j.get<std::string>());
        if(j.is_number()) return Value::string(Value::number(j.get<double>()).format());
        return std::nullopt;
    }
    if(j.is_boolean()) return Value::number(j.get<bool>() ? 1 : 0, type);
    if(j.is_number()) return Value::number(j.get<double>(), type);
    if(j.is_string()) return Value::parse(type, j.get<std::string>());
    return std::nullopt;
}

bool same(const Value& a, const Value& b)
{
    if(a.is_string() || b.is_string()) return a.format() == b.format();
    return std::fabs(a.num - b.num) <= 1e-6 * std::max(1.0, std::fabs(b.num));
}

// RFC 4180-ish: fields separated by commas, "..." with "" for a quote
std::vector<std::vector<std::string>> parse_csv(const std::string& text)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false, any = false;
    size_t i = text.rfind("\xEF\xBB\xBF", 0) == 0 ? 3 : 0;
    for(; i < text.size(); i++) {
        char c = text[i];
        if(quoted) {
            if(c == '"' && i + 1 < text.size() && text[i + 1] == '"') { cell += '"'; i++; }
            else if(c == '"') quoted = false;
            else cell += c;
            continue;
        }
        if(c == '"') { quoted = true; any = true; }
        else if(c == ',') { row.push_back(cell); cell.clear(); any = true; }
        else if(c == '\r') continue;
        else if(c == '\n') { if(any || !cell.empty()) { row.push_back(cell); rows.push_back(row); } row.clear(); cell.clear(); any = false; }
        else { cell += c; any = true; }
    }
    if(any || !cell.empty()) { row.push_back(cell); rows.push_back(row); }
    return rows;
}

} // namespace

// ================================================================ load

void Recipes::clear()
{
    books_.clear();
    level_ = 0;
    verify_ = Verify{};
    gen_++;
}

int Recipes::load(const json* section, const std::string& base_dir)
{
    clear();
    base_dir_ = base_dir;
    set_sys_tag("sys.recipe.status", "");
    if(!section || !section->is_object()) return 0;
    level_ = std::max(0, static_cast<int>(jnum(*section, "level", 0)));
    verify_ms_ = static_cast<uint32_t>(std::clamp(jnum(*section, "verify_ms", 2000), 0.0, 60000.0));
    dir_ = base_dir + "/" + jstr(*section, "path", "recipes");
    const json* list = jget(*section, "books");
    if(!list || !list->is_array()) return 0;
    for(const json& bj : *list) {
        RecipeBook b;
        b.name = jstr(bj, "name", "");
        if(!valid_name(b.name) || b.name.find_first_of("/\\:*?\"<>|.") != std::string::npos) {
            HML_WARN("recipes: book name '%s' skipped (1-32 characters, no / \\ : * ? \" < > | .)", b.name.c_str());
            continue;
        }
        if(book(b.name)) { HML_WARN("recipes: book '%s' defined twice, keeping the first", b.name.c_str()); continue; }
        if(const json* items = jget(bj, "items"); items && items->is_array()) {
            for(const json& it : *items) {
                std::string tn = it.is_string() ? it.get<std::string>() : jstr(it, "tag", "");
                if(!tags().find(tn)) { HML_WARN("recipes: book '%s': unknown tag '%s'", b.name.c_str(), tn.c_str()); continue; }
                if(std::find(b.items.begin(), b.items.end(), tn) != b.items.end() || b.items.size() >= kMaxItems) continue;
                b.items.push_back(tn);
            }
        }
        b.file = dir_ + "/" + b.name + ".json";
        // the device's copy, if there is one; else the recipes of the project
        json rlist;
        std::string err;
        if(auto text = read_file(b.file, kMaxFileBytes, &err)) {
            json dev = json::parse(*text, nullptr, false);
            if(dev.is_object() && jhas(dev, "recipes")) rlist = dev["recipes"];
            else HML_WARN("recipes: '%s' is not a recipe file; using the project's recipes", b.file.c_str());
        }
        else if(std::filesystem::exists(std::filesystem::u8path(b.file))) HML_WARN("recipes: %s", err.c_str());
        if(rlist.is_null()) if(const json* r = jget(bj, "recipes")) rlist = *r;
        if(rlist.is_array()) {
            for(const json& rj : rlist) {
                Recipe r;
                r.name = jstr(rj, "name", "");
                if(!valid_name(r.name) || std::any_of(b.recipes.begin(), b.recipes.end(), [&](const Recipe& x) { return x.name == r.name; })) continue;
                if(const json* vals = jget(rj, "values"); vals && vals->is_object()) {
                    for(const std::string& item : b.items) {
                        const json* v = jget(*vals, item.c_str());
                        if(!v) continue;
                        if(auto val = value_from_json(*v, tags().find(item)->type())) r.values[item] = *val;
                    }
                }
                b.recipes.push_back(std::move(r));
                if(b.recipes.size() >= kMaxRecipes) break;
            }
        }
        b.selected = b.recipes.empty() ? "" : b.recipes.front().name;
        books_.push_back(std::move(b));
        update_tags(books_.back());
    }
    HML_INFO("recipes: %d book(s)", static_cast<int>(books_.size()));
    return static_cast<int>(books_.size());
}

std::vector<std::string> Recipes::books() const
{
    std::vector<std::string> out;
    for(const auto& b : books_) out.push_back(b.name);
    return out;
}

const RecipeBook* Recipes::book(const std::string& name) const
{
    for(const auto& b : books_) if(b.name == name) return &b;
    return nullptr;
}
RecipeBook* Recipes::book_mut(const std::string& name) { return const_cast<RecipeBook*>(book(name)); }

const Recipe* Recipes::recipe(const std::string& bk, const std::string& name) const
{
    const RecipeBook* b = book(bk);
    if(!b) return nullptr;
    for(const auto& r : b->recipes) if(r.name == name) return &r;
    return nullptr;
}

bool Recipes::valid_name(const std::string& name)
{
    if(name.empty() || name.size() > 32) return false;
    for(unsigned char c : name) if(c < 0x20 || c == 0x7f) return false;
    return name.front() != ' ' && name.back() != ' ';
}

uint32_t Recipes::now() const { return now_fn_ ? now_fn_() : millis(); }
std::string Recipes::fail(const std::string& msg) { set_sys_tag("sys.recipe.status", msg); HML_WARN("recipes: %s", msg.c_str()); return msg; }
std::string Recipes::ok(const std::string& msg) { set_sys_tag("sys.recipe.status", msg); gen_++; return ""; }
void Recipes::update_tags(const RecipeBook& b) { set_sys_tag("sys.recipe." + b.name + ".selected", b.selected); }

bool Recipes::persist(RecipeBook& b, std::string* err)
{
    json out = { { "version", 1 }, { "book", b.name }, { "recipes", json::array() } };
    for(const Recipe& r : b.recipes) {
        json vals = json::object();
        for(const auto& [k, v] : r.values) vals[k] = value_json(v);
        out["recipes"].push_back({ { "name", r.name }, { "values", vals } });
    }
    return write_file_atomic(b.file, out.dump(2, ' ', false, json::error_handler_t::replace), err);
}

// ================================================================ operations

std::string Recipes::check_value(const std::string& item, const Value& v) const
{
    Tag* t = tags().find(item);
    if(!t) return "unknown tag '" + item + "'";
    if(t->type() == TagType::String) return v.str.size() > 256 ? "'" + item + "': text too long (256 at most)" : "";
    if(v.is_string()) return "'" + item + "': not a number";
    if(!std::isfinite(v.num)) return "'" + item + "': not a number";
    if(t->type() == TagType::Bool && v.num != 0 && v.num != 1) return "'" + item + "': 0 or 1";
    double mn, mx;
    if(t->limits(mn, mx) && (v.num < mn || v.num > mx))
        return "'" + item + "': " + v.format() + " is outside " + Value::number(mn).format() + ".." + Value::number(mx).format();
    return "";
}

std::string Recipes::select(const std::string& bk, const std::string& name)
{
    RecipeBook* b = book_mut(bk);
    if(!b) return fail("unknown recipe book '" + bk + "'");
    if(!recipe(bk, name)) return fail("no recipe '" + name + "' in '" + bk + "'");
    b->selected = name;
    update_tags(*b);
    gen_++;
    return "";
}

std::string Recipes::write(const std::string& bk, const std::string& name0)
{
    RecipeBook* b = book_mut(bk);
    if(!b) return fail("unknown recipe book '" + bk + "'");
    const std::string name = name0.empty() ? b->selected : name0;
    const Recipe* r = recipe(bk, name);
    if(!r) return fail("no recipe '" + name + "' in '" + bk + "'");
    // all or nothing: every value is checked before anything is written
    for(const auto& [item, v] : r->values) {
        std::string e = check_value(item, v);
        if(!e.empty()) return fail("recipe '" + name + "' not written: " + e);
    }
    verify_ = Verify{};
    for(const std::string& item : b->items) {
        auto it = r->values.find(item);
        if(it == r->values.end()) continue;
        Tag* t = tags().find(item);
        t->set(it->second, nullptr);
        verify_.expected[item] = t->get();
    }
    b->selected = name;
    update_tags(*b);
    operation_log().record("recipe", bk, "", "write " + name);
    if(verify_ms_ > 0) { verify_.active = true; verify_.due = now() + verify_ms_; verify_.book = bk; verify_.name = name; }
    return ok("'" + name + "' written" + std::string(verify_ms_ > 0 ? ", checking ..." : ""));
}

void Recipes::poll()
{
    if(!verify_.active || static_cast<int32_t>(now() - verify_.due) < 0) return;
    verify_.active = false;
    std::vector<std::string> wrong;
    for(const auto& [item, v] : verify_.expected) {
        Tag* t = tags().find(item);
        if(t && !same(t->get(), v)) wrong.push_back(item + " = " + t->get().format() + " (not " + v.format() + ")");
    }
    if(wrong.empty()) { ok("'" + verify_.name + "' written and checked"); return; }
    std::string msg = "'" + verify_.name + "': the PLC holds other values: ";
    for(size_t i = 0; i < wrong.size() && i < 5; i++) msg += (i ? ", " : "") + wrong[i];
    if(wrong.size() > 5) msg += vformat(" and %d more", static_cast<int>(wrong.size() - 5));
    fail(msg);
}

std::string Recipes::save(const std::string& bk, const std::string& name, const std::map<std::string, Value>& values)
{
    RecipeBook* b = book_mut(bk);
    if(!b) return fail("unknown recipe book '" + bk + "'");
    if(!valid_name(name)) return fail("a recipe name is 1 to 32 characters");
    std::map<std::string, Value> vals;
    for(const auto& [item, v] : values) {
        if(std::find(b->items.begin(), b->items.end(), item) == b->items.end()) continue;
        std::string e = check_value(item, v);
        if(!e.empty()) return fail("recipe '" + name + "' not saved: " + e);
        vals[item] = v;
    }
    std::vector<Recipe> before = b->recipes;
    auto it = std::find_if(b->recipes.begin(), b->recipes.end(), [&](const Recipe& r) { return r.name == name; });
    if(it != b->recipes.end()) it->values = vals;
    else {
        if(b->recipes.size() >= kMaxRecipes) return fail(vformat("at most %d recipes per book", static_cast<int>(kMaxRecipes)));
        b->recipes.push_back({ name, vals });
    }
    std::string err;
    if(!persist(*b, &err)) { b->recipes = before; return fail("recipe '" + name + "' not saved: " + err); }
    b->selected = name;
    update_tags(*b);
    operation_log().record("recipe", bk, "", "save " + name);
    return ok("'" + name + "' saved");
}

std::string Recipes::save_current(const std::string& bk, const std::string& name)
{
    const RecipeBook* b = book(bk);
    if(!b) return fail("unknown recipe book '" + bk + "'");
    std::map<std::string, Value> vals;
    for(const std::string& item : b->items) vals[item] = tags().find(item)->get();
    return save(bk, name, vals);
}

std::string Recipes::set_value(const std::string& bk, const std::string& name, const std::string& item, const std::string& text)
{
    const Recipe* r = recipe(bk, name);
    if(!r) return fail("no recipe '" + name + "' in '" + bk + "'");
    Tag* t = tags().find(item);
    if(!t) return fail("unknown tag '" + item + "'");
    auto v = Value::parse(t->type(), text);
    if(!v) return fail("'" + item + "': '" + text + "' is not a " + (t->type() == TagType::String ? "text" : "number"));
    std::map<std::string, Value> vals = r->values;
    vals[item] = *v;
    return save(bk, name, vals);
}

std::string Recipes::remove(const std::string& bk, const std::string& name0)
{
    RecipeBook* b = book_mut(bk);
    if(!b) return fail("unknown recipe book '" + bk + "'");
    const std::string name = name0.empty() ? b->selected : name0;
    auto it = std::find_if(b->recipes.begin(), b->recipes.end(), [&](const Recipe& r) { return r.name == name; });
    if(it == b->recipes.end()) return fail("no recipe '" + name + "' in '" + bk + "'");
    std::vector<Recipe> before = b->recipes;
    b->recipes.erase(it);
    std::string err;
    if(!persist(*b, &err)) { b->recipes = before; return fail("recipe '" + name + "' not deleted: " + err); }
    if(b->selected == name) b->selected = b->recipes.empty() ? "" : b->recipes.front().name;
    update_tags(*b);
    operation_log().record("recipe", bk, "", "delete " + name);
    return ok("'" + name + "' deleted");
}

// ================================================================ CSV

std::string Recipes::csv_cell(const std::string& s0)
{
    std::string s = s0;
    // a spreadsheet takes =, +, -, @ (and tab / CR) at the start of a cell as a formula
    if(!s.empty() && std::string("=+-@\t\r").find(s[0]) != std::string::npos) {
        char* end = nullptr;
        std::strtod(s.c_str(), &end);
        const bool number = end && *end == '\0' && (s[0] == '-' || s[0] == '+');
        if(!number) s = "'" + s;
    }
    if(s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string q = "\"";
    for(char c : s) { if(c == '"') q += '"'; q += c; }
    return q + "\"";
}

int64_t Recipes::export_csv(const std::string& bk, std::string path)
{
    const RecipeBook* b = book(bk);
    if(!b) { fail("unknown recipe book '" + bk + "'"); return -1; }
    std::string out = "\xEF\xBB\xBF" + csv_cell("name");
    for(const std::string& item : b->items) out += "," + csv_cell(item);
    out += "\r\n";
    for(const Recipe& r : b->recipes) {
        out += csv_cell(r.name);
        for(const std::string& item : b->items) {
            auto it = r.values.find(item);
            out += "," + (it == r.values.end() ? std::string() : csv_cell(it->second.format()));
        }
        out += "\r\n";
    }
    if(path.empty()) path = dir_ + "/" + b->name + "-" + LogDb::format_time(LogDb::now_ms()).substr(0, 10) + ".csv";
    std::string err;
    if(!write_file_atomic(path, out, &err)) { fail("export failed: " + err); return -1; }
    operation_log().record("recipe", bk, "", "export");
    ok(vformat("%d recipe(s) exported", static_cast<int>(b->recipes.size())));
    return static_cast<int64_t>(b->recipes.size());
}

int64_t Recipes::import_csv(const std::string& bk, const std::string& path, std::string* err_out)
{
    auto bad = [&](const std::string& msg) { if(err_out) *err_out = msg; fail("import failed: " + msg); return int64_t(-1); };
    RecipeBook* b = book_mut(bk);
    if(!b) return bad("unknown recipe book '" + bk + "'");
    std::string err;
    auto text = read_file(path, kMaxFileBytes, &err);
    if(!text) return bad(err);
    auto rows = parse_csv(*text);
    if(rows.empty() || rows[0].empty() || rows[0][0] != "name") return bad("the first row must be: name,<tag>,<tag>...");
    std::vector<std::string> cols(rows[0].begin() + 1, rows[0].end());
    for(const std::string& c : cols) if(std::find(b->items.begin(), b->items.end(), c) == b->items.end()) return bad("column '" + c + "' is not an item of '" + bk + "'");
    std::vector<Recipe> list;
    for(size_t ri = 1; ri < rows.size(); ri++) {
        const auto& row = rows[ri];
        Recipe r;
        r.name = row[0];
        if(!r.name.empty() && r.name[0] == '\'') r.name.erase(0, 1);                 // (written by export_csv)
        if(!valid_name(r.name)) return bad(vformat("row %d: a recipe name is 1 to 32 characters", static_cast<int>(ri + 1)));
        if(std::any_of(list.begin(), list.end(), [&](const Recipe& x) { return x.name == r.name; })) return bad("recipe '" + r.name + "' is in the file twice");
        if(row.size() > cols.size() + 1) return bad(vformat("row %d has more cells than the first row", static_cast<int>(ri + 1)));
        for(size_t ci = 0; ci + 1 < row.size(); ci++) {
            std::string cell = row[ci + 1];
            if(cell.empty()) continue;
            if(cell[0] == '\'') cell.erase(0, 1);
            Tag* t = tags().find(cols[ci]);
            auto v = Value::parse(t->type(), cell);
            if(!v) return bad("recipe '" + r.name + "', '" + cols[ci] + "': '" + cell + "' is not a " + (t->type() == TagType::String ? "text" : "number"));
            std::string e = check_value(cols[ci], *v);
            if(!e.empty()) return bad("recipe '" + r.name + "': " + e);
            r.values[cols[ci]] = *v;
        }
        list.push_back(std::move(r));
        if(list.size() > kMaxRecipes) return bad(vformat("at most %d recipes per book", static_cast<int>(kMaxRecipes)));
    }
    std::vector<Recipe> before = b->recipes;
    b->recipes = list;
    if(!persist(*b, &err)) { b->recipes = before; return bad(err); }
    if(!recipe(bk, b->selected)) b->selected = b->recipes.empty() ? "" : b->recipes.front().name;
    update_tags(*b);
    operation_log().record("recipe", bk, "", "import " + std::to_string(list.size()));
    ok(vformat("%d recipe(s) imported", static_cast<int>(list.size())));
    return static_cast<int64_t>(list.size());
}

Recipes& recipes()
{
    static Recipes instance;
    return instance;
}

// ================================================================ actions

namespace {
// "book,rest" -> book, rest
std::pair<std::string, std::string> split_arg(const std::string& arg)
{
    size_t c = arg.find(',');
    return { arg.substr(0, c), c == std::string::npos ? "" : arg.substr(c + 1) };
}
} // namespace

bool recipe_allowed()
{
    const int level = recipes().level();
    if(users().allowed(level)) return true;
    users().request_login(level);
    return false;
}

namespace {
bool allowed() { return recipe_allowed(); }
} // namespace

void register_recipe_actions()
{
    ui().register_action("recipe.select", [](const std::string& arg) { auto [b, n] = split_arg(arg); recipes().select(b, n); });
    ui().register_action("recipe.write", [](const std::string& arg) { if(allowed()) { auto [b, n] = split_arg(arg); recipes().write(b, n); } });
    ui().register_action("recipe.save", [](const std::string& arg) { if(allowed()) { auto [b, n] = split_arg(arg); recipes().save_current(b, n); } });
    ui().register_action("recipe.delete", [](const std::string& arg) { if(allowed()) { auto [b, n] = split_arg(arg); recipes().remove(b, n); } });
    ui().register_action("recipe.export", [](const std::string& arg) { auto [b, f] = split_arg(arg); recipes().export_csv(b, f); });
    ui().register_action("recipe.import", [](const std::string& arg) { if(allowed()) { auto [b, f] = split_arg(arg); recipes().import_csv(b, f); } });
}

} // namespace hml
