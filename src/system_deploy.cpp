// System settings page "Deploy": the user interface of DeployService (deploy_service.hpp).
//
//   Receive projects  [switch]     while on, an accepted designer can send a project (no SSH needed)
//   Add a designer    [switch]     for a few minutes a new designer may ask to be accepted
//   Ask before a project that can do more  [switch]   a project with another list of what it can do
//                                  (capabilities.hpp) waits for [Install] / [Reject] here
//   (a designer asks)              sender, name, fingerprint         [Accept] [Reject]
//   Accepted designers             one row each: press it to choose it. Then, in the bar at the bottom:
//                                  [Runtime] -> confirmation   [Remove] -> confirmation
//                                  (Runtime: this designer may replace the runtime of the device, too)
// The questions (a new designer, Remove?, Allow the runtime?) are shown over the top of the page.
//
// A project that arrives is checked and installed here (on the UI thread), like one from a USB
// memory, and the runtime restarts.
#include <filesystem>
#include "deploy_service.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "transfer.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {

namespace {

constexpr int kSlots = 6;                 // designers shown on the page
constexpr uint32_t kAddSeconds = 300;     // "Add a designer" switches itself off

class DeployPage final : public SettingsPage {
public:
    explicit DeployPage(Project& proj) : proj_(proj) {}

    std::string id() const override { return "deploy"; }
    std::string title() const override { return "Deploy"; }
    std::string summary() override
    {
        DeployService& ds = deploy_service();
        if(!ds.listening()) return "Off";
        return vformat("Receiving, %d designer(s)", static_cast<int>(ds.designers().size()));
    }
    bool has_apply() const override { return false; }

    std::vector<SettingField> fields() override
    {
        DeployService& ds = deploy_service();
        list_ = ds.designers();
        std::vector<SettingField> out;
        auto info = [&](const std::string& key, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Info; f.value = value; out.push_back(f);
        };
        auto flag = [&](const std::string& key, bool on) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Bool; f.value = on ? "1" : "0"; out.push_back(f);
        };

        flag("receive", ds.listening());
        if(!ds.available()) info("state", "Not available: the runtime has no place for its keys (start it with --data-dir <folder>).");
        else if(ds.listening())
            info("state", vformat("On, port %d at %s\nThis device: %s", ds.port(), local_ip_addresses().c_str(), fingerprint_for_reading(ds.fingerprint()).c_str()));
        else info("state", "Off: the port is closed, no project can be sent to this device.");

        flag("ask", ds.ask());
        // a project that arrived and can do more than the one before: the operator decides
        flag("arrived", !held_.file.empty());
        info("arrived_text", held_.file.empty() ? "" : "The project '" + held_.name + "' arrived from " + held_.designer.substr(0, held_.designer.find(' ')) + " (" + held_.from +
                                                       "). It can do other things than the project that runs now:\n" + held_caps_.brief() +
                                                       (held_caps_.unattended_writes() ? "\n! Values can reach a device without anybody at the screen." : "") +
                                                       "\nInstall it only if you expect this.");
        flag("adding", ds.adding());
        if(ds.adding()) {
            const uint32_t left = remaining();
            info("adding_text", vformat("On for %u:%02u more: deploy from the new designer now.", left / 60, left % 60));
        }
        else info("adding_text", ds.listening() ? "Off: only the designers below can send." : "");

        DeployService::Offer offer;
        const bool pending = ds.pending(offer);
        flag("pending", pending);
        info("pending_text", pending ? "A new designer asks to send projects to this device:\n" + offer.name + " (" + offer.from + ")\n" + fingerprint_for_reading(offer.fingerprint) +
                                       "\nAccept it only if the designer shows the same fingerprint."
                                     : "");

        const DeployDesigner* del = nullptr;
        for(const DeployDesigner& d : list_) if(d.fingerprint == confirm_) del = &d;
        if(!del) confirm_.clear();
        flag("confirm", del != nullptr);
        info("confirm_text", del ? "Remove this designer? It cannot send projects any more.\n" + describe(*del) : "");
        const DeployDesigner* rt = nullptr;
        for(const DeployDesigner& d : list_) if(d.fingerprint == confirm_rt_) rt = &d;
        if(!rt) confirm_rt_.clear();
        flag("confirm_rt", rt != nullptr);
        info("confirm_rt_text", rt ? "Allow this designer to replace the runtime (the program) of this device?\nWhoever uses that PC can then run anything here. Only for a PC you trust as much as this device.\n" + describe(*rt) : "");

        info("count", list_.empty() ? "No designer is accepted." : vformat("%d accepted designer(s)", static_cast<int>(list_.size())));
        bool chosen = false;
        for(int i = 0; i < kSlots; i++) {
            const bool used = static_cast<size_t>(i) < list_.size();
            const bool sel = used && list_[static_cast<size_t>(i)].fingerprint == selected_;
            chosen = chosen || sel;
            flag("d" + std::to_string(i + 1) + "_used", used);
            flag("d" + std::to_string(i + 1) + "_sel", sel);
            info("d" + std::to_string(i + 1), used ? describe(list_[static_cast<size_t>(i)]) : "");
        }
        if(!chosen) selected_.clear();
        flag("has_sel", chosen);                                      // the bar at the bottom: Runtime / Remove
        info("more", list_.size() > kSlots ? vformat("... and %d more (not shown)", static_cast<int>(list_.size()) - kSlots) : "");
        return out;
    }

    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    void command(const std::string& cmd, const std::string& arg) override
    {
        DeployService& ds = deploy_service();
        std::string err;
        if(cmd == "receive") err = ds.set_enabled(!ds.listening());   // the switch was moved: the other state
        else if(cmd == "adding") {
            if(ds.adding()) ds.set_adding(false);
            else if(!ds.listening()) err = "Switch on \"Receive projects\" first.";
            else { ds.set_adding(true); deadline_ms_ = millis() + kAddSeconds * 1000; }
        }
        else if(cmd == "accept") { err = ds.accept(); ds.set_adding(false); }
        else if(cmd == "reject") ds.reject();
        else if(cmd == "select") {                                    // a row was pressed: chosen, or not any more
            const int slot = std::atoi(arg.c_str());
            if(slot >= 1 && static_cast<size_t>(slot) <= list_.size()) {
                const std::string& fp = list_[static_cast<size_t>(slot - 1)].fingerprint;
                selected_ = selected_ == fp ? std::string() : fp;
            }
        }
        else if(cmd == "delete") {                                    // ask first (the one chosen, or the row given)
            const std::string fp = slot_or_selected(arg);
            if(!fp.empty()) { confirm_ = fp; confirm_rt_.clear(); }
        }
        else if(cmd == "delete_yes") { if(!confirm_.empty()) err = ds.remove(confirm_); confirm_.clear(); }
        else if(cmd == "delete_no") confirm_.clear();
        else if(cmd == "runtime") {                                   // taking it away at once, giving it asks first
            const std::string fp = slot_or_selected(arg);
            for(const DeployDesigner& d : list_) {
                if(d.fingerprint != fp) continue;
                if(d.runtime) err = ds.set_runtime_allowed(d.fingerprint, false);
                else { confirm_rt_ = d.fingerprint; confirm_.clear(); }
            }
        }
        else if(cmd == "runtime_yes") { if(!confirm_rt_.empty()) err = ds.set_runtime_allowed(confirm_rt_, true); confirm_rt_.clear(); }
        else if(cmd == "runtime_no") confirm_rt_.clear();
        else if(cmd == "ask") err = ds.set_ask(!ds.ask());
        else if(cmd == "install_yes") {                               // the operator has seen what it can do
            if(!held_.file.empty()) { install(held_, held_caps_.fingerprint()); held_ = DeployService::Received{}; ds.set_asking(false); }
            return;                                                   // (install() has set the status)
        }
        else if(cmd == "install_no") {
            if(!held_.file.empty()) {
                HML_INFO("deploy: the operator did not allow the project '%s' from %s", held_.name.c_str(), held_.from.c_str());
                operation_log().record("deploy", "project refused", held_.name + " from " + held_.designer + " (" + held_.from + ")", "");
                ds.finish("the operator of the device did not allow this project (it can do other things than the one that runs there)");
                held_ = DeployService::Received{};
                ds.set_asking(false);
                err = "The project was not installed.";
            }
        }
        else HML_WARN("system: deploy: unknown command '%s'", cmd.c_str());
        if(!err.empty()) HML_WARN("system: deploy: %s", err.c_str());
        set_status(err);
    }

    // What arrives on the other thread: a designer that asks, a project to install.
    bool poll() override
    {
        DeployService& ds = deploy_service();
        // (the restart waits until the designer has gone: a project may follow the runtime)
        if(restart_ && !ds.serving() && ++restart_ticks_ >= 3) { restart_ = false; restart_ticks_ = 0; ui().request_restart(RestartTarget::App); return false; }

        DeployService::Received r;
        const bool waiting = ds.received(r);
        if(!held_.file.empty()) {                                     // the operator is being asked
            if(waiting && r.file == held_.file) return false;
            // (the designer gave up, or nobody answered in time: the question goes away)
            held_ = DeployService::Received{};
            ds.set_asking(false);
            set_status("The project that waited for an answer was not installed (nobody answered in time).");
            return true;
        }
        if(waiting) {
            if(r.runtime) { install(r, ""); return true; }
            // what can it do? The same list as what was agreed to (or as what runs now) goes in
            // by itself; another list is the operator's to look at.
            ProjectCapabilities caps;
            const std::string why = transfer_capabilities(target(), r.file, caps);
            if(!why.empty()) { ds.finish(why); set_status("A project was received but not installed: " + why); return true; }
            if(ds.ask() && !caps.empty() && caps.fingerprint() != agreed()) {
                held_ = r;
                held_caps_ = caps;
                ds.set_asking(true);
                HML_INFO("deploy: the project '%s' from %s can do other things than the one before: waiting for the operator", r.name.c_str(), r.from.c_str());
                set_status("");
                // the operator must see it wherever they are
                const std::string screen = SystemSettings::page_screen(id());
                if(ui().current_screen().value_or("") != screen) ui().show_screen(screen, "none");
                return true;
            }
            install(r, caps.fingerprint());
            return true;
        }

        const bool listening = ds.listening();
        DeployService::Offer offer;
        const bool pending = ds.pending(offer);
        if(ds.adding() && !pending && millis() >= deadline_ms_) ds.set_adding(false);
        if(pending && !was_pending_) deadline_ms_ = millis() + kAddSeconds * 1000;
        const bool adding = ds.adding();
        const uint32_t left = adding ? remaining() : 0;
        const bool changed = listening != was_listening_ || pending != was_pending_ || adding != was_adding_ || left != was_left_;
        was_listening_ = listening; was_pending_ = pending; was_adding_ = adding; was_left_ = left;
        return changed;
    }

    json custom_rows(bool compact, int font) const override
    {
        const char* muted = "#9fb3c8";
        auto tag = [&](const std::string& key) { return SystemSettings::tag_name(id(), key); };
        auto call = [&](const std::string& cmd) { return json{ { "action", "call" }, { "fn", "system.do" }, { "arg", id() + ":" + cmd } }; };
        auto text = [&](const std::string& key, const char* color) {
            return json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", tag(key) } } },
                         { "style", { { "font", font }, { "text_color", color } } } };
        };
        auto row = [&](json children) {
            return json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                         { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 8 } } }, { "children", std::move(children) } };
        };
        auto toggle = [&](const char* label, const std::string& key) {
            return row(json::array({
                json{ { "type", "label" }, { "text", label }, { "w", compact ? 150 : 200 }, { "long_mode", "wrap" }, { "style", { { "font", font }, { "text_color", "#ffffff" } } } },   // (the name of a switch is what is read first)
                json{ { "type", "switch" }, { "bind", tag(key) }, { "on_change", call(key) } },
            }));
        };

        // (accepting, removing, allowing the runtime: the questions and the bar at the bottom - dialogs(), actions())
        json rows = json::array();
        rows.push_back(toggle("Receive projects", "receive"));
        rows.push_back(text("state", "#ffffff"));
        rows.push_back(toggle("Add a designer", "adding"));
        rows.push_back(text("adding_text", "#ffffff"));
        rows.push_back(toggle("Ask before a project that can do more", "ask"));
        rows.push_back(text("count", muted));
        // a designer: press its row to choose it, then Runtime / Remove below
        for(int i = 1; i <= kSlots; i++) {
            const std::string n = std::to_string(i);
            json r = row(json::array({
                json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "wrap" }, { "bind", { { "text", tag("d" + n) } } },
                      { "style", { { "font", font }, { "text_color", "#ffffff" } } } },
            }));
            r["bind"] = { { "visible", tag("d" + n + "_used") }, { "bg_color", { { "tag", tag("d" + n + "_sel") }, { "map", { { "true", "#2f4a6b" }, { "default", "#222d3b" } } } } } };
            r["on_click"] = call("select:" + n);
            rows.push_back(std::move(r));
        }
        rows.push_back(text("more", muted));
        return rows;
    }

    std::vector<PageAction> actions() const override
    {
        return {
            // (always there, greyed until a designer is chosen: the bar says what can be done)
            { "Runtime", "runtime", PageAction::Kind::Neutral, "", "has_sel" },
            { "Remove", "delete", PageAction::Kind::Danger, "", "has_sel" },
        };
    }
    std::vector<PageDialog> dialogs() const override
    {
        const PageAction cancel_rm{ "Cancel", "delete_no", PageAction::Kind::Neutral, "", "" }, cancel_rt{ "Cancel", "runtime_no", PageAction::Kind::Neutral, "", "" };
        return {
            { "arrived", "arrived_text", { "Install", "install_yes", PageAction::Kind::Danger, "", "" }, { "Reject", "install_no", PageAction::Kind::Neutral, "", "" }, true },
            { "pending", "pending_text", { "Accept", "accept", PageAction::Kind::Ok, "", "" }, { "Reject", "reject", PageAction::Kind::Danger, "", "" }, true },
            { "confirm", "confirm_text", { "Remove", "delete_yes", PageAction::Kind::Danger, "", "" }, cancel_rm, false },
            { "confirm_rt", "confirm_rt_text", { "Allow", "runtime_yes", PageAction::Kind::Danger, "", "" }, cancel_rt, false },
        };
    }

private:
    // Put what arrived in place, tell the designer, restart. approve: the fingerprint of what
    // the project can do, remembered as agreed to ("" = a runtime: nothing to remember).
    void install(const DeployService::Received& r, const std::string& approve)
    {
        DeployService& ds = deploy_service();
        const TransferTarget t = target();
        const char* what = r.runtime ? "runtime" : "project";
        const std::string msg = r.runtime ? deploy_install_runtime(r.file, ds.runtime_path()) : transfer_install(t, r.file);
        ds.finish(msg);
        if(msg.empty()) {
            if(!approve.empty()) ds.set_approved(approve);
            operation_log().record("deploy", r.runtime ? "install runtime" : "install project", r.runtime ? "" : t.name, r.name + " from " + r.designer + " (" + r.from + ")");
            HML_INFO("deploy: %s '%s' installed, restarting", what, r.name.c_str());
            set_status(r.runtime ? "A new runtime was received and put in place. Restarting ..." : "A project was received and installed. Restarting ...");
            restart_ = true;
            restart_ticks_ = 0;
        }
        else {
            HML_WARN("deploy: %s '%s' from %s was not installed: %s", what, r.name.c_str(), r.from.c_str(), msg.c_str());
            set_status(std::string(r.runtime ? "A runtime" : "A project") + " was received but not installed: " + msg);
        }
    }
    // The fingerprint of what was agreed to: what the operator said yes to last, else (nothing
    // was ever asked) what the project that runs now can do - it was put here by the owner.
    std::string agreed() const
    {
        const std::string a = deploy_service().approved();
        if(!a.empty()) return a;
        ProjectCapabilities now;
        return project_capabilities_of(proj_.info().path, now) ? now.fingerprint() : std::string();
    }
    // the designer a command is about: the row given ("2"), else the one chosen on the page
    std::string slot_or_selected(const std::string& arg) const
    {
        if(arg.empty()) return selected_;
        const int slot = std::atoi(arg.c_str());
        return slot >= 1 && static_cast<size_t>(slot) <= list_.size() ? list_[static_cast<size_t>(slot - 1)].fingerprint : std::string();
    }
    static std::string describe(const DeployDesigner& d)
    {
        return d.name + (d.added.empty() ? "" : "  (" + d.added + ")") + "\n" + fingerprint_for_reading(d.fingerprint) + (d.runtime ? "\nmay replace the runtime" : "");
    }
    TransferTarget target() const
    {
        const ProjectInfo& in = proj_.info();
        TransferTarget t;
        t.dir = in.dir;
        t.archive = transfer_archive();
        t.name = !t.archive.empty() ? std::filesystem::u8path(t.archive).stem().u8string() : std::filesystem::u8path(in.dir).filename().u8string();
        t.width = in.width;
        t.height = in.height;
        return t;
    }
    uint32_t remaining() const
    {
        const uint32_t now = millis();
        return deadline_ms_ > now ? (deadline_ms_ - now + 999) / 1000 : 0;
    }
    void set_status(const std::string& msg)
    {
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string(msg, nullptr);
    }

    Project& proj_;
    std::vector<DeployDesigner> list_;    // as last shown: the slots of the Remove buttons
    DeployService::Received held_;        // a project the operator is being asked about ("" = none)
    ProjectCapabilities held_caps_;       // what it can do
    std::string selected_;                // fingerprint of the designer chosen on the page (its row was pressed)
    std::string confirm_;                 // fingerprint waiting for "Remove?" confirmation
    std::string confirm_rt_;              // fingerprint waiting for "Allow to replace the runtime?"
    uint32_t deadline_ms_ = 0;
    bool restart_ = false;
    int  restart_ticks_ = 0;
    bool was_listening_ = false, was_pending_ = false, was_adding_ = false;
    uint32_t was_left_ = 0;
};

} // namespace

std::unique_ptr<SettingsPage> make_deploy_page(Project& proj) { return std::make_unique<DeployPage>(proj); }

} // namespace hml
