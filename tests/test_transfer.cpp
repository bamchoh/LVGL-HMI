// Backup, install and "previous project" on a USB memory (transfer.hpp) and the page
// System > Storage. A folder stands in for the USB memory.
#include <doctest.h>
#include "miniz.h"
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

TEST_CASE("transfer: the removable file systems in what lsblk says")
{
    // a Raspberry Pi with an empty card reader (sda), a USB memory with two partitions (sdb), its own SD card
    const char* flat = R"({ "blockdevices": [
        { "path": "/dev/loop0", "rm": false, "hotplug": false, "type": "loop", "fstype": "swap", "label": null, "mountpoint": null },
        { "path": "/dev/sda", "rm": true, "hotplug": true, "type": "disk", "fstype": null, "label": null, "mountpoint": null },
        { "path": "/dev/sdb", "rm": true, "hotplug": true, "type": "disk", "fstype": null, "label": null, "mountpoint": null },
        { "path": "/dev/sdb1", "rm": true, "hotplug": true, "type": "part", "fstype": "vfat", "label": "rpi4", "mountpoint": null },
        { "path": "/dev/sdb2", "rm": true, "hotplug": true, "type": "part", "fstype": "ext3", "label": null, "mountpoint": "/media/pi/data" },
        { "path": "/dev/mmcblk0p2", "rm": false, "hotplug": false, "type": "part", "fstype": "ext4", "label": "rootfs", "mountpoint": "/" } ] })";
    std::vector<UsbVolume> v = transfer_parse_lsblk(flat);
    REQUIRE(v.size() == 2);
    CHECK(v[0].device == "/dev/sdb1");
    CHECK(v[0].fstype == "vfat");
    CHECK(v[0].label == "rpi4");
    CHECK(v[0].mountpoint == "");
    CHECK(v[1].device == "/dev/sdb2");
    CHECK(v[1].mountpoint == "/media/pi/data");

    // older lsblk: partitions as children, flags as text; a memory without a partition table
    const char* tree = R"({ "blockdevices": [
        { "path": "/dev/sdc", "rm": "1", "type": "disk", "fstype": null, "children": [
            { "path": "/dev/sdc1", "rm": "1", "type": "part", "fstype": "exfat", "label": "STICK", "mountpoint": null } ] },
        { "path": "/dev/sdd", "rm": "0", "hotplug": "1", "type": "disk", "fstype": "vfat", "label": "WHOLE", "mountpoint": null },
        { "path": "/dev/sde", "rm": "0", "hotplug": "0", "type": "disk", "fstype": "ext4", "label": "fixed", "mountpoint": null } ] })";
    v = transfer_parse_lsblk(tree);
    REQUIRE(v.size() == 2);
    CHECK(v[0].device == "/dev/sdc1");
    CHECK(v[0].label == "STICK");
    CHECK(v[1].device == "/dev/sdd");

    CHECK(transfer_parse_lsblk("").empty());
    CHECK(transfer_parse_lsblk("lsblk: command not found").empty());
    CHECK(transfer_parse_lsblk("{}").empty());
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
    CHECK_FALSE(flag("no_location"));
    CHECK(text("hint") == "");

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

// ================================================================ security: a hostile USB memory

TEST_CASE("transfer: a project file cannot bring keys, local settings or logs, nor leave the folder")
{
    Temp tmp;
    const fs::path proj = tmp.dir / "device" / "projects" / "pump", usb = tmp.dir / "usb", src = tmp.dir / "pc" / "new";
    make_project(proj, "Old");
    write(proj / "logs" / "data.db", "recorded");
    write(proj / "project.local.json", "{ \"drivers\": {} }");
    write(proj / "pki" / "own" / "key.der", "secret");
    make_project(src, "New");
    fs::create_directories(usb);

    // a project file made by hand: a good project, and with it files the device owns
    auto zip = [&](const char* name, const std::vector<std::pair<std::string, std::string>>& entries) {
        mz_zip_archive z;
        std::memset(&z, 0, sizeof(z));
        REQUIRE(mz_zip_writer_init_file(&z, (usb / name).u8string().c_str(), 0));
        for(const auto& e : entries) REQUIRE(mz_zip_writer_add_mem(&z, e.first.c_str(), e.second.data(), e.second.size(), MZ_DEFAULT_LEVEL));
        REQUIRE(mz_zip_writer_finalize_archive(&z));
        mz_zip_writer_end(&z);
    };
    zip("greedy.hmlproj", { { "project.json", read(src / "project.json") }, { "screens/s.json", read(src / "screens" / "s.json") },
                            { "pki/own/key.der", "planted" }, { "pki/trusted/evil.der", "planted" },
                            { "project.local.json", "{ \"users\": [] }" }, { "logs/data.db", "wiped" } });
    const TransferTarget t = target_of(proj);
    const std::string why = transfer_install(t, (usb / "greedy.hmlproj").generic_u8string());
    // (installed without them, or refused: either way what the device owns is as it was)
    CHECK(read(proj / "pki" / "own" / "key.der") == "secret");
    CHECK_FALSE(fs::exists(proj / "pki" / "trusted" / "evil.der"));
    CHECK(read(proj / "project.local.json") == "{ \"drivers\": {} }");
    CHECK(read(proj / "logs" / "data.db") == "recorded");
    if(why.empty()) CHECK(title_of(proj) == "New");
    else CHECK(title_of(proj) == "Old");

    // a file that is not a zip, an empty one, a huge name
    write(usb / "empty.hmlproj", "");
    write(usb / "noise.hmlproj", std::string(4096, '\xA5'));
    CHECK(transfer_install(t, (usb / "empty.hmlproj").generic_u8string()) != "");
    CHECK(transfer_install(t, (usb / "noise.hmlproj").generic_u8string()) != "");
    CHECK(transfer_install(t, (usb / (std::string(300, 'a') + ".hmlproj")).generic_u8string()) != "");
    CHECK(transfer_install(t, "") != "");
    CHECK_FALSE(fs::exists(tmp.dir / "device" / "projects" / "pump.incoming"));
}

TEST_CASE("transfer: what lsblk and udisks say never becomes a command, and is told plainly")
{
    // a device name that is not a device node is not a volume (it would go into a command line)
    const char* hostile = R"J({ "blockdevices": [
        { "path": "/dev/sdb1; reboot", "rm": true, "type": "part", "fstype": "vfat", "label": "a", "mountpoint": null },
        { "path": "/dev/sdb1$(reboot)", "rm": true, "type": "part", "fstype": "vfat", "label": "b", "mountpoint": null },
        { "path": "/dev/../etc/passwd", "rm": true, "type": "part", "fstype": "vfat", "label": "c", "mountpoint": null },
        { "path": "sdb1", "rm": true, "type": "part", "fstype": "vfat", "label": "d", "mountpoint": null },
        { "path": "/dev/sdb1 -o exec", "rm": true, "type": "part", "fstype": "vfat", "label": "e", "mountpoint": null },
        { "path": 5, "rm": true, "type": "part", "fstype": "vfat", "label": "f", "mountpoint": null },
        { "path": "/dev/sdc1", "rm": true, "type": "part", "fstype": "vfat", "label": "$(reboot)`id`;'\"", "mountpoint": null },
        "not an object", null ] })J";
    const std::vector<UsbVolume> v = transfer_parse_lsblk(hostile);
    REQUIRE(v.size() == 1);
    CHECK(v[0].device == "/dev/sdc1");
    CHECK(transfer_parse_lsblk(R"({ "blockdevices": 5 })").empty());
    CHECK(transfer_parse_lsblk(std::string(100000, '[')).empty());

    // the message for the operator: the reason in words, the label without what a shell or a screen could trip over
    UsbVolume vol;
    vol.device = "/dev/sdc1";
    vol.label = v[0].label;
    std::string m = transfer_mount_problem(vol, "Error mounting /dev/sdc1: GDBus.Error:org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain: Not authorized");
    CHECK(m.find("may not mount it") != std::string::npos);
    CHECK(m.find("hml-usb.rules") != std::string::npos);
    CHECK(m.find_first_of("$`;\"()") == m.find("(install"));          // (only our own bracket)
    vol.label = "DATA 1";
    m = transfer_mount_problem(vol, "sh: 1: udisksctl: not found\n");
    CHECK(m.find("'DATA 1'") != std::string::npos);
    CHECK(m.find("udisks2 is not installed") != std::string::npos);
    vol.label = "";
    m = transfer_mount_problem(vol, std::string("Error mounting: wrong fs type\n\x1b[31m") + std::string(500, 'x'));
    CHECK(m.find("/dev/sdc1 cannot be mounted: Error mounting: wrong fs type") != std::string::npos);
    CHECK(m.find('\n') == std::string::npos);
    CHECK(m.find('\x1b') == std::string::npos);
    CHECK(m.size() < 260);
}
