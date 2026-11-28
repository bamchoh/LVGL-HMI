// Languages, "@id" texts and the project's font (texts.hpp). Grouped by ISO/IEC 25010 characteristic.
#include <doctest.h>
#include "alarm.hpp"
#include "font_cmap.hpp"
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "texts.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

const char* kTexts = R"~({ "languages": ["ja", "en"], "language": "ja",
  "texts": { "title": { "ja": "ポンプ場", "en": "Pump station" },
             "start": { "ja": "運転", "en": "Start" },
             "only_ja": { "ja": "日本語だけ" },
             "opt_a": { "ja": "自動", "en": "Auto" } } })~";

// (HML_TEST_SOURCE_DIR in the environment: the sources somewhere else, e.g. copied to a Raspberry Pi)
fs::path source_dir() { const char* e = std::getenv("HML_TEST_SOURCE_DIR"); return e && *e ? fs::path(e) : fs::path(HML_TEST_SOURCE_DIR); }
std::string font_path() { return (source_dir() / "projects" / "demo" / "assets" / "fonts" / "NotoSansJP-subset.otf").generic_string(); }

struct Fixture {
    Runtime rt;
    int rebuilt = 0;
    Fixture()
    {
        texts().load(json::parse(kTexts));
        register_language_actions();
        texts().set_on_change([this] { rebuilt++; ui().rebuild_all(); ui().show_screen("main", "none"); });
    }
    ~Fixture() { texts().set_on_change(nullptr); texts().clear(); set_font_file(""); }
};

std::string label_text(lv_obj_t* o) { return lv_label_get_text(o); }

// the n-th child of a class (LVGL puts a dropdown's list on the screen too, so indexes shift)
lv_obj_t* nth(lv_obj_t* parent, const lv_obj_class_t* cls, int n)
{
    for(uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t* c = lv_obj_get_child(parent, static_cast<int32_t>(i));
        if(lv_obj_check_type(c, cls) && n-- == 0) return c;
    }
    FAIL("no such child");
    return nullptr;
}

} // namespace

// ================================================================ functional suitability

TEST_CASE("texts: @id in the current language, falls back, leaves the rest alone")
{
    Fixture f;
    CHECK(texts().languages() == std::vector<std::string>{ "ja", "en" });
    CHECK(texts().language() == "ja");
    CHECK(texts().resolve("@title") == "ポンプ場");
    CHECK(texts().resolve("title") == "title");
    CHECK(texts().resolve("@unknown") == "@unknown");          // not an id: the text itself
    CHECK(texts().resolve("@@title") == "@title");             // "@@" = "@"
    CHECK(texts().resolve("@") == "@");
    CHECK(texts().resolve("") == "");
    CHECK(texts().missing() == std::vector<std::string>{ "only_ja (en)" });
    CHECK(tags().find("sys.lang")->get().format() == "ja");
    CHECK(tags().find("sys.languages")->get().format() == "ja,en");

    CHECK(texts().set_language("en"));
    CHECK(texts().resolve("@title") == "Pump station");
    CHECK(texts().resolve("@only_ja") == "日本語だけ");          // missing in en: the first language
    CHECK_FALSE(texts().set_language("fr"));
    CHECK(texts().language() == "en");

    // writing sys.lang switches; a value that is not a language is put back
    tags().find("sys.lang")->set_string("ja", nullptr); f.rt.pump();
    CHECK(texts().language() == "ja");
    tags().find("sys.lang")->set_string("xx", nullptr); f.rt.pump();
    CHECK(texts().language() == "ja");
    CHECK(tags().find("sys.lang")->get().format() == "ja");

    // without "languages": the languages of the texts
    texts().load(json::parse(R"~({ "texts": { "a": { "de": "Hallo", "en": "Hello" } } })~"));
    CHECK(texts().languages().size() == 2);
    texts().load(json::parse(R"~({ "languages": ["ja", "../x", 5], "language": "en" })~"));
    CHECK(texts().languages() == std::vector<std::string>{ "ja" });
    CHECK(texts().language() == "ja");                         // "en" is not one of them
}

TEST_CASE("texts: widgets, options and the language action; the screen is built again")
{
    Fixture f;
    json scr = json::parse(R"~({ "name": "main", "children": [
        { "type": "label", "text": "@title" },
        { "type": "button", "text": "@start", "on_click": { "action": "language", "lang": "en" } },
        { "type": "dropdown", "options": ["@opt_a", "Manual"] },
        { "type": "checkbox", "text": "@start" },
        { "type": "button", "text": "next", "on_click": { "action": "language" } },
        { "type": "label", "text": "@@literal" } ] })~");
    REQUIRE(ui().add_screen(scr, "/base"));
    REQUIRE(ui().show_screen("main", "none"));
    lv_obj_t* s = lv_screen_active();
    CHECK(label_text(nth(s, &lv_label_class, 0)) == "ポンプ場");
    CHECK(label_text(lv_obj_get_child(nth(s, &lv_button_class, 0), 0)) == "運転");
    char opt[64];
    lv_dropdown_get_selected_str(nth(s, &lv_dropdown_class, 0), opt, sizeof(opt));
    CHECK(std::string(opt) == "自動");
    CHECK(std::string(lv_checkbox_get_text(nth(s, &lv_checkbox_class, 0))) == "運転");
    CHECK(label_text(nth(s, &lv_label_class, 1)) == "@literal");

    lv_obj_send_event(nth(s, &lv_button_class, 0), LV_EVENT_CLICKED, nullptr);     // "language": "en"
    CHECK(texts().language() == "en");
    CHECK(f.rebuilt == 0);                                      // not inside the event: after it
    f.rt.advance(50);
    CHECK(f.rebuilt == 1);
    s = lv_screen_active();
    CHECK(label_text(nth(s, &lv_label_class, 0)) == "Pump station");
    lv_dropdown_get_selected_str(nth(s, &lv_dropdown_class, 0), opt, sizeof(opt));
    CHECK(std::string(opt) == "Auto");
    lv_obj_send_event(nth(s, &lv_button_class, 1), LV_EVENT_CLICKED, nullptr);     // "next": back to ja
    f.rt.advance(50);
    CHECK(texts().language() == "ja");
    CHECK(label_text(nth(lv_screen_active(), &lv_label_class, 0)) == "ポンプ場");
}

TEST_CASE("texts: alarm messages in the current language")
{
    Fixture f;
    Tag* t = f.rt.tag("hot", TagType::Bool, 1);
    (void)t;
    fs::path dir = fs::temp_directory_path() / "hml_texts_alarm_test";
    fs::remove_all(dir);
    json cfg = json::parse(R"~({ "items": [ { "name": "h", "tag": "hot", "message": "@title" } ] })~");
    REQUIRE(alarms().load(&cfg, dir.generic_string()) == 1);
    CHECK(alarms().current()[0].message == "ポンプ場");
    CHECK(alarms().query()[0].message == "ポンプ場");            // recorded in the language of that moment
    texts().set_language("en");
    CHECK(alarms().current()[0].message == "Pump station");
    alarms().clear();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ================================================================ the font

TEST_CASE("font: every size from the project's file, built-in symbols as the fallback")
{
    Fixture f;
    REQUIRE(fs::exists(font_path()));
    set_font_file(font_path());
    CHECK(has_font_file());
    const lv_font_t* f20 = font_of_size(20);
    REQUIRE(f20);
    CHECK(font_of_size(20) == f20);                             // made once per size
    CHECK(font_of_size(22));                                    // sizes Montserrat does not have
    CHECK(lv_font_get_line_height(f20) >= 20);
    lv_font_glyph_dsc_t g;
    CHECK(lv_font_get_glyph_dsc(f20, &g, 0x6E29 /* 温 */, 0));
    CHECK(g.adv_w > 0);
    CHECK(lv_font_get_glyph_dsc(f20, &g, 0xF00C /* LV_SYMBOL_OK: from the fallback */, 0));
    // drawn on a screen without trouble
    json scr = json::parse(R"~({ "name": "main", "children": [ { "type": "label", "text": "温度 25.0 ℃ ✓", "style": { "font": 20 } },
                                                            { "type": "label", "text": " OK", "style": { "font": 13 } } ] })~");
    REQUIRE(ui().add_screen(scr, "/base"));
    REQUIRE(ui().show_screen("main", "none"));
    CHECK(lv_obj_get_style_text_font(lv_obj_get_child(lv_screen_active(), 0), LV_PART_MAIN) == f20);
    hmltest::Lvgl::get().render();
    ui().unload_all();
    set_font_file("");
    CHECK_FALSE(has_font_file());
    CHECK(font_of_size(20) == nullptr);
}

// ================================================================ security / robustness

TEST_CASE("font: a missing or broken file falls back to the built-in font")
{
    Fixture f;
    set_font_file("/no/such/font.otf");
    CHECK(font_of_size(16) == nullptr);
    json v = 16;
    CHECK(font_from_json(&v) == &lv_font_montserrat_16);
    // random bytes, and a real header followed by garbage
    fs::path p = fs::temp_directory_path() / "hml_bad_font.ttf";
    std::mt19937 rng(9);
    for(int round = 0; round < 20; round++) {
        {
            std::ofstream o(p, std::ios::binary);
            std::string junk(static_cast<size_t>(rng() % 5000), ' ');
            for(auto& c : junk) c = static_cast<char>(rng());
            if(round % 2) junk.replace(0, std::min<size_t>(4, junk.size()), std::string("OTTO", std::min<size_t>(4, junk.size())));
            o << junk;
        }
        set_font_file("");
        set_font_file(p.generic_string());
        const lv_font_t* bad = font_of_size(16);
        if(bad) {                                               // (accepted by the parser: drawing must not crash)
            lv_font_glyph_dsc_t g;
            lv_font_get_glyph_dsc(bad, &g, 'A', 0);
        }
    }
    set_font_file("");
    std::error_code ec;
    fs::remove(p, ec);
}

TEST_CASE("font: the demo project with its font and texts renders every screen many times")
{
    Runtime rt;
    Project p;
    REQUIRE(p.load((source_dir() / "projects" / "demo" / "project.json").generic_string()));
    p.apply_theme(hmltest::Lvgl::get().disp);
    REQUIRE(p.build_screens() > 0);
    const auto t0 = std::chrono::steady_clock::now();
    for(const char* s : { "main", "settings", "alarms", "recipes", "trend", "main" }) {
        if(!ui().show_screen(s, "none")) continue;
        for(int i = 0; i < 5; i++) { rt.advance(50); hmltest::Lvgl::get().render(); }
    }
    // what is on the layers above the screens (popups, dialogs) is in the project's font too
    REQUIRE(ui().open_popup("temp_detail"));
    rt.advance(50);
    CHECK(lv_obj_get_style_text_font(lv_layer_top(), LV_PART_MAIN) == lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN));
    CHECK(lv_obj_get_style_text_font(lv_layer_top(), LV_PART_MAIN) != LV_FONT_DEFAULT);
    ui().close_popup();
    MESSAGE("6 screens x 5 frames with the font file: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() << " ms");
    CHECK(texts().set_language("en"));
    rt.advance(100);
    hmltest::Lvgl::get().render();
    ui().unload_all();
    p.stop_logs();
    p.unload();
    texts().clear();
    set_font_file("");
}

TEST_CASE("font: characters and advances come from the font's own tables")
{
    FontCmap cm;
    std::string err;
    REQUIRE(cm.load(font_path(), &err));
    CHECK(cm.has('A'));
    CHECK(cm.has(0x6E29));                                     // 温
    CHECK_FALSE(cm.has(0xF00C));                               // LV_SYMBOL_OK: not in this font
    CHECK_FALSE(cm.has(0x1F600));
    CHECK(cm.count() > 3500);                                  // (kana, symbols and the level-1 kanji of the subset)
    // narrow letters are narrow: TinyTTF gave "r" the width of another glyph
    CHECK(cm.advance_px('r', 16) < cm.advance_px('m', 16));
    CHECK(cm.advance_px('r', 16) >= 4);
    CHECK(cm.advance_px(0x6E29, 16) == 16);                    // a full-width character is 1 em
    CHECK(cm.advance_px(0xF00C, 16) == -1);

    Fixture f;
    set_font_file(font_path());
    const lv_font_t* f16 = font_of_size(16);
    REQUIRE(f16);
    for(char c : std::string("operator admin")) {
        lv_font_glyph_dsc_t g;
        REQUIRE(lv_font_get_glyph_dsc(f16, &g, static_cast<uint32_t>(c), 0));
        CHECK_MESSAGE(g.adv_w == cm.advance_px(static_cast<uint32_t>(c), 16), "letter " << c);
    }
    set_font_file("");
}

TEST_CASE("font: cmap parsing refuses broken data without reading past it")
{
    std::string err;
    FontCmap cm;
    auto data = read_file(font_path(), 64u << 20);
    REQUIRE(data);
    const auto* d = reinterpret_cast<const uint8_t*>(data->data());
    CHECK_FALSE(cm.parse(d, 3, &err));
    CHECK_FALSE(cm.parse(nullptr, 0, &err));
    // every length cut short, and random bytes in the tables: no crash, no read past the end
    // (AddressSanitizer would see one), never a glyph that is not in the font
    for(size_t n : { size_t(12), size_t(100), size_t(1000), size_t(5000), data->size() / 2, data->size() - 1 }) {
        std::vector<uint8_t> cut(d, d + n);
        cm.parse(cut.data(), cut.size(), &err);
    }
    std::mt19937 rng(5);
    std::vector<uint8_t> copy(d, d + data->size());
    for(int round = 0; round < 50; round++) {
        std::vector<uint8_t> v = copy;
        for(int k = 0; k < 64; k++) v[rng() % std::min<size_t>(v.size(), 4096)] = static_cast<uint8_t>(rng());
        if(cm.parse(v.data(), v.size(), &err)) { cm.has('A'); cm.advance_px('A', 16); }
    }
    for(int round = 0; round < 50; round++) {
        std::vector<uint8_t> v(rng() % 2000);
        for(auto& b : v) b = static_cast<uint8_t>(rng());
        if(v.size() >= 4) { v[0] = 0; v[1] = 1; v[2] = 0; v[3] = 0; }
        cm.parse(v.data(), v.size(), &err);
    }
}

TEST_CASE("font: the one for European languages that comes with the designer has the letters")
{
    Runtime rt;
    const std::string file = (hmltest::source_dir() / "tools" / "designer" / "fonts" / "NotoSans-subset.ttf").generic_string();
    set_font_file(file);
    CHECK(font_file_has("äöüÄÖÜß éèêçœ ñ¿¡ åøæ"));
    CHECK(font_file_has("ąćęłńśźż čďěňřšťůž őű ğışİ ășț āēīūėįų"));
    CHECK(font_file_has("Привет мир ЖЩЫ"));
    CHECK(font_file_has("Καλημέρα Ω"));
    CHECK(font_file_has("5 € £ ± × ÷ µ ‰ ™ – — … °"));
    CHECK_FALSE(font_file_has("温度"));                              // (not a font for Japanese)
    const lv_font_t* f = font_of_size(16);
    REQUIRE(f);
    lv_font_glyph_dsc_t g;
    CHECK(lv_font_get_glyph_dsc(f, &g, 0x00FC /* ü */, 0));
    CHECK(g.adv_w > 0);
    set_font_file("");
}
