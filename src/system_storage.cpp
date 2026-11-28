// System > Storage: a backup of the project and what was recorded to a USB memory, a new
// project from it, and back to the one before (transfer.hpp). A SettingsPage like the others.
#include <filesystem>
#include "operation_log.hpp"
#include "project.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "deploy_service.hpp"
#include "transfer.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

const char* kNone = "(none)";

std::string base_name(const std::string& path)
{
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

class StoragePage final : public SettingsPage {
public:
    explicit StoragePage(Project& proj) : proj_(proj) {}
    std::string id() const override { return "storage"; }
    std::string title() const override { return "Storage"; }
    bool has_apply() const override { return false; }

    TransferTarget target() const
    {
        const ProjectInfo& in = proj_.info();
        TransferTarget t;
        t.dir = in.dir;
        t.archive = transfer_archive();
        t.name = !t.archive.empty() ? std::filesystem::u8path(t.archive).stem().u8string() : base_name(in.dir);
        t.width = in.width;
        t.height = in.height;
        return t;
    }

    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        auto info = [&](const std::string& key, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Info; f.value = value; out.push_back(f);
        };
        auto flag = [&](const std::string& key, bool on) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Bool; f.value = on ? "1" : "0"; out.push_back(f);
        };
        auto select = [&](const std::string& key, const std::vector<std::string>& options, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Select; f.options = options; f.live_options = true;
            if(f.options.empty()) f.options.push_back(kNone);
            f.value = std::find(f.options.begin(), f.options.end(), value) != f.options.end() ? value : f.options[0];
            out.push_back(f);
        };
        const std::string problem = transfer_mount_usb();          // (a device without a desktop mounts nothing by itself)
        if(!problem.empty()) mount_problem_ = problem;
        locations_ = transfer_locations();
        if(!locations_.empty()) mount_problem_.clear();
        if(std::find(locations_.begin(), locations_.end(), location_) == locations_.end()) location_ = locations_.empty() ? "" : locations_[0];
        files_ = transfer_project_files(location_);
        std::vector<std::string> names;
        for(const std::string& f : files_) names.push_back(base_name(f));
        if(std::find(names.begin(), names.end(), file_) == names.end()) file_ = names.empty() ? "" : names[0];
        select("location", locations_, location_);
        select("file", names, file_);
        flag("has_location", !locations_.empty());
        flag("has_file", !names.empty());
        const std::string prev = transfer_previous(target());
        flag("has_previous", !prev.empty());
        flag("confirm", !confirm_.empty());
        info("confirm_text", confirm_ == "install" ? "Replace the project with '" + file_ + "'? The runtime restarts." + install_note_
                             : confirm_ == "rollback" ? "Go back to the project that was replaced last? The runtime restarts." : "");
        info("hint", locations_.empty() ? (mount_problem_.empty() ? "No USB memory is found. Plug one in." : mount_problem_) : "");
        flag("no_location", locations_.empty());
        info("project", target().name + vformat("  (%d x %d)", proj_.info().width, proj_.info().height));
        return out;
    }

    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    // the two dropdowns hold what the operator picked
    void take_form()
    {
        auto pick = [&](const char* key, const std::vector<std::string>& options, std::string& into) {
            Tag* t = tags().find(SystemSettings::tag_name(id(), key));
            if(!t) return;
            const int i = static_cast<int>(t->get_number());
            if(i >= 0 && static_cast<size_t>(i) < options.size()) into = options[static_cast<size_t>(i)];
        };
        pick("location", locations_, location_);
        std::vector<std::string> names;
        for(const std::string& f : files_) names.push_back(base_name(f));
        pick("file", names, file_);
    }

    std::string picked_file() const
    {
        for(const std::string& f : files_) if(base_name(f) == file_) return f;
        return "";
    }

    void command(const std::string& cmd, const std::string&) override
    {
        take_form();
        std::string msg;
        const TransferTarget t = target();
        if(cmd == "refresh") confirm_.clear();
        else if(cmd == "backup") {
            // Writing takes seconds on a USB memory and the screen stands still meanwhile: say so
            // first, and write when that has been drawn (poll()).
            if(backup_wait_ == 0) backup_wait_ = 2;
            msg = "Writing the backup ... Do not take the USB memory out.";
        }
        else if(cmd == "eject") {
            const std::string where = location_;
            msg = transfer_eject(where);
            if(msg.empty()) { msg = "You can take the USB memory out now."; operation_log().record("system", "eject", "", where); }
            else msg = "Not removed: " + msg;
        }
        else if(cmd == "install") {
            // (the location may have changed the list: look again before asking)
            files_ = transfer_project_files(location_);
            if(picked_file().empty()) msg = "There is no project file (.hmlproj) at this location.";
            else {
                // what can it do? Said in the question, so that it is seen before "Yes"
                confirm_ = "install";
                install_note_.clear();
                install_fp_.clear();
                ProjectCapabilities caps, now;
                if(transfer_capabilities(t, picked_file(), caps).empty()) {
                    install_fp_ = caps.fingerprint();
                    const bool same = project_capabilities_of(proj_.info().path, now) && now.fingerprint() == install_fp_;
                    install_note_ = "\nIt can: " + caps.brief() + (caps.unattended_writes() ? "\n! Values can reach a device without anybody at the screen." : "") +
                                    (same || caps.empty() ? "" : "\n(Other things than the project that runs now.)");
                }
            }
        }
        else if(cmd == "rollback") {
            if(transfer_previous(t).empty()) msg = "There is no previous project.";
            else confirm_ = "rollback";
        }
        else if(cmd == "no") confirm_.clear();
        else if(cmd == "yes") {
            const std::string what = confirm_;
            confirm_.clear();
            if(what == "install") {
                const std::string file = picked_file();
                msg = transfer_install(t, file);
                if(msg.empty()) {
                    operation_log().record("system", "install project", t.name, base_name(file));
                    if(!install_fp_.empty()) deploy_service().set_approved(install_fp_);     // (the operator has seen the list and said yes)
                    restart_ = true;
                    msg = "Installed. Restarting ...";
                }
                else msg = "Not installed: " + msg;
            }
            else if(what == "rollback") {
                msg = transfer_rollback(t);
                if(msg.empty()) { operation_log().record("system", "previous project", t.name, ""); restart_ = true; msg = "Restarting with the previous project ..."; }
                else msg = "Not changed: " + msg;
            }
        }
        else HML_WARN("system: storage: unknown command '%s'", cmd.c_str());
        if(!msg.empty()) HML_INFO("system: storage: %s", msg.c_str());
        if(Tag* st = tags().find(SystemSettings::tag_name(id(), "status"))) st->set_string(msg, nullptr);
    }

    // The restart is asked for a moment after the command, so that the message is seen. While
    // the page is shown, a USB memory that is plugged in or taken out is noticed.
    bool poll() override
    {
        if(backup_wait_ > 0 && --backup_wait_ == 0) {
            std::string where, msg = transfer_backup(target(), location_, &where);
            if(msg.empty()) { msg = "Backup written to " + where; operation_log().record("system", "backup", "", where); }
            else msg = "Backup failed: " + msg;
            HML_INFO("system: storage: %s", msg.c_str());
            if(Tag* st = tags().find(SystemSettings::tag_name(id(), "status"))) st->set_string(msg, nullptr);
            take_form();
            return true;
        }
        if(restart_ && ++restart_ticks_ >= 3) { restart_ = false; restart_ticks_ = 0; ui().request_restart(RestartTarget::App); return false; }
        auto cur = ui().current_screen();
        if(!cur || *cur != SystemSettings::page_screen(id())) return false;
        if(++ticks_ % 4) return false;
        if(const std::string pr = transfer_mount_usb(); !pr.empty()) { mount_problem_ = pr; take_form(); return true; }
        std::vector<std::string> loc = transfer_locations();
        if(loc == locations_ && transfer_project_files(location_) == files_) return false;
        take_form();
        return true;
    }

    json custom_rows(bool compact, int font) const override
    {
        const char* muted = "#9fb3c8";
        auto tag = [&](const std::string& key) { return SystemSettings::tag_name(id(), key); };
        auto call = [&](const std::string& cmd) { return json{ { "action", "call" }, { "fn", "system.do" }, { "arg", id() + ":" + cmd } }; };
        auto row = [&](json children) {
            return json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                         { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 6 } } }, { "children", std::move(children) } };
        };
        auto button = [&](const char* text, const char* color, const std::string& cmd, int w, const char* enabled = nullptr) {
            json b = json{ { "type", "button" }, { "text", text }, { "w", w }, { "h", compact ? 34 : 44 }, { "style", { { "bg_color", color } } },
                           { "style_label", { { "font", font } } }, { "on_click", call(cmd) } };
            if(enabled) b["bind"] = { { "enabled", tag(enabled) } };
            return b;
        };
        auto label = [&](const char* text, int w) {
            return json{ { "type", "label" }, { "text", text }, { "w", w }, { "style", { { "font", font }, { "text_color", muted } } } };
        };
        auto dropdown = [&](const char* key) {
            return json{ { "type", "dropdown" }, { "flex_grow", 1 }, { "bind", { { "value", tag(key) }, { "options", tag(std::string(key) + "__options") } } },
                         { "style", { { "font", font } } }, { "on_change", call("refresh") } };
        };
        const int lw = compact ? 70 : 100;

        // (what acts - backup, install ... - is in the bar at the bottom: actions())
        json rows = json::array();
        rows.push_back(row(json::array({ label("Project", lw),
            json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "dots" }, { "bind", { { "text", tag("project") } } }, { "style", { { "font", font + 2 }, { "text_color", "#ffffff" } } } } })));
        rows.push_back(row(json::array({ label("Location", lw), dropdown("location"), button(LV_SYMBOL_REFRESH, "#2c3e50", "refresh", compact ? 56 : 72)   /* (a sign: the word does not fit in every language) */ })));
        rows.push_back(json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", tag("hint") }, { "visible", tag("no_location") } } },
                             { "style", { { "font", font }, { "text_color", "#9fb3c8" } } } });   // (a hint, not a warning)
        rows.push_back(row(json::array({ label("Project file", lw), dropdown("file") })));
        return rows;
    }

    std::vector<PageAction> actions() const override
    {
        return {
            { "Backup", "backup", PageAction::Kind::Primary, "", "has_location" },
            { "Remove safely", "eject", PageAction::Kind::Neutral, "", "has_location" },
            { "Install", "install", PageAction::Kind::Danger, "", "has_file" },
            { "Previous", "rollback", PageAction::Kind::Neutral, "", "has_previous" },
        };
    }
    std::vector<PageDialog> dialogs() const override
    {
        return { { "confirm", "confirm_text", { "Yes", "yes", PageAction::Kind::Danger, "", "" }, { "Cancel", "no", PageAction::Kind::Neutral, "", "" }, false } };
    }

private:
    Project&    proj_;
    std::vector<std::string> locations_, files_;
    std::string location_, file_;       // what is picked (a location, the name of a file in it)
    std::string mount_problem_;         // why a USB memory that is plugged in does not show
    std::string confirm_;               // "install" / "rollback" while the question is shown
    bool        restart_ = false;
    int         restart_ticks_ = 0, ticks_ = 0;
    int         backup_wait_ = 0;       // > 0: a backup was asked for and is written when this reaches 0
    std::string install_note_, install_fp_;   // what the project file picked can do: for the question, and to remember
};

} // namespace

std::unique_ptr<SettingsPage> make_storage_page(Project& proj) { return std::make_unique<StoragePage>(proj); }

} // namespace hml
