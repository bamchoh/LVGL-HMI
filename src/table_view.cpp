#include "table_view.hpp"
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include "log_query.hpp"
#include "tag.hpp"
#include "texts.hpp"
#include "ui.hpp"
#include "util.hpp"

namespace hml {
namespace {

constexpr uint32_t kPollMs = 250;
constexpr size_t   kMaxRows = 2000, kMaxColumns = 32;

struct Live {                             // a cell that follows a tag
    uint32_t    row = 0, col = 0;         // in the lv_table (the titles are row 0 when there are any)
    Tag*        tag = nullptr;
    std::string format;
    std::vector<std::pair<double, std::string>> by_number;
    std::vector<std::pair<std::string, std::string>> by_text;
    std::string fallback;                 // "default" of the map
    bool        has_map = false, has_fallback = false;
    std::string shown;
    bool        drawn = false;

    std::string text() const
    {
        const Value v = tag->get();
        if(has_map) {
            if(!v.is_string()) { const double n = v.as_number(); for(const auto& m : by_number) if(m.first == n) return m.second; }
            const std::string s = v.format();
            for(const auto& m : by_text) if(m.first == s) return m.second;
            if(has_fallback) return fallback;
        }
        return log_view_text(v.format(format));
    }
};

struct Ctx {
    lv_obj_t*   obj = nullptr;            // the lv_table itself
    lv_timer_t* timer = nullptr;
    bool        header = true;
    uint32_t    rows = 0, cols = 0;       // without the titles
    std::vector<int32_t> widths;          // 0 = shares what is left
    std::vector<lv_text_align_t> aligns;
    std::vector<Live> live;
    int32_t     laid_out_for = -1;

    void layout()
    {
        lv_obj_update_layout(obj);
        const int32_t all = lv_obj_get_content_width(obj);
        if(all == laid_out_for || cols == 0) return;
        laid_out_for = all;
        int32_t used = 0;
        int open = 0;
        for(int32_t w : widths) { if(w > 0) used += w; else open++; }
        const int32_t share = open > 0 ? std::max<int32_t>((all - used) / open, 40) : 0;
        for(uint32_t c = 0; c < cols; c++) lv_table_set_column_width(obj, c, widths[c] > 0 ? widths[c] : share);
    }

    void tick(bool force = false)
    {
        if(!force && lv_obj_get_screen(obj) != lv_screen_active()) return;       // (brought up to date when the screen is shown)
        layout();
        for(Live& l : live) {
            std::string now = l.text();
            if(l.drawn && now == l.shown) continue;
            lv_table_set_cell_value(obj, l.row, l.col, now.c_str());
            l.shown = std::move(now);
            l.drawn = true;
        }
    }
};

std::unordered_map<lv_obj_t*, Ctx*>& registry() { static std::unordered_map<lv_obj_t*, Ctx*> r; return r; }

std::string plain(const json& v)
{
    if(v.is_string()) return log_view_text(texts().resolve(v.get<std::string>()));
    if(v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if(v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if(v.is_number()) return vformat("%g", v.get<double>());
    return "";
}

} // namespace

lv_obj_t* build_table_view(lv_obj_t* parent, const json& w)
{
    lv_obj_t* obj = lv_table_create(parent);
    lv_obj_set_size(obj, lv_pct(100), 200);
    lv_obj_set_style_pad_ver(obj, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(obj, 6, LV_PART_ITEMS);

    auto* c = new Ctx;
    c->obj = obj;
    c->header = jbool(w, "header", true);
    const json none = json::array();
    const json* columns = jget(w, "columns");
    if(!columns || !columns->is_array()) columns = &none;
    const json* rows = jget(w, "rows");
    if(!rows || !rows->is_array()) rows = &none;
    if(rows->size() > kMaxRows) HML_WARN("table: %zu rows, the first %zu are shown", rows->size(), kMaxRows);

    size_t ncols = columns->size();
    c->rows = static_cast<uint32_t>(std::min(rows->size(), kMaxRows));
    for(size_t r = 0; r < c->rows; r++) if((*rows)[r].is_array()) ncols = std::max(ncols, (*rows)[r].size());
    if(ncols > kMaxColumns) { HML_WARN("table: %zu columns, the first %zu are shown", ncols, kMaxColumns); ncols = kMaxColumns; }
    c->cols = static_cast<uint32_t>(ncols);
    c->widths.assign(ncols, 0);
    c->aligns.assign(ncols, LV_TEXT_ALIGN_LEFT);

    const uint32_t first = c->header ? 1 : 0;
    lv_table_set_column_count(obj, static_cast<uint32_t>(std::max<size_t>(ncols, 1)));
    lv_table_set_row_count(obj, std::max<uint32_t>(c->rows + first, 1));
    for(size_t i = 0; i < ncols; i++) {
        std::string title;
        if(i < columns->size()) {
            const json& col = (*columns)[i];
            if(col.is_object()) {
                title = plain(col.value("title", json("")));
                c->widths[i] = std::clamp(static_cast<int32_t>(jnum(col, "width", 0)), 0, 4000);
                const std::string a = jstr(col, "align", "left");
                c->aligns[i] = iequals(a, "right") ? LV_TEXT_ALIGN_RIGHT : iequals(a, "center") ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;
            }
            else title = plain(col);
        }
        if(c->header) lv_table_set_cell_value(obj, 0, static_cast<uint32_t>(i), title.c_str());
    }
    for(size_t r = 0; r < c->rows; r++) {
        const json& row = (*rows)[r];
        if(!row.is_array()) continue;
        for(size_t i = 0; i < ncols && i < row.size(); i++) {
            const json& cell = row[i];
            const uint32_t tr = static_cast<uint32_t>(r) + first, tc = static_cast<uint32_t>(i);
            if(!cell.is_object()) { lv_table_set_cell_value(obj, tr, tc, plain(cell).c_str()); continue; }
            const std::string tagname = jstr(cell, "tag", "");
            Tag* tag = tagname.empty() ? nullptr : tags().find(tagname);
            if(!tag) {
                if(!tagname.empty()) HML_WARN("table: unknown tag '%s'", tagname.c_str());
                lv_table_set_cell_value(obj, tr, tc, plain(cell.value("text", json(""))).c_str());
                continue;
            }
            Live l;
            l.row = tr; l.col = tc; l.tag = tag;
            l.format = jstr(cell, "format", "");
            if(const json* map = jget(cell, "map"); map && map->is_object()) {
                l.has_map = true;
                for(auto it = map->begin(); it != map->end(); ++it) {
                    const std::string text = plain(it.value());
                    if(it.key() == "default") { l.fallback = text; l.has_fallback = true; continue; }
                    char* end = nullptr;
                    const double n = std::strtod(it.key().c_str(), &end);
                    if(!it.key().empty() && end && *end == '\0') l.by_number.emplace_back(n, text);
                    else if(it.key() == "true") l.by_number.emplace_back(1, text);
                    else if(it.key() == "false") l.by_number.emplace_back(0, text);
                    l.by_text.emplace_back(it.key(), text);
                }
            }
            c->live.push_back(std::move(l));
        }
    }

    // the titles stand out; a column is aligned as it asks
    lv_obj_set_send_draw_task_events(obj, true);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        lv_draw_task_t* task = lv_event_get_draw_task(e);
        auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
        if(!base || base->part != LV_PART_ITEMS) return;
        if(lv_draw_label_dsc_t* l = lv_draw_task_get_label_dsc(task)) { if(base->id2 < cx->aligns.size()) l->align = cx->aligns[base->id2]; }
        if(lv_draw_fill_dsc_t* f = lv_draw_task_get_fill_dsc(task)) {
            if(cx->header && base->id1 == 0) { f->color = lv_color_mix(lv_palette_main(LV_PALETTE_BLUE), f->color, LV_OPA_30); f->opa = LV_OPA_COVER; }
        }
    }, LV_EVENT_DRAW_TASK_ADDED, c);

    registry()[obj] = c;
    c->timer = lv_timer_create([](lv_timer_t* t) { static_cast<Ctx*>(lv_timer_get_user_data(t))->tick(); }, kPollMs, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) { auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e)); cx->laid_out_for = -1; cx->layout(); }, LV_EVENT_SIZE_CHANGED, c);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        auto* cx = static_cast<Ctx*>(lv_event_get_user_data(e));
        if(cx->timer) lv_timer_delete(cx->timer);
        registry().erase(cx->obj);
        delete cx;
    }, LV_EVENT_DELETE, c);
    c->tick(true);
    return obj;
}

int table_view_rows(lv_obj_t* table) { auto it = registry().find(table); return it == registry().end() ? 0 : static_cast<int>(it->second->rows); }
int table_view_columns(lv_obj_t* table) { auto it = registry().find(table); return it == registry().end() ? 0 : static_cast<int>(it->second->cols); }

std::string table_view_cell(lv_obj_t* table, int row, int column)
{
    auto it = registry().find(table);
    if(it == registry().end()) return "";
    const Ctx* c = it->second;
    if(column < 0 || static_cast<uint32_t>(column) >= c->cols || row < -1 || row >= static_cast<int>(c->rows)) return "";
    if(row == -1 && !c->header) return "";
    const char* t = lv_table_get_cell_value(table, static_cast<uint32_t>(row + (c->header ? 1 : 0)), static_cast<uint32_t>(column));
    return t ? t : "";
}

} // namespace hml
