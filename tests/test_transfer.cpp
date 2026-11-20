// Backup, install and "previous project" on a USB memory (transfer.hpp) and the page
// System > Storage. A folder stands in for the USB memory.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "project.hpp"
#include "project_archive.hpp"
#include "system_settings.hpp"
#include "transfer.hpp"
#include "util.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct Temp {
    fs::path dir;
    Temp()
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_transfer_test_" + std::to_string(n++) + "_" + std::to_string(static_cast<long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Temp() { std::error_code ec; fs::remove_all(dir, ec); }
};

void write(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}
std::string read(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// a small project: one screen with a label
void make_project(const fs::path& dir, const std::string& title, int w = 800, int h = 480, const std::string& goto_screen = "s")
{
    write(dir / "project.json", "{ \"title\": \"" + title + "\", \"display\": { \"width\": " + std::to_string(w) + ", \"height\": " + std::to_string(h) +
          " }, \"start_screen\": \"s\", \"tags\": [ { \"name\": \"a\", \"type\": \"int\" } ], \"screens\": [\"screens/s.json\"] }");
    write(dir / "screens" / "s.json", "{ \"name\": \"s\", \"children\": [ { \"type\": \"button\", \"text\": \"" + title +
          "\", \"on_click\": { \"action\": \"goto\", \"screen\": \"" + goto_screen + "\" } } ] }");
}

std::string title_of(const fs::path& dir)
{
    json j = json::parse(read(dir / "project.json"), nullptr, false);
    return j.is_object() ? jstr(j, "title", "") : "";
}

TransferTarget target_of(const fs::path& dir)
{
    TransferTarget t;
    t.dir = dir.generic_u8string();
    t.name = dir.filename().u8string();
    t.width = 800; t.height = 480;
    return t;
}

} // namespace

TEST_CASE("transfer: a backup holds the project, the logs and the local settings, never the keys")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", usb = tmp.dir / "usb";
    make_project(proj, "Pump");
    write(proj / "logs" / "data.db", "recorded");
    write(proj / "project.local.json", "{ \"drivers\": {} }");
    write(proj / "pki" / "own" / "key.der", "secret");
    fs::create_directories(usb);

    std::string where;
    REQUIRE(transfer_backup(target_of(proj), usb.generic_u8string(), &where) == "");
    const fs::path b = fs::u8path(where);
    CHECK(b.parent_path() == usb / "hml-backup");
    CHECK(b.filename().u8string().rfind("pump-", 0) == 0);
    REQUIRE(fs::exists(b / "pump.hmlproj"));
    CHECK(read(b / "logs" / "data.db") == "recorded");
    CHECK(fs::exists(b / "project.local.json"));
    CHECK_FALSE(fs::exists(b / "pki"));
    // the project file in it is a whole project
    const fs::path out = tmp.dir / "unpacked";
    std::string err;
    REQUIRE(unpack_project(b / "pump.hmlproj", out, &err));
    CHECK(title_of(out) == "Pump");
    CHECK(fs::exists(out / "screens" / "s.json"));

    CHECK(transfer_backup(target_of(proj), (tmp.dir / "nope").generic_u8string()) != "");
    CHECK(transfer_backup(target_of(proj), "") != "");
}

TEST_CASE("transfer: installing replaces the project, keeps what the device added, and can be undone")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", usb = tmp.dir / "usb", src = tmp.dir / "pc" / "new";
    make_project(proj, "Old");
    write(proj / "logs" / "data.db", "recorded");
    write(proj / "project.local.json", "{ \"drivers\": {} }");
    write(proj / "pki" / "own" / "key.der", "secret");
    write(proj / "assets" / "old.txt", "only in the old one");
    make_project(src, "New");
    fs::create_directories(usb / "hml");
    std::string err;
    REQUIRE(pack_project(src, usb / "new.hmlproj", &err));
    REQUIRE(pack_project(src, usb / "hml" / "another.hmlproj", &err));
    write(usb / "notes.txt", "not a project");

    const std::vector<std::string> files = transfer_project_files(usb.generic_u8string());
    REQUIRE(files.size() == 2);
    CHECK(fs::u8path(files[0]).filename() == "another.hmlproj");
    CHECK(fs::u8path(files[1]).filename() == "new.hmlproj");
    CHECK(transfer_project_files("").empty());
    CHECK(transfer_project_files((tmp.dir / "nope").generic_u8string()).empty());

    const TransferTarget t = target_of(proj);
    CHECK(transfer_previous(t) == "");
    CHECK(transfer_rollback(t) != "");
    REQUIRE(transfer_install(t, (usb / "new.hmlproj").generic_u8string()) == "");
    CHECK(title_of(proj) == "New");
    CHECK_FALSE(fs::exists(proj / "assets" / "old.txt"));          // what the old project had is gone
    CHECK(read(proj / "logs" / "data.db") == "recorded");           // what the device added stays
    CHECK(fs::exists(proj / "project.local.json"));
    CHECK(read(proj / "pki" / "own" / "key.der") == "secret");
    CHECK_FALSE(fs::exists(tmp.dir / "device" / "projects" / "pump.incoming"));
    CHECK(transfer_previous(t) != "");

    // back to the one before, and forth again
    REQUIRE(transfer_rollback(t) == "");
    CHECK(title_of(proj) == "Old");
    CHECK(fs::exists(proj / "assets" / "old.txt"));
    CHECK(read(proj / "logs" / "data.db") == "recorded");
    REQUIRE(transfer_rollback(t) == "");
    CHECK(title_of(proj) == "New");
}

TEST_CASE("transfer: a project that does not fit or has problems is refused, and nothing changes")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", usb = tmp.dir / "usb";
    make_project(proj, "Old");
    fs::create_directories(usb);
    std::string err;
    make_project(tmp.dir / "small", "Small", 480, 320);
    REQUIRE(pack_project(tmp.dir / "small", usb / "small.hmlproj", &err));
    make_project(tmp.dir / "broken", "Broken", 800, 480, "no_such_screen");
    REQUIRE(pack_project(tmp.dir / "broken", usb / "broken.hmlproj", &err));
    write(usb / "garbage.hmlproj", "this is not a zip archive");
    const TransferTarget t = target_of(proj);

    std::string why = transfer_install(t, (usb / "small.hmlproj").generic_u8string());
    CHECK(why.find("480 x 320") != std::string::npos);
    CHECK(why.find("800 x 480") != std::string::npos);
    why = transfer_install(t, (usb / "broken.hmlproj").generic_u8string());
    CHECK(why.find("problem") != std::string::npos);
    CHECK(why.find("no_such_screen") != std::string::npos);
    CHECK(transfer_install(t, (usb / "garbage.hmlproj").generic_u8string()).find("not a project file") != std::string::npos);
    CHECK(transfer_install(t, (usb / "gone.hmlproj").generic_u8string()) != "");

    CHECK(title_of(proj) == "Old");
    CHECK(transfer_previous(t) == "");
    CHECK_FALSE(fs::exists(tmp.dir / "device" / "projects" / "pump.incoming"));
}

TEST_CASE("transfer: a runtime started from a project file gets that file replaced")
{
    Temp tmp;
    const fs::path work = tmp.dir / "cache" / "work", usb = tmp.dir / "usb", archive = tmp.dir / "device" / "line.hmlproj";
    std::string err;
    make_project(tmp.dir / "old", "Old");
    make_project(tmp.dir / "new", "New");
    fs::create_directories(archive.parent_path());
    fs::create_directories(usb);
    REQUIRE(pack_project(tmp.dir / "old", archive, &err));
    REQUIRE(pack_project(tmp.dir / "new", usb / "new.hmlproj", &err));
    REQUIRE(unpack_project(archive, work, &err));
    TransferTarget t = target_of(work);
    t.archive = archive.generic_u8string();

    REQUIRE(transfer_install(t, (usb / "new.hmlproj").generic_u8string()) == "");
    const fs::path out = tmp.dir / "out";
    REQUIRE(unpack_project(archive, out, &err));
    CHECK(title_of(out) == "New");                              // what the next start unpacks
    CHECK(transfer_previous(t) != "");
    REQUIRE(transfer_rollback(t) == "");
    REQUIRE(unpack_project(archive, out, &err));
    CHECK(title_of(out) == "Old");
}

TEST_CASE("System > Storage: backup, install after a question, the restart is asked for")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", usb = tmp.dir / "usb";
    make_project(proj, "Old");
    make_project(tmp.dir / "new", "New");
    fs::create_directories(usb);
    std::string err;
    REQUIRE(pack_project(tmp.dir / "new", usb / "new.hmlproj", &err));
#ifdef _WIN32
    _putenv_s("HML_TRANSFER_DIR", usb.generic_u8string().c_str());
#else
    setenv("HML_TRANSFER_DIR", usb.generic_u8string().c_str(), 1);
#endif
    {
        const std::vector<std::string> loc = transfer_locations();
        REQUIRE(!loc.empty());
        CHECK(loc[0] == usb.generic_u8string());                 // the folder named by the environment comes first
    }

    Runtime rt;
    Project p;
    REQUIRE(p.load((proj / "project.json").generic_u8string()));
    SystemSettings& sys = system_settings();
    REQUIRE(sys.page("storage"));
    auto text = [](const char* key) { return tags().find(std::string("sys.storage.") + key)->get().format(); };
    auto flag = [](const char* key) { return tags().find(std::string("sys.storage.") + key)->get_number() != 0; };
    CHECK(text("project") == "pump  (800 x 480)");
    CHECK(flag("has_location"));
    CHECK(flag("has_file"));
    CHECK_FALSE(flag("has_previous"));
    CHECK(text("file__options") == "new.hmlproj");

    sys.command("storage", "backup");
    CHECK(text("status").find("Backup written to ") == 0);
    CHECK(fs::exists(usb / "hml-backup"));

    // install: asked first, "Cancel" changes nothing
    sys.command("storage", "install");
    CHECK(flag("confirm"));
    CHECK(text("confirm_text").find("new.hmlproj") != std::string::npos);
    sys.command("storage", "no");
    CHECK_FALSE(flag("confirm"));
    CHECK(title_of(proj) == "Old");
    sys.command("storage", "install");
    sys.command("storage", "yes");
    CHECK(title_of(proj) == "New");
    CHECK(text("status").find("Installed") == 0);
    CHECK(flag("has_previous"));
    CHECK(rt.restart_requests == 0);
    for(int i = 0; i < 5; i++) sys.poll();                        // (a moment later, so that the message is seen)
    CHECK(rt.restart_requests == 1);

    sys.command("storage", "rollback");
    CHECK(flag("confirm"));
    sys.command("storage", "yes");
    CHECK(title_of(proj) == "Old");

    ui().unload_all();
    p.stop_logs();
    p.unload();
#ifdef _WIN32
    _putenv_s("HML_TRANSFER_DIR", "");
#else
    unsetenv("HML_TRANSFER_DIR");
#endif
}
