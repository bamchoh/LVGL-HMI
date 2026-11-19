#include "screen_saver.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "alarm.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "users.hpp"
#include "util.hpp"
#if defined(__linux__)
#  include <fcntl.h>
#  include <linux/fb.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace hml {

namespace {
constexpr const char* kTag = "sys.screen.off";

#if defined(__linux__)
// the first /sys/class/backlight/<x>/bl_power (0 = on, 4 = off) that can be written
bool sysfs_backlight(bool on)
{
    std::error_code ec;
    for(const auto& e : std::filesystem::directory_iterator("/sys/class/backlight", ec)) {
        std::ofstream f(e.path() / "bl_power");
        if(!f) continue;
        f << (on ? "0" : "4");
        if(f.good()) return true;
    }
    return false;
}
// blank the framebuffer (the display controller switches the output off)
bool fb_blank(bool on)
{
    const char* dev = std::getenv("HML_FBDEV");
    int fd = ::open(dev && *dev ? dev : "/dev/fb0", O_RDWR);
    if(fd < 0) return false;
    bool ok = ::ioctl(fd, FBIOBLANK, on ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN) == 0;
    ::close(fd);
    return ok;
}
#endif
} // namespace

void ScreenSaver::clear()
{
    if(dark_) wake();
    tags().unsubscribe_group(SubGroup::Screen);
    after_s_ = base_after_s_ = 0;
    wake_on_alarm_ = true;
    logout_ = false;
    store_.clear();
    if(layer_ && lv_is_initialized()) lv_obj_delete(layer_);
    layer_ = nullptr;
}

void ScreenSaver::configure(const json* section, const json* local, const std::string& store)
{
    if(dark_) wake();
    store_ = store;
    base_after_s_ = section ? std::max(0, static_cast<int>(jnum(*section, "after_s", 0))) : 0;
    after_s_ = base_after_s_;
    if(local && local->is_object() && jhas(*local, "after_s")) after_s_ = std::max(0, static_cast<int>(jnum(*local, "after_s", 0)));
    wake_on_alarm_ = section ? jbool(*section, "wake_on_alarm", true) : true;
    logout_ = section ? jbool(*section, "logout", false) : false;
    seen_active_ = alarms().active_count();
    seen_unacked_ = alarms().unacked_count();
    // sys.screen.off: written by a button / Lua -> dark now, or awake
    tags().unsubscribe_group(SubGroup::Screen);
    Tag* t = tags().find(kTag);
    if(!t) {
        Value v = Value::number(0, TagType::Bool);
        t = tags().create(kTag, TagType::Bool, &v, false, 0, 0);
    }
    if(t) t->subscribe(SubGroup::Screen, [this](Tag&, const Value& v, const void* src) {
        if(src == this || updating_) return;
        if(v.truthy() && !dark_) blank();
        else if(!v.truthy() && dark_) wake();
    });
    update_tag();
    if(after_s_ > 0) HML_INFO("screen saver: dark after %d s without input", after_s_);
}

void ScreenSaver::update_tag()
{
    if(Tag* t = tags().find(kTag)) { updating_ = true; t->set_number(dark_ ? 1 : 0, this); updating_ = false; }
}

void ScreenSaver::backlight(bool on)
{
#if defined(__linux__)
    if(sysfs_backlight(on) || fb_blank(on)) return;
    if(!backlight_warned_) {
        HML_INFO("screen saver: the backlight cannot be switched here (no writable /sys/class/backlight, framebuffer not blankable): the screen is made black only");
        backlight_warned_ = true;
    }
#else
    (void)on;
#endif
}

void ScreenSaver::blank()
{
    if(dark_ || !lv_is_initialized()) return;
    if(!layer_) {
        // over everything: the system layer is above screens, popups and dialogs
        layer_ = lv_obj_create(lv_layer_sys());
        lv_obj_remove_style_all(layer_);
        lv_obj_set_size(layer_, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(layer_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(layer_, LV_OPA_COVER, 0);
        lv_obj_set_clickable(layer_, true);
        // The waking touch is the layer's: LVGL is told to ignore the rest of that press, so
        // nothing under the finger gets the release / click.
        lv_obj_add_event_cb(layer_, [](lv_event_t*) {
            if(lv_indev_t* indev = lv_indev_active()) lv_indev_wait_release(indev);
            screen_saver().wake();
        }, LV_EVENT_PRESSED, nullptr);
    }
    lv_obj_set_hidden(layer_, false);
    lv_obj_move_foreground(layer_);
    lv_obj_update_layout(layer_);                  // full size now: a touch right after must hit it
    dark_ = true;
    backlight(false);
    update_tag();
    if(logout_ && users().enabled() && users().level() > 0) users().logout("screen off");
    HML_INFO("screen saver: dark");
}

void ScreenSaver::wake()
{
    if(!dark_) return;
    dark_ = false;
    if(layer_) lv_obj_set_hidden(layer_, true);
    backlight(true);
    if(lv_display_t* d = lv_display_get_default()) lv_display_trigger_activity(d);
    update_tag();
    HML_INFO("screen saver: awake");
}

void ScreenSaver::poll()
{
    // a new alarm wakes the screen (also one that comes while it is awake: nothing to do then)
    const size_t active = alarms().active_count(), unacked = alarms().unacked_count();
    const bool news = active > seen_active_ || unacked > seen_unacked_;
    seen_active_ = active;
    seen_unacked_ = unacked;
    if(dark_) {
        if(news && wake_on_alarm_) wake();
        return;
    }
    if(after_s_ <= 0) return;
    const uint32_t idle = inactive_fn_ ? inactive_fn_() : lv_display_get_inactive_time(nullptr);
    if(idle >= static_cast<uint32_t>(after_s_) * 1000u) blank();
}

std::string ScreenSaver::set_after_s(int s)
{
    s = std::max(0, s);
    after_s_ = s;
    if(store_.empty()) return "";
    // only what differs from the project is kept (a later deploy of the project still counts)
    json overlay = json::object();
    {
        std::ifstream f(std::filesystem::u8path(store_), std::ios::binary);
        if(f) {
            std::stringstream ss;
            ss << f.rdbuf();
            overlay = json::parse(ss.str(), nullptr, false, true);
            if(!overlay.is_object()) overlay = json::object();
        }
    }
    if(s == base_after_s_) {
        if(overlay.contains("screen_saver")) {
            overlay["screen_saver"].erase("after_s");
            if(overlay["screen_saver"].empty()) overlay.erase("screen_saver");
        }
    }
    else overlay["screen_saver"]["after_s"] = s;
    std::string err;
    if(overlay.empty()) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(store_), ec);
        return "";
    }
    if(!write_file_atomic(store_, overlay.dump(2) + "\n", &err)) return err;
    return "";
}

ScreenSaver& screen_saver()
{
    static ScreenSaver instance;
    return instance;
}

// ================================================================ System > Display

namespace {
class DisplayPage final : public SettingsPage {
public:
    std::string id() const override { return "display"; }
    std::string title() const override { return "Display"; }
    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        SettingField f;
        f.key = "after_s"; f.label = "Screen off after (0 = never)"; f.type = SettingField::Type::Number; f.unit = "s";
        f.value = std::to_string(screen_saver().after_s());
        out.push_back(f);
        SettingField p;
        p.key = "project"; p.label = "Project setting"; p.type = SettingField::Type::Info;
        p.value = screen_saver().project_after_s() > 0 ? std::to_string(screen_saver().project_after_s()) + " s" : "never";
        out.push_back(p);
        return out;
    }
    std::string apply(const std::map<std::string, std::string>& values) override
    {
        auto it = values.find("after_s");
        if(it == values.end()) return "";
        const double s = std::atof(it->second.c_str());
        if(s < 0 || s > 86400) return "0 to 86400 seconds";
        return screen_saver().set_after_s(static_cast<int>(s));
    }
};
} // namespace

std::unique_ptr<SettingsPage> make_display_page() { return std::make_unique<DisplayPage>(); }

} // namespace hml
