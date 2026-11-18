#include "project_archive.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

#include <nlohmann/json.hpp>
#include "miniz.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace hml {

namespace {

std::string ustr(const fs::path& p) { return p.generic_u8string(); }

bool read_file(const fs::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if(!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool fail(std::string* error, const std::string& msg)
{
    if(error) *error = msg;
    return false;
}

// A path inside a project: relative, no "..", no drive letter. Returns it normalised ('/').
bool safe_rel(const std::string& in, std::string& out)
{
    std::string s = in;
    std::replace(s.begin(), s.end(), '\\', '/');
    fs::path p = fs::u8path(s).lexically_normal();
    out = ustr(p);
    if(out.empty() || out == "." || p.is_absolute() || out[0] == '/' || out.find(':') != std::string::npos) return false;
    for(const auto& part : p) if(part == "..") return false;
    return true;
}

} // namespace

bool is_project_archive(const std::string& path)
{
    const size_t n = std::strlen(kProjectArchiveExt);
    if(path.size() <= n) return false;
    std::string ext = path.substr(path.size() - n);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == kProjectArchiveExt;
}

std::vector<ProjectFile> project_files(const fs::path& dir)
{
    std::vector<ProjectFile> out;
    std::error_code ec;
    auto add = [&](const std::string& rel_in) {
        std::string rel;
        if(!safe_rel(rel_in, rel)) return false;
        if(rel == "project.local.json" || rel.rfind("logs/", 0) == 0) return false;
        fs::path file = dir / fs::u8path(rel);
        if(!fs::is_regular_file(file, ec)) return false;
        for(const auto& o : out) if(o.rel == rel) return true;
        out.push_back({ file, rel });
        return true;
    };
    // every string in a JSON document that is the name of a file in the project folder
    std::function<void(const json&)> walk = [&](const json& j) {
        if(j.is_string()) {
            const std::string s = j.get<std::string>();
            if(s.size() < 260 && (s.find('.') != std::string::npos || s.find('/') != std::string::npos)) add(s);
        }
        else if(j.is_array() || j.is_object()) for(const auto& v : j) walk(v);
    };
    auto parse = [&](const fs::path& file) {
        std::string text;
        return read_file(file, text) ? json::parse(text, nullptr, false, true) : json(json::value_t::discarded);
    };
    add("project.json");
    json proj = parse(dir / "project.json");
    if(!proj.is_discarded()) {
        walk(proj);
        if(proj.is_object() && proj.contains("screens") && proj["screens"].is_array()) {
            for(const auto& s : proj["screens"]) {
                if(!s.is_string()) continue;
                json scr = parse(dir / fs::u8path(s.get<std::string>()));
                if(!scr.is_discarded()) walk(scr);
            }
        }
    }
    for(auto it = fs::recursive_directory_iterator(dir / "assets", ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if(it->is_regular_file(ec)) add(ustr(fs::relative(it->path(), dir, ec)));
    }
    std::sort(out.begin(), out.end(), [](const ProjectFile& a, const ProjectFile& b) { return a.rel < b.rel; });
    return out;
}

bool pack_project(const fs::path& dir, const fs::path& archive, std::string* error)
{
    std::vector<ProjectFile> files = project_files(dir);
    if(files.empty() || files[0].rel.empty() || !fs::exists(dir / "project.json")) return fail(error, "no project.json in " + ustr(dir));

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if(!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) return fail(error, "cannot start the project file");
    bool ok = true;
    std::string bad;
    for(const ProjectFile& f : files) {
        std::string data;
        if(!read_file(f.file, data)) { ok = false; bad = "cannot read " + ustr(f.file); break; }
        if(!mz_zip_writer_add_mem(&zip, f.rel.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL)) { ok = false; bad = "cannot add " + f.rel; break; }
    }
    void* buf = nullptr;
    size_t size = 0;
    if(ok && !mz_zip_writer_finalize_heap_archive(&zip, &buf, &size)) { ok = false; bad = "cannot finish the project file"; }
    mz_zip_writer_end(&zip);
    if(!ok) { if(buf) mz_free(buf); return fail(error, bad); }

    // (written through the standard library, not by miniz: paths with non-ASCII characters work on Windows)
    std::error_code ec;
    if(archive.has_parent_path()) fs::create_directories(archive.parent_path(), ec);
    fs::path tmp = archive;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if(f) f.write(static_cast<const char*>(buf), static_cast<std::streamsize>(size));
        f.flush();
        if(!f) { mz_free(buf); fs::remove(tmp, ec); return fail(error, "cannot write " + ustr(archive)); }
    }
    mz_free(buf);
    fs::rename(tmp, archive, ec);
    if(ec) { fs::remove(tmp, ec); return fail(error, "cannot replace " + ustr(archive)); }
    return true;
}

bool unpack_project(const fs::path& archive, const fs::path& dir, std::string* error)
{
    std::string data;
    if(!read_file(archive, data)) return fail(error, "cannot read " + ustr(archive));
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if(!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0)) return fail(error, ustr(archive.filename()) + " is not a project file (not a zip archive)");

    // look at everything first: nothing is touched on disk for a file that is not a project
    struct Entry { mz_uint index; std::string rel; };
    std::vector<Entry> entries;
    bool has_project = false;
    std::string bad;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for(mz_uint i = 0; i < n && bad.empty(); i++) {
        mz_zip_archive_file_stat st;
        if(!mz_zip_reader_file_stat(&zip, i, &st)) { bad = "damaged project file"; break; }
        if(mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        std::string rel;
        if(!safe_rel(st.m_filename, rel)) { bad = std::string("the project file contains a path that leads outside the project: ") + st.m_filename; break; }
        if(st.m_uncomp_size > 512ull * 1024 * 1024) { bad = "the project file contains a file that is too large: " + rel; break; }
        if(rel == "project.local.json" || rel.rfind("logs/", 0) == 0) continue;      // a machine's own files: never taken from a project file
        if(rel == "project.json") has_project = true;
        entries.push_back({ i, rel });
    }
    if(bad.empty() && !has_project) bad = ustr(archive.filename()) + " is not a project file (no project.json in it)";
    if(!bad.empty()) { mz_zip_reader_end(&zip); return fail(error, bad); }

    std::error_code ec;
    fs::create_directories(dir, ec);
    for(const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = ustr(e.path().filename());
        if(name == "logs" || name == "project.local.json") continue;                 // what this machine added stays
        std::error_code rec;
        fs::remove_all(e.path(), rec);
    }
    for(const Entry& e : entries) {
        size_t size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, e.index, &size, 0);
        if(!p && size != 0) { bad = "cannot unpack " + e.rel; break; }
        fs::path out = dir / fs::u8path(e.rel);
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if(f && size) f.write(static_cast<const char*>(p), static_cast<std::streamsize>(size));
        f.flush();
        const bool written = static_cast<bool>(f);
        if(p) mz_free(p);
        if(!written) { bad = "cannot write " + ustr(out); break; }
    }
    mz_zip_reader_end(&zip);
    return bad.empty() ? true : fail(error, bad);
}

fs::path archive_work_dir(const fs::path& archive)
{
    std::error_code ec;
    fs::path abs = fs::absolute(archive, ec);
    std::string key = ustr(ec ? archive : abs.lexically_normal());
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });   // one file, one folder
    const char* base = std::getenv("LOCALAPPDATA");
    fs::path root = base && *base ? fs::u8path(base) / "hml" : fs::temp_directory_path(ec) / "hml";
#else
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    fs::path root = xdg && *xdg ? fs::path(xdg) / "hml" : home && *home ? fs::path(home) / ".cache" / "hml" : fs::temp_directory_path(ec) / "hml";
#endif
    uint64_t h = 1469598103934665603ull;                        // FNV-1a
    for(unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
    char hex[20];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    return root / "work" / (archive_project_name(archive) + "-" + std::string(hex).substr(0, 10));
}

std::string archive_project_name(const fs::path& archive) { return ustr(archive.stem()); }

} // namespace hml
