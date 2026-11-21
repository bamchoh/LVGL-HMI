// One thread that runs jobs one after the other, for the widgets that read a log (trend,
// log_view): the UI thread never waits for the disk. The widget posts a job that fills a
// structure it shares with the worker (a std::shared_ptr, so that a widget deleted meanwhile
// does no harm) and looks at it from an LVGL timer.
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include "util.hpp"

namespace hml {

class BackgroundWorker {
public:
    // the one worker of the program; joined when the program ends
    static BackgroundWorker& get() { static BackgroundWorker w; return w; }

    void post(std::function<void()> job)
    {
        { std::lock_guard<std::mutex> l(mu_); jobs_.push_back(std::move(job)); }
        cv_.notify_one();
    }
    void wait_idle()                                       // (tests)
    {
        for(;;) {
            { std::lock_guard<std::mutex> l(mu_); if(jobs_.empty() && !busy_) return; }
            sleep_ms(2);
        }
    }
    ~BackgroundWorker()
    {
        { std::lock_guard<std::mutex> l(mu_); stop_ = true; jobs_.clear(); }
        cv_.notify_one();
        if(th_.joinable()) th_.join();
    }

private:
    BackgroundWorker() : th_([this] { run(); }) {}
    void run()
    {
        for(;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return stop_ || !jobs_.empty(); });
                if(stop_) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
                busy_ = true;
            }
            job();
            std::lock_guard<std::mutex> l(mu_);
            busy_ = false;
        }
    }
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false, busy_ = false;
    std::thread th_;
};

} // namespace hml
