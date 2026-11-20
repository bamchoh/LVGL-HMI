// Trend history viewer: the data log of a group as a line chart over a time window.
//
//   { "type": "trend", "group": "process", "w": "100%", "h": 300,
//     "tags": ["temp", { "tag": "pressure", "color": "#e91e63" }],   // default: every tag of the group
//     "range_s": 3600,                // the window (default 1 hour); - / + halve and double it
//     "min": 0, "max": 100,           // y axis (default: from the data)
//     "buckets": 200,                 // points across the chart (each = min..max of its slice)
//     "refresh_ms": 2000,             // while following "now"
//     "labels": { "now": "Now" } }
//
// Each point is a time slice: the line of its largest value and the line of its smallest value
// are drawn, so that a short peak is never lost however long the window is. ◀ ▶ move half a
// window, "Now" follows the newest data again. A touch on the chart shows the time and the
// values there. The data is fetched by a background thread (SQLite does the slicing), so a
// long window does not stop the screen.
//
// A slice without a sample breaks the line, except where the group would not have recorded one
// anyway (see TrendFill): a window so short that the slices are shorter than the interval of the
// group, or a group that records on change.
#include "trend_view.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include "data_log.hpp"
#include "texts.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

// one worker for every trend widget; joined when the program ends
class TrendWorker {
public:
    static TrendWorker& get() { static TrendWorker w; return w; }
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
    ~TrendWorker()
    {
        { std::lock_guard<std::mutex> l(mu_); stop_ = true; jobs_.clear(); }
        cv_.notify_one();
        if(th_.joinable()) th_.join();
    }
private:
    TrendWorker() : th_([this] { run(); }) {}
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

struct Shared {                       // between the widget (UI thread) and the worker
    std::mutex mu;
    bool busy = false, ready = false;
    int64_t from = 0, to = 0;
    std::vector<std::vector<TrendBucket>> data;
};

std::string short_time(int64_t ms)
{
    std::string t = LogDb::format_time(ms);                // YYYY-MM-DD HH:MM:SS.mmm
    return t.size() >= 19 ? t.substr(5, 14) : t;
}

std::string num(double v)
{
    if(std::fabs(v) >= 1e6 || (std::fabs(v) < 1e-3 && v != 0)) return vformat("%.3g", v);
    std::string s = vformat("%.3f", v);
    while(!s.empty() && s.back() == '0') s.pop_back();
    if(!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

constexpr int32_t kScale = 10000;      // chart values are 0..kScale of the y range

struct TrendCtx {
    std::string group;
    std::vector<std::string> tags;
    std::vector<lv_color_t> colors;
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   chart = nullptr;
    lv_obj_t*   range_label = nullptr;
    lv_obj_t*   read_label = nullptr;
    lv_obj_t*   ymax_label = nullptr;
    lv_obj_t*   ymin_label = nullptr;
    lv_obj_t*   now_btn = nullptr;
    lv_chart_cursor_t* cursor = nullptr;
    std::vector<lv_chart_series_t*> s_max, s_min;
    lv_timer_t* timer = nullptr;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    int64_t     range_ms = 3600000;
    int64_t     end_ms = 0;            // the right edge when not following
    bool        live = true;
    bool        fixed_y = false;
    double      ymin = 0, ymax = 100;
    int         buckets = 200;
    TrendFill   fill;                  // how slices without a sample are drawn
    uint32_t    refresh_ms = 2000, last_fetch = 0;
    bool        want = true;           // fetch again
    bool        bad_group = false;
    int64_t     shown_from = 0, shown_to = 0;
    std::vector<std::vector<TrendBucket>> shown;
    double      lo = 0, hi = 1;        // the y range drawn

    int64_t window_end() const { return live ? LogDb::now_ms() : end_ms; }

    void fetch()
    {
        {
            std::lock_guard<std::mutex> l(shared->mu);
            if(shared->busy) return;
            shared->busy = true;
        }
        const int64_t to = window_end(), from = to - range_ms;
        auto sh = shared;
        const std::string g = group;
        const std::vector<std::string> t = tags;
        const int n = buckets;
        const TrendFill fl = fill;
        TrendWorker::get().post([sh, g, t, from, to, n, fl] {
            auto data = data_log().trend(g, t, from, to, n);
            trend_fill_gaps(data, from, to, fl, LogDb::now_ms());
            std::lock_guard<std::mutex> l(sh->mu);
            sh->data = std::move(data);
            sh->from = from; sh->to = to;
            sh->ready = true;
            sh->busy = false;
        });
        last_fetch = millis();
        want = false;
    }

    void tick()
    {
        bool got = false;
        {
            std::lock_guard<std::mutex> l(shared->mu);
            if(shared->ready) {
                shared->ready = false;
                shown = std::move(shared->data);
                shown_from = shared->from; shown_to = shared->to;
                got = true;
            }
        }
        if(got) draw();
        if(bad_group || lv_obj_get_screen(obj) != lv_screen_active()) return;        // nothing to do while not shown
        if(want || (live && millis() - last_fetch >= refresh_ms)) fetch();
    }

    void draw()
    {
        // the y range: fixed, or from what is shown (with a little room)
        if(fixed_y) { lo = ymin; hi = ymax; }
        else {
            bool any = false;
            double a = 0, b = 0;
            for(const auto& series : shown) for(const TrendBucket& t : series) if(t.has) {
                if(!any) { a = t.min; b = t.max; any = true; }
                a = std::min(a, t.min); b = std::max(b, t.max);
            }
            if(!any) { a = 0; b = 1; }
            const double pad = (b - a) * 0.05;
            lo = a - pad; hi = b + pad;
            if(hi - lo < 1e-9) { lo -= 1; hi += 1; }
        }
        lv_label_set_text(ymax_label, num(hi).c_str());
        lv_label_set_text(ymin_label, num(lo).c_str());
        lv_label_set_text(range_label, (short_time(shown_from) + "  -  " + short_time(shown_to) + (live ? "  (live)" : "")).c_str());
        auto y = [&](double v) { return static_cast<int32_t>(std::lround(std::clamp((v - lo) / (hi - lo), -0.05, 1.05) * kScale)); };
        for(size_t i = 0; i < tags.size() && i < shown.size(); i++) {
            for(int k = 0; k < buckets && k < static_cast<int>(shown[i].size()); k++) {
                const TrendBucket& t = shown[i][static_cast<size_t>(k)];
                lv_chart_set_series_value_by_id(chart, s_max[i], static_cast<uint32_t>(k), t.has ? y(t.max) : LV_CHART_POINT_NONE);
                lv_chart_set_series_value_by_id(chart, s_min[i], static_cast<uint32_t>(k), t.has ? y(t.min) : LV_CHART_POINT_NONE);
            }
        }
        lv_chart_refresh(chart);
        if(live) lv_obj_add_state(now_btn, LV_STATE_CHECKED); else lv_obj_remove_state(now_btn, LV_STATE_CHECKED);
    }

    // the values at a bucket, as text
    std::string readout(int k) const
    {
        if(k < 0 || k >= buckets || shown_to <= shown_from) return "";
        const int64_t t = shown_from + (shown_to - shown_from) * k / buckets;
        std::string s = short_time(t);
        for(size_t i = 0; i < tags.size() && i < shown.size(); i++) {
            const TrendBucket& b = shown[i][static_cast<size_t>(k)];
            s += "   " + tags[i] + " ";
            if(!b.has) s += "-";
            else if(b.min == b.max) s += num(b.last);
            else s += num(b.last) + " (" + num(b.min) + ".." + num(b.max) + ")";
        }
        return s;
    }

    void move(double windows)
    {
        if(live) end_ms = LogDb::now_ms();
        end_ms += static_cast<int64_t>(static_cast<double>(range_ms) * windows);
        live = false;
        if(end_ms >= LogDb::now_ms()) live = true;          // moved past now: follow again
        want = true;
    }
    void zoom(double factor)
    {
        range_ms = std::clamp(static_cast<int64_t>(static_cast<double>(range_ms) * factor), int64_t(60000), int64_t(366) * 86400000);
        want = true;
    }

    static TrendCtx* ctx(lv_event_t* e) { return static_cast<TrendCtx*>(lv_event_get_user_data(e)); }
    static void on_timer(lv_timer_t* t) { static_cast<TrendCtx*>(lv_timer_get_user_data(t))->tick(); }
    static void on_back(lv_event_t* e) { ctx(e)->move(-0.5); }
    static void on_fwd(lv_event_t* e) { ctx(e)->move(0.5); }
    static void on_out(lv_event_t* e) { ctx(e)->zoom(2.0); }
    static void on_in(lv_event_t* e) { ctx(e)->zoom(0.5); }
    static void on_now(lv_event_t* e) { TrendCtx* c = ctx(e); c->live = true; c->want = true; }
    static void on_press(lv_event_t* e)
    {
        TrendCtx* c = ctx(e);
        lv_indev_t* indev = lv_indev_active();
        if(!indev) return;
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        lv_area_t a;
        lv_obj_get_content_coords(c->chart, &a);
        const int32_t w = lv_area_get_width(&a);
        if(w <= 0) return;
        const int k = static_cast<int>(static_cast<int64_t>(p.x - a.x1) * c->buckets / w);
        c->show_cursor(k);
    }
    void show_cursor(int k)
    {
        k = std::clamp(k, 0, buckets - 1);
        lv_label_set_text(read_label, readout(k).c_str());
        if(cursor && !s_max.empty()) lv_chart_set_cursor_point(chart, cursor, s_max[0], static_cast<uint32_t>(k));
    }
    static void on_delete(lv_event_t* e)
    {
        TrendCtx* c = ctx(e);
        if(c->timer) lv_timer_delete(c->timer);
        delete c;                                              // (a fetch still running keeps `shared` alive)
    }
};

lv_obj_t* small_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, TrendCtx* c)
{
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, 56, 40);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, c);
    return b;
}

const lv_palette_t kPalette[] = { LV_PALETTE_BLUE, LV_PALETTE_ORANGE, LV_PALETTE_GREEN, LV_PALETTE_PINK, LV_PALETTE_PURPLE, LV_PALETTE_TEAL, LV_PALETTE_AMBER, LV_PALETTE_RED };

} // namespace

lv_obj_t* build_trend(lv_obj_t* parent, const json& w)
{
    auto* c = new TrendCtx();
    c->group = jstr(w, "group", "");
    if(const json* tl = jget(w, "tags"); tl && tl->is_array()) {
        for(const json& t : *tl) {
            std::string name = t.is_string() ? t.get<std::string>() : jstr(t, "tag", "");
            if(name.empty() || c->tags.size() >= 8) continue;
            c->tags.push_back(name);
            c->colors.push_back(parse_color(t.is_object() ? jstr(t, "color", "") : "", lv_palette_main(kPalette[c->colors.size() % 8])));
        }
    }
    if(c->tags.empty()) for(const std::string& n : data_log().group_tags(c->group)) {
        if(c->tags.size() >= 8) break;
        c->tags.push_back(n);
        c->colors.push_back(lv_palette_main(kPalette[c->colors.size() % 8]));
    }
    c->range_ms = std::clamp(static_cast<int64_t>(jnum(w, "range_s", 3600) * 1000), int64_t(60000), int64_t(366) * 86400000);
    c->buckets = std::clamp(static_cast<int>(jnum(w, "buckets", 200)), 10, 1000);
    c->refresh_ms = static_cast<uint32_t>(std::clamp(jnum(w, "refresh_ms", 2000), 250.0, 600000.0));
    c->fixed_y = jhas(w, "min") && jhas(w, "max") && jnum(w, "max", 1) > jnum(w, "min", 0);
    c->ymin = jnum(w, "min", 0); c->ymax = jnum(w, "max", 100);
    std::string now_label = "Now";
    if(const json* l = jget(w, "labels"); l && l->is_object()) now_label = texts().resolve(jstr(*l, "now", now_label));

    lv_obj_t* obj = lv_obj_create(parent);
    c->obj = obj;
    lv_obj_set_size(obj, lv_pct(100), 300);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(obj, 6, 0);
    lv_obj_set_style_pad_row(obj, 4, 0);
    lv_obj_set_scrollable(obj, false);

    c->range_label = lv_label_create(obj);
    lv_label_set_text(c->range_label, "");
    c->read_label = lv_label_create(obj);
    lv_label_set_long_mode(c->read_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(c->read_label, lv_pct(100));
    lv_label_set_text(c->read_label, "");

    lv_obj_t* mid = lv_obj_create(obj);
    lv_obj_remove_style_all(mid);
    lv_obj_set_width(mid, lv_pct(100));
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(mid, 4, 0);
    lv_obj_t* axis = lv_obj_create(mid);
    lv_obj_remove_style_all(axis);
    lv_obj_set_size(axis, LV_SIZE_CONTENT, lv_pct(100));
    c->ymax_label = lv_label_create(axis);
    lv_obj_align(c->ymax_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    c->ymin_label = lv_label_create(axis);
    lv_obj_align(c->ymin_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_label_set_text(c->ymax_label, "");
    lv_label_set_text(c->ymin_label, "");
    lv_obj_set_style_min_width(axis, 40, 0);

    c->chart = lv_chart_create(mid);
    lv_obj_set_height(c->chart, lv_pct(100));
    lv_obj_set_flex_grow(c->chart, 1);
    lv_chart_set_type(c->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c->chart, static_cast<uint32_t>(c->buckets));
    lv_chart_set_axis_range(c->chart, LV_CHART_AXIS_PRIMARY_Y, 0, kScale);
    lv_chart_set_update_mode(c->chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_div_line_count(c->chart, 5, 7);
    lv_obj_set_style_width(c->chart, 0, LV_PART_INDICATOR);       // no dots on the points
    lv_obj_set_style_height(c->chart, 0, LV_PART_INDICATOR);
    for(size_t i = 0; i < c->tags.size(); i++) {
        c->s_max.push_back(lv_chart_add_series(c->chart, c->colors[i], LV_CHART_AXIS_PRIMARY_Y));
        c->s_min.push_back(lv_chart_add_series(c->chart, lv_color_mix(c->colors[i], lv_color_white(), 200), LV_CHART_AXIS_PRIMARY_Y));
        lv_chart_set_all_values(c->chart, c->s_max.back(), LV_CHART_POINT_NONE);
        lv_chart_set_all_values(c->chart, c->s_min.back(), LV_CHART_POINT_NONE);
    }
    c->cursor = lv_chart_add_cursor(c->chart, lv_palette_main(LV_PALETTE_GREY), LV_DIR_VER);
    lv_obj_add_event_cb(c->chart, &TrendCtx::on_press, LV_EVENT_PRESSED, c);
    lv_obj_add_event_cb(c->chart, &TrendCtx::on_press, LV_EVENT_PRESSING, c);

    lv_obj_t* foot = lv_obj_create(obj);
    lv_obj_remove_style_all(foot);
    lv_obj_set_size(foot, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(foot, 6, 0);
    small_button(foot, LV_SYMBOL_LEFT, &TrendCtx::on_back, c);
    small_button(foot, LV_SYMBOL_RIGHT, &TrendCtx::on_fwd, c);
    small_button(foot, LV_SYMBOL_MINUS, &TrendCtx::on_out, c);
    small_button(foot, LV_SYMBOL_PLUS, &TrendCtx::on_in, c);
    c->now_btn = lv_button_create(foot);
    lv_obj_set_height(c->now_btn, 40);
    lv_obj_t* nl = lv_label_create(c->now_btn);
    lv_label_set_text(nl, now_label.c_str());
    lv_obj_center(nl);
    lv_obj_add_event_cb(c->now_btn, &TrendCtx::on_now, LV_EVENT_CLICKED, c);
    // (the legend: tag names in their colours)
    for(size_t i = 0; i < c->tags.size(); i++) {
        lv_obj_t* l = lv_label_create(foot);
        lv_label_set_text(l, c->tags[i].c_str());
        lv_obj_set_style_text_color(l, c->colors[i], 0);
        lv_obj_set_style_pad_top(l, 10, 0);
    }
    c->fill = data_log().trend_fill(c->group);
    const auto groups = data_log().groups();
    if(std::find(groups.begin(), groups.end(), c->group) == groups.end()) {
        c->bad_group = true;
        lv_label_set_text(c->range_label, ("no data log group '" + c->group + "'").c_str());
    }

    c->timer = lv_timer_create(&TrendCtx::on_timer, 100, c);
    lv_obj_add_event_cb(obj, &TrendCtx::on_delete, LV_EVENT_DELETE, c);
    return obj;
}

void trend_wait_idle() { TrendWorker::get().wait_idle(); }

} // namespace hml
