// Bringing a project to a device, and taking one away, on a USB memory (or any folder): for
// places without a network. What the page System > Storage (system_storage.cpp) does.
//
//   <location>/*.hmlproj, <location>/hml/*.hmlproj     project files that can be installed
//   <location>/hml-backup/<name>-<YYYYMMDD-HHMMSS>/    a backup: <name>.hmlproj, logs/, project.local.json
//
// Installing never touches what the device itself added (logs/, pki/, project.local.json), and
// keeps the project that was replaced as "<project folder>.previous.hmlproj" to go back to.
// Private keys (pki/) are never written to a backup.
//
// No dependency on LVGL except through check.hpp (the project is checked before it is installed).
#pragma once
#include <string>
#include <vector>
#include "capabilities.hpp"

namespace hml {

// Where to look: the folder named by the environment variable HML_TRANSFER_DIR (tests, a PC),
// the removable drives (Windows), what is mounted below /media, /run/media and /mnt (Linux).
std::vector<std::string> transfer_locations();

// A file system on a removable drive, as "lsblk" tells (Linux).
struct UsbVolume {
    std::string device;       // /dev/sda1
    std::string fstype;       // vfat, exfat, ext4 ...
    std::string label;
    std::string mountpoint;   // "" = not mounted
};
// The removable file systems in the output of "lsblk -J -o PATH,RM,HOTPLUG,TYPE,FSTYPE,LABEL,MOUNTPOINT".
std::vector<UsbVolume> transfer_parse_lsblk(const std::string& json_text);
// Why a volume could not be mounted, for the operator: from what "udisksctl mount" printed.
std::string transfer_mount_problem(const UsbVolume& v, const std::string& output);
// A device without a desktop mounts nothing by itself: mount the removable file systems that
// are not mounted yet (udisks, as the user the runtime runs as). Returns "" or what stands in
// the way (told once per device). Volumes taken out with transfer_eject stay out until they
// are unplugged. Nothing happens on Windows.
std::string transfer_mount_usb();
// Write everything out and unmount what is mounted at this location, so that it can be pulled.
// Returns "" or what went wrong.
std::string transfer_eject(const std::string& location);

// The project files of a location (full paths, sorted by name).
std::vector<std::string> transfer_project_files(const std::string& location);

// The project file the runtime was started with ("" = a folder). Set once by main().
void transfer_set_archive(const std::string& archive);
const std::string& transfer_archive();

// The project the runtime runs.
struct TransferTarget {
    std::string dir;          // its folder (project.json is in it)
    std::string name;         // for file names
    std::string archive;      // the project file it was started from ("" = a folder project)
    int width = 0, height = 0;   // of its display: a project for another size is refused
};

// Write a backup below <location>/hml-backup. *out: the folder written. Returns "" or what went wrong.
std::string transfer_backup(const TransferTarget& t, const std::string& location, std::string* out = nullptr);

// Check a project file and put it in the place of the running project. The runtime has to be
// restarted afterwards. Returns "" or why it was not installed (nothing is changed then).
std::string transfer_install(const TransferTarget& t, const std::string& project_file);

// What the project in a project file can do (capabilities.hpp): the file is unpacked beside the
// project, read, and removed again. Returns "" or why it could not be looked at.
std::string transfer_capabilities(const TransferTarget& t, const std::string& project_file, ProjectCapabilities& out);

// The project that the last install replaced ("" = none), and going back to it.
std::string transfer_previous(const TransferTarget& t);
std::string transfer_rollback(const TransferTarget& t);

} // namespace hml
