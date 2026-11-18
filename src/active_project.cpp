#include "active_project.hpp"

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace hml {

std::string install_dir()
{
    std::error_code ec;
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if(n == 0) return {};
    return fs::path(std::wstring(buf, n)).parent_path().generic_u8string();
#else
    // (after the file was replaced by a deploy the link reads ".../hml_runtime (deleted)":
    // the folder is still the right one)
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : p.parent_path().generic_string();
#endif
}

namespace {

bool deployed_layout(const fs::path& dir)
{
    std::error_code ec;
    return !dir.empty() && fs::is_directory(dir / "projects", ec);
}

std::string first_line(const fs::path& file)
{
    std::ifstream f(file, std::ios::binary);
    std::string line;
    std::getline(f, line);
    while(!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    size_t start = line.find_first_not_of(" \t");
    return start == std::string::npos ? std::string() : line.substr(start);
}

} // namespace

std::string resolve_active_project(const std::string& dir_s, const std::string& cmdline_project, std::string* note)
{
    if(note) note->clear();
    const fs::path dir = fs::u8path(dir_s);
    if(!deployed_layout(dir)) return cmdline_project;
    std::error_code ec;
    const fs::path file = dir / "active_project";
    if(!fs::is_regular_file(file, ec)) return cmdline_project;
    const std::string rel = first_line(file);
    if(rel.empty()) return cmdline_project;
    // only a project below this folder: the file is written by whoever may deploy here, and
    // must not make the runtime read somewhere else
    const fs::path relp = fs::u8path(rel);
    bool inside = relp.is_relative() && rel.find(':') == std::string::npos;
    for(const auto& part : relp) if(part == "..") inside = false;
    const fs::path target = dir / relp;
    if(!inside || !fs::is_regular_file(target, ec)) {
        if(note) *note = "active_project names '" + rel + "', which is not a project file below " + dir_s + ": ignored";
        return cmdline_project;
    }
    const std::string chosen = target.generic_u8string();
    if(!fs::equivalent(target, fs::u8path(cmdline_project), ec)) {
        if(note) *note = "active_project: running '" + rel + "' instead of '" + cmdline_project + "' from the command line";
    }
    return chosen;
}

void note_running_project(const std::string& dir_s, const std::string& project)
{
    const fs::path dir = fs::u8path(dir_s);
    if(!deployed_layout(dir)) return;
    std::error_code ec;
    fs::path abs = fs::absolute(fs::u8path(project), ec);
    fs::path rel = fs::relative(abs, dir, ec);
    std::string text = (!ec && !rel.empty() && *rel.begin() != "..") ? rel.generic_u8string() : abs.generic_u8string();
    std::ofstream f(dir / "running_project", std::ios::binary | std::ios::trunc);
    if(f) f << text << "\n";
}

} // namespace hml
