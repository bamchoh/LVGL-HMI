// Which project a deployed runtime runs.
//
// A target has this layout (what the designer's Deploy creates):
//
//   <dir>/hml_runtime
//   <dir>/projects/<name>/project.json ...
//   <dir>/active_project        one line: "projects/<name>/project.json"
//   <dir>/running_project       written by the runtime: the project it loaded
//
// The service that starts the runtime names one project on its command line, and changing
// that needs administrator rights on the target. active_project lets a deploy switch the
// project without them: when the file is there, the runtime loads the project it names
// instead of the one from the command line. It is only looked at in the layout above (a
// "projects" folder next to the executable), never in a build tree.
#pragma once
#include <string>

namespace hml {

// The folder of the running executable ("" if unknown).
std::string install_dir();

// The project to load: the one named by <dir>/active_project if that file is there and
// valid, else cmdline_project. *note says what was decided when it differs from the plain
// case (for the log; empty otherwise).
std::string resolve_active_project(const std::string& dir, const std::string& cmdline_project, std::string* note = nullptr);

// Record what was loaded in <dir>/running_project (relative to dir when inside it), so that a
// deploy can tell which project the target really runs. Does nothing outside the layout above.
void note_running_project(const std::string& dir, const std::string& project);

} // namespace hml
