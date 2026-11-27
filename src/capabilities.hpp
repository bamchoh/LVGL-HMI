// What a project can do beyond drawing screens - found by reading its files, nothing is started.
//
// A project is a program: it can write to a PLC, send data to other machines, open ports and
// run scripts on its own. Before a project that somebody else made is run, the person who runs
// it should see that list (`hml_runtime <project> --capabilities`, the designer, the device
// when a project arrives). The list is deliberately coarse: it says what the project is able
// to do, not what it will do.
//
//   devices    the PLCs and servers it writes values to (a driver with rows that are not read-only)
//   sends      where it sends data on its own: mail, uploads of the logs, an MQTT broker
//   listens    the ports it opens for others: OPC UA server, VNC
//   automatic  what runs without anybody pressing a button: scripts, schedules, transfers
//
// The fingerprint is a hash of the list: the same list, the same fingerprint. Whoever has
// agreed to one list is asked again only when the list changes - not for every edit of a screen.
//
// No dependency on LVGL or the rest of the runtime.
#pragma once
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

struct ProjectCapabilities {
    std::vector<std::string> devices, sends, listens, automatic;

    bool empty() const { return devices.empty() && sends.empty() && listens.empty() && automatic.empty(); }
    // Values can reach a device without anybody at the screen: it writes to one, and something runs by itself.
    bool unattended_writes() const { return !devices.empty() && !automatic.empty(); }
    json        to_json() const;
    std::string fingerprint() const;                 // 64 hex digits
    std::string text() const;                        // for a person: sections, one line per item
    std::string brief() const;                       // a few lines, for a question on a small screen
};

// root: project.json. dir: the folder of the project, to look into its screens for scripts
// that buttons run ("" = the screens are not looked at).
ProjectCapabilities project_capabilities(const json& root, const std::string& dir = "");
// From a project.json on disk. False (and *error) when it cannot be read.
bool project_capabilities_of(const std::string& project_json, ProjectCapabilities& out, std::string* error = nullptr);

} // namespace hml
