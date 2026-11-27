// System > Tags: every tag of the project with its present value, a filter by name, and a form
// to write a value (commissioning and maintenance: look at what the PLC says and set a value
// that no screen shows). A SettingsPage like the others (system_settings.hpp).
//
// A write goes the way a write from a screen goes: limits of the tag, the driver sends it to the
// device, and the operation log records it ("monitor"). The whole of the system settings is
// behind "users.system_level" when the project has users.
#include <algorithm>
#include "operation_log.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr int kRows = 8;                // tags shown at a time

const char* tag_type_name(TagType t)
{
    switch(t) {
        case TagType::Bool: return "bool";
        case TagType::Int: return "int";
        case TagType::Float: return "float";
        default: return "string";
    }
}

std::string lower(std::string s)
{
    for(char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

class MonitorPage final : public SettingsPage {
public:
    std::string id() const override { return "monitor"; }
    std::string title() const override { return "Tags"; }
    bool has_apply() const override { return false; }

    // The tags the list shows: those of the project; the runtime's own ("sys.") only when the
    // filter asks for them.
    std::vector<Tag*> shown() const
    {
        const std::string f = lower(filter_);
        std::vector<Tag*> out;
        for(Tag* t : tags().all()) {
            const bool system = SystemSettings::is_system_tag(t->name());
            if(f.empty() ? system : lower(t->name()).find(f) == std::string::npos) continue;
            if(system && t->name().rfind("sys." + id() + ".", 0) == 0) continue;      // (not this page's own)
            out.push_back(t);
        }
        return out;
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
        const std::vector<Tag*> list = shown();
        const int n = static_cast<int>(list.size());
        if(first_ >= n) first_ = std::max(0, (n - 1) / kRows * kRows);
        text("filter", filter_);
        text("value", value_);
        info("range", n == 0 ? "no tags" : vformat("%d - %d / %d", first_ + 1, std::min(first_ + kRows, n), n));
        Tag* sel = selected_.empty() ? nullptr : tags().find(selected_);
        if(!sel) selected_.clear();
        flag("selected", sel != nullptr);
        // the form to write with follows the type: two buttons, the number pad, the keyboard
        const bool is_bool = sel && sel->type() == TagType::Bool;
        const bool is_number = sel && (sel->type() == TagType::Int || sel->type() == TagType::Float);
        flag("sel_bool", is_bool);
        flag("sel_number", is_number);
        flag("sel_text", sel && !is_bool && !is_number);
        flag("sel_typed", sel && !is_bool);
        flag("filtered", !filter_.empty());
        info("sel_name", sel ? sel->name() + "  (" + tag_type_name(sel->type()) + ")" : "");
        info("sel_value", sel ? sel->get().format() : "");
        for(int i = 0; i < kRows; i++) {
            Tag* t = first_ + i < n ? list[static_cast<size_t>(first_ + i)] : nullptr;
            flag("r" + std::to_string(i + 1) + "_used", t != nullptr);
            info("r" + std::to_string(i + 1), t ? t->name() : "");
            info("v" + std::to_string(i + 1), t ? t->get().format() : "");
        }
        return out;
    }

    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    // What the operator typed lives in the tags of the form: take it before the tags are filled again.
    void take_form()
    {
        auto tag_text = [&](const char* key, std::string& into) {
            if(Tag* t = tags().find(SystemSettings::tag_name(id(), key))) into = t->get().format();
        };
        const std::string before = filter_;
        tag_text("filter", filter_);
        tag_text("value", value_);
        if(filter_ != before) first_ = 0;
    }

    void command(const std::string& cmd, const std::string& arg) override
    {
        take_form();
        std::string msg;
        const std::vector<Tag*> list = shown();
        const int n = static_cast<int>(list.size());
        if(cmd == "next") { if(first_ + kRows < n) first_ += kRows; }
        else if(cmd == "prev") first_ = std::max(0, first_ - kRows);
        else if(cmd == "find") first_ = 0;                        // (the filter was taken above)
        else if(cmd == "select") {
            const int row = std::atoi(arg.c_str());
            if(row >= 1 && first_ + row - 1 < n) {
                Tag* t = list[static_cast<size_t>(first_ + row - 1)];
                selected_ = t->name();
                value_ = t->get().format();
            }
        }
        else if(cmd == "close") selected_.clear();
        else if(cmd == "clear") { filter_.clear(); first_ = 0; }
        else if(cmd == "write" || cmd == "set") {
            // "set:<value>" = the buttons of a bool, "write" = what was typed
            const std::string text = cmd == "set" ? arg : value_;
            Tag* t = selected_.empty() ? nullptr : tags().find(selected_);
            if(!t) msg = "Pick a tag first.";
            else if(SystemSettings::is_system_tag(t->name())) msg = "The tags of the runtime (sys.) are not written here.";
            else {
                auto v = Value::parse(t->type(), text);
                if(!v) msg = "\"" + text + "\" is not a value for a tag of type " + tag_type_name(t->type()) + ".";
                else {
                    const std::string old = t->get().format();
                    t->set(*v, nullptr);
                    const std::string now = t->get().format();
                    operation_log().record("monitor", t->name(), old, now);
                    HML_INFO("system: tag monitor: '%s' %s -> %s", t->name().c_str(), old.c_str(), now.c_str());
                    msg = t->name() + " = " + now;
                    if(cmd == "set") value_ = now;
                    if(now != v->format()) msg += "  (kept inside the limits of the tag)";
                }
            }
        }
        else HML_WARN("system: monitor: unknown command '%s'", cmd.c_str());
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string(msg, nullptr);
    }

    // While the page is on the screen the values follow the tags.
    bool poll() override
    {
        auto cur = ui().current_screen();
        if(!cur || *cur != SystemSettings::page_screen(id())) return false;
        if(++ticks_ % 2) return false;
        std::string now;
        const std::vector<Tag*> list = shown();
        for(int i = 0; i < kRows && first_ + i < static_cast<int>(list.size()); i++) now += list[static_cast<size_t>(first_ + i)]->get().format() + "\n";
        if(Tag* t = selected_.empty() ? nullptr : tags().find(selected_)) now += t->get().format();
        if(now == last_) return false;
        last_ = now;
        take_form();
        return true;
    }

    // what writes a value is in the bar at the bottom, not in the list
    std::vector<PageAction> actions() const override
    {
        return {
            { "Write", "write", PageAction::Kind::Danger, "sel_typed", "" },
            { "TRUE", "set:true", PageAction::Kind::Ok, "sel_bool", "" },
            { "FALSE", "set:false", PageAction::Kind::Neutral, "sel_bool", "" },
        };
    }

    bool form_on_top() const override { return true; }
    json custom_rows(bool compact, int font) const override
    {
        const char* muted = "#9fb3c8";
        auto tag = [&](const std::string& key) { return SystemSettings::tag_name(id(), key); };
        auto call = [&](const std::string& cmd) { return json{ { "action", "call" }, { "fn", "system.do" }, { "arg", id() + ":" + cmd } }; };
        auto row = [&](json children) {
            return json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                         { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", 6 } } }, { "children", std::move(children) } };
        };
        auto button = [&](const char* text, const char* color, const std::string& cmd, int w, const char* shown = nullptr) {
            json b = json{ { "type", "button" }, { "text", text }, { "w", w }, { "h", compact ? 30 : 40 }, { "style", { { "bg_color", color } } },
                           { "style_label", { { "font", font } } }, { "on_click", call(cmd) } };
            if(shown) b["bind"] = { { "visible", tag(shown) } };
            return b;
        };
        auto entry = [&](const char* shown, const char* accepted) {
            json e = json{ { "type", "text_display" }, { "input", true }, { "flex_grow", 1 }, { "max_length", 64 }, { "bind", { { "text", tag("value") }, { "visible", tag(shown) } } },
                           { "style", { { "font", font }, { "pad", 6 } } } };
            if(accepted) e["accepted_chars"] = accepted;
            return e;
        };
        auto label = [&](const std::string& key, const char* color, int f) {
            return json{ { "type", "label" }, { "long_mode", "dots" }, { "bind", { { "text", tag(key) } } }, { "style", { { "font", f }, { "text_color", color } } } };
        };

        json rows = json::array();
        // the tag picked: its value, and a value to write
        json form = json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "column" }, { "scrollable", false },
                          { "bind", { { "visible", tag("selected") } } },
                          { "style", { { "bg_color", "#22303f" }, { "radius", 6 }, { "border_width", 0 }, { "pad", 8 }, { "pad_row", 6 } } } };
        json name = label("sel_name", "#ffffff", font + 2); name["flex_grow"] = 1;
        json value = label("sel_value", "#90caf9", font + 2); value["w"] = compact ? 110 : 180; value["style"]["text_align"] = "right";
        form["children"] = json::array({
            row(json::array({ name, value, button("x", "#455a64", "close", compact ? 34 : 44) })),
            row(json::array({
                entry("sel_text", nullptr),                                  // the keyboard
                entry("sel_number", "0123456789.-"),                         // the number pad
            })),                                                             // (Write / TRUE / FALSE: the bar at the bottom)
        });
        rows.push_back(std::move(form));

        json filter = json{ { "type", "text_display" }, { "input", true }, { "flex_grow", 1 }, { "max_length", 32 }, { "placeholder", "part of a name (sys. = the runtime's tags)" },
                            { "bind", tag("filter") }, { "style", { { "font", font }, { "pad", 6 } } }, { "on_change", call("find") } };
        json range = label("range", muted, font); range["w"] = compact ? 86 : 120; range["style"]["text_align"] = "center";
        rows.push_back(row(json::array({ filter, button("x", "#455a64", "clear", compact ? 34 : 44, "filtered"), button("<", "#2c3e50", "prev", compact ? 40 : 56), range, button(">", "#2c3e50", "next", compact ? 40 : 56) })));

        for(int i = 1; i <= kRows; i++) {
            json n = label("r" + std::to_string(i), "#ffffff", font + 2); n["flex_grow"] = 1;
            json v = label("v" + std::to_string(i), "#90caf9", font + 2); v["w"] = compact ? 150 : 260; v["style"]["text_align"] = "right";
            json r = json{ { "type", "panel" }, { "w", "100%" }, { "h", compact ? 30 : 40 }, { "flex", "row" }, { "flex_cross", "center" }, { "flex_track", "center" }, { "scrollable", false },
                           { "style", { { "bg_color", "#2c3e50" }, { "radius", 6 }, { "border_width", 0 }, { "pad_left", 8 }, { "pad_right", 8 }, { "pad_top", 0 }, { "pad_bottom", 0 }, { "pad_column", 6 } } },
                           { "bind", { { "visible", tag("r" + std::to_string(i) + "_used") } } },
                           { "on_click", call("select:" + std::to_string(i)) },
                           { "children", json::array({ n, v }) } };
            rows.push_back(std::move(r));
        }
        return rows;
    }

private:
    std::string filter_, value_, selected_;
    mutable int first_ = 0;
    int         ticks_ = 0;
    std::string last_;
};

} // namespace

std::unique_ptr<SettingsPage> make_monitor_page() { return std::make_unique<MonitorPage>(); }

} // namespace hml
