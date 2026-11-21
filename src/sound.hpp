// Sound: a beep for a touch, a sound for a new alarm, sounds played by actions.
//
//   "sound": {
//     "output": "auto",            // "audio" (Windows: the default device, Linux: ALSA), "gpio" (a buzzer
//                                  // on a GPIO line, Linux), "none". "auto": gpio when "gpio" is given, else audio
//     "device": "default",         // Linux: the ALSA device
//     "gpio": 18, "gpio_active_low": false, "gpio_chip": "/dev/gpiochip0",
//     "volume": 80, "mute": false, // 0..100 (a buzzer has no volume)
//     "touch": "click",            // a built-in sound ("click", "beep", "alarm"), a WAV file of the project, or false
//     "alarm": "alarm",            // ... for a new alarm that is to be acknowledged, or false
//     "alarm_every_s": 3           // it starts again every N seconds until the alarms are acknowledged (0: once)
//   }
//
// Without "sound" nothing sounds. A device without a sound output runs as before: what cannot be
// opened is logged once and left alone.
//
// Tags: sys.sound.mute, sys.sound.volume (changes are kept in project.local.json), sys.sound.alarm
// (1 while the alarm sound is on; writing 0 silences it until the next new alarm).
// Actions: { "action": "call", "fn": "sound.play", "arg": "beep" | "assets/x.wav" }, "sound.stop",
// "sound.silence". A sound file is a WAV (PCM, 8 or 16 bit, mono or stereo) inside the project folder.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "jsonutil.hpp"

namespace hml {

struct Pcm {
    int rate = 22050;
    int channels = 1;
    std::vector<int16_t> samples;         // interleaved
    std::vector<int> pattern;             // a buzzer: on, off, on ... in ms
    int duration_ms() const { return rate > 0 && channels > 0 ? static_cast<int>(static_cast<int64_t>(samples.size()) / channels * 1000 / rate) : 0; }
};

constexpr size_t kSoundFileMax = 4u * 1024 * 1024;      // bytes of a sound file
constexpr int    kSoundMaxMs = 30000;                   // what is longer is cut

// A WAV file as it is on disk -> samples. False with a reason for what is no WAV we can play.
bool wav_parse(const std::string& bytes, Pcm& out, std::string* err = nullptr);
std::string wav_encode(const Pcm& pcm);
// "click", "beep", "alarm"
bool sound_builtin(const std::string& name, Pcm& out);
std::vector<std::string> sound_builtin_names();
void pcm_scale(Pcm& pcm, int volume);                   // 0..100
// a relative path inside the project folder that ends in .wav
bool sound_file_name_ok(const std::string& rel);

// Where the sound goes. start() does not wait for the sound to end; a new start() replaces what plays.
class SoundOutput {
public:
    virtual ~SoundOutput() = default;
    virtual std::string name() const = 0;
    virtual bool start(const Pcm& pcm, std::string* err) = 0;
    virtual void stop() = 0;
};
std::unique_ptr<SoundOutput> make_audio_output(const std::string& device, std::string* err);
std::unique_ptr<SoundOutput> make_gpio_output(const std::string& chip, int line, bool active_low, std::string* err);

class Sound {
public:
    enum Priority { Touch = 0, Action = 1, Alarm = 2, Test = 3 };

    Sound();
    ~Sound();
    void configure(const json* section, const json* local, const std::string& project_dir, const std::string& store);
    void clear();
    // Open the output (the runtime calls this once the display is up; not in --check, not in tests).
    void start();
    bool configured() const;
    // Call regularly (main loop): the alarm sound, touch panels that came, settings to save.
    void poll();

    // A built-in sound or a WAV file of the project. False: unknown, muted, no output, or something
    // more important is sounding.
    bool play(const std::string& name, int priority = Action);
    void stop();
    void touch();                         // the touch sound
    void silence();                       // the alarm sound, until the next new alarm
    bool alarm_sounding() const;

    bool mute() const;
    int  volume() const;
    std::string set(bool mute, int volume);   // "" or why it could not be saved
    std::string output_name() const;          // "none" without one
    std::string last_error() const;

    // (tests) where the sound goes, the clock in ms
    void set_output(std::unique_ptr<SoundOutput> out);
    void set_clock(std::function<uint32_t()> now_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Sound& sound();
void register_sound_actions();

class SettingsPage;
std::unique_ptr<SettingsPage> make_sound_page();        // System > Sound

} // namespace hml
