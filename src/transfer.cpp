#include "transfer.hpp"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "check.hpp"
#include "jsonutil.hpp"
#include "project_archive.hpp"
#include "util.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cstdio>
#  include <set>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace hml {
namespace {

std::string u8(const fs::path& p) { return p.generic_u8string(); }

std::string stamp()
{
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char buf[32] = "";
    if(tm) std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", tm);
    return buf;
}

std::string safe_name(const std::string& s)
{
    std::string out;
    for(char c : s) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return out.empty() ? "project" : out;
}

fs::path previous_path(const TransferTarget& t)
{
    if(!t.archive.empty()) return fs::u8path(t.archive + ".previous");
    fs::path dir = fs::u8path(t.dir);
    return dir.parent_path() / (dir.filename().u8string() + ".previous" + kProjectArchiveExt);
}

bool read_display(const fs::path& project_json, int& w, int& h, std::string& err)
{
    std::ifstream f(project_json, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    json j = json::parse(ss.str(), nullptr, false, true);
    if(!j.is_object()) { err = "project.json cannot be read"; return false; }
    const json* d = jget(j, "display");
    w = d ? static_cast<int>(jnum(*d, "width", 0)) : 0;
    h = d ? static_cast<int>(jnum(*d, "height", 0)) : 0;
    return true;
}

// Unpack into a folder next to the target and look at it. "" = fine.
std::string examine(const TransferTarget& t, const fs::path& file, const fs::path& tmp)
{
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::string err;
    if(!unpack_project(file, tmp, &err)) return "not a project file: " + err;
    if(!fs::exists(tmp / "project.json", ec)) return "there is no project.json in the file";
    int w = 0, h = 0;
    if(!read_display(tmp / "project.json", w, h, err)) return err;
    if(t.width > 0 && t.height > 0 && (w != t.width || h != t.height))
        return vformat("the project is made for %d x %d, this display is %d x %d", w, h, t.width, t.height);
    const std::vector<CheckProblem> problems = check_project(u8(tmp / "project.json"));
    if(!problems.empty())
        return vformat("the project has %d problem(s), the first: %s", static_cast<int>(problems.size()), problems[0].line().c_str());
    return "";
}

void copy_tree(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    if(!fs::exists(from, ec)) return;
    fs::create_directories(to, ec);
    for(const auto& e : fs::directory_iterator(from, ec)) {
        // (the write-ahead files of a database that is open: the copy of the database itself is what counts)
        if(e.is_regular_file(ec)) fs::copy_file(e.path(), to / e.path().filename(), fs::copy_options::overwrite_existing, ec);
        else if(e.is_directory(ec)) copy_tree(e.path(), to / e.path().filename());
    }
}

} // namespace

namespace { std::string s_archive; }
void transfer_set_archive(const std::string& archive) { s_archive = archive; }
const std::string& transfer_archive() { return s_archive; }

std::vector<std::string> transfer_locations()
{
    std::vector<std::string> out;
    std::error_code ec;
    auto add = [&](const fs::path& p) {
        if(!fs::is_directory(p, ec)) return;
        const std::string s = u8(p);
        if(std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    };
    if(const char* env = std::getenv("HML_TRANSFER_DIR"); env && *env) add(fs::u8path(env));
#ifdef _WIN32
    const DWORD drives = GetLogicalDrives();
    for(int i = 0; i < 26; i++) {
        if(!(drives & (1u << i))) continue;
        const char root[] = { static_cast<char>('A' + i), ':', '\\', '\0' };
        if(GetDriveTypeA(root) == DRIVE_REMOVABLE) add(fs::path(root));
    }
#else
    // where desktops and udisks mount (/media/<user>/<label>, /run/media/<user>/<label>) and where
    // a hand-written fstab or a udev rule usually does (/media/<label>, /mnt/<label>)
    for(const char* base : { "/media", "/run/media", "/mnt" }) {
        for(const auto& e : fs::directory_iterator(base, ec)) {
            if(!e.is_directory(ec)) continue;
            bool has_sub = false;
            if(std::string(base) != "/mnt") {
                for(const auto& s : fs::directory_iterator(e.path(), ec)) if(s.is_directory(ec)) { add(s.path()); has_sub = true; }
            }
            if(!has_sub) add(e.path());
        }
    }
#endif
    return out;
}

namespace {
void lsblk_walk(const json& list, std::vector<UsbVolume>& out)
{
    if(!list.is_array()) return;
    auto truth = [](const json& o, const char* key) {
        const json* v = jget(o, key);
        if(!v) return false;
        if(v->is_boolean()) return v->get<bool>();
        if(v->is_number()) return v->get<double>() != 0;
        return v->is_string() && (v->get<std::string>() == "1" || v->get<std::string>() == "true");
    };
    auto text = [](const json& o, const char* key) {
        const json* v = jget(o, key);
        return v && v->is_string() ? v->get<std::string>() : std::string();
    };
    for(const json& d : list) {
        if(!d.is_object()) continue;
        const std::string fs_type = text(d, "fstype"), type = text(d, "type");
        if((truth(d, "rm") || truth(d, "hotplug")) && (type == "part" || type == "disk") && !fs_type.empty() && fs_type != "swap") {
            UsbVolume v;
            v.device = text(d, "path");
            v.fstype = fs_type;
            v.label = text(d, "label");
            v.mountpoint = text(d, "mountpoint");
            if(!v.device.empty()) out.push_back(std::move(v));
        }
        if(const json* c = jget(d, "children")) lsblk_walk(*c, out);
    }
}

#ifndef _WIN32
std::string run(const std::string& cmd, int* rc)
{
    std::string out;
    FILE* p = popen((cmd + " 2>&1").c_str(), "r");
    if(!p) { if(rc) *rc = -1; return out; }
    char buf[512];
    while(size_t n = fread(buf, 1, sizeof(buf), p)) out.append(buf, n);
    const int st = pclose(p);
    if(rc) *rc = st;
    return out;
}
std::vector<UsbVolume> usb_volumes()
{
    int rc = 0;
    const std::string text = run("lsblk -J -o PATH,RM,HOTPLUG,TYPE,FSTYPE,LABEL,MOUNTPOINT", &rc);
    return rc == 0 ? transfer_parse_lsblk(text) : std::vector<UsbVolume>();
}
std::set<std::string>& usb_refused() { static std::set<std::string> s; return s; }    // could not be mounted: said once
std::set<std::string>& usb_ejected() { static std::set<std::string> s; return s; }    // taken out by the operator
#endif
} // namespace

std::vector<UsbVolume> transfer_parse_lsblk(const std::string& json_text)
{
    std::vector<UsbVolume> out;
    const json j = json::parse(json_text, nullptr, false);
    if(j.is_object()) if(const json* list = jget(j, "blockdevices")) lsblk_walk(*list, out);
    return out;
}

std::string transfer_mount_usb()
{
#ifdef _WIN32
    return "";
#else
    std::string problem;
    const std::vector<UsbVolume> vols = usb_volumes();
    // what was unplugged is forgotten: plugged in again it is mounted (and complained about) anew
    auto forget = [&](std::set<std::string>& set) {
        for(auto it = set.begin(); it != set.end();) {
            const bool there = std::any_of(vols.begin(), vols.end(), [&](const UsbVolume& v) { return v.device == *it; });
            it = there ? std::next(it) : set.erase(it);
        }
    };
    forget(usb_refused());
    forget(usb_ejected());
    for(const UsbVolume& v : vols) {
        if(!v.mountpoint.empty() || usb_refused().count(v.device) || usb_ejected().count(v.device)) continue;
        if(v.device.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-") != std::string::npos) continue;
        int rc = 0;
        const std::string out = run("udisksctl mount -b " + v.device + " --no-user-interaction", &rc);
        if(rc == 0) { HML_INFO("storage: mounted %s (%s)", v.device.c_str(), v.label.empty() ? v.fstype.c_str() : v.label.c_str()); continue; }
        usb_refused().insert(v.device);
        const std::string name = v.label.empty() ? v.device : "'" + v.label + "'";
        if(out.find("NotAuthorized") != std::string::npos)
            problem = "The USB memory " + name + " is there, but the runtime may not mount it (install tools/rpi/hml-usb.rules, see the README).";
        else if(out.find("not found") != std::string::npos && out.find("udisksctl") != std::string::npos)
            problem = "The USB memory " + name + " is there but not mounted, and udisks2 is not installed to mount it.";
        else problem = "The USB memory " + name + " cannot be mounted: " + out.substr(0, 160);
        HML_WARN("storage: %s", problem.c_str());
    }
    return problem;
#endif
}

std::string transfer_eject(const std::string& location)
{
    if(location.empty()) return "there is no location";
#ifdef _WIN32
    return "take the drive out with \"Safely Remove Hardware\" of Windows";
#else
    sync();
    for(const UsbVolume& v : usb_volumes()) {
        if(v.mountpoint != location) continue;
        int rc = 0;
        const std::string out = run("udisksctl unmount -b " + v.device + " --no-user-interaction", &rc);
        if(rc != 0) return out.substr(0, 160);
        usb_ejected().insert(v.device);
        HML_INFO("storage: unmounted %s", v.device.c_str());
        return "";
    }
    return "this location is not a removable drive (everything is written out; nothing was unmounted)";
#endif
}

std::vector<std::string> transfer_project_files(const std::string& location)
{
    std::vector<std::string> out;
    if(location.empty()) return out;
    std::error_code ec;
    const fs::path root = fs::u8path(location);
    for(const fs::path& dir : { root, root / "hml" }) {
        for(const auto& e : fs::directory_iterator(dir, ec)) {
            if(e.is_regular_file(ec) && is_project_archive(u8(e.path())) && e.path().filename().u8string().rfind(".", 0) != 0) out.push_back(u8(e.path()));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string transfer_backup(const TransferTarget& t, const std::string& location, std::string* out)
{
    std::error_code ec;
    if(location.empty() || !fs::is_directory(fs::u8path(location), ec)) return "no place to write to";
    const std::string name = safe_name(t.name);
    const fs::path dir = fs::u8path(location) / "hml-backup" / (name + "-" + stamp());
    fs::create_directories(dir, ec);
    if(ec) return "cannot create " + u8(dir) + ": " + ec.message();
    std::string err;
    if(!pack_project(fs::u8path(t.dir), dir / (name + kProjectArchiveExt), &err)) return "the project could not be written: " + err;
    copy_tree(fs::u8path(t.dir) / "logs", dir / "logs");
    const fs::path local = fs::u8path(t.dir) / "project.local.json";
    if(fs::exists(local, ec)) fs::copy_file(local, dir / "project.local.json", fs::copy_options::overwrite_existing, ec);
    if(out) *out = u8(dir);
    return "";
}

std::string transfer_install(const TransferTarget& t, const std::string& project_file)
{
    std::error_code ec;
    const fs::path file = fs::u8path(project_file);
    if(!fs::is_regular_file(file, ec)) return "the file is not there any more";
    const fs::path dir = fs::u8path(t.dir);
    const fs::path tmp = dir.parent_path() / (dir.filename().u8string() + ".incoming");
    std::string why = examine(t, file, tmp);
    fs::remove_all(tmp, ec);
    if(!why.empty()) return why;

    std::string err;
    const fs::path prev = previous_path(t);
    if(!t.archive.empty()) {
        // started from a project file: that file is what the next start unpacks
        const fs::path archive = fs::u8path(t.archive);
        fs::copy_file(archive, prev, fs::copy_options::overwrite_existing, ec);
        if(ec) return "the present project could not be kept: " + ec.message();
        const fs::path part = fs::u8path(t.archive + ".part");
        fs::copy_file(file, part, fs::copy_options::overwrite_existing, ec);
        if(ec) return "the file could not be copied: " + ec.message();
        fs::rename(part, archive, ec);
        if(ec) { fs::remove(part, ec); return "the project file could not be replaced"; }
        return "";
    }
    if(!pack_project(dir, prev, &err)) return "the present project could not be kept: " + err;
    if(!unpack_project(file, dir, &err)) {
        // (half written: put the old one back)
        std::string again;
        unpack_project(prev, dir, &again);
        return "the project could not be written: " + err;
    }
    return "";
}

std::string transfer_previous(const TransferTarget& t)
{
    std::error_code ec;
    const fs::path prev = previous_path(t);
    return fs::is_regular_file(prev, ec) ? u8(prev) : "";
}

std::string transfer_rollback(const TransferTarget& t)
{
    std::error_code ec;
    const fs::path prev = previous_path(t);
    if(!fs::is_regular_file(prev, ec)) return "there is no previous project";
    std::string err;
    if(!t.archive.empty()) {
        const fs::path archive = fs::u8path(t.archive), swap = fs::u8path(t.archive + ".swap");
        fs::rename(archive, swap, ec);
        if(ec) return "the project file could not be replaced";
        fs::rename(prev, archive, ec);
        if(ec) { fs::rename(swap, archive, ec); return "the project file could not be replaced"; }
        fs::rename(swap, prev, ec);              // (so that the step can be undone again)
        return "";
    }
    const fs::path dir = fs::u8path(t.dir);
    const fs::path keep = dir.parent_path() / (dir.filename().u8string() + ".swap" + kProjectArchiveExt);
    if(!pack_project(dir, keep, &err)) return "the present project could not be kept: " + err;
    if(!unpack_project(prev, dir, &err)) { std::string again; unpack_project(keep, dir, &again); fs::remove(keep, ec); return "the previous project could not be written: " + err; }
    fs::rename(keep, prev, ec);
    return "";
}

} // namespace hml
