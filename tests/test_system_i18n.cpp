// The system screens in Japanese (system_i18n.hpp): which language is chosen, and that no text
// of a screen was forgotten in the table.
#include <doctest.h>
#include "deploy_service.hpp"
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "system_i18n.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "texts.hpp"

#include <functional>
#include <set>

using namespace hml;
using hmltest::Runtime;

namespace {

// every fixed text of a screen: what its labels and buttons say (not what is bound to a tag)
void fixed_texts(const json& w, std::set<std::string>& out)
{
    if(w.is_array()) { for(const json& c : w) fixed_texts(c, out); return; }
    if(!w.is_object()) return;
    for(const char* key : { "text", "placeholder" })
        if(const json* t = jget(w, key); t && t->is_string() && !t->get<std::string>().empty()) out.insert(t->get<std::string>());
    if(w.contains("children")) fixed_texts(w["children"], out);
}

bool has_letters(const std::string& s)
{
    int n = 0;
    for(unsigned char c : s) if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) n++;
    return n >= 2;
}

} // namespace

TEST_CASE("system i18n: every fixed text of the system screens of the demo project has a Japanese entry")
{
    Runtime rt;
    Project p;
    REQUIRE(p.load((hmltest::source_dir() / "projects" / "demo" / "project.json").generic_string()));
    SystemSettings& sys = system_settings();
    set_system_language("en");                                    // (collect what the code says)
    sys.create_tags();
    std::set<std::string> texts;
    for(const json& s : sys.screens(800, 480)) fixed_texts(s, texts);
    // what the pages say about themselves: their titles, the lines in the menu, what they only show
    for(const std::string& id : sys.page_ids()) {
        SettingsPage* page = sys.page(id);
        texts.insert(page->title());
        texts.insert(page->summary());
        for(const SettingField& f : page->fields()) {
            if(id == "about" || id == "network") { texts.insert(f.label); continue; }   // (their values are the names of this machine and this project)
            if(f.label != f.key) texts.insert(f.label);           // (a label that is the key: a page with rows of its own, or a setting of a driver)
            // what a page only shows: its sentences (they begin with a capital), not the names and values of the project
            if(f.type == SettingField::Type::Info && !f.value.empty() && f.value[0] >= 'A' && f.value[0] <= 'Z') texts.insert(f.value);
        }
    }
    std::set<std::string> names;
    for(const DeployDesigner& d : deploy_service().designers()) names.insert(d.name);
    std::string missing;
    int n = 0;
    for(const std::string& t : texts) {
        if(!has_letters(t)) continue;                             // numbers, symbols, icons
        if(t[0] >= '0' && t[0] <= '9' && !sys_text_known(t)) continue;   // (an address of this machine: "10.1.0.57 (eth0)")
        if(static_cast<unsigned char>(t[0]) >= 0x80) continue;   // (an icon and a title: the title is looked at by itself)
        // names and values that are not language: tags, files, addresses, fingerprints, versions
        if(t.find("SHA256:") != std::string::npos || t.find('/') != std::string::npos || t.find("HML ") == 0) continue;
        if(names.count(t)) continue;                              // (a designer this machine has accepted: a name)
        n++;
        if(!sys_text_known(t)) missing += "\n  " + t;
    }
    CHECK(n > 60);
    CHECK_MESSAGE(missing.empty(), "no Japanese for:" << missing);

    ui().unload_all();
    p.stop_logs();
    p.unload();
    set_system_language("en");
}

TEST_CASE("system i18n: a text is translated only in Japanese, a text without an entry stays as it is")
{
    set_system_language("ja");
    CHECK(system_language() == "ja");
    CHECK(sys_text("Apply") == "適用");
    CHECK(sys_text("Driver: plc") == "ドライバー: plc");
    CHECK(sys_text("3 key(s)") == "鍵 3 個");
    CHECK(sys_text("pump_1") == "pump_1");
    CHECK(sys_text("Admin or higher is needed.") == "Admin 以上のレベルが必要です。");
    CHECK(sys_text("Wrong PIN (2 more tries).") == "PIN が違います（あと 2 回）。");
    CHECK(sys_text("Not installed: the file is damaged") == "入れ替えられませんでした: the file is damaged");
    CHECK(sys_text("(none)") == "(なし)");
    CHECK(sys_text("08:00  weekdays") == "08:00  平日");
    CHECK(sys_text("Next 11-30 08:00") == "次回 11-30 08:00");
    CHECK(sys_text("") == "");
    set_system_language("fr");                                    // (anything else is English)
    CHECK(system_language() == "en");
    CHECK(sys_text("Apply") == "Apply");
    CHECK(sys_text("Driver: plc") == "Driver: plc");
}

TEST_CASE("system i18n: the system screens follow the language of the project, also when it is switched")
{
    Runtime rt;
    Project p;
    REQUIRE(p.load((hmltest::source_dir() / "projects" / "demo" / "project.json").generic_string()));
    REQUIRE(texts().language() == "ja");                          // (the demo starts in Japanese and has a Japanese font)
    CHECK(system_language() == "ja");
    CHECK(tags().find("sys.deploy.summary") != nullptr);
    REQUIRE(texts().set_language("en"));
    p.rebuild_screens();
    CHECK(system_language() == "en");
    REQUIRE(texts().set_language("ja"));
    p.rebuild_screens();
    CHECK(system_language() == "ja");

    ui().unload_all();
    p.stop_logs();
    p.unload();
    set_system_language("en");
}
