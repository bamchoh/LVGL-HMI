// System settings page "Deploy": the user interface of DeployService (deploy_service.hpp).
//
//   Receive projects  [switch]     while on, an accepted designer can send a project (no SSH needed)
//   Add a designer    [switch]     for a few minutes a new designer may ask to be accepted
//   (a designer asks)              sender, name, fingerprint         [Accept] [Reject]
//   Accepted designers             one row each          [Runtime] -> confirmation   [Remove] -> confirmation
//                                  (Runtime: this designer may replace the runtime of the device, too)
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
            info("state", vformat("On: an accepted designer can send a project to %s port %d.\nThis device: %s",
                                  local_ip_addresses().c_str(), ds.port(), ds.fingerprint().c_str()));
        else info("state", "Off: no project can be sent to this device over the network (the port is closed).");

        flag("adding", ds.adding());
        if(ds.adding()) {
            const uint32_t left = remaining();
            info("adding_text", vformat("On for %u:%02u more: deploy from the new designer now.", left / 60, left % 60));
        }
        else info("adding_text", ds.listening() ? "Off: only the designers below can send." : "");

        DeployService::Offer offer;
        const bool pending = ds.pending(offer);
        flag("pending", pending);
        info("pending_text", pending ? "A new designer asks to send projects to this device:\n" + offer.name + " (" + offer.from + ")\n" + offer.fingerprint +
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
        for(int i = 0; i < kSlots; i++) {
            const bool used = static_cast<size_t>(i) < list_.size();
            flag("d" + std::to_string(i + 1) + "_used", used);
            info("d" + std::to_string(i + 1), used ? describe(list_[static_cast<size_t>(i)]) : "");
        }
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
        else if(cmd == "delete") {                                    // ask first
            const int slot = std::atoi(arg.c_str());
            if(slot >= 1 && static_cast<size_t>(slot) <= list_.size()) { confirm_ = list_[static_cast<size_t>(slot - 1)].fingerprint; confirm_rt_.clear(); }
        }
        else if(cmd == "delete_yes") { if(!confirm_.empty()) err = ds.remove(confirm_); confirm_.clear(); }
        else if(cmd == "delete_no") confirm_.clear();
        else if(cmd == "runtime") {                                   // taking it away at once, giving it asks first
            const int slot = std::atoi(arg.c_str());
            if(slot >= 1 && static_cast<size_t>(slot) <= list_.size()) {
                const DeployDesigner& d = list_[static_cast<size_t>(slot - 1)];
                if(d.runtime) err = ds.set_runtime_allowed(d.fingerprint, false);
                else { confirm_rt_ = d.fingerprint; confirm_.clear(); }
            }
        }
        else if(cmd == "runtime_yes") { if(!confirm_rt_.empty()) err = ds.set_runtime_allowed(confirm_rt_, true); confirm_rt_.clear(); }
        else if(cmd == "runtime_no") confirm_rt_.clear();
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
        if(ds.received(r)) {
            const TransferTarget t = target();
            const char* what = r.runtime ? "runtime" : "project";
            const std::string msg = r.runtime ? deploy_install_runtime(r.file, ds.runtime_path()) : transfer_install(t, r.file);
            ds.finish(msg);
            if(msg.empty()) {
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
        auto button = [&](const char* label, const char* color, const std::string& cmd) {
            return json{ { "type", "button" }, { "text", label }, { "w", compact ? 96 : 130 }, { "h", compact ? 32 : 42 },
                         { "style", { { "bg_color", color } } }, { "style_label", { { "font", font + 2 } } }, { "on_click", call(cmd) } };
        };
        auto row = [&](json children) {
            return json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                         { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 8 } } }, { "children", std::move(children) } };
        };
        auto box = [&](const std::string& visible_tag, const char* color, json children) {
            return json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "column" }, { "scrollable", false },
                         { "bind", { { "visible", tag(visible_tag) } } },
                         { "style", { { "bg_color", color }, { "radius", 6 }, { "border_width", 0 }, { "pad", 8 }, { "pad_row", 6 } } },
                         { "children", std::move(children) } };
        };
        auto toggle = [&](const char* label, const std::string& key) {
            return row(json::array({
                json{ { "type", "label" }, { "text", label }, { "w", compact ? 130 : 180 }, { "style", { { "font", font }, { "text_color", muted } } } },
                json{ { "type", "switch" }, { "bind", tag(key) }, { "on_change", call(key) } },
            }));
        };

        json rows = json::array();
        rows.push_back(toggle("Receive projects", "receive"));
        rows.push_back(text("state", "#ffffff"));
        rows.push_back(toggle("Add a designer", "adding"));
        rows.push_back(text("adding_text", "#ffffff"));
        rows.push_back(box("pending", "#4a3a10", json::array({
            text("pending_text", "#ffffff"),
            row(json::array({ button("Accept", "#2e7d32", "accept"), button("Reject", "#c62828", "reject") })),
        })));
        rows.push_back(box("confirm", "#4a1c1c", json::array({
            text("confirm_text", "#ffffff"),
            row(json::array({ button("Remove", "#c62828", "delete_yes"), button("Cancel", "#455a64", "delete_no") })),
        })));
        rows.push_back(box("confirm_rt", "#4a1c1c", json::array({
            text("confirm_rt_text", "#ffffff"),
            row(json::array({ button("Allow", "#c62828", "runtime_yes"), button("Cancel", "#455a64", "runtime_no") })),
        })));
        rows.push_back(text("count", muted));
        for(int i = 1; i <= kSlots; i++) {
            json r = row(json::array({
                json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "wrap" }, { "bind", { { "text", tag("d" + std::to_string(i)) } } },
                      { "style", { { "font", font }, { "text_color", "#ffffff" } } } },
                json{ { "type", "button" }, { "text", "Runtime" }, { "w", compact ? 70 : 96 }, { "h", compact ? 30 : 40 },
                      { "style", { { "bg_color", "#455a64" } } }, { "style_label", { { "font", font } } }, { "on_click", call("runtime:" + std::to_string(i)) } },
                json{ { "type", "button" }, { "text", "Remove" }, { "w", compact ? 70 : 96 }, { "h", compact ? 30 : 40 },
                      { "style", { { "bg_color", "#455a64" } } }, { "style_label", { { "font", font } } }, { "on_click", call("delete:" + std::to_string(i)) } },
            }));
            r["bind"] = { { "visible", tag("d" + std::to_string(i) + "_used") } };
            rows.push_back(std::move(r));
        }
        rows.push_back(text("more", muted));
        return rows;
    }

private:
    static std::string describe(const DeployDesigner& d)
    {
        return d.name + (d.added.empty() ? "" : "  (" + d.added + ")") + "\n" + d.fingerprint + (d.runtime ? "\nmay replace the runtime" : "");
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
