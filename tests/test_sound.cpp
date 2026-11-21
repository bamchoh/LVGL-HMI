// Sound (sound.hpp): the touch sound, the alarm sound, sounds played by actions, mute and volume.
// Grouped by ISO/IEC 25010 characteristic (issue #70). Nothing is heard in these tests: the
// output is a list of what would have been played, the clock is the test's.
#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "alarm.hpp"
#include "check.hpp"
#include "project.hpp"
#include "sound.hpp"
#include "system_settings.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace hml;
using hmltest::Runtime;
namespace fs = std::filesystem;

namespace {

struct Played {
    int ms = 0, peak = 0;
    size_t samples = 0;
    std::vector<int> pattern;
};
struct Log {
    std::vector<Played> played;
    int stops = 0;
    bool fail = false;
};
class FakeOutput final : public SoundOutput {
public:
    explicit FakeOutput(Log* log) : log_(log) {}
    std::string name() const override { return "fake"; }
    bool start(const Pcm& pcm, std::string* err) override
    {
        if(log_->fail) { if(err) *err = "the fake device is broken"; return false; }
        Played p;
        p.ms = pcm.duration_ms();
        p.samples = pcm.samples.size();
        p.pattern = pcm.pattern;
        for(int16_t v : pcm.samples) p.peak = std::max<int>(p.peak, std::abs(v));
        log_->played.push_back(p);
        return true;
    }
    void stop() override { log_->stops++; }

private:
    Log* log_;
};

struct Finger {
    lv_indev_t* indev = nullptr;
    lv_point_t  point{ 0, 0 };
    bool        pressed = false;
    static void read(lv_indev_t* i, lv_indev_data_t* d)
    {
        auto* self = static_cast<Finger*>(lv_indev_get_user_data(i));
        d->point = self->point;
        d->state = self->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    Finger()
    {
        indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, &Finger::read);
        lv_indev_set_user_data(indev, this);
    }
    ~Finger() { lv_indev_delete(indev); }
    void tap(lv_obj_t* obj, Runtime& rt)
    {
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        point = { (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2 };
        pressed = true; rt.advance(60, 10);
        pressed = false; rt.advance(60, 10);
    }
};

struct Fixture {
    Runtime rt;
    Log log;
    uint32_t now = 1000;
    fs::path dir;
    Fixture()
    {
        static std::atomic<int> n{ 0 };
        dir = fs::temp_directory_path() / ("hml_sound_test_" + std::to_string(n++));
        fs::remove_all(dir);
        fs::create_directories(dir / "assets");
    }
    ~Fixture()
    {
        sound().clear();
        sound().set_output(nullptr);
        sound().set_clock(nullptr);
        alarms().clear();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string store() const { return (dir / "project.local.json").generic_string(); }
    void configure(const char* cfg, const json* local = nullptr)
    {
        const json c = json::parse(cfg);
        sound().set_clock([this] { return now; });
        sound().configure(&c, local, dir.generic_string(), store());
        sound().set_output(std::make_unique<FakeOutput>(&log));
    }
    // time goes on in steps of 50 ms, the main loop looks every step
    void run(uint32_t ms) { for(uint32_t t = 0; t < ms; t += 50) { now += 50; sound().poll(); } }
    json local() const
    {
        std::ifstream f(dir / "project.local.json", std::ios::binary);
        if(!f) return json();
        std::stringstream ss;
        ss << f.rdbuf();
        return json::parse(ss.str(), nullptr, false);
    }
    void file(const std::string& rel, const std::string& bytes)
    {
        std::ofstream f(dir / fs::u8path(rel), std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    double num(const char* tag) { return tags().find(tag)->get_number(); }
};

Pcm tone_of(int rate, int channels, int ms, int16_t level)
{
    Pcm p;
    p.rate = rate;
    p.channels = channels;
    p.samples.assign(static_cast<size_t>(rate) * static_cast<size_t>(ms) / 1000 * static_cast<size_t>(channels), level);
    return p;
}
void put32(std::string& s, size_t at, uint32_t v) { for(int i = 0; i < 4; i++) s[at + static_cast<size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF); }
void put16(std::string& s, size_t at, uint16_t v) { for(int i = 0; i < 2; i++) s[at + static_cast<size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF); }

} // namespace

// ================================================================ functional suitability: sounds and files

TEST_CASE("sound: the built-in sounds; the volume makes them quieter")
{
    for(const std::string& n : sound_builtin_names()) {
        CAPTURE(n);
        Pcm p;
        REQUIRE(sound_builtin(n, p));
        CHECK(p.rate == 22050);
        CHECK(p.channels == 1);
        CHECK(p.duration_ms() >= 20);
        CHECK(p.duration_ms() <= 2000);
        CHECK(p.pattern.size() % 2 == 1);                        // a buzzer: on, off, ... on
        int peak = 0;
        for(int16_t v : p.samples) peak = std::max<int>(peak, std::abs(v));
        CHECK(peak > 10000);
        CHECK(p.samples.front() == 0);                           // soft edges: no crack
        CHECK(std::abs(p.samples.back()) < 2000);
    }
    Pcm x;
    CHECK_FALSE(sound_builtin("", x));
    CHECK_FALSE(sound_builtin("Click", x));
    CHECK_FALSE(sound_builtin("../beep", x));
    Pcm p = tone_of(8000, 1, 100, 20000);
    pcm_scale(p, 100);
    CHECK(p.samples[0] == 20000);
    pcm_scale(p, 50);
    CHECK(p.samples[0] == 5000);
    pcm_scale(p, 0);
    CHECK(p.samples[0] == 0);
    p = tone_of(8000, 1, 100, -20000);
    pcm_scale(p, 250);                                           // more than all: all
    CHECK(p.samples[0] == -20000);
    pcm_scale(p, -5);
    CHECK(p.samples[0] == 0);
}

TEST_CASE("sound: a WAV file is read (8 and 16 bit, mono and stereo) and written back")
{
    Pcm in = tone_of(44100, 2, 100, 1234);
    in.samples[1] = -32768;
    in.samples[2] = 32767;
    const std::string wav = wav_encode(in);
    CHECK(wav.size() == 44 + in.samples.size() * 2);
    Pcm out;
    std::string err;
    REQUIRE_MESSAGE(wav_parse(wav, out, &err), err);
    CHECK(out.rate == 44100);
    CHECK(out.channels == 2);
    CHECK(out.samples == in.samples);
    CHECK(out.duration_ms() == 100);
    CHECK(out.pattern == std::vector<int>{ 100 });

    // 8 bit: unsigned, 128 is silence
    std::string w8 = wav_encode(tone_of(8000, 1, 10, 0)).substr(0, 44);
    put16(w8, 32, 1); put16(w8, 34, 8); put32(w8, 28, 8000);
    w8 += std::string("\x80\xFF\x00\x81", 4);
    put32(w8, 40, 4); put32(w8, 4, 36 + 4);
    REQUIRE_MESSAGE(wav_parse(w8, out, &err), err);
    CHECK(out.samples == std::vector<int16_t>{ 0, 127 * 256, -128 * 256, 256 });

    // other chunks before the data (with an odd length: padded), the "extensible" header
    std::string ext = "RIFF____WAVE";
    ext += "LIST"; ext += std::string("\x03\x00\x00\x00", 4); ext += "abc"; ext += '\0';
    ext += "fmt "; ext += std::string("\x28\x00\x00\x00", 4);
    std::string fmt(40, '\0');
    put16(fmt, 0, 0xFFFE); put16(fmt, 2, 1); put32(fmt, 4, 16000); put32(fmt, 8, 32000); put16(fmt, 12, 2); put16(fmt, 14, 16); put16(fmt, 24, 1);
    ext += fmt;
    ext += "data"; ext += std::string("\x04\x00\x00\x00", 4); ext += std::string("\x10\x00\xF0\xFF", 4);
    REQUIRE_MESSAGE(wav_parse(ext, out, &err), err);
    CHECK(out.rate == 16000);
    CHECK(out.samples == std::vector<int16_t>{ 16, -16 });

    // a sound of two minutes is cut to 30 seconds
    Pcm lng = tone_of(8000, 1, 120000, 100);
    REQUIRE(wav_parse(wav_encode(lng), out, &err));
    CHECK(out.duration_ms() == kSoundMaxMs);
    CHECK(out.pattern == std::vector<int>{ 1000 });              // a buzzer beeps a second at most for a file
}

// ================================================================ security / reliability: hostile files

TEST_CASE("sound: what is no playable WAV is refused with a reason, whatever is in it")
{
    const std::string good = wav_encode(tone_of(8000, 1, 50, 1000));
    Pcm out;
    std::string err;
    auto bad = [&](std::string w, const char* what) {
        CAPTURE(what);
        err.clear();
        CHECK_FALSE(wav_parse(w, out, &err));
        CHECK(err != "");
    };
    bad("", "empty");
    bad("RIFF", "four bytes");
    bad(std::string(100, 'x'), "no RIFF");
    bad(good.substr(0, 12), "a header and nothing else");
    bad(good.substr(0, 30), "cut in the format");
    bad(good.substr(0, 44), "no samples");
    { std::string w = good; put16(w, 20, 3); bad(w, "floating point"); }
    { std::string w = good; put16(w, 20, 85); bad(w, "MP3 in a WAV"); }
    { std::string w = good; put16(w, 22, 0); bad(w, "no channels"); }
    { std::string w = good; put16(w, 22, 6); bad(w, "six channels"); }
    { std::string w = good; put16(w, 34, 24); bad(w, "24 bit"); }
    { std::string w = good; put16(w, 34, 0); bad(w, "0 bit"); }
    { std::string w = good; put32(w, 24, 0); bad(w, "rate 0"); }
    { std::string w = good; put32(w, 24, 0xFFFFFFFFu); bad(w, "a rate of four thousand million"); }
    { std::string w = good; std::memcpy(&w[12], "data", 4); bad(w, "data before the format"); }
    { std::string w = good; put32(w, 16, 4); bad(w, "a format of four bytes"); }
    bad(std::string(kSoundFileMax + 1, 'R'), "larger than the limit");
    // lengths that say more than the file has: read as far as the file goes, nothing beyond
    { std::string w = good; put32(w, 40, 0xFFFFFFFFu); REQUIRE(wav_parse(w, out, &err)); CHECK(out.samples.size() == (good.size() - 44) / 2); }
    { std::string w = good; put32(w, 16, 0xFFFFFFF0u); bad(w, "a format chunk of 4 GB"); }
    { std::string w = good.substr(0, 45); REQUIRE_FALSE(wav_parse(w, out, &err)); }                 // half a sample
    // every cut and a thousand files of noise: no crash, no hang
    for(size_t n = 0; n < 60; n++) wav_parse(good.substr(0, n), out, &err);
    uint32_t seed = 12345;
    for(int i = 0; i < 1000; i++) {
        std::string w = good.substr(0, 60);
        for(int k = 0; k < 6; k++) { seed = seed * 1664525u + 1013904223u; w[12 + (seed >> 8) % 48] = static_cast<char>(seed >> 24); }
        if(wav_parse(w, out, &err)) { CHECK(out.channels >= 1); CHECK(out.rate >= 4000); CHECK(out.samples.size() <= 16); }
    }
}

TEST_CASE("sound: a sound file is a .wav inside the project folder")
{
    for(const char* ok : { "a.wav", "assets/beep.wav", "assets\\sub\\x.WAV", "音/警報.wav", "a b/c.wav" }) { CAPTURE(ok); CHECK(sound_file_name_ok(ok)); }
    for(const char* bad : { "", ".wav", "a.mp3", "a.wav.exe", "/etc/passwd", "/tmp/a.wav", "C:/Windows/Media/tada.wav", "C:\\x.wav", "../a.wav", "assets/../../a.wav", "assets/..\\..\\a.wav",
                            "./a.wav", "a.wav\n", "\\\\server\\share\\a.wav", "file:a.wav", "..", "wav" }) {
        CAPTURE(bad);
        CHECK_FALSE(sound_file_name_ok(bad));
    }
    CHECK_FALSE(sound_file_name_ok(std::string(300, 'a') + ".wav"));

    Fixture f;
    f.file("assets/ok.wav", wav_encode(tone_of(8000, 1, 40, 3000)));
    f.file("assets/broken.wav", "RIFFxxxxWAVEjunk");
    f.file("secret.txt", "not for ears");
    { std::ofstream o(f.dir.parent_path() / "hml_sound_outside.wav", std::ios::binary); const std::string w = wav_encode(tone_of(8000, 1, 40, 3000)); o.write(w.data(), static_cast<std::streamsize>(w.size())); }
    f.configure(R"({ "volume": 100, "touch": false, "alarm": false })");
    CHECK(sound().play("assets/ok.wav"));
    REQUIRE(f.log.played.size() == 1);
    CHECK(f.log.played[0].ms == 40);
    CHECK(f.log.played[0].peak == 3000);
    f.run(100);
    for(const char* bad : { "assets/broken.wav", "assets/none.wav", "secret.txt", "../hml_sound_outside.wav", "assets/../../hml_sound_outside.wav", "nothing", "" }) {
        CAPTURE(bad);
        CHECK_FALSE(sound().play(bad));
        CHECK(sound().last_error() != "");
    }
    CHECK(f.log.played.size() == 1);
    CHECK(sound().play("beep"));
    CHECK(sound().last_error() == "");
    std::error_code ec;
    fs::remove(f.dir.parent_path() / "hml_sound_outside.wav", ec);
}

// ================================================================ functional suitability: touch

TEST_CASE("sound: a touch on something that takes it sounds; a bare panel, a drag and a disabled button do not")
{
    Fixture f;
    Tag* count = f.rt.tag("count", TagType::Int, 0);
    f.configure(R"({ "volume": 100 })");
    Finger finger;
    const json scr = json::parse(R"~({ "name": "main", "children": [
        { "type": "button", "x": 10, "y": 10, "w": 120, "h": 60, "text": "+1", "on_click": { "action": "inc", "tag": "count" } },
        { "type": "panel", "x": 10, "y": 90, "w": 120, "h": 60 },
        { "type": "button", "x": 150, "y": 10, "w": 120, "h": 60, "text": "off", "on_click": { "action": "inc", "tag": "count" } },
        { "type": "switch", "x": 150, "y": 90 },
        { "type": "label", "x": 10, "y": 170, "text": "only words" },
        { "type": "panel", "x": 160, "y": 130, "w": 150, "h": 100, "children": [
            { "type": "button", "x": 0, "y": 0, "w": 100, "h": 70, "text": "in", "on_click": { "action": "inc", "tag": "count" } },
            { "type": "label", "x": 0, "y": 500, "text": "far down" } ] } ] })~");
    REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen("main", "none"));
    f.rt.advance(50);
    lv_obj_update_layout(lv_screen_active());
    sound().poll();                                              // (the touch panel came after the sound was set up)
    lv_obj_t* s = lv_screen_active();
    lv_obj_add_state(lv_obj_get_child(s, 2), LV_STATE_DISABLED);

    finger.tap(lv_obj_get_child(s, 0), f.rt);
    CHECK(count->get_number() == 1);
    REQUIRE(f.log.played.size() == 1);
    CHECK(f.log.played[0].ms < 30);                              // the click
    f.run(100);
    finger.tap(lv_obj_get_child(s, 1), f.rt);                    // a panel with nothing to do
    CHECK(count->get_number() == 1);
    CHECK(f.log.played.size() == 1);
    finger.tap(lv_obj_get_child(s, 4), f.rt);                    // a text
    CHECK(count->get_number() == 1);
    CHECK(f.log.played.size() == 1);
    finger.point = { 300, 60 }; finger.pressed = true; f.rt.advance(60); finger.pressed = false; f.rt.advance(60);      // the empty screen
    CHECK(f.log.played.size() == 1);
    finger.tap(lv_obj_get_child(s, 2), f.rt);                    // disabled
    CHECK(count->get_number() == 1);
    CHECK(f.log.played.size() == 1);
    finger.tap(lv_obj_get_child(s, 3), f.rt);                    // a switch
    CHECK(f.log.played.size() == 2);
    f.run(100);
    // a list is dragged with the finger on a button in it: that scrolls, it is no click, no sound
    lv_area_t a;
    lv_obj_get_coords(lv_obj_get_child(lv_obj_get_child(s, 5), 0), &a);
    finger.point = { a.x1 + 30, a.y1 + 55 }; finger.pressed = true; f.rt.advance(40);
    for(int i = 0; i < 10; i++) { finger.point.y -= 5; f.rt.advance(20); }
    finger.pressed = false; f.rt.advance(60);
    CHECK(lv_obj_get_scroll_y(lv_obj_get_child(s, 5)) > 0);
    CHECK(count->get_number() == 1);
    CHECK(f.log.played.size() == 2);

    // "touch": false - silent; a file of the project - that file
    f.log.played.clear();
    f.configure(R"({ "touch": false })");
    finger.tap(lv_obj_get_child(s, 0), f.rt);
    CHECK(count->get_number() == 2);
    CHECK(f.log.played.empty());
    f.file("assets/tap.wav", wav_encode(tone_of(8000, 1, 60, 5000)));
    f.configure(R"({ "touch": "assets/tap.wav", "volume": 100 })");
    finger.tap(lv_obj_get_child(s, 0), f.rt);
    REQUIRE(f.log.played.size() == 1);
    CHECK(f.log.played[0].ms == 60);
    // a file that is not there: the device works, without that sound
    f.log.played.clear();
    f.configure(R"({ "touch": "assets/gone.wav" })");
    finger.tap(lv_obj_get_child(s, 0), f.rt);
    CHECK(count->get_number() == 4);
    CHECK(f.log.played.empty());
    ui().unload_all();
}

// ================================================================ functional suitability: alarms

TEST_CASE("sound: a new alarm sounds again and again until it is acknowledged; it can be silenced")
{
    Fixture f;
    Tag* hot = f.rt.tag("hot", TagType::Bool, 0);
    Tag* low = f.rt.tag("low", TagType::Bool, 0);
    const json a = json::parse(R"~({ "items": [ { "name": "h", "tag": "hot" }, { "name": "l", "tag": "low" } ] })~");
    REQUIRE(alarms().load(&a, f.dir.generic_string()) == 2);
    f.configure(R"({ "volume": 100, "alarm_every_s": 2 })");
    f.run(5000);
    CHECK(f.log.played.empty());
    CHECK(f.num("sys.sound.alarm") == 0);

    hot->set_number(1, nullptr); f.rt.pump();
    f.run(100);
    REQUIRE(f.log.played.size() == 1);
    CHECK(f.log.played[0].ms > 500);                             // the alarm sound, not the click
    CHECK(f.log.played[0].pattern.size() == 7);
    CHECK(sound().alarm_sounding());
    CHECK(f.num("sys.sound.alarm") == 1);
    f.run(1800);
    CHECK(f.log.played.size() == 1);
    f.run(300);
    CHECK(f.log.played.size() == 2);                             // two seconds later: again
    f.run(4000);
    CHECK(f.log.played.size() == 4);
    // the cause is gone but nobody has acknowledged: it goes on
    hot->set_number(0, nullptr); f.rt.pump();
    f.run(2000);
    CHECK(f.log.played.size() == 5);
    // a touch does not cut the alarm sound short; a test of the sound does
    const size_t before = f.log.played.size();
    f.run(2000 - 100);
    f.run(150);
    REQUIRE(f.log.played.size() == before + 1);
    sound().touch();
    CHECK_FALSE(sound().play("beep"));
    CHECK(f.log.played.size() == before + 1);
    CHECK(sound().play("beep", Sound::Test));
    f.run(1000);
    // acknowledged: silent at once
    const int stops = f.log.stops;
    f.run(1200);                                                 // (the alarm sound has started again and is sounding)
    const size_t n = f.log.played.size();
    CHECK(alarms().ack_all("op") == 1);
    f.run(100);
    CHECK(f.log.stops == stops + 1);
    CHECK_FALSE(sound().alarm_sounding());
    CHECK(f.num("sys.sound.alarm") == 0);
    f.run(10000);
    CHECK(f.log.played.size() == n);

    // silenced by the operator (the tag, as a button on a screen writes it): until the next new alarm
    hot->set_number(1, nullptr); f.rt.pump();
    f.run(100);
    CHECK(f.log.played.size() == n + 1);
    tags().find("sys.sound.alarm")->set_number(0, nullptr); f.rt.pump();
    CHECK_FALSE(sound().alarm_sounding());
    f.run(10000);
    CHECK(f.log.played.size() == n + 1);
    low->set_number(1, nullptr); f.rt.pump();                    // another alarm: it sounds again
    f.run(100);
    CHECK(f.log.played.size() == n + 2);
    CHECK(sound().alarm_sounding());
    // it cannot be switched on from outside
    sound().silence();
    tags().find("sys.sound.alarm")->set_number(1, nullptr); f.rt.pump();
    CHECK(f.num("sys.sound.alarm") == 0);
    f.run(5000);
    CHECK(f.log.played.size() == n + 2);
    alarms().ack_all("op");
}

TEST_CASE("sound: alarm_every_s 0 sounds once; \"alarm\": false never; alarms that waited at the start do not sound")
{
    Fixture f;
    Tag* hot = f.rt.tag("hot", TagType::Bool, 0);
    Tag* low = f.rt.tag("low", TagType::Bool, 1);
    const json a = json::parse(R"~({ "items": [ { "name": "h", "tag": "hot" }, { "name": "l", "tag": "low" } ] })~");
    REQUIRE(alarms().load(&a, f.dir.generic_string()) == 2);
    f.rt.pump();
    REQUIRE(alarms().unacked_count() == 1);                      // "low" is there before the sound is set up (a reload of the project)
    f.configure(R"({ "alarm_every_s": 0 })");
    f.run(5000);
    CHECK(f.log.played.empty());
    hot->set_number(1, nullptr); f.rt.pump();
    f.run(20000);
    CHECK(f.log.played.size() == 1);
    CHECK(sound().alarm_sounding());                             // (still to be acknowledged)
    alarms().ack_all("op");
    f.run(100);
    CHECK_FALSE(sound().alarm_sounding());

    f.log.played.clear();
    hot->set_number(0, nullptr); f.rt.pump();
    f.configure(R"({ "alarm": false })");
    hot->set_number(1, nullptr); f.rt.pump();
    f.run(5000);
    CHECK(f.log.played.empty());
    CHECK_FALSE(sound().alarm_sounding());
    // a pause shorter than the sound: the sound is not cut by itself
    alarms().ack_all("op");
    hot->set_number(0, nullptr); f.rt.pump();
    f.configure(R"({ "alarm_every_s": 0.2 })");
    hot->set_number(1, nullptr); f.rt.pump();
    f.run(3000);
    Pcm al;
    REQUIRE(sound_builtin("alarm", al));
    CHECK(f.log.played.size() <= static_cast<size_t>(3000 / al.duration_ms()) + 1);
    CHECK(f.log.played.size() >= 2);
    alarms().ack_all("op");
}

// ================================================================ usability: mute and volume

TEST_CASE("sound: mute and volume - by tag, kept in project.local.json, back to the project's values leaves nothing")
{
    Fixture f;
    Tag* hot = f.rt.tag("hot", TagType::Bool, 0);
    const json a = json::parse(R"~({ "items": [ { "name": "h", "tag": "hot" } ] })~");
    REQUIRE(alarms().load(&a, f.dir.generic_string()) == 1);
    f.configure(R"({ "volume": 50 })");
    CHECK(sound().volume() == 50);
    CHECK_FALSE(sound().mute());
    CHECK(f.num("sys.sound.volume") == 50);
    CHECK(f.num("sys.sound.mute") == 0);
    CHECK(sound().play("beep"));
    Pcm beep;
    REQUIRE(sound_builtin("beep", beep));
    int full = 0;
    for(int16_t v : beep.samples) full = std::max<int>(full, std::abs(v));
    CHECK(f.log.played[0].peak < full / 3);                      // 50 %: a quarter of the amplitude
    CHECK(f.log.played[0].peak > full / 5);
    f.run(500);

    // the slider of the page is dragged: many values, one write at the end
    for(int v = 51; v <= 90; v++) { tags().find("sys.sound.volume")->set_number(v, nullptr); f.rt.pump(); f.run(50); }
    CHECK(sound().volume() == 90);
    CHECK_FALSE(fs::exists(f.store()));
    f.run(2000);
    CHECK(f.local()["sound"] == json{ { "volume", 90 } });
    // muted: nothing sounds, an alarm that sounds stops; the test button of the page is still heard
    hot->set_number(1, nullptr); f.rt.pump();
    f.run(100);
    const size_t n = f.log.played.size();
    const int stops = f.log.stops;
    tags().find("sys.sound.mute")->set_number(1, nullptr); f.rt.pump();
    CHECK(sound().mute());
    CHECK(f.log.stops == stops + 1);
    f.run(8000);
    CHECK_FALSE(sound().play("beep"));
    sound().touch();
    CHECK(f.log.played.size() == n);
    CHECK(sound().alarm_sounding());                             // (the alarm still waits: it is heard again when the mute goes)
    CHECK(sound().play("beep", Sound::Test));
    CHECK(f.local()["sound"] == json{ { "volume", 90 }, { "mute", true } });
    f.run(500);
    tags().find("sys.sound.mute")->set_number(0, nullptr); f.rt.pump();
    f.run(3100);
    CHECK(f.log.played.size() >= n + 2);
    alarms().ack_all("op");

    // the next start has them
    const json loc = f.local()["sound"];
    f.run(2000);
    f.configure(R"({ "volume": 50 })", &loc);
    CHECK(sound().volume() == 90);
    CHECK(f.num("sys.sound.volume") == 90);
    // out of range is the nearest that is in it
    tags().find("sys.sound.volume")->set_number(100, nullptr); f.rt.pump();
    CHECK(sound().set(false, 500) == "");
    CHECK(sound().volume() == 100);
    CHECK(sound().set(false, -3) == "");
    CHECK(sound().volume() == 0);
    CHECK(f.num("sys.sound.volume") == 0);
    // back to what the project says: nothing is left in the file
    sound().set(false, 50);
    f.run(2000);
    CHECK_FALSE(fs::exists(f.store()));
    // other settings in the file are left alone
    f.file("project.local.json", R"({ "screen_saver": { "after_s": 30 }, "sound": { "volume": 90 } })");
    sound().set(true, 50);
    f.run(2000);
    CHECK(f.local() == json::parse(R"({ "screen_saver": { "after_s": 30 }, "sound": { "mute": true } })"));
    // a change that is not yet written is written when the project is unloaded
    sound().set(true, 70);
    sound().clear();
    CHECK(f.local()["sound"] == json{ { "mute", true }, { "volume", 70 } });
    // nonsense in the local file changes nothing
    const json junk = json::parse(R"({ "volume": "loud", "mute": 7, "output": "gpio", "gpio": 4, "touch": "/etc/passwd", "device": "x; rm -rf /" })");
    f.configure(R"({ "volume": 50 })", &junk);
    CHECK(sound().volume() == 50);
    CHECK_FALSE(sound().mute());
    const json huge = json::parse(R"({ "volume": 1e300 })");
    f.configure(R"({ "volume": 50 })", &huge);
    CHECK(sound().volume() == 100);
}

// ================================================================ reliability / portability: no output, a broken one

TEST_CASE("sound: without \"sound\" nothing sounds; a device without a sound output works as before")
{
    Fixture f;
    Tag* hot = f.rt.tag("hot", TagType::Bool, 0);
    const json a = json::parse(R"~({ "items": [ { "name": "h", "tag": "hot" } ] })~");
    REQUIRE(alarms().load(&a, f.dir.generic_string()) == 1);
    // no section: no tags, no sound, the actions do nothing
    sound().configure(nullptr, nullptr, f.dir.generic_string(), f.store());
    sound().set_output(std::make_unique<FakeOutput>(&f.log));
    CHECK_FALSE(sound().configured());
    CHECK(tags().find("sys.sound.mute") == nullptr);
    hot->set_number(1, nullptr); f.rt.pump();
    for(int i = 0; i < 20; i++) sound().poll();
    sound().touch();
    CHECK_FALSE(sound().play("beep"));
    CHECK(f.log.played.empty());
    const json not_object = json::parse("[1, 2]");
    sound().configure(&not_object, nullptr, f.dir.generic_string(), f.store());
    CHECK_FALSE(sound().configured());
    alarms().ack_all("op");
    hot->set_number(0, nullptr); f.rt.pump();

    // a section, but no output (a device with nothing to make a sound): all runs, silently
    const json c = json::parse(R"({ "alarm_every_s": 1 })");
    sound().set_clock([&f] { return f.now; });
    sound().configure(&c, nullptr, f.dir.generic_string(), f.store());
    CHECK(sound().configured());
    CHECK(sound().output_name() == "none");
    hot->set_number(1, nullptr); f.rt.pump();
    f.run(5000);
    sound().touch();
    CHECK_FALSE(sound().play("beep"));
    CHECK_FALSE(sound().play("beep", Sound::Test));
    CHECK(sound().alarm_sounding());                             // the tag still tells a lamp on the screen
    CHECK(f.num("sys.sound.alarm") == 1);
    sound().silence();
    CHECK(f.num("sys.sound.alarm") == 0);
    alarms().ack_all("op");

    // an output that fails every time: said once, tried again the next time, nothing else suffers
    sound().set_output(std::make_unique<FakeOutput>(&f.log));
    f.log.fail = true;
    for(int i = 0; i < 50; i++) { CHECK_FALSE(sound().play("beep")); f.run(200); }
    CHECK(sound().last_error() == "the fake device is broken");
    f.log.fail = false;
    CHECK(sound().play("beep"));
    CHECK(sound().last_error() == "");

    // settings that are nonsense: defaults, no crash
    for(const char* cfg : { R"({ "output": "loudspeaker" })", R"({ "volume": "x", "mute": "y", "touch": 5, "alarm": [], "alarm_every_s": "often" })",
                            R"({ "gpio": -4, "gpio_chip": "/etc/passwd", "device": "hw:0;reboot" })", R"({ "gpio": 1e9, "gpio_chip": "/dev/gpiochip", "device": "" })",
                            R"({ "alarm_every_s": -1, "volume": 1e30 })", R"({ "touch": "", "alarm": "" })", R"({ "touch": true, "alarm": true })" }) {
        CAPTURE(cfg);
        f.configure(cfg);
        CHECK(sound().configured());
        sound().touch();
        f.run(300);
    }
    // the real outputs with what cannot be opened: no output, a reason, no crash
    std::string err;
    CHECK(make_gpio_output("/dev/gpiochip999", 5, false, &err) == nullptr);
    CHECK(err != "");
    err.clear();
    CHECK(make_gpio_output("/dev/gpiochip0", -1, false, &err) == nullptr);
    CHECK(err != "");
#if defined(__linux__)
    err.clear();
    CHECK(make_audio_output("no_such_device_hml", &err) == nullptr);
    CHECK(err != "");
#endif
}

// ================================================================ usability: actions and the page of the device

TEST_CASE("sound: the actions sound.play / sound.stop / sound.silence, and System > Sound")
{
    Fixture f;
    f.configure(R"({ "volume": 100, "touch": false })");
    register_sound_actions();
    CHECK(ui().run_actions(json::parse(R"({ "action": "call", "fn": "sound.play", "arg": "alarm" })")) == 1);
    REQUIRE(f.log.played.size() == 1);
    CHECK(f.log.played[0].ms > 500);
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "sound.stop" })"));
    CHECK(f.log.stops == 1);
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "sound.play" })"));          // no name: the beep
    REQUIRE(f.log.played.size() == 2);
    CHECK(f.log.played[1].ms / 10 == 14);
    f.run(500);
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "sound.play", "arg": "../../etc/passwd" })"));
    ui().run_actions(json::parse(R"({ "action": "call", "fn": "sound.silence" })"));
    CHECK(f.log.played.size() == 2);

    SystemSettings& sys = system_settings();
    sys.add_page(make_sound_page());
    sys.create_tags();
    CHECK(tags().find("sys.soundcfg.output")->get().format() == "fake");
    CHECK(tags().find("sys.soundcfg.state")->get().format().find("etc/passwd") != std::string::npos);   // what went wrong last
    // the output comes after the page was made (the runtime opens it when the display is up): the page follows
    sound().set_output(nullptr);
    sys.poll();
    CHECK(tags().find("sys.soundcfg.output")->get().format() == "none");
    sound().set_output(std::make_unique<FakeOutput>(&f.log));
    sys.poll();
    CHECK(tags().find("sys.soundcfg.output")->get().format() == "fake");
    sound().set(true, 30);                                       // muted: the test is heard all the same
    sys.command("soundcfg", "test");
    REQUIRE(f.log.played.size() == 3);
    CHECK(f.log.played[2].ms / 10 == 14);
    CHECK(tags().find("sys.soundcfg.state")->get().format() == "");
    f.run(500);
    sound().set_output(nullptr);
    sys.command("soundcfg", "test");
    CHECK(tags().find("sys.soundcfg.output")->get().format() == "none");
    CHECK(tags().find("sys.soundcfg.state")->get().format() == "This device has no sound output.");
    sys.command("soundcfg", "silence");
    sys.command("soundcfg", "explode");
    for(const json& scr : sys.screens(480, 320)) REQUIRE(ui().build_screen(scr, ""));
    REQUIRE(ui().show_screen(SystemSettings::page_screen("soundcfg"), "none"));
    f.rt.advance(300);
    ui().unload_all();
    sys.clear();
}

// ================================================================ maintainability: the checker

TEST_CASE("sound: --check finds a wrong output, a sound that is none, a file that is no WAV or outside the project")
{
    Fixture f;
    fs::create_directories(f.dir / "screens");
    f.file("screens/s.json", "{ \"name\": \"s\", \"children\": [] }");
    f.file("assets/ok.wav", wav_encode(tone_of(8000, 1, 40, 3000)));
    f.file("assets/bad.wav", "RIFF0000WAVEnothing");
    f.file("assets/song.mp3", "ID3");
    auto problems = [&](const std::string& snd) {
        f.file("project.json", "{ \"start_screen\": \"s\", \"sound\": " + snd + ", \"screens\": [\"screens/s.json\"] }");
        std::string all;
        for(const CheckProblem& p : check_project((f.dir / "project.json").generic_string())) all += p.line() + "\n";
        return all;
    };
    CHECK(problems("{}") == "");
    CHECK(problems(R"({ "output": "gpio", "gpio": 18, "gpio_active_low": true, "volume": 60, "mute": false, "touch": "assets/ok.wav", "alarm": false, "alarm_every_s": 0 })") == "");
    CHECK(problems(R"({ "output": "audio", "device": "plughw:1,0", "touch": "beep", "alarm": "alarm" })") == "");
    CHECK(problems("5").find("sound: an object is needed") != std::string::npos);
    const std::string out = problems(R"({ "output": "speaker", "volume": 150, "gpio": 1.5, "alarm_every_s": -1, "touch": "assets/bad.wav", "alarm": "../x.wav", "loud": true })");
    CHECK(out.find("output: \"speaker\" is not") != std::string::npos);
    CHECK(out.find("volume: 0 to 100") != std::string::npos);
    CHECK(out.find("gpio: the number") != std::string::npos);
    CHECK(out.find("alarm_every_s") != std::string::npos);
    CHECK(out.find("touch: \"assets/bad.wav\":") != std::string::npos);
    CHECK(out.find("alarm: \"../x.wav\" is no built-in sound") != std::string::npos);
    CHECK(out.find("unknown key \"loud\"") != std::string::npos);
    const std::string out2 = problems(R"({ "output": "gpio", "touch": "assets/song.mp3", "alarm": "assets/none.wav" })");
    CHECK(out2.find("needs \"gpio\"") != std::string::npos);
    CHECK(out2.find("touch: \"assets/song.mp3\" is no built-in sound") != std::string::npos);
    CHECK(out2.find("alarm: no file \"assets/none.wav\"") != std::string::npos);
    CHECK(problems(R"({ "touch": 5, "alarm": [] })").find("touch: the name of a sound, or false") != std::string::npos);
}
