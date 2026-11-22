// Recipes (recipe.hpp) and the recipe_table widget. Grouped by ISO/IEC 25010 characteristic.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "recipe.hpp"
#include "users.hpp"
#include "util.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_recipe_test_" + std::to_string(counter++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        recipes().clear();
        recipes().set_clock(nullptr);
        operation_log().clear();
        users().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string base() const { return dir.generic_string(); }
    std::string path(const char* rel) const { return (dir / rel).generic_string(); }
    void write(const char* rel, const std::string& text) const { std::ofstream f(dir / rel, std::ios::binary); f << text; }
};

json J(const char* t) { return json::parse(t); }
std::string status() { return tags().find("sys.recipe.status")->get().format(); }
std::string read_text(const std::string& p) { auto s = read_file(p, 1 << 20); return s ? *s : ""; }

const char* kBook = R"~({ "level": 0, "verify_ms": 1000, "books": [
  { "name": "product", "items": ["temp_sp", "speed_sp", "mode", "label", "nope"],
    "recipes": [ { "name": "A", "values": { "temp_sp": 80, "speed_sp": 1200, "mode": true, "label": "alpha" } },
                 { "name": "B", "values": { "temp_sp": 95.5, "speed_sp": "900" } },
                 { "name": "A", "values": {} } ] } ] })~";

struct Fixture {
    Runtime rt;
    TempDir t;
    Tag *temp, *speed, *mode, *label;
    uint32_t clock = 1000;
    Fixture()
    {
        temp = rt.tag("temp_sp", TagType::Float, 0, 0, 150);
        speed = rt.tag("speed_sp", TagType::Int, 0, 0, 3000);
        mode = rt.tag("mode", TagType::Bool, 0);
        label = rt.tag("label", std::string(""));
        recipes().set_clock([this] { return clock; });
        json oc = J("{}");
        operation_log().load(&oc, t.base());
    }
    int load(const char* cfg = kBook) { json c = json::parse(cfg); return recipes().load(&c, t.base()); }
};

bool oplog_has(const std::string& what)
{
    for(const Operation& o : operation_log().query()) if(o.kind == "recipe" && o.new_value == what) return true;
    return false;
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("recipes: load, select, write all values, save, change one value, delete")
{
    Fixture f;
    REQUIRE(f.load() == 1);
    const RecipeBook* b = recipes().book("product");
    REQUIRE(b);
    CHECK(b->items == std::vector<std::string>{ "temp_sp", "speed_sp", "mode", "label" });   // "nope" is no tag
    REQUIRE(b->recipes.size() == 2);                                                        // the second "A" is dropped
    CHECK(b->selected == "A");
    CHECK(tags().find("sys.recipe.product.selected")->get().format() == "A");
    CHECK(recipes().recipe("product", "B")->values.at("speed_sp").num == 900);              // "900" as the tag's type

    CHECK(recipes().write("product", "A") == "");
    CHECK(f.temp->get_number() == 80);
    CHECK(f.speed->get_number() == 1200);
    CHECK(f.mode->get_number() == 1);
    CHECK(f.label->get().format() == "alpha");
    CHECK(oplog_has("write A"));

    CHECK(recipes().select("product", "B") == "");
    CHECK(recipes().write("product", "") == "");                                           // "" = the selected one
    CHECK(f.temp->get_number() == 95.5);
    CHECK(f.label->get().format() == "alpha");                                             // B has no label: unchanged

    f.temp->set_number(101, nullptr);
    CHECK(recipes().save_current("product", "C") == "");
    CHECK(recipes().recipe("product", "C")->values.at("temp_sp").num == 101);
    CHECK(recipes().book("product")->selected == "C");
    CHECK(recipes().set_value("product", "C", "speed_sp", "1500") == "");
    CHECK(recipes().recipe("product", "C")->values.at("speed_sp").num == 1500);
    CHECK(recipes().set_value("product", "C", "speed_sp", "fast") != "");
    CHECK(recipes().remove("product", "C") == "");
    CHECK_FALSE(recipes().recipe("product", "C"));
    CHECK(recipes().book("product")->selected == "A");
    CHECK(oplog_has("save C"));
    CHECK(oplog_has("delete C"));

    CHECK(recipes().select("product", "nothing") != "");
    CHECK(recipes().write("nobook", "A") != "");
    CHECK(status().find("unknown recipe book") != std::string::npos);
}

TEST_CASE("recipes: a value out of range writes nothing at all")
{
    Fixture f;
    REQUIRE(f.load(R"~({ "books": [ { "name": "p", "items": ["temp_sp", "speed_sp"],
        "recipes": [ { "name": "bad", "values": { "temp_sp": 50, "speed_sp": 9999 } } ] } ] })~") == 1);
    f.temp->set_number(1, nullptr);
    CHECK(recipes().write("p", "bad").find("outside 0..3000") != std::string::npos);
    CHECK(f.temp->get_number() == 1);                                                      // not even the good one
    CHECK(f.speed->get_number() == 0);
    CHECK(status().find("not written") != std::string::npos);
    std::map<std::string, Value> vals = { { "temp_sp", Value::number(200) } };
    CHECK(recipes().save("p", "x", vals).find("outside") != std::string::npos);
    CHECK_FALSE(recipes().recipe("p", "x"));
}

TEST_CASE("recipes: after a write, the values are checked against what the PLC holds")
{
    Fixture f;
    REQUIRE(f.load() == 1);
    CHECK(recipes().write("product", "A") == "");
    CHECK(recipes().verifying());
    CHECK(status().find("checking") != std::string::npos);
    f.clock += 500; recipes().poll();
    CHECK(recipes().verifying());                                                          // not yet
    f.clock += 600; recipes().poll();
    CHECK_FALSE(recipes().verifying());
    CHECK(status() == "'A' written and checked");

    CHECK(recipes().write("product", "A") == "");
    f.speed->set_number_from_device(1100, nullptr);                                        // the PLC did not take it
    f.clock += 1100; recipes().poll();
    CHECK(status() == "'A': the PLC holds other values: speed_sp = 1100 (not 1200)");
}

// ================================================================ reliability

TEST_CASE("recipes: saved on the device, read back after a restart, atomically")
{
    Fixture f;
    REQUIRE(f.load() == 1);
    f.temp->set_number(42, nullptr);
    CHECK(recipes().save_current("product", "night") == "");
    const std::string file = f.t.path("recipes/product.json");
    REQUIRE(fs::exists(file));
    CHECK_FALSE(fs::exists(file + ".tmp"));
    json saved = json::parse(read_text(file));
    CHECK(saved["version"] == 1);
    CHECK(saved["recipes"].size() == 3);

    recipes().clear();
    REQUIRE(f.load() == 1);                                                                // the device's copy wins
    CHECK(recipes().book("product")->recipes.size() == 3);
    CHECK(recipes().recipe("product", "night")->values.at("temp_sp").num == 42);

    // a broken file is not used (the project's recipes are), and nothing is lost by it
    f.t.write("recipes/product.json", "{ broken");
    recipes().clear();
    REQUIRE(f.load() == 1);
    CHECK(recipes().book("product")->recipes.size() == 2);

    // when the file cannot be written, the recipes stay as they were
    recipes().clear();
    fs::remove(file);
    fs::create_directories(file);                                                          // a directory where the file should be
    REQUIRE(f.load() == 1);
    CHECK(recipes().save_current("product", "lost") != "");
    CHECK_FALSE(recipes().recipe("product", "lost"));
    CHECK(recipes().book("product")->recipes.size() == 2);
}

TEST_CASE("util: write_file_atomic replaces the whole file or nothing")
{
    TempDir t;
    const std::string p = t.path("sub/dir/f.txt");
    REQUIRE(write_file_atomic(p, "one"));
    REQUIRE(write_file_atomic(p, "two two"));
    CHECK(read_text(p) == "two two");
    CHECK_FALSE(fs::exists(p + ".tmp"));
    std::string err;
    CHECK_FALSE(read_file(p, 3, &err));
    CHECK(err.find("too large") != std::string::npos);
    CHECK_FALSE(read_file(t.path("none"), 10, &err));
}

// ================================================================ security

TEST_CASE("recipes: CSV export escapes formulas, import checks everything")
{
    Fixture f;
    REQUIRE(f.load() == 1);
    std::map<std::string, Value> vals = { { "temp_sp", Value::number(-5.5) }, { "label", Value::string("=HYPERLINK(\"http://x\")") } };
    REQUIRE(f.temp);
    // (-5.5 is below the tag's minimum 0: use another value for temp)
    vals["temp_sp"] = Value::number(5.5);
    CHECK(recipes().save("product", "+evil,name", vals) == "");
    const std::string csv = f.t.path("out.csv");
    CHECK(recipes().export_csv("product", csv) == 3);
    std::string text = read_text(csv);
    CHECK(text.rfind("\xEF\xBB\xBFname,temp_sp,speed_sp,mode,label\r\n", 0) == 0);
    CHECK(text.find("\"'+evil,name\",5.5,,,\"'=HYPERLINK(\"\"http://x\"\")\"\r\n") != std::string::npos);
    CHECK(Recipes::csv_cell("-12.5") == "-12.5");                                           // numbers stay numbers
    CHECK(Recipes::csv_cell("@SUM(A1)") == "'@SUM(A1)");

    // the file comes back as it was
    CHECK(recipes().import_csv("product", csv) == 3);
    CHECK(recipes().recipe("product", "+evil,name")->values.at("label").str == "=HYPERLINK(\"http://x\")");

    std::string err;
    f.t.write("bad1.csv", "nom,temp_sp\nA,1\n");
    CHECK(recipes().import_csv("product", f.t.path("bad1.csv"), &err) == -1);
    f.t.write("bad2.csv", "name,secret_tag\nA,1\n");
    CHECK(recipes().import_csv("product", f.t.path("bad2.csv"), &err) == -1);
    CHECK(err.find("not an item") != std::string::npos);
    f.t.write("bad3.csv", "name,temp_sp\nA,9999\n");
    CHECK(recipes().import_csv("product", f.t.path("bad3.csv"), &err) == -1);
    CHECK(err.find("outside") != std::string::npos);
    f.t.write("bad4.csv", "name,temp_sp\nA,1\nA,2\n");
    CHECK(recipes().import_csv("product", f.t.path("bad4.csv"), &err) == -1);
    f.t.write("bad5.csv", "name,temp_sp\nA,1,2\n");
    CHECK(recipes().import_csv("product", f.t.path("bad5.csv"), &err) == -1);
    f.t.write("bad6.csv", "name,speed_sp\nA,fast\n");
    CHECK(recipes().import_csv("product", f.t.path("bad6.csv"), &err) == -1);
    f.t.write("big.csv", "name,temp_sp\n" + std::string(Recipes::kMaxFileBytes + 10, 'x'));
    CHECK(recipes().import_csv("product", f.t.path("big.csv"), &err) == -1);
    CHECK(err.find("too large") != std::string::npos);
    CHECK(recipes().book("product")->recipes.size() == 3);                                 // nothing changed by them
}

TEST_CASE("recipes: names, book names and random bytes")
{
    Fixture f;
    CHECK(Recipes::valid_name("A"));
    CHECK(Recipes::valid_name(std::string(32, 'x')));
    CHECK_FALSE(Recipes::valid_name(std::string(33, 'x')));
    CHECK_FALSE(Recipes::valid_name(""));
    CHECK_FALSE(Recipes::valid_name(" A"));
    CHECK_FALSE(Recipes::valid_name("a\nb"));
    // a book name is a file name: no way out of the recipes folder
    REQUIRE(f.load(R"~({ "books": [ { "name": "../../evil", "items": ["temp_sp"] }, { "name": "ok", "items": ["temp_sp"] } ] })~") == 1);
    CHECK(recipes().books() == std::vector<std::string>{ "ok" });

    std::mt19937 rng(3);
    for(int i = 0; i < 200; i++) {
        std::string junk(rng() % 300, ' ');
        for(auto& c : junk) c = static_cast<char>(rng());
        f.t.write("junk.csv", "name,temp_sp\n" + junk);
        recipes().import_csv("ok", f.t.path("junk.csv"));
        recipes().set_value("ok", "x", "temp_sp", junk);
    }
    CHECK(recipes().book("ok"));
}

TEST_CASE("recipes: changing needs the level; the widget shows and writes")
{
    Fixture f;
    json ucfg = J(R"~({ "levels": ["Op", "Boss"], "list": [ { "name": "boss", "level": 2, "pin": "2222" } ] })~");
    users().load(&ucfg, nullptr, "");
    std::string cfg = std::string(kBook);
    cfg.replace(cfg.find("\"level\": 0"), 10, "\"level\": 2");
    REQUIRE(f.load(cfg.c_str()) == 1);
    register_recipe_actions();
    ui().call_action("recipe.write", "product,A");
    CHECK(f.temp->get_number() == 0);                                                      // nobody logged in
    CHECK(users().required() == 2);
    users().finish_login(false);
    ui().call_action("recipe.select", "product,B");                                        // selecting needs no level
    CHECK(recipes().book("product")->selected == "B");
    REQUIRE(users().login("boss", "2222") == Users::LoginResult::Ok);
    ui().call_action("recipe.write", "product");
    CHECK(f.temp->get_number() == 95.5);

    json scr = { { "name", "rec" }, { "children", J(R"~([ { "type": "recipe_table", "book": "product", "w": 460, "h": 300 },
                                                         { "type": "recipe_table", "book": "missing" } ])~") } };
    lv_obj_t* s = ui().build_screen(scr, "/base");
    REQUIRE(s);
    REQUIRE(ui().show_screen("rec", "none"));
    f.rt.advance(400);
    lv_obj_t* table = lv_obj_get_child(s, 0);
    lv_obj_t* list = lv_obj_get_child(table, 0);
    CHECK(lv_obj_get_child_count(list) == 2);
    CHECK(lv_obj_has_state(lv_obj_get_child(list, 1), LV_STATE_CHECKED));                  // B selected
    lv_obj_t* rows = lv_obj_get_child(lv_obj_get_child(table, 1), 1);
    CHECK(lv_obj_get_child_count(rows) == 5);                                              // heading + 4 items
    // pick A: its values differ from the current ones and are marked
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    CHECK(recipes().book("product")->selected == "A");
    rows = lv_obj_get_child(lv_obj_get_child(table, 1), 1);
    lv_obj_update_layout(s);                                                              // (LVGL shortens texts until it knows the width)
    lv_obj_t* temp_val = lv_obj_get_child(lv_obj_get_child(rows, 1), 1);
    CHECK(std::string(lv_label_get_text(temp_val)) == "* 80");
    // Write asks first (a dialog on the top layer); OK writes
    lv_obj_t* foot = lv_obj_get_child(lv_obj_get_child(table, 1), 2);
    lv_obj_send_event(lv_obj_get_child(foot, 0), LV_EVENT_CLICKED, nullptr);
    CHECK(f.temp->get_number() == 95.5);
    lv_obj_t* dlg = lv_obj_get_child(lv_layer_top(), -1);
    REQUIRE(dlg);
    lv_obj_t* ok_btn = lv_obj_get_child(lv_obj_get_child(lv_obj_get_child(dlg, 0), 1), 1);
    lv_obj_send_event(ok_btn, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(ok_btn, LV_EVENT_CLICKED, nullptr);                                  // a double tap writes once
    CHECK(f.temp->get_number() == 80);
    f.rt.advance(400);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);                                    // the dialog is gone
    rows = lv_obj_get_child(lv_obj_get_child(table, 1), 1);
    lv_obj_update_layout(s);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(rows, 1), 1))) == "80");   // no longer different

    lv_obj_t* missing = lv_obj_get_child(s, 1);
    CHECK(std::string(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(missing, 1), 0))).find("no recipe book") != std::string::npos);
    ui().unload_all();
    f.rt.advance(400);
}

TEST_CASE("recipes: items with a label and a unit; the recipe written last; the question names no PLC")
{
    Fixture f;
    REQUIRE(f.load(R"~({ "books": [ { "name": "bread", "items": [
        { "tag": "temp_sp", "label": "Baking temperature", "unit": "°C" }, { "tag": "speed_sp", "unit": "rpm" }, "mode" ],
        "recipes": [ { "name": "Baguette", "values": { "temp_sp": 120, "speed_sp": 90 } }, { "name": "Roll", "values": { "temp_sp": 100 } } ] } ] })~") == 1);
    const RecipeBook* b = recipes().book("bread");
    REQUIRE(b);
    CHECK(b->items == std::vector<std::string>{ "temp_sp", "speed_sp", "mode" });
    Tag* written = tags().find("sys.recipe.bread.written");
    REQUIRE(written);
    CHECK(written->get().format() == "");                                                   // nothing written yet
    CHECK(recipes().select("bread", "Roll") == "");
    CHECK(written->get().format() == "");                                                   // picking is not writing
    CHECK(recipes().write("bread", "Baguette") == "");
    CHECK(written->get().format() == "Baguette");
    CHECK(tags().find("sys.recipe.bread.selected")->get().format() == "Baguette");

    json scr = { { "name", "rec2" }, { "children", J(R"~([ { "type": "recipe_table", "book": "bread", "w": 600, "h": 300 } ])~") } };
    lv_obj_t* s = ui().build_screen(scr, "/base");
    REQUIRE(s);
    REQUIRE(ui().show_screen("rec2", "none"));
    f.rt.advance(400);
    lv_obj_update_layout(s);
    lv_obj_t* table = lv_obj_get_child(s, 0);
    lv_obj_t* rows = lv_obj_get_child(lv_obj_get_child(table, 1), 1);
    auto item = [&](int r) { return std::string(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(rows, r), 0))); };
    CHECK(item(1) == "Baking temperature (°C)");
    CHECK(item(2) == "speed_sp (rpm)");                                                     // no label: the tag's name
    CHECK(item(3) == "mode");
    lv_obj_t* foot = lv_obj_get_child(lv_obj_get_child(table, 1), 2);
    lv_obj_send_event(lv_obj_get_child(foot, 0), LV_EVENT_CLICKED, nullptr);               // Write: the question
    lv_obj_t* dlg = lv_obj_get_child(lv_layer_top(), -1);
    REQUIRE(dlg);
    const std::string q = lv_label_get_text(lv_obj_get_child(lv_obj_get_child(dlg, 0), 0));
    CHECK(q == "Write 'Baguette'?");
    lv_obj_t* cancel = lv_obj_get_child(lv_obj_get_child(lv_obj_get_child(dlg, 0), 1), 0);
    lv_obj_send_event(cancel, LV_EVENT_CLICKED, nullptr);                                  // (the dialog must not stay for later tests)
    f.rt.advance(400);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    ui().unload_all();
    f.rt.advance(400);
}
