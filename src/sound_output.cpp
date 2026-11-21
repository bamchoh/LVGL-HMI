// Where a sound goes (sound.hpp):
//   Windows  the default sound device (PlaySound)
//   Linux    ALSA - libasound is loaded when the runtime runs, so that it builds and starts on
//            systems without it; a buzzer on a GPIO line (the character device, no library)
#include "sound.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "util.hpp"

#if defined(_WIN32)
#  include <windows.h>
#  include <mmsystem.h>
#elif defined(__linux__)
#  include <dlfcn.h>
#  include <fcntl.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#  include <cerrno>
#  include <cstring>
#  if __has_include(<linux/gpio.h>)
#    include <linux/gpio.h>
#  endif
#endif

namespace hml {
namespace {

#if defined(__linux__)

// A thread that plays one sound at a time; a new one replaces what plays.
class ThreadedOutput : public SoundOutput {
public:
    bool start(const Pcm& pcm, std::string*) override
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            job_ = std::make_shared<Pcm>(pcm);
            gen_++;
        }
        cv_.notify_all();
        return true;
    }
    void stop() override
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            job_.reset();
            gen_++;
        }
        cv_.notify_all();
    }

protected:
    // play it; give up soon after stopped(gen) turns true
    virtual void run(const Pcm& pcm, uint64_t gen) = 0;
    bool stopped(uint64_t gen) { std::lock_guard<std::mutex> lk(mu_); return gen_ != gen; }
    void begin() { thread_ = std::thread([this] { loop(); }); }
    void shutdown()                       // (in the destructor of the class that has run())
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            quit_ = true;
            job_.reset();
            gen_++;
        }
        cv_.notify_all();
        if(thread_.joinable()) thread_.join();
    }
    // wait, but not longer than until the sound is stopped; false when it was
    bool wait_ms(int ms, uint64_t gen)
    {
        std::unique_lock<std::mutex> lk(mu_);
        return !cv_.wait_for(lk, std::chrono::milliseconds(ms), [&] { return gen_ != gen; });
    }

private:
    void loop()
    {
        for(;;) {
            std::shared_ptr<Pcm> job;
            uint64_t gen;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return quit_ || job_; });
                if(quit_) return;
                job.swap(job_);
                gen = gen_;
            }
            run(*job, gen);
        }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::shared_ptr<Pcm> job_;
    uint64_t gen_ = 0;
    bool quit_ = false;
    std::thread thread_;
};

// ---------------------------------------------------------------- ALSA

struct Alsa {
    void* lib = nullptr;
    int (*open)(void**, const char*, int, int) = nullptr;
    int (*set_params)(void*, int, int, unsigned, unsigned, int, unsigned) = nullptr;
    long (*writei)(void*, const void*, unsigned long) = nullptr;
    int (*recover)(void*, int, int) = nullptr;
    int (*drain)(void*) = nullptr;
    int (*drop)(void*) = nullptr;
    int (*close)(void*) = nullptr;
    const char* (*strerror)(int) = nullptr;

    bool load(std::string* err)
    {
        lib = ::dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if(!lib) { if(err) *err = "ALSA is not installed (libasound.so.2)"; return false; }
        auto sym = [&](const char* n) { return ::dlsym(lib, n); };
        open = reinterpret_cast<decltype(open)>(sym("snd_pcm_open"));
        set_params = reinterpret_cast<decltype(set_params)>(sym("snd_pcm_set_params"));
        writei = reinterpret_cast<decltype(writei)>(sym("snd_pcm_writei"));
        recover = reinterpret_cast<decltype(recover)>(sym("snd_pcm_recover"));
        drain = reinterpret_cast<decltype(drain)>(sym("snd_pcm_drain"));
        drop = reinterpret_cast<decltype(drop)>(sym("snd_pcm_drop"));
        close = reinterpret_cast<decltype(close)>(sym("snd_pcm_close"));
        strerror = reinterpret_cast<decltype(strerror)>(sym("snd_strerror"));
        if(open && set_params && writei && recover && drain && drop && close && strerror) return true;
        if(err) *err = "libasound.so.2 lacks what is needed";
        ::dlclose(lib);
        lib = nullptr;
        return false;
    }
};

class AlsaOutput final : public ThreadedOutput {
public:
    AlsaOutput(Alsa a, std::string device) : a_(a), device_(std::move(device)) { begin(); }
    ~AlsaOutput() override { shutdown(); }
    std::string name() const override { return "ALSA (" + device_ + ")"; }

    // can the device be opened at all? (asked once, when the output is made)
    static bool probe(const Alsa& a, const std::string& device, std::string* err)
    {
        void* pcm = nullptr;
        const int rc = a.open(&pcm, device.c_str(), 0 /* playback */, 0);
        if(rc < 0) { if(err) *err = "ALSA device \"" + device + "\": " + a.strerror(rc); return false; }
        a.close(pcm);
        return true;
    }

protected:
    void run(const Pcm& pcm, uint64_t gen) override
    {
        void* h = nullptr;
        int rc = a_.open(&h, device_.c_str(), 0 /* playback */, 0);
        if(rc < 0) { complain(std::string("cannot open: ") + a_.strerror(rc)); return; }
        // signed 16 bit little endian, interleaved; ALSA converts the rate if the device has another; 50 ms of buffer
        rc = a_.set_params(h, 2, 3, static_cast<unsigned>(pcm.channels), static_cast<unsigned>(pcm.rate), 1, 50000);
        if(rc < 0) { complain(std::string("cannot set the format: ") + a_.strerror(rc)); a_.close(h); return; }
        const size_t frames = pcm.samples.size() / static_cast<size_t>(pcm.channels);
        size_t done = 0;
        bool cut = false;
        while(done < frames) {
            if(stopped(gen)) { cut = true; break; }
            const size_t n = std::min<size_t>(frames - done, static_cast<size_t>(pcm.rate) / 50 + 1);      // 20 ms at a time
            long w = a_.writei(h, pcm.samples.data() + done * static_cast<size_t>(pcm.channels), static_cast<unsigned long>(n));
            if(w < 0) w = a_.recover(h, static_cast<int>(w), 1);
            if(w < 0) { complain(std::string("cannot write: ") + a_.strerror(static_cast<int>(w))); cut = true; break; }
            done += static_cast<size_t>(w);
        }
        if(cut) a_.drop(h);
        else a_.drain(h);
        a_.close(h);
    }

private:
    void complain(const std::string& what)
    {
        if(complained_.exchange(true)) return;          // (once: a device that is gone would fill the log)
        HML_WARN("sound: ALSA device \"%s\": %s", device_.c_str(), what.c_str());
    }
    Alsa a_;
    std::string device_;
    std::atomic<bool> complained_{ false };
};

// ---------------------------------------------------------------- a buzzer on a GPIO line

#if defined(GPIO_V2_GET_LINE_IOCTL)
class GpioOutput final : public ThreadedOutput {
public:
    GpioOutput(int fd, std::string label) : fd_(fd), label_(std::move(label)) { set(false); begin(); }
    ~GpioOutput() override
    {
        shutdown();
        set(false);
        ::close(fd_);
    }
    std::string name() const override { return label_; }

protected:
    void run(const Pcm& pcm, uint64_t gen) override
    {
        bool on = true;
        for(int ms : pcm.pattern) {
            set(on);
            if(!wait_ms(std::clamp(ms, 1, 5000), gen)) break;
            on = !on;
        }
        set(false);                                     // (whatever happened: the buzzer never stays on)
    }

private:
    void set(bool on)
    {
        gpio_v2_line_values v{};
        v.mask = 1;
        v.bits = on ? 1 : 0;
        ::ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
    }
    int fd_;
    std::string label_;
};
#endif

#endif // __linux__

#if defined(_WIN32)
class WinOutput final : public SoundOutput {
public:
    ~WinOutput() override { stop(); }
    std::string name() const override { return "Windows sound"; }
    bool start(const Pcm& pcm, std::string* err) override
    {
        // (the memory is played from while it sounds: the old sound is stopped before its memory goes)
        ::PlaySoundA(nullptr, nullptr, 0);
        wav_ = wav_encode(pcm);
        if(::PlaySoundA(wav_.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) return true;
        if(err) *err = "Windows could not play the sound (no sound device?)";
        return false;
    }
    void stop() override { ::PlaySoundA(nullptr, nullptr, 0); }

private:
    std::string wav_;
};
#endif

} // namespace

std::unique_ptr<SoundOutput> make_audio_output(const std::string& device, std::string* err)
{
#if defined(_WIN32)
    (void)device;
    if(::waveOutGetNumDevs() == 0) { if(err) *err = "Windows has no sound device"; return nullptr; }
    return std::make_unique<WinOutput>();
#elif defined(__linux__)
    Alsa a;
    if(!a.load(err)) return nullptr;
    if(!AlsaOutput::probe(a, device, err)) return nullptr;
    return std::make_unique<AlsaOutput>(a, device);
#else
    (void)device;
    if(err) *err = "no sound output on this system";
    return nullptr;
#endif
}

std::unique_ptr<SoundOutput> make_gpio_output(const std::string& chip, int line, bool active_low, std::string* err)
{
#if defined(__linux__) && defined(GPIO_V2_GET_LINE_IOCTL)
    if(line < 0) { if(err) *err = "\"gpio\" (the number of the line of the buzzer) is missing"; return nullptr; }
    const int cfd = ::open(chip.c_str(), O_RDWR | O_CLOEXEC);
    if(cfd < 0) { if(err) *err = chip + ": " + std::strerror(errno); return nullptr; }
    gpio_v2_line_request req{};
    req.offsets[0] = static_cast<uint32_t>(line);
    req.num_lines = 1;
    std::strncpy(req.consumer, "hml-buzzer", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT | (active_low ? GPIO_V2_LINE_FLAG_ACTIVE_LOW : 0);
    const int rc = ::ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req);
    const int e = errno;
    ::close(cfd);
    if(rc < 0 || req.fd < 0) { if(err) *err = chip + " line " + std::to_string(line) + ": " + std::strerror(e); return nullptr; }
    return std::make_unique<GpioOutput>(req.fd, "GPIO buzzer (" + chip.substr(5) + " line " + std::to_string(line) + ")");
#else
    (void)chip; (void)line; (void)active_low;
    if(err) *err = "a GPIO buzzer needs Linux";
    return nullptr;
#endif
}

} // namespace hml
