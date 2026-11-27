#include "sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <lvgl.h>

#include "alarm.hpp"
#include "system_settings.hpp"
#include "tag.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr const char* kMuteTag = "sys.sound.mute";
constexpr const char* kVolumeTag = "sys.sound.volume";
constexpr const char* kAlarmTag = "sys.sound.alarm";
constexpr size_t kCacheMax = 16;            // sound files held in memory
constexpr uint32_t kSaveAfterMs = 1500;     // a slider that is dragged writes the file once, at the end

uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t rd16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void wr32(std::string& s, uint32_t v) { for(int i = 0; i < 4; i++) s += static_cast<char>((v >> (8 * i)) & 0xFF); }
void wr16(std::string& s, uint16_t v) { for(int i = 0; i < 2; i++) s += static_cast<char>((v >> (8 * i)) & 0xFF); }

// a tone with soft edges (a hard edge is heard as a crack), appended to the samples
void tone(Pcm& p, double hz, int ms, double level = 0.6)
{
    const int n = p.rate * ms / 1000, edge = std::max(1, p.rate * 4 / 1000);
    const double pi = 3.14159265358979323846;
    for(int i = 0; i < n; i++) {
        double env = 1.0;
        if(i < edge) env = static_cast<double>(i) / edge;
        else if(n - i <= edge) env = static_cast<double>(n - i) / edge;
        p.samples.push_back(hz <= 0 ? 0 : static_cast<int16_t>(std::sin(2 * pi * hz * i / p.rate) * env * level * 32767));
    }
    p.pattern.push_back(ms);
}

} // namespace

// ================================================================ WAV

bool wav_parse(const std::string& bytes, Pcm& out, std::string* err)
{
    auto fail = [&](const char* why) { if(err) *err = why; return false; };
    const auto* d = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t n = bytes.size();
    if(n > kSoundFileMax) return fail("the file is larger than 4 MB");
    if(n < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) return fail("not a WAV file");
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    bool have_fmt = false;
    size_t pos = 12;
    while(pos + 8 <= n) {
        const uint32_t size = rd32(d + pos + 4);
        const size_t body = pos + 8, left = n - body;
        const size_t len = std::min<size_t>(size, left);          // (a length that says more than the file has)
        if(std::memcmp(d + pos, "fmt ", 4) == 0) {
            if(len < 16) return fail("the WAV header is cut off");
            format = rd16(d + body);
            channels = rd16(d + body + 2);
            rate = rd32(d + body + 4);
            bits = rd16(d + body + 14);
            if(format == 0xFFFE && len >= 26) format = rd16(d + body + 24);      // "extensible": the real format follows
            have_fmt = true;
        }
        else if(std::memcmp(d + pos, "data", 4) == 0) {
            if(!have_fmt) return fail("the WAV file has its data before its format");
            if(format != 1) return fail("only PCM WAV files are played (not compressed, not floating point)");
            if(channels < 1 || channels > 2) return fail("only mono and stereo WAV files are played");
            if(bits != 8 && bits != 16) return fail("only 8 and 16 bit WAV files are played");
            if(rate < 4000 || rate > 96000) return fail("the sample rate of the WAV file is not between 4000 and 96000");
            const size_t bytes_per = static_cast<size_t>(bits / 8) * static_cast<size_t>(channels);
            size_t frames = len / bytes_per;
            frames = std::min<size_t>(frames, static_cast<size_t>(rate) * kSoundMaxMs / 1000);
            if(frames == 0) return fail("the WAV file has no sound in it");
            out = Pcm();
            out.rate = static_cast<int>(rate);
            out.channels = channels;
            out.samples.resize(frames * static_cast<size_t>(channels));
            const unsigned char* s = d + body;
            for(size_t i = 0; i < out.samples.size(); i++)
                out.samples[i] = bits == 8 ? static_cast<int16_t>((static_cast<int>(s[i]) - 128) * 256) : static_cast<int16_t>(rd16(s + 2 * i));
            out.pattern = { std::min(out.duration_ms(), 1000) };  // a buzzer: one beep as long as the sound, a second at most
            return true;
        }
        pos = body + len + (len & 1);                             // chunks are padded to even lengths
    }
    return fail("the WAV file has no sound in it");
}

std::string wav_encode(const Pcm& pcm)
{
    const uint32_t data = static_cast<uint32_t>(pcm.samples.size() * 2);
    std::string s;
    s.reserve(44 + data);
    s += "RIFF"; wr32(s, 36 + data); s += "WAVE";
    s += "fmt "; wr32(s, 16); wr16(s, 1); wr16(s, static_cast<uint16_t>(pcm.channels)); wr32(s, static_cast<uint32_t>(pcm.rate));
    wr32(s, static_cast<uint32_t>(pcm.rate * pcm.channels * 2)); wr16(s, static_cast<uint16_t>(pcm.channels * 2)); wr16(s, 16);
    s += "data"; wr32(s, data);
    for(int16_t v : pcm.samples) wr16(s, static_cast<uint16_t>(v));
    return s;
}

std::vector<std::string> sound_builtin_names() { return { "click", "beep", "alarm" }; }

bool sound_builtin(const std::string& name, Pcm& out)
{
    Pcm p;
    if(name == "click") tone(p, 1500, 25);
    else if(name == "beep") tone(p, 1000, 150);
    else if(name == "alarm") { tone(p, 1200, 180); tone(p, 0, 70); tone(p, 900, 180); tone(p, 0, 70); tone(p, 1200, 180); tone(p, 0, 70); tone(p, 900, 180); }
    else return false;
    out = std::move(p);
    return true;
}

void pcm_scale(Pcm& pcm, int volume)
{
    volume = std::clamp(volume, 0, 100);
    if(volume == 100) return;
    // (what the ear takes for "half as loud" is far less than half the amplitude)
    const double f = volume == 0 ? 0.0 : std::pow(volume / 100.0, 2.0);
    for(int16_t& v : pcm.samples) v = static_cast<int16_t>(v * f);
}

bool sound_file_name_ok(const std::string& rel)
{
    if(rel.empty() || rel.size() > 200) return false;
    std::string n = rel;
    for(char& c : n) { if(c == '\\') c = '/'; if(static_cast<unsigned char>(c) < 0x20) return false; }
    if(n[0] == '/' || n.find(':') != std::string::npos) return false;
    std::stringstream ss(n);
    std::string part;
    while(std::getline(ss, part, '/')) if(part == ".." || part == ".") return false;
    if(n.size() < 5) return false;
    std::string ext = n.substr(n.size() - 4);
    for(char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".wav";
}

// ================================================================ Sound

struct Sound::Impl {
    bool configured = false, started = false;
    std::string output = "auto", device = "default", chip = "/dev/gpiochip0";
    int  gpio = -1;
    bool gpio_low = false;
    bool mute = false, base_mute = false;
    int  volume = 80, base_volume = 80;
    std::shared_ptr<Pcm> touch, alarm;
    uint32_t alarm_every_ms = 3000;
    std::string dir, store, error;
    std::unique_ptr<SoundOutput> out;
    std::map<std::string, std::shared_ptr<Pcm>> cache;
    std::function<uint32_t()> clock;

    bool busy = false;
    uint32_t busy_until = 0;
    int  busy_prio = 0;
    size_t seen_unacked = 0;
    bool alarm_on = false, silenced = false;
    uint32_t next_alarm = 0;
    bool dirty = false, updating = false;
    uint32_t dirty_at = 0;

    uint32_t now() const { return clock ? clock() : millis(); }
    static bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

    void note(const std::string& e)
    {
        if(e == error) return;                  // (the same trouble is logged once)
        error = e;
        if(!e.empty()) HML_WARN("sound: %s", e.c_str());
    }

    // a built-in sound, or a WAV file of the project
    std::shared_ptr<Pcm> load(const std::string& name, std::string* why)
    {
        auto it = cache.find(name);
        if(it != cache.end()) return it->second;
        auto p = std::make_shared<Pcm>();
        if(!sound_builtin(name, *p)) {
            if(!sound_file_name_ok(name)) { if(why) *why = "\"" + name + "\" is no built-in sound and no WAV file inside the project folder"; return nullptr; }
            std::string err;
            const auto bytes = read_file((std::filesystem::u8path(dir) / std::filesystem::u8path(name)).generic_u8string(), kSoundFileMax, &err);
            if(!bytes) { if(why) *why = "\"" + name + "\": " + (err.empty() ? "cannot be read" : err); return nullptr; }
            if(!wav_parse(*bytes, *p, &err)) { if(why) *why = "\"" + name + "\": " + err; return nullptr; }
        }
        if(cache.size() >= kCacheMax) cache.clear();
        cache[name] = p;
        return p;
    }

    // "touch" / "alarm" of the section: a name, false, or nothing (the built-in one)
    std::shared_ptr<Pcm> configured_sound(const json* section, const char* key, const char* fallback)
    {
        std::string name = fallback;
        if(const json* v = section ? jget(*section, key) : nullptr) {
            if(v->is_boolean()) { if(!v->get<bool>()) return nullptr; }
            else if(v->is_string()) { name = v->get<std::string>(); if(name.empty()) return nullptr; }
            else { HML_WARN("sound: \"%s\" is a name of a sound, or false", key); return nullptr; }
        }
        std::string why;
        auto p = load(name, &why);
        if(!p) HML_WARN("sound: %s: %s", key, why.c_str());
        return p;
    }

    void open_output()
    {
        out.reset();
        if(!configured || !started) return;
        std::string kind = output;
        if(kind == "auto") kind = gpio >= 0 ? "gpio" : "audio";
        std::string err;
        if(kind == "none") { }
        else if(kind == "gpio") out = make_gpio_output(chip, gpio, gpio_low, &err);
        else if(kind == "audio") out = make_audio_output(device, &err);
        else err = "output: \"" + output + "\" is not auto, audio, gpio or none";
        if(out) HML_INFO("sound: output %s, volume %d%s", out->name().c_str(), volume, mute ? " (muted)" : "");
        else if(kind != "none") HML_WARN("sound: no output (%s): the device stays silent", err.empty() ? "none here" : err.c_str());
    }

    bool start(const std::shared_ptr<Pcm>& p, int prio)
    {
        if(!configured || !p || !out) return false;
        if(mute && prio != Test) return false;
        const uint32_t t = now();
        if(busy && !reached(t, busy_until) && prio < busy_prio) return false;
        std::string err;
        bool ok;
        if(volume >= 100) ok = out->start(*p, &err);
        else { Pcm scaled = *p; pcm_scale(scaled, volume); ok = out->start(scaled, &err); }
        if(!ok) { note(err.empty() ? "the sound could not be played" : err); return false; }
        note("");
        busy = true;
        busy_until = t + static_cast<uint32_t>(p->duration_ms());
        busy_prio = prio;
        return true;
    }

    void halt()
    {
        if(out) out->stop();
        busy = false;
    }

    void set_tag(const char* name, double v, const void* src)
    {
        if(Tag* t = tags().find(name)) { updating = true; t->set_number(v, src); updating = false; }
    }

    void alarm_off()
    {
        if(!alarm_on) return;
        alarm_on = false;
        if(busy && busy_prio == Alarm) halt();
        set_tag(kAlarmTag, 0, this);
    }

    std::string save()
    {
        dirty = false;
        if(store.empty()) return "";
        // only what differs from the project is kept (a later deploy of the project still counts)
        json overlay = json::object();
        {
            std::ifstream f(std::filesystem::u8path(store), std::ios::binary);
            if(f) {
                std::stringstream ss;
                ss << f.rdbuf();
                overlay = json::parse(ss.str(), nullptr, false, true);
                if(!overlay.is_object()) overlay = json::object();
            }
        }
        json mine = json::object();
        if(mute != base_mute) mine["mute"] = mute;
        if(volume != base_volume) mine["volume"] = volume;
        if(mine.empty()) overlay.erase("sound");
        else overlay["sound"] = mine;
        if(overlay.empty()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::u8path(store), ec);
            return "";
        }
        std::string err;
        return write_file_atomic(store, overlay.dump(2) + "\n", &err) ? "" : err;
    }
};

namespace {

// A touch that was a click on something that takes clicks. (A panel is clickable for LVGL too:
// the bare ones, with nothing to do on a click, stay silent.)
void on_indev_click(lv_event_t* e)
{
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_param(e));
    if(!obj || !lv_obj_is_valid(obj)) return;
    if(lv_obj_get_class(obj) == &lv_obj_class && lv_obj_get_event_count(obj) == 0) return;
    sound().touch();
}

void attach_indevs()
{
    if(!lv_is_initialized()) return;
    for(lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i)) {
        if(lv_indev_get_type(i) != LV_INDEV_TYPE_POINTER) continue;
        bool have = false;
        for(uint32_t k = 0; k < lv_indev_get_event_count(i) && !have; k++) have = lv_event_dsc_get_cb(lv_indev_get_event_dsc(i, k)) == on_indev_click;
        if(!have) lv_indev_add_event_cb(i, on_indev_click, LV_EVENT_CLICKED, nullptr);
    }
}

} // namespace

Sound::Sound() : impl_(std::make_unique<Impl>()) {}
Sound::~Sound() = default;

void Sound::clear()
{
    Impl& m = *impl_;
    if(m.dirty) m.save();
    tags().unsubscribe_group(SubGroup::Sound);
    if(m.out) m.out->stop();
    const bool started = m.started;
    auto clock = m.clock;
    *impl_ = Impl();
    impl_->started = started;
    impl_->clock = clock;
}

void Sound::configure(const json* section, const json* local, const std::string& project_dir, const std::string& store)
{
    clear();
    Impl& m = *impl_;
    if(!section || !section->is_object()) return;
    m.configured = true;
    m.dir = project_dir;
    m.store = store;
    m.output = jstr(*section, "output", "auto");
    m.device = jstr(*section, "device", "default");
    // (a device name goes to ALSA as it is: letters, digits and the few signs its names have)
    if(m.device.empty() || m.device.size() > 64 || m.device.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-:,.=") != std::string::npos) {
        HML_WARN("sound: \"device\" is not the name of an ALSA device: the default one is used");
        m.device = "default";
    }
    m.chip = jstr(*section, "gpio_chip", "/dev/gpiochip0");
    if(m.chip.rfind("/dev/gpiochip", 0) != 0 || m.chip.size() > 20 || m.chip.find_first_not_of("0123456789", 13) != std::string::npos || m.chip.size() == 13) {
        HML_WARN("sound: \"gpio_chip\" is /dev/gpiochipN");
        m.chip = "/dev/gpiochip0";
    }
    const double g = jnum(*section, "gpio", -1);
    m.gpio = g >= 0 && g <= 511 && g == std::floor(g) ? static_cast<int>(g) : -1;
    if(jhas(*section, "gpio") && m.gpio < 0) HML_WARN("sound: \"gpio\" is the number of a GPIO line (0..511)");
    m.gpio_low = jbool(*section, "gpio_active_low", false);
    m.base_volume = static_cast<int>(std::clamp(jnum(*section, "volume", 80), 0.0, 100.0));
    m.base_mute = jbool(*section, "mute", false);
    m.volume = m.base_volume;
    m.mute = m.base_mute;
    if(local && local->is_object()) {
        if(const json* v = jget(*local, "volume"); v && v->is_number()) m.volume = static_cast<int>(std::clamp(v->get<double>(), 0.0, 100.0));
        if(const json* v = jget(*local, "mute"); v && v->is_boolean()) m.mute = v->get<bool>();
    }
    const double every = jnum(*section, "alarm_every_s", 3);
    m.alarm_every_ms = every > 0 ? static_cast<uint32_t>(std::clamp(every, 0.2, 3600.0) * 1000) : 0;
    m.touch = m.configured_sound(section, "touch", "click");
    m.alarm = m.configured_sound(section, "alarm", "alarm");
    m.seen_unacked = alarms().unacked_count();           // (what is unacknowledged at the start does not sound)

    auto make = [&](const char* name, TagType type, double v, bool limits, double lo, double hi) {
        Tag* t = tags().find(name);
        Value init = Value::number(v, type);
        if(!t) t = tags().create(name, type, &init, limits, lo, hi);
        else t->set_number(v, this);
        return t;
    };
    if(Tag* t = make(kMuteTag, TagType::Bool, m.mute ? 1 : 0, false, 0, 0))
        t->subscribe(SubGroup::Sound, [this](Tag&, const Value& v, const void* src) { if(src != this && !impl_->updating) set(v.truthy(), impl_->volume); });
    if(Tag* t = make(kVolumeTag, TagType::Int, m.volume, true, 0, 100))
        t->subscribe(SubGroup::Sound, [this](Tag&, const Value& v, const void* src) { if(src != this && !impl_->updating) set(impl_->mute, static_cast<int>(std::clamp(v.as_number(), 0.0, 100.0))); });
    if(Tag* t = make(kAlarmTag, TagType::Bool, 0, false, 0, 0))
        t->subscribe(SubGroup::Sound, [this](Tag&, const Value& v, const void* src) {
            if(src == this || impl_->updating) return;
            if(!v.truthy()) silence();
            else if(!impl_->alarm_on) impl_->set_tag(kAlarmTag, 0, this);       // (it cannot be switched on from outside)
        });
    m.open_output();
    attach_indevs();
}

void Sound::start()
{
    impl_->started = true;
    impl_->open_output();
    attach_indevs();
}

bool Sound::configured() const { return impl_->configured; }
bool Sound::mute() const { return impl_->mute; }
int  Sound::volume() const { return impl_->volume; }
bool Sound::alarm_sounding() const { return impl_->alarm_on; }
std::string Sound::output_name() const { return impl_->out ? impl_->out->name() : "none"; }
std::string Sound::last_error() const { return impl_->error; }
void Sound::set_clock(std::function<uint32_t()> now_ms) { impl_->clock = std::move(now_ms); }

void Sound::set_output(std::unique_ptr<SoundOutput> out)
{
    impl_->out = std::move(out);
    impl_->busy = false;
}

bool Sound::play(const std::string& name, int priority)
{
    Impl& m = *impl_;
    if(!m.configured) return false;
    std::string why;
    auto p = m.load(name, &why);
    if(!p) { m.note(why); return false; }
    return m.start(p, priority);
}

void Sound::stop() { impl_->halt(); }
void Sound::touch() { impl_->start(impl_->touch, Touch); }

void Sound::silence()
{
    Impl& m = *impl_;
    if(m.alarm_on) HML_INFO("sound: the alarm sound is silenced");
    m.silenced = true;
    m.alarm_off();
}

std::string Sound::set(bool mute, int volume)
{
    Impl& m = *impl_;
    volume = std::clamp(volume, 0, 100);
    if(mute == m.mute && volume == m.volume) return "";
    const bool muted_now = mute && !m.mute;
    m.mute = mute;
    m.volume = volume;
    if(muted_now) m.halt();
    m.set_tag(kMuteTag, mute ? 1 : 0, this);
    m.set_tag(kVolumeTag, volume, this);
    m.dirty = true;
    m.dirty_at = m.now();
    return "";
}

void Sound::poll()
{
    Impl& m = *impl_;
    if(!m.configured) return;
    attach_indevs();
    const uint32_t t = m.now();
    if(m.busy && Impl::reached(t, m.busy_until)) m.busy = false;
    if(m.dirty && Impl::reached(t, m.dirty_at + kSaveAfterMs)) {
        const std::string err = m.save();
        if(!err.empty()) HML_WARN("sound: the settings could not be saved: %s", err.c_str());
    }
    // a new alarm that waits for its acknowledgement sounds, again and again, until all are acknowledged
    const size_t unacked = alarms().unacked_count();
    if(unacked > m.seen_unacked && m.alarm) {
        m.silenced = false;
        if(!m.alarm_on) { m.alarm_on = true; m.next_alarm = t; m.set_tag(kAlarmTag, 1, this); }
    }
    m.seen_unacked = unacked;
    if(m.alarm_on && unacked == 0) m.alarm_off();
    if(m.alarm_on && Impl::reached(t, m.next_alarm)) {
        m.start(m.alarm, Alarm);
        if(m.alarm_every_ms == 0) m.next_alarm = t + 0x7FFFFFFFu;                 // once
        else m.next_alarm = t + std::max<uint32_t>(m.alarm_every_ms, static_cast<uint32_t>(m.alarm->duration_ms()) + 100);
    }
}

Sound& sound()
{
    static Sound instance;
    return instance;
}

void register_sound_actions()
{
    ui().register_action("sound.play", [](const std::string& arg) { sound().play(arg.empty() ? "beep" : arg); });
    ui().register_action("sound.stop", [](const std::string&) { sound().stop(); });
    ui().register_action("sound.silence", [](const std::string&) { sound().silence(); });
}

// ================================================================ System > Sound

namespace {

class SoundPage final : public SettingsPage {
public:
    std::string id() const override { return "soundcfg"; }
    std::string title() const override { return "Sound"; }
    bool has_apply() const override { return false; }

    std::vector<SettingField> fields() override
    {
        std::vector<SettingField> out;
        auto info = [&](const char* key, const std::string& value) {
            SettingField f; f.key = key; f.label = key; f.type = SettingField::Type::Info; f.value = value; out.push_back(f);
        };
        shown_output_ = sound().output_name();
        shown_error_ = sound().last_error();
        info("output", shown_output_);
        info("state", !status_.empty() ? status_ : shown_error_);
        return out;
    }
    std::string apply(const std::map<std::string, std::string>&) override { return ""; }

    void command(const std::string& cmd, const std::string&) override
    {
        if(cmd == "test") {
            // (also while muted: this is how one hears what the volume is)
            const bool ok = sound().play("beep", Sound::Test);
            status_ = ok ? "" : sound().output_name() == "none" ? "This device has no sound output." : "The sound could not be played.";
        }
        else if(cmd == "silence") sound().silence();
        else HML_WARN("system: sound: unknown command '%s'", cmd.c_str());
    }

    // the output is opened after the pages are made, and can fail later: the page follows
    bool poll() override { return sound().output_name() != shown_output_ || sound().last_error() != shown_error_; }

    std::vector<PageAction> actions() const override
    {
        // ("Silence alarm" is there while the alarm sound is: a tag of the runtime, not of this page)
        return { { "Test", "test", PageAction::Kind::Neutral, "", "" }, { "Silence alarm", "silence", PageAction::Kind::Danger, kAlarmTag, "" } };
    }

    json custom_rows(bool compact, int font) const override
    {
        const char* muted = "#9fb3c8";
        auto row = [&](json children) {
            return json{ { "type", "panel" }, { "w", "100%" }, { "h", "content" }, { "flex", "row" }, { "flex_cross", "center" }, { "scrollable", false },
                         { "style", { { "bg_opa", 0 }, { "border_width", 0 }, { "pad", 4 }, { "pad_column", compact ? 8 : 12 } } }, { "children", std::move(children) } };
        };
        auto text = [&](const char* t, int w) { return json{ { "type", "label" }, { "text", t }, { "w", w }, { "style", { { "font", font }, { "text_color", muted } } } }; };
        const int lw = compact ? 90 : 140;
        json rows = json::array();
        rows.push_back(row(json::array({ text("Mute (no sound)", lw), json{ { "type", "switch" }, { "bind", kMuteTag } } })));
        rows.push_back(row(json::array({ text("Volume", lw),
                                         json{ { "type", "slider" }, { "w", compact ? 250 : 420 }, { "min", 0 }, { "max", 100 }, { "bind", kVolumeTag } },
                                         json{ { "type", "panel" }, { "w", 4 }, { "h", 1 }, { "style", { { "bg_opa", 0 }, { "border_width", 0 } } } },
                                         json{ { "type", "label" }, { "w", compact ? 40 : 56 }, { "bind", { { "text", { { "tag", kVolumeTag }, { "format", "%d" } } } } },
                                               { "style", { { "font", font }, { "text_align", "right" } } } } })));
        rows.push_back(row(json::array({ text("Output", lw), json{ { "type", "label" }, { "flex_grow", 1 }, { "long_mode", "dots" },
                                                                    { "bind", { { "text", SystemSettings::tag_name(id(), "output") } } }, { "style", { { "font", font } } } } })));
        // (Test / Silence alarm: the bar at the bottom)
        rows.push_back(json{ { "type", "label" }, { "w", "100%" }, { "long_mode", "wrap" }, { "bind", { { "text", SystemSettings::tag_name(id(), "state") } } },
                             { "style", { { "font", font }, { "text_color", "#ffb74d" } } } });
        return rows;
    }

private:
    std::string status_, shown_output_, shown_error_;
};

} // namespace

std::unique_ptr<SettingsPage> make_sound_page() { return std::make_unique<SoundPage>(); }

} // namespace hml
