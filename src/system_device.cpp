// System > Device: read and write an address of a PLC that no tag is mapped to (commissioning
// and maintenance). Pick a driver, type an address the way the driver writes it, and the page
// keeps reading it while it is shown; a value can be written. A SettingsPage like the others.
//
// The driver does the work in its own thread (Driver::device_post / device_take): the page
// only asks and shows. A write obeys what the driver allows ("write_ranges",
// "allow_system_write") and is recorded in the operation log ("device"). The whole of the
// system settings is behind "users.system_level" when the project has users.
#include <algorithm>
#include "driver.hpp"
#include "operation_log.hpp"
#include "project.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

const char* kNone = "(none)";
constexpr int kGiveUpTicks = 40;        // an answer that never comes (the driver was stopped)

class DevicePage final : public SettingsPage {
public:
    std::string id() const override { return "device"; }
    std::string title() const override { return "Device"; }
    bool has_apply() const override { return false; }

    static std::vector<std::string> capable()
    {
        std::vector<std::string> out;
        for(Driver* d : drivers().all()) if(d->device_access()) out.push_back(d->name());
        return out;
    }
    Driver* driver() const
    {
        for(Driver* d : drivers().all()) if(d->device_access() && d->name() == driver_) return d;
        return nullptr;
    }
    static const std::vector<std::string>& counts()
    {
        static const std::vector<std::string> c = { "1", "8", "16" };
        return c;
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
        auto text = [&](const std::string& key, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Text; f.value = value; out.push_back(f);
        };
        auto select = [&](const std::string& key, std::vector<std::string> options, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Select; f.options = std::move(options); f.live_options = true;
            if(f.options.empty()) f.options.push_back(kNone);
            f.value = std::find(f.options.begin(), f.options.end(), value) != f.options.end() ? value : f.options[0];
            out.push_back(f);
        };
        names_ = capable();
        if(std::find(names_.begin(), names_.end(), driver_) == names_.end()) { driver_ = names_.empty() ? "" : names_[0]; forget(); }
        Driver* d = driver();
        select("driver", names_, driver_);
        select("type", device_types(), type_);
        select("count", counts(), count_);
        text("address", address_);
        text("value", value_);
        info("values", values_);
        info("error", error_);
        info("help", d ? d->device_help() : "No driver that runs reads addresses (Modbus, SLMP and FINS do).");
        flag("has_driver", d != nullptr);
        flag("has_error", !error_.empty());
        return out;
    }

    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    void forget()
    {
        values_.clear();
        error_.clear();
        read_id_ = write_id_ = 0;
        waited_ = 0;
    }

    // What the operator picked and typed lives in the tags of the form. True when it asks for
    // something else than before (what is shown belongs to the old question then).
    bool take_form()
    {
        const std::string before = driver_ + "\n" + address_ + "\n" + type_ + "\n" + count_;
        auto pick = [&](const char* key, const std::vector<std::string>& options, std::string& into) {
            Tag* t = tags().find(SystemSettings::tag_name(id(), key));
            if(!t) return;
            const int i = static_cast<int>(t->get_number());
            if(i >= 0 && static_cast<size_t>(i) < options.size()) into = options[static_cast<size_t>(i)];
        };
        auto typed = [&](const char* key, std::string& into) {
            if(Tag* t = tags().find(SystemSettings::tag_name(id(), key))) into = t->get().format();
        };
        pick("driver", names_, driver_);
        pick("type", device_types(), type_);
        pick("count", counts(), count_);
        typed("address", address_);
        typed("value", value_);
        if(before == driver_ + "\n" + address_ + "\n" + type_ + "\n" + count_) return false;
        forget();
        return true;
    }

    void say(const std::string& msg)
    {
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string(msg, nullptr);
    }

    void command(const std::string& cmd, const std::string&) override
    {
        take_form();
        std::string msg;
        if(cmd == "read") ticks_ = 0;                             // (what was typed is taken; the next poll asks)
        else if(cmd == "write") {
            Driver* d = driver();
            if(!d) msg = "There is no driver to write with.";
            else if(address_.empty()) msg = "Type an address first.";
            else if(value_.empty()) msg = "Type a value first.";
            else if(write_id_) msg = "The last write is still on its way.";
            else {
                DeviceRequest r;
                r.address = address_; r.type = type_; r.write = true; r.value = value_;
                write_id_ = d->device_post(r);
                write_what_ = driver_ + ": " + address_;
                write_value_ = value_;
                write_waited_ = 0;
                msg = write_id_ ? "Writing ..." : "The driver is busy. Try again.";
            }
        }
        else HML_WARN("system: device: unknown command '%s'", cmd.c_str());
        say(msg);
    }

    // While the page is on the screen the address is read again and again.
    bool poll() override
    {
        auto cur = ui().current_screen();
        if(!cur || *cur != SystemSettings::page_screen(id())) { read_id_ = 0; return false; }
        Driver* d = driver();
        if(!d) return false;
        bool changed = false;
        DeviceResult r;
        if(write_id_) {
            if(d->device_take(write_id_, r)) {
                write_id_ = 0;
                if(r.ok) {
                    operation_log().record("device", write_what_, "", write_value_);
                    say(write_what_ + " = " + write_value_ + " written");
                }
                else say("Not written: " + r.error);
                ticks_ = 0;                                       // (read at once: show what the device has now)
            }
            else if(++write_waited_ > kGiveUpTicks) { write_id_ = 0; say("Not written: the driver did not answer."); }
        }
        if(read_id_) {
            if(d->device_take(read_id_, r)) {
                read_id_ = 0;
                if(!take_form()) {                                // (an answer to the question still asked)
                    std::string v;
                    for(size_t i = 0; i < r.values.size(); i++)
                        v += (i ? "\n" : "") + (r.values.size() > 1 ? vformat("+%d:   ", static_cast<int>(i)) : std::string()) + r.values[i];
                    if(v != values_ || r.error != error_) { values_ = v; error_ = r.error; }
                }
                changed = true;
            }
            else if(++waited_ > kGiveUpTicks) { read_id_ = 0; waited_ = 0; }
        }
        else if(ticks_++ % 2 == 0) {
            if(take_form()) changed = true;
            if(!address_.empty()) {
                DeviceRequest q;
                q.address = address_; q.type = type_; q.count = std::atoi(count_.c_str());
                read_id_ = d->device_post(q);
                waited_ = 0;
            }
            else if(!values_.empty() || !error_.empty()) { forget(); changed = true; }
        }
        return changed;
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
        auto label = [&](const char* text, int w) {
            return json{ { "type", "label" }, { "text", text }, { "w", w }, { "style", { { "font", font }, { "text_color", muted } } } };
        };
        auto dropdown = [&](const char* key) {
            return json{ { "type", "dropdown" }, { "bind", { { "value", tag(key) }, { "options", tag(std::string(key) + "__options") } } },
                         { "style", { { "font", font } } }, { "on_change", call("read") } };
        };
        const int lw = compact ? 70 : 100;

        json rows = json::array();
        json drv = dropdown("driver"); drv["flex_grow"] = 1;
        rows.push_back(row(json::array({ label("Driver", lw), drv })));
        json type = dropdown("type"); type["w"] = compact ? 104 : 140;
        json count = dropdown("count"); count["w"] = compact ? 64 : 84;
        rows.push_back(row(json::array({
            json{ { "type", "text_display" }, { "input", true }, { "flex_grow", 1 }, { "max_length", 40 }, { "placeholder", "address" },
                  { "bind", tag("address") }, { "style", { { "font", font }, { "pad", 6 } } }, { "on_change", call("read") } },
            type, count })));
        rows.push_back(json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", tag("help") } } },
                             { "style", { { "font", font }, { "text_color", muted } } } });
        json write = json{ { "type", "button" }, { "text", "Write" }, { "w", compact ? 80 : 110 }, { "h", compact ? 34 : 44 }, { "style", { { "bg_color", "#c62828" } } },
                           { "style_label", { { "font", font } } }, { "on_click", call("write") }, { "bind", { { "enabled", tag("has_driver") } } } };
        rows.push_back(row(json::array({
            json{ { "type", "text_display" }, { "input", true }, { "flex_grow", 1 }, { "max_length", 24 }, { "placeholder", "value to write" },
                  { "bind", tag("value") }, { "style", { { "font", font }, { "pad", 6 } } } },
            write })));
        rows.push_back(json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "column" }, { "scrollable", false },
                             { "style", { { "bg_color", "#22303f" }, { "radius", 6 }, { "border_width", 0 }, { "pad", 8 }, { "pad_row", 4 } } },
                             { "children", json::array({
                                 json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", tag("values") } } },
                                       { "style", { { "font", font + 4 }, { "text_color", "#ffffff" } } } },
                                 json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", tag("error") }, { "visible", tag("has_error") } } },
                                       { "style", { { "font", font }, { "text_color", "#ffcc80" } } } },
                             }) } });
        return rows;
    }

private:
    std::vector<std::string> names_;
    std::string driver_, address_, type_ = "int16", count_ = "1", value_;
    std::string values_, error_;        // what the last read gave
    uint64_t    read_id_ = 0, write_id_ = 0;
    int         ticks_ = 0, waited_ = 0, write_waited_ = 0;
    std::string write_what_, write_value_;
};

} // namespace

std::unique_ptr<SettingsPage> make_device_page() { return std::make_unique<DevicePage>(); }

// A project with a driver that reads addresses gets the page (also when the driver is disabled
// for now: it can be enabled on its own page).
bool has_device_driver(const json* drivers)
{
    if(!drivers || !drivers->is_array()) return false;
    for(const json& cfg : *drivers) {
        const std::string type = jstr(cfg, "type", "");
        if(type.rfind("modbus", 0) == 0 || type == "slmp" || type == "fins") return true;
    }
    return false;
}

} // namespace hml
