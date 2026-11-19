// A project as one file: "<name>.hmlproj", a zip archive of the files a project is made of
// (project.json, the screens, script files, images ...). Easy to carry around, mail or put
// under version control as a single item.
//
// Everything that works on a project (the runtime, Run / Preview and Deploy in the designer)
// keeps working on a folder: a project file is unpacked into a working folder first, one per
// project file, in the user's cache folder. What a machine adds while a project runs there
// (recorded logs, project.local.json with that machine's settings, pki/ with its certificates) stays in the working
// folder and never gets into the project file.
//
// No dependency on the rest of the runtime: the designer host uses this file too.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace hml {

constexpr const char* kProjectArchiveExt = ".hmlproj";

struct ProjectFile {
    std::filesystem::path file;    // on disk
    std::string           rel;     // inside the project, '/' separated
};

// Is this the name of a project file (by its extension)?
bool is_project_archive(const std::string& path);

// The files a project is made of: project.json, every file it or one of its screens names
// (screens, scripts, images, the splash) and everything below assets/. Never the recorded logs
// or the machine's local settings, and nothing else that happens to lie in the folder.
std::vector<ProjectFile> project_files(const std::filesystem::path& dir);

// Write the project in `dir` as a project file. The file is replaced only when complete.
bool pack_project(const std::filesystem::path& dir, const std::filesystem::path& archive, std::string* error = nullptr);

// Unpack a project file into `dir` (created if needed). What was in `dir` before is removed,
// except logs/, pki/ and project.local.json. Entries that would land outside `dir` are refused.
bool unpack_project(const std::filesystem::path& archive, const std::filesystem::path& dir, std::string* error = nullptr);

// The working folder of a project file on this machine: always the same one for the same file.
std::filesystem::path archive_work_dir(const std::filesystem::path& archive);

// The project's name: the file name without the extension.
std::string archive_project_name(const std::filesystem::path& archive);

} // namespace hml
