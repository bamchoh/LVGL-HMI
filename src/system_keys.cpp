// System settings page "Keys": the user interface of KeyService (key_service.hpp).
//
//   Key management   [switch]      while on, a new key can be sent to this device
//   (a key was offered)            sender, fingerprint, comment      [Accept] [Reject]
//   Registered keys                one row per key                   [Delete] -> confirmation
#include "key_service.hpp"
#include "project.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {

namespace {

constexpr int kSlots = 8;               // keys shown on the page
constexpr uint32_t kModeSeconds = 300;  // key management mode switches itself off

class KeysPage final : public SettingsPage {
public:
    KeysPage(int port, std::string file) : port_(port)
    {
        key_service().stop();                                    // a reloaded project starts with the mode off
        key_service().set_file(file);
    }

    std::string id() const override { return "keys"; }
    std::string title() const override { return "Keys"; }
    std::string summary() override
    {
        KeyService& ks = key_service();
        return ks.listening() ? "Accepting a key" : vformat("%d key(s)", static_cast<int>(ks.list().size()));
    }
    bool has_apply() const override { return false; }

    std::vector<SettingField> fields() override
    {
        KeyService& ks = key_service();
        keys_ = ks.list();
        std::vector<SettingField> out;
        auto info = [&](const std::string& key, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Info; f.value = value; out.push_back(f);
        };
        auto flag = [&](const std::string& key, bool on) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Bool; f.value = on ? "1" : "0"; out.push_back(f);
        };

        flag("mode", ks.listening());
        if(ks.listening()) {
            uint32_t left = remaining();
            info("state", vformat("On for %u:%02u more. Send the key to %s port %d (designer: Deploy > Register key).",
                                  left / 60, left % 60, local_ip_addresses().c_str(), ks.port()));
        }
        else info("state", "Off: no key can be sent to this device.");

        KeyService::Offer offer;
        bool pending = ks.pending(offer);
        flag("pending", pending);
        info("pending_text", pending ? "New key from " + offer.from + "\n" + describe(offer.key) + "\nAccept it only if the fingerprint matches the one shown by the sender."
                                     : "");

        const PublicKey* del = nullptr;
        for(const PublicKey& k : keys_) if(k.fingerprint == confirm_) del = &k;
        if(!del) confirm_.clear();
        flag("confirm", del != nullptr);
        info("confirm_text", del ? "Delete this key?\n" + describe(*del) +
                                   (keys_.size() == 1 ? "\nIt is the last key: nobody can log in with a key (deploy) until a new one is registered here." : "")
                                 : "");

        info("count", keys_.empty() ? "No key is registered." : vformat("%d registered key(s) in %s", static_cast<int>(keys_.size()), ks.file().c_str()));
        bool chosen = false;
        for(int i = 0; i < kSlots; i++) {
            bool used = static_cast<size_t>(i) < keys_.size();
            const bool sel = used && keys_[static_cast<size_t>(i)].fingerprint == selected_;
            chosen = chosen || sel;
            flag("k" + std::to_string(i + 1) + "_used", used);
            flag("k" + std::to_string(i + 1) + "_sel", sel);
            info("k" + std::to_string(i + 1), used ? describe(keys_[static_cast<size_t>(i)]) : "");
        }
        if(!chosen) selected_.clear();
        flag("has_sel", chosen);                                  // the bar at the bottom: Delete
        info("more", keys_.size() > kSlots ? vformat("... and %d more (not shown)", static_cast<int>(keys_.size()) - kSlots) : "");
        return out;
    }

    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    void command(const std::string& cmd, const std::string& arg) override
    {
        KeyService& ks = key_service();
        std::string err;
        if(cmd == "mode") {                                       // the switch was moved: the other state
            if(ks.listening()) ks.stop();                         // (the tag is set from the real state afterwards)
            else if(ks.start(port_, &err)) deadline_ms_ = millis() + kModeSeconds * 1000;
        }
        else if(cmd == "accept") ks.accept(&err);
        else if(cmd == "reject") ks.reject();
        else if(cmd == "select") {                                // a row was pressed: chosen, or not any more
            int slot = std::atoi(arg.c_str());
            if(slot >= 1 && static_cast<size_t>(slot) <= keys_.size()) {
                const std::string& fp = keys_[static_cast<size_t>(slot - 1)].fingerprint;
                selected_ = selected_ == fp ? std::string() : fp;
            }
        }
        else if(cmd == "delete") {                                // ask first (the key chosen, or the row given)
            int slot = std::atoi(arg.c_str());
            if(arg.empty()) confirm_ = selected_;
            else if(slot >= 1 && static_cast<size_t>(slot) <= keys_.size()) confirm_ = keys_[static_cast<size_t>(slot - 1)].fingerprint;
        }
        else if(cmd == "delete_yes") { if(!confirm_.empty()) ks.remove(confirm_, &err); confirm_.clear(); }
        else if(cmd == "delete_no") confirm_.clear();
        else HML_WARN("system: keys: unknown command '%s'", cmd.c_str());
        if(!err.empty()) HML_WARN("system: keys: %s", err.c_str());
        set_status(err);
    }

    // The offers arrive on another thread: bring them to the screen, count the mode down.
    bool poll() override
    {
        KeyService& ks = key_service();
        bool listening = ks.listening();
        KeyService::Offer offer;
        bool pending = ks.pending(offer);
        if(listening && !pending && millis() >= deadline_ms_) { ks.stop(); listening = false; }
        if(pending && !was_pending_) {                            // the operator must see it wherever they are
            deadline_ms_ = millis() + kModeSeconds * 1000;
            const std::string screen = SystemSettings::page_screen(id());
            if(ui().current_screen().value_or("") != screen) ui().show_screen(screen, "none");
        }
        uint32_t left = listening ? remaining() : 0;
        bool changed = listening != was_listening_ || pending != was_pending_ || left != was_left_;
        was_listening_ = listening; was_pending_ = pending; was_left_ = left;
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

        json rows = json::array();
        rows.push_back(row(json::array({
            json{ { "type", "label" }, { "text", "Key management" }, { "w", compact ? 130 : 180 }, { "style", { { "font", font }, { "text_color", muted } } } },
            json{ { "type", "switch" }, { "bind", tag("mode") }, { "on_change", call("mode") } },
        })));
        rows.push_back(text("state", "#ffffff"));
        rows.push_back(text("count", muted));
        // a key: press its row to choose it, then Delete below (the questions are over the top of the page)
        for(int i = 1; i <= kSlots; i++) {
            const std::string n = std::to_string(i);
            json r = row(json::array({
                json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "wrap" }, { "bind", { { "text", tag("k" + n) } } },
                      { "style", { { "font", font }, { "text_color", "#ffffff" } } } },
            }));
            r["bind"] = { { "visible", tag("k" + n + "_used") }, { "bg_color", { { "tag", tag("k" + n + "_sel") }, { "map", { { "true", "#2f4a6b" }, { "default", "#222d3b" } } } } } };
            r["on_click"] = call("select:" + n);
            rows.push_back(std::move(r));
        }
        rows.push_back(text("more", muted));
        return rows;
    }

    std::vector<PageAction> actions() const override { return { { "Delete", "delete", PageAction::Kind::Danger, "has_sel", "" } }; }
    std::vector<PageDialog> dialogs() const override
    {
        return {
            { "pending", "pending_text", { "Accept", "accept", PageAction::Kind::Ok, "", "" }, { "Reject", "reject", PageAction::Kind::Danger, "", "" }, true },
            { "confirm", "confirm_text", { "Delete", "delete_yes", PageAction::Kind::Danger, "", "" }, { "Cancel", "delete_no", PageAction::Kind::Neutral, "", "" }, false },
        };
    }

private:
    static std::string describe(const PublicKey& k)
    {
        return k.short_type() + " " + k.fingerprint + (k.comment.empty() ? "" : "\n" + k.comment);
    }
    uint32_t remaining() const
    {
        uint32_t now = millis();
        return deadline_ms_ > now ? (deadline_ms_ - now + 999) / 1000 : 0;
    }
    void set_status(const std::string& msg)
    {
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string(msg, nullptr);
    }

    int port_;
    std::vector<PublicKey> keys_;        // as last shown: the slots of the Delete buttons
    std::string selected_;               // fingerprint of the key chosen on the page (its row was pressed)
    std::string confirm_;                // fingerprint waiting for "Delete?" confirmation
    uint32_t deadline_ms_ = 0;
    bool was_listening_ = false, was_pending_ = false;
    uint32_t was_left_ = 0;
};

} // namespace

std::unique_ptr<SettingsPage> make_keys_page(Project& proj)
{
    const json* cfg = proj.section("key_management");
#if defined(HML_BACKEND_FBDEV) || defined(HML_BACKEND_DRM)
    const bool by_default = true;        // a device: its keys are managed on its own screen
#else
    const bool by_default = false;       // a PC: the page would edit the developer's own key file
#endif
    if(!jbool(cfg ? *cfg : json::object(), "enabled", by_default)) return nullptr;
    int port = cfg ? static_cast<int>(jnum(*cfg, "port", KeyService::kDefaultPort)) : KeyService::kDefaultPort;
    std::string file = cfg ? jstr(*cfg, "file", "") : "";
    if(!file.empty()) {
        bool absolute = file[0] == '/' || file[0] == '\\' || (file.size() > 1 && file[1] == ':');
        if(!absolute) file = proj.info().dir + "/" + file;
    }
    return std::make_unique<KeysPage>(port, file);
}

} // namespace hml
