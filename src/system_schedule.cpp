// System > Schedule: the schedules of the project (schedule.hpp) with their next run, and a form
// to change the time, the days and "enabled" of one. What a schedule does is part of the
// project and cannot be changed here. A SettingsPage like the others; the whole of the system
// settings is behind "users.system_level", a change is in the operation log.
#include <algorithm>
#include <ctime>
#include "logdb.hpp"
#include "schedule.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr int kRows = 5;             // (with the line above them: what a 480x320 display shows without scrolling)
const char* const kDayKeys[7] = { "d1", "d2", "d3", "d4", "d5", "d6", "d0" };      // Monday first; dN = bit N of the mask
const char* const kDayShort[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

class SchedulePage final : public SettingsPage {
public:
    std::string id() const override { return "schedule"; }
    std::string title() const override { return "Schedule"; }
    bool has_apply() const override { return false; }

    const ScheduleItem* selected() const
    {
        for(const ScheduleItem& s : scheduler().items()) if(s.name == selected_) return &s;
        return nullptr;
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
        const auto& list = scheduler().items();
        const int n = static_cast<int>(list.size());
        if(first_ >= n) first_ = std::max(0, (n - 1) / kRows * kRows);
        const ScheduleItem* sel = selected();
        if(!sel) selected_.clear();
        flag("selected", sel != nullptr);
        flag("daily", sel && !sel->once);
        flag("once", sel && sel->once);
        info("sel_name", sel ? sel->name : "");
        text("at", at_);
        for(int i = 0; i < 7; i++) flag(kDayKeys[i], (days_ >> ((i + 1) % 7)) & 1u);
        flag("enabled", enabled_);
        flag("more", n > kRows);
        // the line above the list: what the last command said, else where in the list we are
        info("range", !status_.empty() ? status_ : n == 0 ? "no schedules" : n > kRows ? vformat("%d - %d / %d", first_ + 1, std::min(first_ + kRows, n), n) : "");
        flag("head", !status_.empty() || n == 0 || n > kRows);
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        for(int i = 0; i < kRows; i++) {
            const ScheduleItem* s = first_ + i < n ? &list[static_cast<size_t>(first_ + i)] : nullptr;
            const std::string k = std::to_string(i + 1);
            flag("r" + k + "_used", s != nullptr);
            info("n" + k, s ? s->name : "");
            info("t" + k, s ? schedule_at_text(*s) + (s->once ? "" : "  " + schedule_days_text(s->days)) : "");
            std::string next;
            if(s) {
                if(!s->enabled) next = "off";
                else if(auto t = schedule_next_due(*s, now)) { const std::string full = LogDb::format_time(*t * 1000); next = full.size() >= 16 ? full.substr(5, 11) : full; }
                else next = "done";
            }
            info("x" + k, next);
        }
        return out;
    }

    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    // what the operator set lives in the tags of the form
    void take_form()
    {
        auto tag = [&](const std::string& key) { return tags().find(SystemSettings::tag_name(id(), key)); };
        if(Tag* t = tag("at")) at_ = t->get().format();
        unsigned d = 0;
        bool any = false;
        for(int i = 0; i < 7; i++) if(Tag* t = tag(kDayKeys[i])) { any = true; if(t->get_number() != 0) d |= 1u << ((i + 1) % 7); }
        if(any) days_ = d;
        if(Tag* t = tag("enabled")) enabled_ = t->get_number() != 0;
    }

    void command(const std::string& cmd, const std::string& arg) override
    {
        std::string msg;
        const auto& list = scheduler().items();
        const int n = static_cast<int>(list.size());
        if(cmd == "next") { if(first_ + kRows < n) first_ += kRows; }
        else if(cmd == "prev") first_ = std::max(0, first_ - kRows);
        else if(cmd == "select") {
            const int row = std::atoi(arg.c_str());
            if(row >= 1 && first_ + row - 1 < n) {
                const ScheduleItem& s = list[static_cast<size_t>(first_ + row - 1)];
                selected_ = s.name;
                at_ = schedule_at_text(s);
                days_ = s.days;
                enabled_ = s.enabled;
            }
        }
        else if(cmd == "close") selected_.clear();
        else if(cmd == "save") {
            take_form();
            if(selected_.empty()) msg = "Pick a schedule first.";
            else {
                msg = scheduler().change(selected_, at_, days_, enabled_);
                if(msg.empty()) {
                    msg = "Saved: " + selected_ + "  " + at_ + (enabled_ ? "" : "  (off)");
                    HML_INFO("system: schedule '%s' changed to %s %s%s", selected_.c_str(), at_.c_str(), schedule_days_text(days_).c_str(), enabled_ ? "" : " (off)");
                    selected_.clear();
                }
            }
        }
        else HML_WARN("system: schedule: unknown command '%s'", cmd.c_str());
        status_ = msg;
        // (shown in the line above the list; the status line of the page is below it, out of sight on a small display)
        if(Tag* t = tags().find(SystemSettings::tag_name(id(), "status"))) t->set_string("", nullptr);
    }

    // the "next run" column moves on while the page is looked at
    bool poll() override
    {
        auto cur = ui().current_screen();
        if(!cur || *cur != SystemSettings::page_screen(id())) return false;
        if(++ticks_ % 20) return false;
        take_form();
        return true;
    }

    json custom_rows(bool compact, int font) const override
    {
        const char* muted = "#9fb3c8";
        auto tag = [&](const std::string& key) { return SystemSettings::tag_name(id(), key); };
        auto call = [&](const std::string& cmd) { return json{ { "action", "call" }, { "fn", "system.do" }, { "arg", id() + ":" + cmd } }; };
        auto row = [&](json children, const char* visible = nullptr) {
            json r = json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                           { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 0 }, { "pad_column", compact ? 4 : 6 } } }, { "children", std::move(children) } };
            if(visible) r["bind"] = { { "visible", tag(visible) } };
            return r;
        };
        auto button = [&](const char* text, const char* color, const std::string& cmd, int w) {
            return json{ { "type", "button" }, { "text", text }, { "w", w }, { "h", compact ? 34 : 44 }, { "style", { { "bg_color", color } } },
                         { "style_label", { { "font", font } } }, { "on_click", call(cmd) } };
        };
        auto label = [&](const std::string& key, const char* color, int f) {
            return json{ { "type", "label" }, { "long_mode", "dots" }, { "bind", { { "text", tag(key) } } }, { "style", { { "font", f }, { "text_color", color } } } };
        };

        json rows = json::array();
        // the schedule picked: its time, its days, on / off
        json form = json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "column" }, { "scrollable", false },
                          { "bind", { { "visible", tag("selected") } } },
                          { "style", { { "bg_color", "#22303f" }, { "radius", 6 }, { "border_width", 0 }, { "pad", 8 }, { "pad_row", 6 } } } };
        json name = label("sel_name", "#ffffff", font + 2); name["flex_grow"] = 1;
        json days = json::array();
        for(int i = 0; i < 7; i++)
            days.push_back(json{ { "type", "button" }, { "text", kDayShort[i] }, { "checkable", true }, { "flex_grow", 1 }, { "h", compact ? 34 : 44 },
                                 { "bind", { { "checked", tag(kDayKeys[i]) } } }, { "style", { { "bg_color", "#37474f" } } }, { "style_checked", { { "bg_color", "#1565c0" } } },
                                 { "style_label", { { "font", font } } } });
        form["children"] = json::array({
            row(json::array({ name, json{ { "type", "label" }, { "text", "On" }, { "style", { { "font", font }, { "text_color", muted } } } },
                              json{ { "type", "switch" }, { "bind", tag("enabled") } }, button("x", "#455a64", "close", compact ? 34 : 44) })),
            row(json::array({ json{ { "type", "datetime_input" }, { "mode", "time" }, { "bind", tag("at") } }, json{ { "type", "panel" }, { "flex_grow", 1 }, { "h", 1 }, { "style", { { "bg_opa", 0 }, { "border_width", 0 } } } },
                              button("Save", "#c62828", "save", compact ? 90 : 120) }), "daily"),
            row(std::move(days), "daily"),
            row(json::array({ json{ { "type", "text_display" }, { "input", true }, { "flex_grow", 1 }, { "max_length", 16 }, { "accepted_chars", "0123456789-: " }, { "placeholder", "YYYY-MM-DD HH:MM" },
                                    { "bind", tag("at") }, { "style", { { "font", font }, { "pad", 6 } } } },
                              button("Save", "#c62828", "save", compact ? 90 : 120) }), "once"),
        });
        rows.push_back(std::move(form));

        json range = label("range", muted, font); range["flex_grow"] = 1;
        json prev = button("<", "#2c3e50", "prev", compact ? 40 : 56), next = button(">", "#2c3e50", "next", compact ? 40 : 56);
        prev["bind"] = { { "visible", tag("more") } };
        next["bind"] = { { "visible", tag("more") } };
        rows.push_back(row(json::array({ range, prev, next }), "head"));

        for(int i = 1; i <= kRows; i++) {
            const std::string k = std::to_string(i);
            json n = label("n" + k, "#ffffff", font + 2); n["w"] = compact ? 110 : 180;
            json t = label("t" + k, "#90caf9", font); t["flex_grow"] = 1;
            json x = label("x" + k, muted, font); x["w"] = compact ? 92 : 120; x["style"]["text_align"] = "right";
            rows.push_back(json{ { "type", "panel" }, { "w", "100%" }, { "h", compact ? 34 : 44 }, { "flex", "row" }, { "flex_cross", "center" }, { "flex_track", "center" }, { "scrollable", false },
                                 { "style", { { "bg_color", "#2c3e50" }, { "radius", 6 }, { "border_width", 0 }, { "pad_left", 8 }, { "pad_right", 8 }, { "pad_top", 0 }, { "pad_bottom", 0 }, { "pad_column", 6 } } },
                                 { "bind", { { "visible", tag("r" + k + "_used") } } },
                                 { "on_click", call("select:" + k) },
                                 { "children", json::array({ n, t, x }) } });
        }
        return rows;
    }

private:
    std::string selected_, at_ = "00:00", status_;
    unsigned    days_ = 0x7F;
    bool        enabled_ = true;
    mutable int first_ = 0;
    int         ticks_ = 0;
};

} // namespace

std::unique_ptr<SettingsPage> make_schedule_page() { return std::make_unique<SchedulePage>(); }

} // namespace hml
