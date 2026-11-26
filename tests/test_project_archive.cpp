// A project as one file (<name>.hmlproj): what a project is made of, packing, unpacking.
#include <doctest.h>
#include "project_archive.hpp"
#include "miniz.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace hml;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        dir = fs::temp_directory_path() / ("hml_archive_test_" + std::to_string(counter++) + "_" +
                                           std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFF)));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
    void write(const std::string& rel, const std::string& text) const
    {
        fs::path p = dir / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        f << text;
    }
    std::string read(const std::string& rel) const
    {
        std::ifstream f(dir / rel, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    bool has(const std::string& rel) const { return fs::exists(dir / rel); }
};

std::vector<std::string> names(const std::vector<ProjectFile>& files)
{
    std::vector<std::string> out;
    for(const auto& f : files) out.push_back(f.rel);
    return out;
}

// a project in a folder that also holds things that are none of its business
void make_project(const TempDir& t)
{
    t.write("project.json", R"json({
        // comments are allowed in project files
        "title": "T", "display": { "width": 480, "height": 320, "splash": "branding/splash.png" },
        "tags": [ { "name": "x" } ],
        "scripts": [ { "name": "s", "trigger": "manual", "file": "scripts/calc.lua" } ],
        "screens": [ "screens/main.json", "screens/second.json" ] })json");
    t.write("screens/main.json", R"json({ "name": "main", "children": [
        { "type": "image", "src": "pictures/pump.png" }, { "type": "label", "text": "version 1.2" },
        { "type": "image", "src": "../outside.png" }, { "type": "image", "src": "missing.png" } ] })json");
    t.write("screens/second.json", R"json({ "name": "second", "children": [] })json");
    t.write("screens/unused.json", R"json({ "name": "unused" })json");          // not listed in project.json
    t.write("pictures/pump.png", std::string("\x89PNG\0binary\xff", 12));
    t.write("branding/splash.png", "splash");
    t.write("scripts/calc.lua", "tag.x = 1\n");
    t.write("assets/logo.png", "logo");                                           // named nowhere: assets/ goes along as a whole
    t.write("assets/sub/icon.png", "icon");
    t.write("logs/data.db", "recorded on this machine");
    t.write("project.local.json", R"({ "drivers": {} })");
    t.write("holiday.jpg", "not part of the project");
    t.write("other/stuff.txt", "neither");
}

} // namespace

TEST_CASE("project archive: what counts as a path inside a project")
{
    std::string n;
    CHECK(hml::project_rel_path("screens/main.json", &n));
    CHECK(n == "screens/main.json");
    CHECK(hml::project_rel_path("screens\\sub\\..\\main.json", &n));
    CHECK(n == "screens/main.json");
    CHECK(hml::project_rel_path("./a.json", &n));
    CHECK(n == "a.json");
    for(const char* bad : { "", ".", "..", "../x.json", "screens/../../x.json", "..\\..\\Users\\x\\notes.json", "/etc/passwd", "\\\\server\\share\\x.json",
                            "\\Windows\\x.json", "C:/Windows/x.json", "C:\\Windows\\x.json", "c:x.json", "screens/a.json:stream" })
        CHECK_MESSAGE(!hml::project_rel_path(bad), bad);
}

TEST_CASE("project file: the name")
{
    CHECK(is_project_archive("D:/work/line1.hmlproj"));
    CHECK(is_project_archive("LINE1.HMLPROJ"));
    CHECK_FALSE(is_project_archive("D:/work/project.json"));
    CHECK_FALSE(is_project_archive(".hmlproj"));
    CHECK_FALSE(is_project_archive(""));
    CHECK(archive_project_name(fs::u8path("D:/work/line1.hmlproj")) == "line1");
}

TEST_CASE("project file: a project is the files it names plus assets/, nothing else in its folder")
{
    TempDir t;
    make_project(t);
    CHECK(names(project_files(t.dir)) == std::vector<std::string>{
        "assets/logo.png", "assets/sub/icon.png", "branding/splash.png", "pictures/pump.png",
        "project.json", "screens/main.json", "screens/second.json", "scripts/calc.lua" });

    TempDir empty;
    CHECK(project_files(empty.dir).empty());
}

TEST_CASE("project file: pack and unpack give the same project back")
{
    TempDir src, out, where;
    make_project(src);
    const fs::path file = where.dir / "sub folder" / "line1.hmlproj";
    std::string err;
    REQUIRE(pack_project(src.dir, file, &err));
    CHECK(fs::file_size(file) > 100);
    {
        std::ifstream f(file, std::ios::binary);
        char magic[2] = {};
        f.read(magic, 2);
        CHECK(std::string(magic, 2) == "PK");                    // an ordinary zip archive
    }
    CHECK_FALSE(fs::exists(fs::path(file.string() + ".tmp")));

    REQUIRE(unpack_project(file, out.dir, &err));
    CHECK(names(project_files(out.dir)) == names(project_files(src.dir)));
    for(const std::string& rel : names(project_files(src.dir))) {
        CAPTURE(rel);
        CHECK(out.read(rel) == src.read(rel));                   // byte for byte, binary files too
    }
    CHECK_FALSE(out.has("holiday.jpg"));
    CHECK_FALSE(out.has("screens/unused.json"));
    CHECK_FALSE(out.has("logs/data.db"));                        // a machine's files are not carried around
    CHECK_FALSE(out.has("project.local.json"));

    // ---- unpacking again: the project is replaced, what this machine added stays
    out.write("logs/data.db", "recorded here");
    out.write("pki/own/cert.der", "this machine's certificate");
    out.write("project.local.json", R"({ "drivers": { "plc": { "device": "COM4" } } })");
    out.write("screens/removed_meanwhile.json", "{}");
    out.write("stale.txt", "x");
    src.write("screens/second.json", R"json({ "name": "second", "children": [ { "type": "label", "text": "new" } ] })json");
    REQUIRE(pack_project(src.dir, file, &err));
    REQUIRE(unpack_project(file, out.dir, &err));
    CHECK(out.read("screens/second.json").find("new") != std::string::npos);
    CHECK(out.read("logs/data.db") == "recorded here");
    CHECK(out.read("pki/own/cert.der") == "this machine's certificate");   // (a deploy keeps the device's certificates)
    CHECK(out.read("project.local.json").find("COM4") != std::string::npos);
    CHECK_FALSE(out.has("screens/removed_meanwhile.json"));
    CHECK_FALSE(out.has("stale.txt"));
}

TEST_CASE("project file: what is not a project is refused, and nothing is written for it")
{
    TempDir t, out;
    std::string err;
    out.write("project.json", "{ \"title\": \"kept\" }");

    CHECK_FALSE(pack_project(t.dir, t.dir / "empty.hmlproj", &err));          // no project.json to pack
    CHECK(err.find("no project.json") != std::string::npos);

    CHECK_FALSE(unpack_project(t.dir / "missing.hmlproj", out.dir, &err));
    t.write("text.hmlproj", "this is not a zip archive");
    CHECK_FALSE(unpack_project(t.dir / "text.hmlproj", out.dir, &err));
    CHECK(err.find("not a project file") != std::string::npos);

    auto make_zip = [&](const char* name, std::vector<std::pair<std::string, std::string>> entries) {
        mz_zip_archive zip;
        std::memset(&zip, 0, sizeof(zip));
        REQUIRE(mz_zip_writer_init_heap(&zip, 0, 1024));
        // The zip library refuses to write the names a hostile archive would use. Entries are
        // therefore added with a '~' in place of the first character of such a name, and the
        // name is put right in the finished archive (it is stored as plain text, twice).
        std::vector<std::pair<std::string, std::string>> renames;
        for(const auto& e : entries) {
            std::string stored = e.first;
            const bool hostile = stored[0] == '/' || stored[0] == '.' || stored.find(':') != std::string::npos;
            if(hostile) {
                stored[0] = '~';
                for(char& c : stored) if(c == ':') c = '~';
                renames.emplace_back(stored, e.first);
            }
            REQUIRE(mz_zip_writer_add_mem(&zip, stored.c_str(), e.second.data(), e.second.size(), MZ_DEFAULT_LEVEL));
        }
        void* buf = nullptr;
        size_t size = 0;
        REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &buf, &size));
        mz_zip_writer_end(&zip);
        std::string bytes(static_cast<const char*>(buf), size);
        mz_free(buf);
        for(const auto& r : renames) {
            int found = 0;
            for(size_t at = bytes.find(r.first); at != std::string::npos; at = bytes.find(r.first, at + 1)) { bytes.replace(at, r.first.size(), r.second); found++; }
            REQUIRE(found == 2);                                  // local header and central directory
        }
        std::ofstream f(t.dir / name, std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    make_zip("other.hmlproj", { { "readme.txt", "a zip, but not a project" } });
    CHECK_FALSE(unpack_project(t.dir / "other.hmlproj", out.dir, &err));
    CHECK(err.find("no project.json") != std::string::npos);

    // an entry that would be written outside the folder: the whole file is refused
    make_zip("evil.hmlproj", { { "project.json", "{}" }, { "../escaped.txt", "x" } });
    CHECK_FALSE(unpack_project(t.dir / "evil.hmlproj", out.dir / "inner", &err));
    CHECK(err.find("outside the project") != std::string::npos);
    CHECK_FALSE(fs::exists(out.dir / "escaped.txt"));
    CHECK_FALSE(fs::exists(out.dir / "inner" / "project.json"));
    make_zip("abs.hmlproj", { { "project.json", "{}" }, { "/etc/hml-test", "x" } });
    CHECK_FALSE(unpack_project(t.dir / "abs.hmlproj", out.dir / "inner", &err));
    make_zip("drive.hmlproj", { { "project.json", "{}" }, { "C:/hml-test.txt", "x" } });
    CHECK_FALSE(unpack_project(t.dir / "drive.hmlproj", out.dir / "inner", &err));

    CHECK(out.read("project.json").find("kept") != std::string::npos);        // none of the refused files touched it

    // a machine's own files inside a project file (made by hand) are not taken over
    make_zip("local.hmlproj", { { "project.json", "{ \"title\": \"new\" }" }, { "project.local.json", "{ \"x\": 1 }" }, { "logs/data.db", "x" }, { "pki/trusted/evil.der", "x" } });
    REQUIRE(unpack_project(t.dir / "local.hmlproj", out.dir, &err));
    CHECK(out.read("project.json").find("new") != std::string::npos);
    CHECK_FALSE(out.has("project.local.json"));
    CHECK_FALSE(out.has("logs/data.db"));
    CHECK_FALSE(out.has("pki/trusted/evil.der"));                // a project file cannot plant a trusted certificate
}

TEST_CASE("project file: one working folder per file")
{
    const fs::path a = archive_work_dir(fs::u8path("D:/work/line1.hmlproj"));
    CHECK(a == archive_work_dir(fs::u8path("D:/work/line1.hmlproj")));
    CHECK(a != archive_work_dir(fs::u8path("D:/other/line1.hmlproj")));       // the same name elsewhere is another project
    CHECK(a != archive_work_dir(fs::u8path("D:/work/line2.hmlproj")));
    CHECK(a.filename().generic_u8string().rfind("line1-", 0) == 0);           // recognisable in the cache folder
    CHECK(a.parent_path().filename() == "work");
}
