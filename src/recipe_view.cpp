// The recipe screen widget.
//
//   { "type": "recipe_table", "book": "product", "w": "100%", "h": 300,
//     "row_height": 40, "labels": { "write": "Write", "read": "Read current", "save_as": "Save as",
//     "delete": "Delete", "item": "Item", "recipe": "Recipe", "current": "Current", "ok": "OK",
//     "cancel": "Cancel", "name": "Recipe name", "confirm_write": "Write '%s' to the PLC?",
//     "confirm_delete": "Delete '%s'?", "confirm_read": "Put the current values into '%s'?" } }
//
// Left: the recipes (tap = select). Right: item | value of the selected recipe | current value;
// a value that differs from the current one is marked (colour and "*"). Tap a recipe value to
// change it. Write / Read current / Delete ask first; Save as asks for a name. Changing anything
// needs the recipes' "level" (else the login screen). "%s" in a label is the recipe name; the
// label is never used as a printf format.
#include "recipe.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include "texts.hpp"
#include "util.hpp"

namespace hml {
namespace {

std::string with_name(std::string text, const std::string& name)
{
    size_t at = text.find("%s");
    return at == std::string::npos ? text : text.replace(at, 2, name);
}

std::string one_line(const std::string& s, size_t max = 64)
{
    std::string out;
    for(unsigned char c : s) out.push_back(c < 0x20 || c == 0x7f ? ' ' : static_cast<char>(c));
    if(out.size() > max) { size_t cut = max; while(cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--; out = out.substr(0, cut) + "..."; }
    return out;
}

// ---------------------------------------------------------------- a small dialog on the top layer

struct Dialog {
    lv_obj_t* back = nullptr;
    lv_obj_t* ta = nullptr;
    bool      closed = false;
    std::function<void(const std::string&)> on_ok;

    // (deleted later: this runs inside an event of one of its own children; the delete event frees d)
    static bool close(Dialog* d) { if(d->closed) return false; d->closed = true; lv_obj_delete_async(d->back); return true; }
    static void on_delete(lv_event_t* e) { delete static_cast<Dialog*>(lv_event_get_user_data(e)); }
    static void on_ok_click(lv_event_t* e)
    {
        Dialog* d = static_cast<Dialog*>(lv_event_get_user_data(e));
        std::string text = d->ta ? lv_textarea_get_text(d->ta) : "";
        auto fn = d->on_ok;
        if(close(d) && fn) fn(text);                                   // (only once, also on a double tap)
    }
    static void on_cancel(lv_event_t* e) { close(static_cast<Dialog*>(lv_event_get_user_data(e))); }
};

// question: text; input: nullptr = only OK / Cancel, else the start text of a field (number: number keys)
void dialog(const std::string& question, const std::string* input, bool number, const std::map<std::string, std::string>& labels,
            std::function<void(const std::string&)> on_ok)
{
    auto lab = [&](const char* k, const char* d) { auto it = labels.find(k); return it == labels.end() ? std::string(d) : it->second; };
    auto* d = new Dialog();
    d->on_ok = std::move(on_ok);
    d->back = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(d->back);
    lv_obj_set_size(d->back, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(d->back, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(d->back, LV_OPA_50, 0);
    lv_obj_add_flag(d->back, LV_OBJ_FLAG_CLICKABLE);                     // modal: nothing below can be touched
    lv_obj_add_event_cb(d->back, &Dialog::on_delete, LV_EVENT_DELETE, d);

    lv_obj_t* box = lv_obj_create(d->back);
    lv_obj_set_width(box, lv_pct(input ? 96 : 70));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_align(box, input ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, input ? 4 : 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 8, 0);
    lv_obj_t* q = lv_label_create(box);
    lv_label_set_long_mode(q, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(q, lv_pct(100));
    lv_label_set_text(q, question.c_str());
    if(input) {
        d->ta = lv_textarea_create(box);
        lv_textarea_set_one_line(d->ta, true);
        lv_textarea_set_max_length(d->ta, number ? 24 : 32);
        lv_textarea_set_text(d->ta, input->c_str());
        lv_obj_set_width(d->ta, lv_pct(100));
        if(number) lv_textarea_set_accepted_chars(d->ta, "0123456789.-+eE");
    }
    lv_obj_t* row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    auto button = [&](const std::string& text, lv_event_cb_t cb) {
        lv_obj_t* b = lv_button_create(row);
        lv_obj_set_height(b, 40);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, text.c_str());
        lv_obj_center(l);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, d);
        return b;
    };
    button(lab("cancel", "Cancel"), &Dialog::on_cancel);
    button(lab("ok", "OK"), &Dialog::on_ok_click);
    if(input) {
        lv_obj_t* kb = lv_keyboard_create(d->back);
        lv_keyboard_set_mode(kb, number ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(kb, d->ta);
        lv_obj_add_event_cb(kb, &Dialog::on_ok_click, LV_EVENT_READY, d);
        lv_obj_add_event_cb(kb, &Dialog::on_cancel, LV_EVENT_CANCEL, d);
    }
}

// ---------------------------------------------------------------- the widget

struct TableCtx {
    std::string book;
    lv_obj_t*   obj = nullptr;
    lv_obj_t*   list = nullptr;
    lv_obj_t*   title = nullptr;
    lv_obj_t*   rows = nullptr;
    lv_obj_t*   btn_write = nullptr;
    lv_timer_t* timer = nullptr;
    int32_t     row_h = 40;
    std::map<std::string, std::string> labels;
    uint64_t    seen = 0;
    std::string current_sig;                   // the current values drawn
    std::vector<std::string> list_names, row_items;

    std::string lab(const char* k, const char* d) const { auto it = labels.find(k); return it == labels.end() ? std::string(d) : it->second; }

    std::string current_signature(const RecipeBook& b) const
    {
        std::string s;
        for(const std::string& item : b.items) { Tag* t = tags().find(item); s += (t ? t->get().format() : "") + "\x1f"; }
        return s;
    }

    void refresh(bool force = false)
    {
        const RecipeBook* b = recipes().book(book);
        if(!force && lv_obj_get_screen(obj) != lv_screen_active()) return;
        if(!b) {
            if(force) { lv_obj_clean(rows); lv_label_set_text(title, ("no recipe book '" + one_line(book) + "'").c_str()); }
            return;
        }
        const std::string sig = current_signature(*b);
        if(!force && recipes().generation() == seen && sig == current_sig) return;
        const bool list_changed = force || recipes().generation() != seen;
        seen = recipes().generation();
        current_sig = sig;
        if(list_changed) draw_list(*b);
        draw_rows(*b);
    }

    void draw_list(const RecipeBook& b)
    {
        lv_obj_clean(list);
        list_names.clear();
        for(const Recipe& r : b.recipes) {
            lv_obj_t* btn = lv_button_create(list);
            lv_obj_set_size(btn, lv_pct(100), row_h);
            if(r.name == b.selected) lv_obj_add_state(btn, LV_STATE_CHECKED);
            else lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
            lv_obj_t* l = lv_label_create(btn);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(l, lv_pct(100));
            lv_label_set_text(l, one_line(r.name, 32).c_str());
            lv_obj_center(l);
            list_names.push_back(r.name);
            lv_obj_add_event_cb(btn, &TableCtx::on_pick, LV_EVENT_CLICKED, this);
        }
    }

    void draw_rows(const RecipeBook& b)
    {
        lv_obj_clean(rows);
        row_items.clear();
        const Recipe* r = recipes().recipe(book, b.selected);
        lv_label_set_text(title, r ? one_line(r->name, 32).c_str() : "-");
        if(r) lv_obj_remove_state(btn_write, LV_STATE_DISABLED); else lv_obj_add_state(btn_write, LV_STATE_DISABLED);
        auto cell = [](lv_obj_t* parent, const std::string& text, int pct, lv_color_t color) {
            lv_obj_t* l = lv_label_create(parent);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(l, lv_pct(pct));
            lv_label_set_text(l, text.c_str());
            lv_obj_set_style_text_color(l, color, 0);
            return l;
        };
        const lv_color_t dim = lv_palette_main(LV_PALETTE_GREY), fg = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
        lv_obj_t* head = lv_obj_create(rows);
        lv_obj_remove_style_all(head);
        lv_obj_set_size(head, lv_pct(100), 24);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        cell(head, lab("item", "Item"), 36, dim); cell(head, lab("recipe", "Recipe"), 32, dim); cell(head, lab("current", "Current"), 32, dim);
        for(const std::string& item : b.items) {
            Tag* t = tags().find(item);
            const std::string cur = t ? t->get().format() : "";
            std::string val = "-";
            bool differs = false;
            if(r) {
                auto it = r->values.find(item);
                if(it != r->values.end()) { val = it->second.format(); differs = val != cur; }
            }
            lv_obj_t* row = lv_obj_create(rows);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, lv_pct(100), row_h);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_border_color(row, dim, 0);
            lv_obj_set_style_border_opa(row, LV_OPA_30, 0);
            cell(row, one_line(item, 40), 36, fg);
            // a value that will change when the recipe is written: colour and a sign (not only colour;
            // "*" because the built-in fonts have no "≠")
            lv_obj_t* v = cell(row, (differs ? std::string("* ") : std::string()) + one_line(val), 32, differs ? lv_palette_main(LV_PALETTE_ORANGE) : fg);
            lv_obj_add_flag(v, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_height(v, row_h);
            lv_obj_set_style_pad_top(v, row_h / 2 - 10, 0);
            lv_obj_add_event_cb(v, &TableCtx::on_value, LV_EVENT_CLICKED, this);
            cell(row, one_line(cur), 32, fg);
            row_items.push_back(item);
        }
    }

    // ---- events
    static TableCtx* ctx(lv_event_t* e) { return static_cast<TableCtx*>(lv_event_get_user_data(e)); }
    static void on_pick(lv_event_t* e)
    {
        TableCtx* c = ctx(e);
        int32_t i = lv_obj_get_index(lv_event_get_current_target_obj(e));
        if(i >= 0 && static_cast<size_t>(i) < c->list_names.size()) recipes().select(c->book, c->list_names[static_cast<size_t>(i)]);
        c->refresh(true);
    }
    static void on_value(lv_event_t* e)
    {
        TableCtx* c = ctx(e);
        const int32_t i = lv_obj_get_index(lv_obj_get_parent(lv_event_get_current_target_obj(e))) - 1;   // (row 0 is the heading)
        const RecipeBook* b = recipes().book(c->book);
        if(!b || i < 0 || static_cast<size_t>(i) >= c->row_items.size() || !recipes().recipe(c->book, b->selected)) return;
        if(!recipe_allowed()) return;
        const std::string item = c->row_items[static_cast<size_t>(i)], name = b->selected;
        const Recipe* r = recipes().recipe(c->book, name);
        auto it = r->values.find(item);
        std::string start = it == r->values.end() ? "" : it->second.format();
        Tag* t = tags().find(item);
        const bool number = t && t->type() != TagType::String;
        std::string book = c->book;
        dialog(one_line(name, 32) + ": " + one_line(item, 40), &start, number, c->labels,
               [book, name, item](const std::string& text) { recipes().set_value(book, name, item, text); });
    }
    static void on_write(lv_event_t* e)
    {
        TableCtx* c = ctx(e);
        const RecipeBook* b = recipes().book(c->book);
        if(!b || b->selected.empty() || !recipe_allowed()) return;
        std::string book = c->book, name = b->selected;
        dialog(with_name(c->lab("confirm_write", "Write '%s' to the PLC?"), one_line(name, 32)), nullptr, false, c->labels,
               [book, name](const std::string&) { recipes().write(book, name); });
    }
    static void on_read(lv_event_t* e)
    {
        TableCtx* c = ctx(e);
        const RecipeBook* b = recipes().book(c->book);
        if(!b || b->selected.empty() || !recipe_allowed()) return;
        std::string book = c->book, name = b->selected;
        dialog(with_name(c->lab("confirm_read", "Put the current values into '%s'?"), one_line(name, 32)), nullptr, false, c->labels,
               [book, name](const std::string&) { recipes().save_current(book, name); });
    }
    static void on_save_as(lv_event_t* e)
    {
        TableCtx* c = ctx(e);
        if(!recipes().book(c->book) || !recipe_allowed()) return;
        std::string book = c->book, start;
        dialog(c->lab("name", "Recipe name"), &start, false, c->labels, [book](const std::string& name) { recipes().save_current(book, name); });
    }
    static void on_delete_recipe(lv_event_t* e)
    {
        TableCtx* c = ctx(e);
        const RecipeBook* b = recipes().book(c->book);
        if(!b || b->selected.empty() || !recipe_allowed()) return;
        std::string book = c->book, name = b->selected;
        dialog(with_name(c->lab("confirm_delete", "Delete '%s'?"), one_line(name, 32)), nullptr, false, c->labels,
               [book, name](const std::string&) { recipes().remove(book, name); });
    }
    static void on_timer(lv_timer_t* t) { static_cast<TableCtx*>(lv_timer_get_user_data(t))->refresh(); }
    static void on_delete(lv_event_t* e)
    {
        TableCtx* c = ctx(e);
        if(c->timer) lv_timer_delete(c->timer);
        delete c;
    }
};

} // namespace

lv_obj_t* build_recipe_table(lv_obj_t* parent, const json& w)
{
    auto* c = new TableCtx();
    c->book = jstr(w, "book", "");
    c->row_h = std::clamp(static_cast<int32_t>(jnum(w, "row_height", 40)), 24, 120);
    if(const json* l = jget(w, "labels"); l && l->is_object())
        for(auto it = l->begin(); it != l->end(); ++it) if(it.value().is_string()) c->labels[it.key()] = one_line(texts().resolve(it.value().get<std::string>()), 80);

    lv_obj_t* obj = lv_obj_create(parent);
    c->obj = obj;
    lv_obj_set_size(obj, lv_pct(100), 300);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(obj, 6, 0);
    lv_obj_set_style_pad_column(obj, 8, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    c->list = lv_obj_create(obj);
    lv_obj_remove_style_all(c->list);
    lv_obj_set_size(c->list, lv_pct(30), lv_pct(100));
    lv_obj_set_flex_flow(c->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c->list, 4, 0);
    lv_obj_add_flag(c->list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* right = lv_obj_create(obj);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 4, 0);
    c->title = lv_label_create(right);
    c->rows = lv_obj_create(right);
    lv_obj_remove_style_all(c->rows);
    lv_obj_set_width(c->rows, lv_pct(100));
    lv_obj_set_flex_grow(c->rows, 1);
    lv_obj_set_flex_flow(c->rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(c->rows, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* foot = lv_obj_create(right);
    lv_obj_remove_style_all(foot);
    lv_obj_set_size(foot, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(foot, 6, 0);
    lv_obj_set_style_pad_row(foot, 4, 0);
    auto button = [&](const std::string& text, lv_event_cb_t cb, lv_palette_t color) {
        lv_obj_t* b = lv_button_create(foot);
        lv_obj_set_height(b, 40);
        if(color != LV_PALETTE_LAST) lv_obj_set_style_bg_color(b, lv_palette_main(color), 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, text.c_str());
        lv_obj_center(l);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, c);
        return b;
    };
    c->btn_write = button(c->lab("write", "Write"), &TableCtx::on_write, LV_PALETTE_RED);
    button(c->lab("read", "Read current"), &TableCtx::on_read, LV_PALETTE_LAST);
    button(c->lab("save_as", "Save as"), &TableCtx::on_save_as, LV_PALETTE_LAST);
    button(c->lab("delete", "Delete"), &TableCtx::on_delete_recipe, LV_PALETTE_GREY);

    c->timer = lv_timer_create(&TableCtx::on_timer, 300, c);
    lv_obj_add_event_cb(obj, &TableCtx::on_delete, LV_EVENT_DELETE, c);
    c->refresh(true);
    return obj;
}

} // namespace hml
