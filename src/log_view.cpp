#include "log_view.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "background_worker.hpp"
#include "log_query.hpp"
#include "tag.hpp"
#include "texts.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr uint32_t kPollMs = 200;

struct Shared {                       // between the widget (UI thread) and the worker
    std::mutex  mu;
    bool        busy = false, ready = false;
    uint64_t    gen = 0;              // the question the page answers
    LogViewPage page;
};

struct Ctx {
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   table = nullptr;
    lv_obj_t*   info = nullptr;       // "No entries" / why there is nothing
    lv_obj_t*   page_label = nullptr;
    lv_obj_t*   prev = nullptr;
    lv_obj_t*   next = nullptr;
    lv_obj_t*   level_dd = nullptr;
    lv_timer_t* timer = nullptr;
    LogViewQuery q;
    std::vector<std::string> cols;
    std::map<std::string, std::string> labels{ { "empty", "No entries" }, { "reading", "..." } };
    std::map<std::string, int> widths;
    Tag*        filter_tag = nullptr;
    uint32_t    refresh_ms = 2000;
    int         page = 0;
    int64_t     total = 0;
    uint64_t    asked = 0;            // number of the last question
    uint32_t    asked_at = 0;
    bool        dirty = true, drawn = false;
    bool        own_columns = false;  // the project named the columns
    bool        sized = false;
    std::vector<int> levels;          // of the rows shown (for the colour)
    std::shared_ptr<Shared> sh = std::make_shared<Shared>();

    std::string title(const std::string& col) const
    {
        auto it = labels.find(col);
        if(it != labels.end()) return it->second;
        if(q.source == "data" && col != "time") return col;       // (the name of a tag, as it is)
        std::string t = col;
        if(!t.empty()) t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
        return t;
    }

    void ask()
    {
        {
            std::lock_guard<std::mutex> l(sh->mu);
            if(sh->busy) return;                         // (asked again when that one is in)
            sh->busy = true;
        }
        dirty = false;
        asked_at = lv_tick_get();
        const uint64_t g = ++asked;
        LogViewQuery query = q;
        query.offset = page * q.limit;
        auto shared = sh;
        BackgroundWorker::get().post([shared, query, g] {
            LogViewPage p = log_view_fetch(query);
            std::lock_guard<std::mutex> l(shared->mu);
            shared->page = std::move(p);
            shared->gen = g;
            shared->ready = true;
            shared->busy = false;
        });
    }

    // the widths of the columns: what is given, the usual width of a short column, the rest shared
    void layout()
    {
        lv_obj_update_layout(obj);
        const int32_t all = std::max<int32_t>(lv_obj_get_content_width(table), 100);
        std::vector<int32_t> w(cols.size(), 0);
        int32_t used = 0;
        int open = 0;
        const bool data = q.source == "data";
        for(size_t i = 0; i < cols.size(); i++) {
            const std::string& c = cols[i];
            auto it = widths.find(c);
            if(it != widths.end()) w[i] = it->second;
            else if(c == "time") w[i] = 136;
            else if(data) w[i] = 0;
            else if(c == "level") w[i] = 72;
            else if(c == "count") w[i] = 56;
            else if(c == "source" || c == "user" || c == "kind") w[i] = 84;
            else if(c == "screen") w[i] = 96;
            if(w[i] > 0) used += w[i]; else open++;
        }
        // (what is left is shared; below 56 px a column is of no use: then the table scrolls sideways)
        const int32_t share = open > 0 ? std::max<int32_t>((all - used) / open, 56) : 0;
        for(size_t i = 0; i < cols.size(); i++) lv_table_set_column_width(table, static_cast<uint32_t>(i), w[i] > 0 ? w[i] : share);
    }

    void draw(const LogViewPage& p)
    {
        drawn = true;
        total = p.total;
        // (a page past the end: the log was cleared or the filter narrowed)
        const int pages = static_cast<int>((total + q.limit - 1) / q.limit);
        if(page > 0 && page >= pages) { page = std::max(0, pages - 1); dirty = true; }
        levels.assign(1, -1);
        lv_table_set_row_count(table, static_cast<uint32_t>(p.rows.size() + 1));
        lv_table_set_column_count(table, static_cast<uint32_t>(cols.size()));
        for(size_t c = 0; c < cols.size(); c++) lv_table_set_cell_value(table, 0, static_cast<uint32_t>(c), title(cols[c]).c_str());
        for(size_t r = 0; r < p.rows.size(); r++) {
            levels.push_back(p.rows[r].level);
            for(size_t c = 0; c < cols.size(); c++)
                lv_table_set_cell_value(table, static_cast<uint32_t>(r + 1), static_cast<uint32_t>(c), c < p.rows[r].cells.size() ? p.rows[r].cells[c].c_str() : "");
        }
        layout();
        const bool none = p.rows.empty();
        lv_label_set_text(info, !p.problem.empty() ? p.problem.c_str() : labels["empty"].c_str());
        lv_obj_set_hidden(info, !none);
        const int first = none ? 0 : page * q.limit + 1, last = page * q.limit + static_cast<int>(p.rows.size());
        lv_label_set_text(page_label, vformat("%d - %d / %lld", first, last, static_cast<long long>(total)).c_str());
        lv_obj_set_state(prev, LV_STATE_DISABLED, page <= 0);
        lv_obj_set_state(next, LV_STATE_DISABLED, static_cast<int64_t>(page + 1) * q.limit >= total);
        lv_obj_scroll_to_y(table, 0, LV_ANIM_OFF);
    }

    void tick()
    {
        if(lv_obj_get_screen(obj) != lv_screen_active()) { dirty = true; return; }      // read (again) when the screen is shown
        if(!sized) {
            // a narrow widget (a 480 px display): the message needs the room more than the source does
            sized = true;
            lv_obj_update_layout(obj);
            if(!own_columns && lv_obj_get_content_width(obj) < 560) {
                if(q.source == "system") { q.columns = { "time", "level", "message" }; widths.emplace("level", 64); }
                else if(q.source == "operation") { q.columns = { "time", "user", "target", "new" }; widths.emplace("user", 64); }
                if(!q.columns.empty()) cols = q.columns;
            }
        }
        if(filter_tag) {
            const std::string f = filter_tag->get().format();
            if(f != q.text) { q.text = f; page = 0; dirty = true; }
        }
        LogViewPage p;
        bool have = false;
        {
            std::lock_guard<std::mutex> l(sh->mu);
            if(sh->ready) { sh->ready = false; if(sh->gen == asked) { p = std::move(sh->page); have = true; } }
        }
        if(have) draw(p);
        // the first page follows the log while it is looked at
        if(!dirty && refresh_ms > 0 && page == 0 && lv_tick_elaps(asked_at) >= refresh_ms) dirty = true;
        if(dirty) ask();
    }
};

void go(Ctx* c, int by)
{
    const int pages = static_cast<int>((c->total + c->q.limit - 1) / c->q.limit);
    const int to = std::clamp(c->page + by, 0, std::max(0, pages - 1));
    if(to == c->page) return;
    c->page = to;
    c->dirty = true;
    c->tick();
}

lv_obj_t* tool_button(lv_obj_t* parent, const char* symbol)
{
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, 56, 40);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    return b;
}

} // namespace

lv_obj_t* build_log_view(lv_obj_t* parent, const json& w)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, lv_pct(100), 300);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(obj, 6, 0);
    lv_obj_set_style_pad_row(obj, 6, 0);
    lv_obj_set_scrollable(obj, false);

    auto* c = new Ctx;
    c->obj = obj;
    c->q.source = jstr(w, "source", "system");
    for(char& ch : c->q.source) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    c->q.group = jstr(w, "group", "");
    c->q.limit = std::clamp(static_cast<int>(jnum(w, "rows", 10)), 1, 200);
    c->q.days = std::max(0, static_cast<int>(jnum(w, "days", 0)));
    const std::string lv = jstr(w, "min_level", "info");
    c->q.min_level = iequals(lv, "error") ? 2 : (iequals(lv, "warn") || iequals(lv, "warning")) ? 1 : 0;
    c->refresh_ms = static_cast<uint32_t>(std::max(0.0, jnum(w, "refresh_ms", 2000)));
    if(c->refresh_ms > 0 && c->refresh_ms < 500) c->refresh_ms = 500;
    // the columns: those the source has, in the order asked for
    const std::vector<std::string> known = log_view_known_columns(c->q.source, c->q.group);
    if(const json* cols = jget(w, "columns")) {
        std::vector<std::string> names;
        if(cols->is_array()) { for(const json& n : *cols) if(n.is_string()) names.push_back(n.get<std::string>()); }
        else if(cols->is_string()) {                                  // "time, message" (the designer writes a list like this)
            std::string s = cols->get<std::string>(), one;
            for(char ch : s + ",") {
                if(ch == ',') { if(!one.empty()) names.push_back(one); one.clear(); }
                else if(ch != ' ') one += ch;
            }
        }
        for(const std::string& n : names) {
            if(std::find(known.begin(), known.end(), n) != known.end()) c->q.columns.push_back(n);
            else HML_WARN("log_view: the %s log has no column '%s'", c->q.source.c_str(), n.c_str());
        }
    }
    c->own_columns = !c->q.columns.empty();
    c->cols = log_view_columns(c->q);
    if(const json* l = jget(w, "labels"); l && l->is_object())
        for(auto it = l->begin(); it != l->end(); ++it) if(it.value().is_string()) c->labels[it.key()] = log_view_text(texts().resolve(it.value().get<std::string>()), 64);
    if(const json* ws = jget(w, "widths"); ws && ws->is_object())
        for(auto it = ws->begin(); it != ws->end(); ++it) if(it.value().is_number()) c->widths[it.key()] = std::clamp(static_cast<int>(it.value().get<double>()), 20, 2000);
    if(auto ft = jstr_opt(w, "filter_tag"); ft && !ft->empty()) {
        c->filter_tag = tags().find(*ft);
        if(!c->filter_tag) HML_WARN("log_view: unknown filter_tag '%s'", ft->c_str());
        else c->q.text = c->filter_tag->get().format();
    }

    // ---- the bar: level (system), where we are, the pages
    lv_obj_t* bar = lv_obj_create(obj);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_scrollable(bar, false);
    if(c->q.source == "system" && jbool(w, "filter", true)) {
        c->level_dd = lv_dropdown_create(bar);
        lv_dropdown_set_options(c->level_dd, "INFO\nWARN\nERROR");
        lv_dropdown_set_selected(c->level_dd, static_cast<uint32_t>(c->q.min_level));
        lv_obj_set_size(c->level_dd, 120, 40);
        lv_obj_add_event_cb(c->level_dd, [](lv_event_t* e) {
            auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
            cx->q.min_level = static_cast<int>(lv_dropdown_get_selected(cx->level_dd));
            cx->page = 0;
            cx->dirty = true;
            cx->tick();
        }, LV_EVENT_VALUE_CHANGED, c);
    }
    c->page_label = lv_label_create(bar);
    lv_label_set_text(c->page_label, "");
    lv_obj_set_flex_grow(c->page_label, 1);
    lv_obj_set_style_text_align(c->page_label, LV_TEXT_ALIGN_RIGHT, 0);
    c->prev = tool_button(bar, LV_SYMBOL_LEFT);
    c->next = tool_button(bar, LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(c->prev, [](lv_event_t* e) { go(static_cast<Ctx*>(lv_event_get_user_data(e)), -1); }, LV_EVENT_CLICKED, c);
    lv_obj_add_event_cb(c->next, [](lv_event_t* e) { go(static_cast<Ctx*>(lv_event_get_user_data(e)), +1); }, LV_EVENT_CLICKED, c);
    lv_obj_add_state(c->prev, LV_STATE_DISABLED);
    lv_obj_add_state(c->next, LV_STATE_DISABLED);

    // ---- the rows
    c->table = lv_table_create(obj);
    lv_obj_set_width(c->table, lv_pct(100));
    lv_obj_set_flex_grow(c->table, 1);
    lv_obj_set_style_pad_ver(c->table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(c->table, 6, LV_PART_ITEMS);
    lv_table_set_column_count(c->table, static_cast<uint32_t>(std::max<size_t>(c->cols.size(), 1)));
    lv_table_set_row_count(c->table, 1);
    for(size_t i = 0; i < c->cols.size(); i++) lv_table_set_cell_value(c->table, 0, static_cast<uint32_t>(i), c->title(c->cols[i]).c_str());
    // a warning and an error are told by the word in the "level" column and by the colour of the row
    lv_obj_set_send_draw_task_events(c->table, true);
    lv_obj_add_event_cb(c->table, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        lv_draw_task_t* task = lv_event_get_draw_task(e);
        auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
        if(!base || base->part != LV_PART_ITEMS) return;
        const uint32_t row = base->id1;
        const int level = row < cx->levels.size() ? cx->levels[row] : 0;
        if(lv_draw_label_dsc_t* l = lv_draw_task_get_label_dsc(task)) {
            if(level == 2) l->color = lv_palette_lighten(LV_PALETTE_RED, 2);
            else if(level == 1) l->color = lv_palette_lighten(LV_PALETTE_AMBER, 1);
        }
        if(lv_draw_fill_dsc_t* f = lv_draw_task_get_fill_dsc(task)) {
            if(row == 0) { f->color = lv_color_mix(lv_palette_main(LV_PALETTE_BLUE), f->color, LV_OPA_30); f->opa = LV_OPA_COVER; }
        }
    }, LV_EVENT_DRAW_TASK_ADDED, c);

    c->info = lv_label_create(obj);
    lv_label_set_text(c->info, c->labels["reading"].c_str());
    lv_obj_set_width(c->info, lv_pct(100));
    lv_label_set_long_mode(c->info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(c->info, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_ignore_layout(c->info, true);
    lv_obj_align(c->info, LV_ALIGN_CENTER, 0, 20);

    c->timer = lv_timer_create([](lv_timer_t* t) { static_cast<Ctx*>(lv_timer_get_user_data(t))->tick(); }, kPollMs, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        if(cx->timer) lv_timer_delete(cx->timer);
        delete cx;                                         // (a job still running keeps its own share of `sh`)
    }, LV_EVENT_DELETE, c);
    return obj;
}

void log_view_wait_idle() { BackgroundWorker::get().wait_idle(); }

} // namespace hml
