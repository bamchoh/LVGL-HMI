#include "touch_filter.hpp"
#include <algorithm>
#include <cmath>
#include "util.hpp"

namespace hml {

TouchFilterConfig parse_touch_filter(const json* t)
{
    TouchFilterConfig c;
    if(!t || !t->is_object()) return c;
    c.settle = std::clamp(static_cast<int>(jnum(*t, "settle", 0)), 0, 5);
    c.smooth = std::clamp(static_cast<int>(jnum(*t, "smooth", 1)), 1, 8);
    c.jitter = std::clamp(static_cast<int>(jnum(*t, "jitter", 0)), 0, 100);
    c.release = std::clamp(static_cast<int>(jnum(*t, "release", 0)), 0, 3);
    c.lift_ms = std::clamp(static_cast<int>(jnum(*t, "lift_ms", 0)), 0, 300);
    c.scroll_limit = std::clamp(static_cast<int>(jnum(*t, "scroll_limit", 0)), 0, 200);
    c.log = jbool(*t, "log", false);
    return c;
}

TouchFilter::Point TouchFilter::feed(int x, int y, bool pressed, uint32_t now_ms)
{
    finished_ = false;
    if(!pressed && down_ && cfg_.lift_ms > 0) {
        // released - or the finger lost contact for a moment? Wait before saying so.
        if(!lifting_) { lifting_ = true; lift_at_ = now_ms; }
        if(now_ms - lift_at_ < static_cast<uint32_t>(cfg_.lift_ms)) return { ax_, ay_, reported_ };
    }
    if(pressed && lifting_) {                                // it came back: the same touch goes on
        lifting_ = false;
        lifts_++;
        longest_lift_ = std::max(longest_lift_, now_ms - lift_at_);
        skip_ = cfg_.settle;                                 // (it lands again)
        late_n_ = 0;
    }
    if(!pressed) {
        if(down_) {                                          // the touch ends where it was last reported
            last_.lifts = lifts_;
            last_.longest_lift_ms = longest_lift_;
            last_.since_last_ms = started_gap_;
            ended_at_ = lifting_ ? lift_at_ : now_ms;
            ever_ = true;
            lifting_ = false;
            last_.samples = count_;
            last_.ms = t_last_ - t0_;
            last_.raw_dx = max_x_ - min_x_; last_.raw_dy = max_y_ - min_y_;
            last_.core_dx = core_any_ ? cmax_x_ - cmin_x_ : 0; last_.core_dy = core_any_ ? cmax_y_ - cmin_y_ : 0;
            last_.out_dx = reported_ ? omax_x_ - omin_x_ : 0; last_.out_dy = reported_ ? omax_y_ - omin_y_ : 0;
            finished_ = true;
        }
        down_ = false;
        const bool was = reported_;
        reported_ = false;
        return { was ? ax_ : x, was ? ay_ : y, false };
    }
    if(!down_) {
        down_ = true;
        count_ = 0;
        ring_n_ = ring_at_ = 0;
        late_n_ = 0;
        core_any_ = false;
        lifts_ = 0; longest_lift_ = 0; skip_ = 0;
        started_gap_ = ever_ ? now_ms - ended_at_ : 0;
        t0_ = now_ms;
        min_x_ = max_x_ = x; min_y_ = max_y_ = y;
    }
    count_++;
    t_last_ = now_ms;
    min_x_ = std::min(min_x_, x); max_x_ = std::max(max_x_, x);
    min_y_ = std::min(min_y_, y); max_y_ = std::max(max_y_, y);
    // for the log: the readings without the first two and the last two (the one from two readings ago
    // is certain not to be one of the last two)
    if(count_ > 4) {
        const int ox = tail_x_[count_ % 2], oy = tail_y_[count_ % 2];
        if(!core_any_) { core_any_ = true; cmin_x_ = cmax_x_ = ox; cmin_y_ = cmax_y_ = oy; }
        cmin_x_ = std::min(cmin_x_, ox); cmax_x_ = std::max(cmax_x_, ox);
        cmin_y_ = std::min(cmin_y_, oy); cmax_y_ = std::max(cmax_y_, oy);
    }
    tail_x_[count_ % 2] = x; tail_y_[count_ % 2] = y;
    if(count_ <= cfg_.settle) return { x, y, false };        // the finger is still landing: not a press yet
    if(skip_ > 0) { skip_--; return { ax_, ay_, reported_ }; }     // ... or landing again after a short lift
    if(cfg_.release > 0) {
        // held back: what is passed on is the reading from `release` readings ago, so that the last
        // ones (the finger lifting) never are
        if(late_n_ < cfg_.release) {
            late_x_[late_n_] = x; late_y_[late_n_] = y; late_n_++;
            return { reported_ ? ax_ : x, reported_ ? ay_ : y, reported_ };
        }
        const int ox = late_x_[0], oy = late_y_[0];
        for(int i = 1; i < late_n_; i++) { late_x_[i - 1] = late_x_[i]; late_y_[i - 1] = late_y_[i]; }
        late_x_[late_n_ - 1] = x; late_y_[late_n_ - 1] = y;
        x = ox; y = oy;
    }

    // the average of the last readings
    ring_x_[ring_at_] = x; ring_y_[ring_at_] = y;
    ring_at_ = (ring_at_ + 1) % cfg_.smooth;
    ring_n_ = std::min(ring_n_ + 1, cfg_.smooth);
    long sx = 0, sy = 0;
    for(int i = 0; i < ring_n_; i++) { sx += ring_x_[i]; sy += ring_y_[i]; }
    const int mx = static_cast<int>(std::lround(static_cast<double>(sx) / ring_n_)), my = static_cast<int>(std::lround(static_cast<double>(sy) / ring_n_));

    if(!reported_) {
        reported_ = true;
        ax_ = mx; ay_ = my;
        omin_x_ = omax_x_ = ax_; omin_y_ = omax_y_ = ay_;
    }
    else if(cfg_.jitter <= 0) { ax_ = mx; ay_ = my; }
    else {
        // the reported point is pulled along as on a string of `jitter` pixels: wandering inside
        // that length moves nothing, a real movement is followed
        const double dx = mx - ax_, dy = my - ay_, d = std::sqrt(dx * dx + dy * dy);
        if(d > cfg_.jitter) {
            const double pull = (d - cfg_.jitter) / d;
            ax_ += static_cast<int>(std::lround(dx * pull));
            ay_ += static_cast<int>(std::lround(dy * pull));
        }
    }
    omin_x_ = std::min(omin_x_, ax_); omax_x_ = std::max(omax_x_, ax_);
    omin_y_ = std::min(omin_y_, ay_); omax_y_ = std::max(omax_y_, ay_);
    return { ax_, ay_, true };
}

std::string TouchFilter::describe() const
{
    std::string s = vformat("touch: %d samples in %u ms, the readings wandered %d x %d px (%d x %d without the first and last two), reported %d x %d px",
                            last_.samples, static_cast<unsigned>(last_.ms), last_.raw_dx, last_.raw_dy, last_.core_dx, last_.core_dy, last_.out_dx, last_.out_dy);
    if(last_.lifts > 0) s += vformat(", %d short lift(s) bridged (longest %u ms)", last_.lifts, static_cast<unsigned>(last_.longest_lift_ms));
    if(last_.since_last_ms > 0 && last_.since_last_ms < 300) s += vformat(", %u ms after the touch before", static_cast<unsigned>(last_.since_last_ms));
    return s;
}

} // namespace hml
