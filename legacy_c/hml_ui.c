#include "hml_ui.h"
#include "hml_tag.h"
#include "hml_driver.h"   /* json helpers */
#include "hml_os.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ================================================================ types */

typedef enum {
    W_PANEL, W_LABEL, W_BUTTON, W_SWITCH, W_CHECKBOX, W_SLIDER, W_BAR, W_ARC, W_LED,
    W_DROPDOWN, W_ROLLER, W_TEXTAREA, W_IMAGE, W_CHART, W_SPINNER, W_KEYBOARD, W_LINE
} widget_kind_t;

typedef enum { B_TEXT, B_VALUE, B_CHECKED, B_VISIBLE, B_ENABLED, B_BG_COLOR, B_TEXT_COLOR, B_OPTIONS } bind_kind_t;

typedef enum { CMP_NONE, CMP_EQ, CMP_NE, CMP_GT, CMP_LT, CMP_GE, CMP_LE } cmp_t;

typedef struct {
    double     key;
    lv_color_t color;
} color_map_t;

typedef struct binding {
    lv_obj_t     *obj;       /* the widget (for buttons with text: the inner label for B_TEXT) */
    widget_kind_t wkind;
    bind_kind_t   kind;
    hml_tag_t    *tag;
    char          format[96];
    bool          invert;
    cmp_t         cmp;
    double        cmp_num;
    char          cmp_str[64];
    bool          cmp_is_str;
    double        scale, offset;   /* widget = tag*scale + offset (B_VALUE) */
    color_map_t   map[16];
    int           map_n;
    bool          has_dflt_color;
    lv_color_t    dflt_color;
    lv_style_selector_t part;
    bool          two_way;
    struct binding *next;
} binding_t;

typedef enum { A_GOTO, A_BACK, A_SET, A_TOGGLE, A_INC, A_DEC, A_RELOAD, A_EXIT } action_kind_t;

typedef struct action {
    action_kind_t kind;
    char          screen[64];
    char          anim[24];
    hml_tag_t    *tag;
    hml_value_t   value;
    double        by;
    struct action *next;
} action_t;

typedef struct action_list {
    action_t *first;
    struct action_list *next;
} action_list_t;

typedef struct chart_series {
    lv_chart_series_t *ser;
    hml_tag_t         *tag;
    double             scale;
} chart_series_t;

typedef struct chart_ctx {
    lv_obj_t      *chart;
    lv_timer_t    *timer;
    chart_series_t series[8];
    int            series_n;
    struct chart_ctx *next;
} chart_ctx_t;

typedef struct {
    char      name[64];
    lv_obj_t *obj;
    lv_obj_t *keyboard;  /* optional on-screen keyboard */
} screen_t;

#define MAX_SCREENS 64
#define HISTORY_LEN 16

static screen_t       s_screens[MAX_SCREENS];
static int            s_screen_count;
static int            s_current = -1;
static int            s_history[HISTORY_LEN];
static int            s_history_n;
static binding_t     *s_bindings;
static action_list_t *s_action_lists;
static chart_ctx_t   *s_charts;
static lv_group_t    *s_group;
static lv_style_t     s_style_disabled;   /* visual feedback for LV_STATE_DISABLED (theme has none for most widgets) */
static lv_indev_t    *s_kb_indev;
static hml_ui_app_cb_t s_app_cb;
static char           s_base_dir[512];
static screen_t      *s_building;  /* screen currently being built */

/* ================================================================ small helpers */

lv_color_t hml_ui_parse_color(const char *s, lv_color_t dflt)
{
    if(!s) return dflt;
    if(s[0] == '#') {
        size_t n = strlen(s + 1);
        unsigned v = (unsigned)strtoul(s + 1, NULL, 16);
        if(n == 3) {
            unsigned r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
            return lv_color_make((uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17));
        }
        return lv_color_hex(v);
    }
    static const struct { const char *n; lv_palette_t p; } pal[] = {
        { "red", LV_PALETTE_RED }, { "pink", LV_PALETTE_PINK }, { "purple", LV_PALETTE_PURPLE },
        { "deep_purple", LV_PALETTE_DEEP_PURPLE }, { "indigo", LV_PALETTE_INDIGO }, { "blue", LV_PALETTE_BLUE },
        { "light_blue", LV_PALETTE_LIGHT_BLUE }, { "cyan", LV_PALETTE_CYAN }, { "teal", LV_PALETTE_TEAL },
        { "green", LV_PALETTE_GREEN }, { "light_green", LV_PALETTE_LIGHT_GREEN }, { "lime", LV_PALETTE_LIME },
        { "yellow", LV_PALETTE_YELLOW }, { "amber", LV_PALETTE_AMBER }, { "orange", LV_PALETTE_ORANGE },
        { "deep_orange", LV_PALETTE_DEEP_ORANGE }, { "brown", LV_PALETTE_BROWN }, { "grey", LV_PALETTE_GREY },
        { "gray", LV_PALETTE_GREY }, { "blue_grey", LV_PALETTE_BLUE_GREY },
    };
    for(size_t i = 0; i < sizeof(pal) / sizeof(pal[0]); i++) {
        if(hml_strieq(s, pal[i].n)) return lv_palette_main(pal[i].p);
    }
    if(hml_strieq(s, "white")) return lv_color_white();
    if(hml_strieq(s, "black")) return lv_color_black();
    HML_LOG_WARN("unknown color '%s'", s);
    return dflt;
}

const lv_font_t *hml_ui_font(const cJSON *v)
{
    int size = 0;
    if(cJSON_IsNumber(v)) size = (int)v->valuedouble;
    else if(cJSON_IsString(v)) {
        const char *s = v->valuestring;
        const char *p = strrchr(s, '_');
        size = atoi(p ? p + 1 : s);
    }
    switch(size) {
        case 12: return &lv_font_montserrat_12;
        case 14: return &lv_font_montserrat_14;
        case 16: return &lv_font_montserrat_16;
        case 18: return &lv_font_montserrat_18;
        case 20: return &lv_font_montserrat_20;
        case 24: return &lv_font_montserrat_24;
        case 28: return &lv_font_montserrat_28;
        case 32: return &lv_font_montserrat_32;
        case 40: return &lv_font_montserrat_40;
        case 48: return &lv_font_montserrat_48;
        default:
            HML_LOG_WARN("font size %d not available (12,14,16,18,20,24,28,32,40,48)", size);
            return LV_FONT_DEFAULT;
    }
}

static int32_t parse_size(const cJSON *v, int32_t dflt)
{
    if(cJSON_IsNumber(v)) return (int32_t)v->valuedouble;
    if(cJSON_IsString(v)) {
        const char *s = v->valuestring;
        if(hml_strieq(s, "content")) return LV_SIZE_CONTENT;
        if(hml_strieq(s, "full")) return lv_pct(100);
        size_t n = strlen(s);
        if(n > 1 && s[n - 1] == '%') return lv_pct(atoi(s));
        return atoi(s);
    }
    return dflt;
}

static lv_align_t parse_align(const char *s)
{
    static const struct { const char *n; lv_align_t a; } tbl[] = {
        { "center", LV_ALIGN_CENTER }, { "top_left", LV_ALIGN_TOP_LEFT }, { "top_mid", LV_ALIGN_TOP_MID },
        { "top_right", LV_ALIGN_TOP_RIGHT }, { "left_mid", LV_ALIGN_LEFT_MID }, { "right_mid", LV_ALIGN_RIGHT_MID },
        { "bottom_left", LV_ALIGN_BOTTOM_LEFT }, { "bottom_mid", LV_ALIGN_BOTTOM_MID },
        { "bottom_right", LV_ALIGN_BOTTOM_RIGHT }, { "top", LV_ALIGN_TOP_MID }, { "bottom", LV_ALIGN_BOTTOM_MID },
        { "left", LV_ALIGN_LEFT_MID }, { "right", LV_ALIGN_RIGHT_MID },
    };
    for(size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) if(hml_strieq(s, tbl[i].n)) return tbl[i].a;
    HML_LOG_WARN("unknown align '%s'", s);
    return LV_ALIGN_TOP_LEFT;
}

static lv_flex_align_t parse_flex_align(const char *s, lv_flex_align_t dflt)
{
    if(!s) return dflt;
    if(hml_strieq(s, "start")) return LV_FLEX_ALIGN_START;
    if(hml_strieq(s, "end")) return LV_FLEX_ALIGN_END;
    if(hml_strieq(s, "center")) return LV_FLEX_ALIGN_CENTER;
    if(hml_strieq(s, "space_evenly")) return LV_FLEX_ALIGN_SPACE_EVENLY;
    if(hml_strieq(s, "space_around")) return LV_FLEX_ALIGN_SPACE_AROUND;
    if(hml_strieq(s, "space_between")) return LV_FLEX_ALIGN_SPACE_BETWEEN;
    return dflt;
}

static lv_screen_load_anim_t parse_anim(const char *s)
{
    if(!s || hml_strieq(s, "none")) return LV_SCREEN_LOAD_ANIM_NONE;
    if(hml_strieq(s, "fade")) return LV_SCREEN_LOAD_ANIM_FADE_IN;
    if(hml_strieq(s, "slide_left") || hml_strieq(s, "move_left")) return LV_SCREEN_LOAD_ANIM_MOVE_LEFT;
    if(hml_strieq(s, "slide_right") || hml_strieq(s, "move_right")) return LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
    if(hml_strieq(s, "slide_top") || hml_strieq(s, "move_top")) return LV_SCREEN_LOAD_ANIM_MOVE_TOP;
    if(hml_strieq(s, "slide_bottom") || hml_strieq(s, "move_bottom")) return LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM;
    if(hml_strieq(s, "over_left")) return LV_SCREEN_LOAD_ANIM_OVER_LEFT;
    if(hml_strieq(s, "over_right")) return LV_SCREEN_LOAD_ANIM_OVER_RIGHT;
    return LV_SCREEN_LOAD_ANIM_FADE_IN;
}

/* Join a JSON string array with '\n' (dropdown/roller options). Caller frees. */
static char *join_options(const cJSON *arr)
{
    if(!cJSON_IsArray(arr)) return hml_strdup("");
    size_t total = 1;
    const cJSON *e;
    cJSON_ArrayForEach(e, arr) total += (cJSON_IsString(e) ? strlen(e->valuestring) : 16) + 1;
    char *out = malloc(total);
    if(!out) return NULL;
    out[0] = '\0';
    bool first = true;
    cJSON_ArrayForEach(e, arr) {
        char tmp[32];
        const char *s = cJSON_IsString(e) ? e->valuestring : (snprintf(tmp, sizeof(tmp), "%g", e->valuedouble), tmp);
        if(!first) strcat(out, "\n");
        strcat(out, s);
        first = false;
    }
    return out;
}

static screen_t *find_screen(const char *name)
{
    for(int i = 0; i < s_screen_count; i++) if(strcmp(s_screens[i].name, name) == 0) return &s_screens[i];
    return NULL;
}

/* ================================================================ styles */

static void apply_style(lv_obj_t *obj, const cJSON *st, lv_style_selector_t sel)
{
    if(!cJSON_IsObject(st)) return;
    const cJSON *p;
    cJSON_ArrayForEach(p, st) {
        const char *k = p->string;
        const char *sv = cJSON_IsString(p) ? p->valuestring : NULL;
        int32_t nv = cJSON_IsNumber(p) ? (int32_t)p->valuedouble : (cJSON_IsBool(p) ? cJSON_IsTrue(p) : 0);

        if(hml_strieq(k, "bg_color")) lv_obj_set_style_bg_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel), lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        else if(hml_strieq(k, "bg_opa")) lv_obj_set_style_bg_opa(obj, (lv_opa_t)(nv * 255 / 100), sel);
        else if(hml_strieq(k, "bg_grad_color")) lv_obj_set_style_bg_grad_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel);
        else if(hml_strieq(k, "bg_grad_dir")) lv_obj_set_style_bg_grad_dir(obj, hml_strieq(sv, "hor") ? LV_GRAD_DIR_HOR : hml_strieq(sv, "ver") ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, sel);
        else if(hml_strieq(k, "border_color")) lv_obj_set_style_border_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel);
        else if(hml_strieq(k, "border_width")) lv_obj_set_style_border_width(obj, nv, sel);
        else if(hml_strieq(k, "border_opa")) lv_obj_set_style_border_opa(obj, (lv_opa_t)(nv * 255 / 100), sel);
        else if(hml_strieq(k, "outline_width")) lv_obj_set_style_outline_width(obj, nv, sel);
        else if(hml_strieq(k, "outline_color")) lv_obj_set_style_outline_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel);
        else if(hml_strieq(k, "radius")) lv_obj_set_style_radius(obj, nv, sel);
        else if(hml_strieq(k, "shadow_width")) lv_obj_set_style_shadow_width(obj, nv, sel);
        else if(hml_strieq(k, "shadow_color")) lv_obj_set_style_shadow_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel);
        else if(hml_strieq(k, "shadow_opa")) lv_obj_set_style_shadow_opa(obj, (lv_opa_t)(nv * 255 / 100), sel);
        else if(hml_strieq(k, "text_color")) lv_obj_set_style_text_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel);
        else if(hml_strieq(k, "text_opa")) lv_obj_set_style_text_opa(obj, (lv_opa_t)(nv * 255 / 100), sel);
        else if(hml_strieq(k, "text_align")) lv_obj_set_style_text_align(obj, hml_strieq(sv, "center") ? LV_TEXT_ALIGN_CENTER : hml_strieq(sv, "right") ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, sel);
        else if(hml_strieq(k, "text_letter_space")) lv_obj_set_style_text_letter_space(obj, nv, sel);
        else if(hml_strieq(k, "text_line_space")) lv_obj_set_style_text_line_space(obj, nv, sel);
        else if(hml_strieq(k, "font")) lv_obj_set_style_text_font(obj, hml_ui_font(p), sel);
        else if(hml_strieq(k, "pad")) lv_obj_set_style_pad_all(obj, nv, sel);
        else if(hml_strieq(k, "pad_left")) lv_obj_set_style_pad_left(obj, nv, sel);
        else if(hml_strieq(k, "pad_right")) lv_obj_set_style_pad_right(obj, nv, sel);
        else if(hml_strieq(k, "pad_top")) lv_obj_set_style_pad_top(obj, nv, sel);
        else if(hml_strieq(k, "pad_bottom")) lv_obj_set_style_pad_bottom(obj, nv, sel);
        else if(hml_strieq(k, "pad_row")) lv_obj_set_style_pad_row(obj, nv, sel);
        else if(hml_strieq(k, "pad_column")) lv_obj_set_style_pad_column(obj, nv, sel);
        else if(hml_strieq(k, "pad_gap")) lv_obj_set_style_pad_gap(obj, nv, sel);
        else if(hml_strieq(k, "margin")) lv_obj_set_style_margin_all(obj, nv, sel);
        else if(hml_strieq(k, "opa")) lv_obj_set_style_opa(obj, (lv_opa_t)(nv * 255 / 100), sel);
        else if(hml_strieq(k, "line_color")) lv_obj_set_style_line_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel);
        else if(hml_strieq(k, "line_width")) lv_obj_set_style_line_width(obj, nv, sel);
        else if(hml_strieq(k, "line_rounded")) lv_obj_set_style_line_rounded(obj, nv != 0, sel);
        else if(hml_strieq(k, "arc_color")) lv_obj_set_style_arc_color(obj, hml_ui_parse_color(sv, lv_color_black()), sel);
        else if(hml_strieq(k, "arc_width")) lv_obj_set_style_arc_width(obj, nv, sel);
        else if(hml_strieq(k, "arc_rounded")) lv_obj_set_style_arc_rounded(obj, nv != 0, sel);
        else if(hml_strieq(k, "image_recolor")) lv_obj_set_style_image_recolor(obj, hml_ui_parse_color(sv, lv_color_black()), sel), lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, sel);
        else if(hml_strieq(k, "width")) lv_obj_set_style_width(obj, parse_size(p, 0), sel);
        else if(hml_strieq(k, "height")) lv_obj_set_style_height(obj, parse_size(p, 0), sel);
        else HML_LOG_WARN("unknown style property '%s'", k);
    }
}

static void apply_styles(lv_obj_t *obj, const cJSON *w)
{
    static const struct { const char *key; lv_style_selector_t sel; } tbl[] = {
        { "style", LV_PART_MAIN },
        { "style_pressed", (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_PRESSED },
        { "style_checked", (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_CHECKED },
        { "style_disabled", (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_DISABLED },
        { "style_focused", (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_FOCUSED },
        { "style_indicator", LV_PART_INDICATOR },
        { "style_indicator_checked", (lv_style_selector_t)LV_PART_INDICATOR | (lv_style_selector_t)LV_STATE_CHECKED },
        { "style_knob", LV_PART_KNOB },
        { "style_items", LV_PART_ITEMS },
        { "style_selected", LV_PART_SELECTED },
        { "style_scrollbar", LV_PART_SCROLLBAR },
    };
    for(size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        const cJSON *st = cJSON_GetObjectItemCaseSensitive(w, tbl[i].key);
        if(st) apply_style(obj, st, tbl[i].sel);
    }
}

/* ================================================================ bindings */

static bool cond_true(const binding_t *b, const hml_value_t *v)
{
    bool r;
    if(b->cmp == CMP_NONE) {
        r = (v->type == HML_TYPE_STRING) ? v->str[0] != '\0' : v->num != 0;
    }
    else if(b->cmp_is_str) {
        int c = strcmp(v->type == HML_TYPE_STRING ? v->str : "", b->cmp_str);
        r = (b->cmp == CMP_EQ) ? c == 0 : (b->cmp == CMP_NE) ? c != 0 : false;
    }
    else {
        double n = (v->type == HML_TYPE_STRING) ? atof(v->str) : v->num;
        switch(b->cmp) {
            case CMP_EQ: r = n == b->cmp_num; break;
            case CMP_NE: r = n != b->cmp_num; break;
            case CMP_GT: r = n > b->cmp_num; break;
            case CMP_LT: r = n < b->cmp_num; break;
            case CMP_GE: r = n >= b->cmp_num; break;
            case CMP_LE: r = n <= b->cmp_num; break;
            default: r = false;
        }
    }
    return b->invert ? !r : r;
}

static bool map_color(const binding_t *b, const hml_value_t *v, lv_color_t *out)
{
    double n = (v->type == HML_TYPE_STRING) ? atof(v->str) : v->num;
    for(int i = 0; i < b->map_n; i++) {
        if(b->map[i].key == n) { *out = b->map[i].color; return true; }
    }
    if(b->has_dflt_color) { *out = b->dflt_color; return true; }
    return false;
}

static void bind_apply_value(binding_t *b, double n)
{
    int32_t iv = (int32_t)llround(n * b->scale + b->offset);
    switch(b->wkind) {
        case W_SLIDER:   lv_slider_set_value(b->obj, iv, LV_ANIM_OFF); break;
        case W_BAR:      lv_bar_set_value(b->obj, iv, LV_ANIM_ON); break;
        case W_ARC:      lv_arc_set_value(b->obj, iv); break;
        case W_DROPDOWN: lv_dropdown_set_selected(b->obj, (uint32_t)(iv < 0 ? 0 : iv)); break;
        case W_ROLLER:   lv_roller_set_selected(b->obj, (uint32_t)(iv < 0 ? 0 : iv), LV_ANIM_OFF); break;
        case W_LED:      lv_led_set_brightness(b->obj, (uint8_t)(iv < 0 ? 0 : iv > 255 ? 255 : iv)); break;
        default: {
            char buf[HML_STR_MAX * 2];
            hml_value_t tmp;
            hml_value_set_num(&tmp, HML_TYPE_FLOAT, n * b->scale + b->offset);
            hml_value_format(&tmp, b->format[0] ? b->format : NULL, buf, sizeof(buf));
            if(b->wkind == W_TEXTAREA) lv_textarea_set_text(b->obj, buf);
            else if(b->wkind == W_CHECKBOX) lv_checkbox_set_text(b->obj, buf);
            else lv_label_set_text(b->obj, buf);
            break;
        }
    }
}

static void on_tag_changed(hml_tag_t *tag, const hml_value_t *v, void *source, void *user)
{
    binding_t *b = user;
    (void)tag;
    if(source == b) return;   /* our own write echoed back */

    switch(b->kind) {
        case B_TEXT: {
            char buf[HML_STR_MAX * 2];
            hml_value_format(v, b->format[0] ? b->format : NULL, buf, sizeof(buf));
            if(b->wkind == W_TEXTAREA) lv_textarea_set_text(b->obj, buf);
            else if(b->wkind == W_CHECKBOX) lv_checkbox_set_text(b->obj, buf);
            else if(b->wkind == W_IMAGE) lv_image_set_src(b->obj, buf);
            else lv_label_set_text(b->obj, buf);
            break;
        }
        case B_VALUE:
            bind_apply_value(b, (v->type == HML_TYPE_STRING) ? atof(v->str) : v->num);
            break;
        case B_CHECKED: {
            bool on = cond_true(b, v);
            if(b->wkind == W_LED) { if(on) lv_led_on(b->obj); else lv_led_off(b->obj); }
            else if(on) lv_obj_add_state(b->obj, LV_STATE_CHECKED);
            else lv_obj_remove_state(b->obj, LV_STATE_CHECKED);
            break;
        }
        case B_VISIBLE:
            if(cond_true(b, v)) lv_obj_remove_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
            break;
        case B_ENABLED:
            if(cond_true(b, v)) lv_obj_remove_state(b->obj, LV_STATE_DISABLED);
            else lv_obj_add_state(b->obj, LV_STATE_DISABLED);
            break;
        case B_BG_COLOR: {
            lv_color_t c;
            if(map_color(b, v, &c)) {
                lv_obj_set_style_bg_color(b->obj, c, b->part);
                lv_obj_set_style_bg_opa(b->obj, LV_OPA_COVER, b->part);
            }
            break;
        }
        case B_TEXT_COLOR: {
            lv_color_t c;
            if(map_color(b, v, &c)) lv_obj_set_style_text_color(b->obj, c, b->part);
            break;
        }
        case B_OPTIONS:
            if(b->wkind == W_DROPDOWN) lv_dropdown_set_options(b->obj, v->str);
            else if(b->wkind == W_ROLLER) lv_roller_set_options(b->obj, v->str, LV_ROLLER_MODE_NORMAL);
            break;
    }
}

/* widget -> tag (two-way bindings) */
static void on_widget_value_changed(lv_event_t *e)
{
    binding_t *b = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if(b->kind == B_CHECKED) {
        bool on = lv_obj_has_state(obj, LV_STATE_CHECKED);
        if(b->invert) on = !on;
        if(b->cmp == CMP_EQ && !b->cmp_is_str) {
            if(on) hml_tag_set_num(b->tag, b->cmp_num, b);
        }
        else hml_tag_set_num(b->tag, on ? 1 : 0, b);
        return;
    }
    if(b->kind == B_TEXT) {
        if(b->wkind == W_TEXTAREA) hml_tag_set_str(b->tag, lv_textarea_get_text(obj), b);
        return;
    }
    if(b->kind == B_VALUE) {
        double wv = 0;
        switch(b->wkind) {
            case W_SLIDER:   wv = lv_slider_get_value(obj); break;
            case W_ARC:      wv = lv_arc_get_value(obj); break;
            case W_DROPDOWN: wv = lv_dropdown_get_selected(obj); break;
            case W_ROLLER:   wv = lv_roller_get_selected(obj); break;
            case W_TEXTAREA: wv = atof(lv_textarea_get_text(obj)); break;
            default: return;
        }
        double tv = b->scale != 0 ? (wv - b->offset) / b->scale : wv;
        hml_tag_set_num(b->tag, tv, b);
    }
}

static void parse_cmp(binding_t *b, const cJSON *spec)
{
    static const struct { const char *k; cmp_t c; } tbl[] = {
        { "eq", CMP_EQ }, { "ne", CMP_NE }, { "gt", CMP_GT }, { "lt", CMP_LT }, { "ge", CMP_GE }, { "le", CMP_LE },
    };
    for(size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(spec, tbl[i].k);
        if(!v) continue;
        b->cmp = tbl[i].c;
        if(cJSON_IsString(v)) {
            b->cmp_is_str = true;
            snprintf(b->cmp_str, sizeof(b->cmp_str), "%s", v->valuestring);
            /* allow "eq": "3" on numeric tags */
            if(hml_tag_type(b->tag) != HML_TYPE_STRING) { b->cmp_is_str = false; b->cmp_num = atof(v->valuestring); }
        }
        else if(cJSON_IsBool(v)) b->cmp_num = cJSON_IsTrue(v) ? 1 : 0;
        else b->cmp_num = v->valuedouble;
        return;
    }
}

static binding_t *add_binding(lv_obj_t *obj, widget_kind_t wk, bind_kind_t kind, const cJSON *spec, bool two_way)
{
    const char *tagname = cJSON_IsString(spec) ? spec->valuestring : hml_json_str(spec, "tag", NULL);
    hml_tag_t *tag = hml_tag_find(tagname);
    if(!tag) { HML_LOG_WARN("bind: unknown tag '%s'", tagname ? tagname : "(null)"); return NULL; }

    binding_t *b = calloc(1, sizeof(*b));
    if(!b) return NULL;
    b->obj = obj; b->wkind = wk; b->kind = kind; b->tag = tag;
    b->scale = 1; b->offset = 0; b->part = LV_PART_MAIN;
    if(cJSON_IsObject(spec)) {
        const char *fmt = hml_json_str(spec, "format", NULL);
        if(fmt) snprintf(b->format, sizeof(b->format), "%s", fmt);
        b->invert = hml_json_bool(spec, "invert", false);
        b->scale = hml_json_num(spec, "scale", 1);
        b->offset = hml_json_num(spec, "offset", 0);
        if(hml_strieq(hml_json_str(spec, "part", "main"), "indicator")) b->part = LV_PART_INDICATOR;
        else if(hml_strieq(hml_json_str(spec, "part", "main"), "knob")) b->part = LV_PART_KNOB;
        parse_cmp(b, spec);
        const cJSON *map = cJSON_GetObjectItemCaseSensitive(spec, "map");
        if(cJSON_IsObject(map)) {
            const cJSON *m;
            cJSON_ArrayForEach(m, map) {
                if(!cJSON_IsString(m)) continue;
                if(hml_strieq(m->string, "default")) { b->has_dflt_color = true; b->dflt_color = hml_ui_parse_color(m->valuestring, lv_color_black()); }
                else if(b->map_n < 16) {
                    double key = hml_strieq(m->string, "true") ? 1 : hml_strieq(m->string, "false") ? 0 : atof(m->string);
                    b->map[b->map_n].key = key;
                    b->map[b->map_n].color = hml_ui_parse_color(m->valuestring, lv_color_black());
                    b->map_n++;
                }
            }
        }
        two_way = hml_json_bool(spec, "two_way", two_way);
    }
    b->two_way = two_way;
    b->next = s_bindings;
    s_bindings = b;

    hml_tag_subscribe(tag, HML_SUB_GROUP_UI, on_tag_changed, b);
    if(two_way) lv_obj_add_event_cb(obj, on_widget_value_changed, LV_EVENT_VALUE_CHANGED, b);

    /* initial value */
    hml_value_t v;
    hml_tag_get(tag, &v);
    on_tag_changed(tag, &v, NULL, b);
    return b;
}

/* bind property with inverted meaning ("hidden", "disabled") */
static void add_binding_inverted(lv_obj_t *obj, widget_kind_t wk, bind_kind_t kind, const cJSON *spec)
{
    binding_t *b = add_binding(obj, wk, kind, spec, false);
    if(!b) return;
    b->invert = !b->invert;
    hml_value_t v;
    hml_tag_get(b->tag, &v);
    on_tag_changed(b->tag, &v, NULL, b);
}

static void parse_bindings(lv_obj_t *obj, lv_obj_t *text_obj, widget_kind_t wk, const cJSON *w)
{
    const cJSON *bind = cJSON_GetObjectItemCaseSensitive(w, "bind");
    if(!bind) return;
    if(cJSON_IsString(bind)) {
        /* "bind": "tag" - pick the natural property for the widget */
        switch(wk) {
            case W_SWITCH: case W_CHECKBOX: case W_LED: add_binding(obj, wk, B_CHECKED, bind, wk != W_LED); break;
            case W_BUTTON: add_binding(obj, wk, B_CHECKED, bind, true); break;
            case W_SLIDER: case W_BAR: case W_ARC: case W_DROPDOWN: case W_ROLLER:
                add_binding(obj, wk, B_VALUE, bind, wk != W_BAR); break;
            case W_TEXTAREA: add_binding(obj, wk, B_TEXT, bind, true); break;
            default: add_binding(text_obj ? text_obj : obj, wk, B_TEXT, bind, false); break;
        }
        return;
    }
    if(!cJSON_IsObject(bind)) return;
    const cJSON *spec;
    cJSON_ArrayForEach(spec, bind) {
        const char *k = spec->string;
        if(hml_strieq(k, "text")) add_binding(text_obj ? text_obj : obj, wk, B_TEXT, spec, wk == W_TEXTAREA);
        else if(hml_strieq(k, "value")) add_binding(obj, wk, B_VALUE, spec, wk == W_SLIDER || wk == W_ARC || wk == W_DROPDOWN || wk == W_ROLLER || wk == W_TEXTAREA);
        else if(hml_strieq(k, "checked") || hml_strieq(k, "on")) add_binding(obj, wk, B_CHECKED, spec, wk == W_SWITCH || wk == W_CHECKBOX || wk == W_BUTTON);
        else if(hml_strieq(k, "visible")) add_binding(obj, wk, B_VISIBLE, spec, false);
        else if(hml_strieq(k, "hidden")) add_binding_inverted(obj, wk, B_VISIBLE, spec);
        else if(hml_strieq(k, "enabled")) add_binding(obj, wk, B_ENABLED, spec, false);
        else if(hml_strieq(k, "disabled")) add_binding_inverted(obj, wk, B_ENABLED, spec);
        else if(hml_strieq(k, "bg_color")) add_binding(obj, wk, B_BG_COLOR, spec, false);
        else if(hml_strieq(k, "text_color")) add_binding(text_obj ? text_obj : obj, wk, B_TEXT_COLOR, spec, false);
        else if(hml_strieq(k, "options")) add_binding(obj, wk, B_OPTIONS, spec, false);
        else if(hml_strieq(k, "src")) add_binding(obj, wk, B_TEXT, spec, false);
        else HML_LOG_WARN("bind: unknown property '%s'", k);
    }
}

/* ================================================================ actions */

static void run_actions(const action_list_t *list)
{
    for(const action_t *a = list->first; a; a = a->next) {
        switch(a->kind) {
            case A_GOTO:   hml_ui_show_screen(a->screen, a->anim); break;
            case A_BACK:   hml_ui_back(); break;
            case A_SET:    if(a->tag) hml_tag_set(a->tag, &a->value, NULL); break;
            case A_TOGGLE: if(a->tag) hml_tag_set_num(a->tag, hml_tag_get_num(a->tag) != 0 ? 0 : 1, NULL); break;
            case A_INC:    if(a->tag) hml_tag_set_num(a->tag, hml_tag_get_num(a->tag) + a->by, NULL); break;
            case A_DEC:    if(a->tag) hml_tag_set_num(a->tag, hml_tag_get_num(a->tag) - a->by, NULL); break;
            case A_RELOAD: if(s_app_cb.request_reload) s_app_cb.request_reload(); break;
            case A_EXIT:   if(s_app_cb.request_exit) s_app_cb.request_exit(); break;
        }
    }
}

static void on_action_event(lv_event_t *e)
{
    run_actions(lv_event_get_user_data(e));
}

static action_t *parse_action(const cJSON *j)
{
    const char *kind = hml_json_str(j, "action", NULL);
    if(!kind) { HML_LOG_WARN("action without \"action\" key"); return NULL; }
    action_t *a = calloc(1, sizeof(*a));
    if(!a) return NULL;
    if(hml_strieq(kind, "goto")) {
        a->kind = A_GOTO;
        snprintf(a->screen, sizeof(a->screen), "%s", hml_json_str(j, "screen", ""));
        snprintf(a->anim, sizeof(a->anim), "%s", hml_json_str(j, "anim", "fade"));
    }
    else if(hml_strieq(kind, "back")) a->kind = A_BACK;
    else if(hml_strieq(kind, "reload")) a->kind = A_RELOAD;
    else if(hml_strieq(kind, "exit")) a->kind = A_EXIT;
    else {
        const char *tagname = hml_json_str(j, "tag", NULL);
        a->tag = hml_tag_find(tagname);
        if(!a->tag) { HML_LOG_WARN("action '%s': unknown tag '%s'", kind, tagname ? tagname : "(null)"); free(a); return NULL; }
        if(hml_strieq(kind, "set")) {
            a->kind = A_SET;
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "value");
            if(cJSON_IsString(v)) hml_value_set_str(&a->value, v->valuestring);
            else if(cJSON_IsBool(v)) hml_value_set_num(&a->value, HML_TYPE_BOOL, cJSON_IsTrue(v));
            else hml_value_set_num(&a->value, HML_TYPE_FLOAT, cJSON_IsNumber(v) ? v->valuedouble : 0);
        }
        else if(hml_strieq(kind, "toggle")) a->kind = A_TOGGLE;
        else if(hml_strieq(kind, "inc")) { a->kind = A_INC; a->by = hml_json_num(j, "by", 1); }
        else if(hml_strieq(kind, "dec")) { a->kind = A_DEC; a->by = hml_json_num(j, "by", 1); }
        else { HML_LOG_WARN("unknown action '%s'", kind); free(a); return NULL; }
    }
    return a;
}

static void parse_actions(lv_obj_t *obj, const cJSON *w)
{
    static const struct { const char *key; lv_event_code_t ev; } tbl[] = {
        { "on_click", LV_EVENT_CLICKED }, { "on_press", LV_EVENT_PRESSED }, { "on_release", LV_EVENT_RELEASED },
        { "on_long_press", LV_EVENT_LONG_PRESSED }, { "on_long_press_repeat", LV_EVENT_LONG_PRESSED_REPEAT },
        { "on_change", LV_EVENT_VALUE_CHANGED }, { "on_ready", LV_EVENT_READY },
    };
    for(size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        const cJSON *acts = cJSON_GetObjectItemCaseSensitive(w, tbl[i].key);
        if(!acts) continue;
        action_list_t *list = calloc(1, sizeof(*list));
        if(!list) return;
        action_t **tail = &list->first;
        if(cJSON_IsArray(acts)) {
            const cJSON *j;
            cJSON_ArrayForEach(j, acts) {
                action_t *a = parse_action(j);
                if(a) { *tail = a; tail = &a->next; }
            }
        }
        else if(cJSON_IsObject(acts)) {
            action_t *a = parse_action(acts);
            if(a) *tail = a;
        }
        list->next = s_action_lists;
        s_action_lists = list;
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, on_action_event, tbl[i].ev, list);
    }
}

/* ================================================================ chart */

static void chart_tick(lv_timer_t *t)
{
    chart_ctx_t *c = lv_timer_get_user_data(t);
    for(int i = 0; i < c->series_n; i++) {
        double v = hml_tag_get_num(c->series[i].tag) * c->series[i].scale;
        lv_chart_set_next_value(c->chart, c->series[i].ser, (int32_t)llround(v));
    }
}

static void build_chart(lv_obj_t *chart, const cJSON *w)
{
    const char *type = hml_json_str(w, "chart_type", "line");
    lv_chart_set_type(chart, hml_strieq(type, "bar") ? LV_CHART_TYPE_BAR : hml_strieq(type, "scatter") ? LV_CHART_TYPE_SCATTER : LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, (uint32_t)hml_json_num(w, "points", 50));
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)hml_json_num(w, "min", 0), (int32_t)hml_json_num(w, "max", 100));
    lv_chart_set_update_mode(chart, hml_strieq(hml_json_str(w, "update_mode", "shift"), "circular") ? LV_CHART_UPDATE_MODE_CIRCULAR : LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, (uint8_t)hml_json_num(w, "hdiv", 5), (uint8_t)hml_json_num(w, "vdiv", 8));
    if(hml_json_bool(w, "no_points", true)) { lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR); lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR); }

    const cJSON *series = cJSON_GetObjectItemCaseSensitive(w, "series");
    if(!cJSON_IsArray(series)) return;
    chart_ctx_t *c = calloc(1, sizeof(*c));
    if(!c) return;
    c->chart = chart;
    const cJSON *s;
    cJSON_ArrayForEach(s, series) {
        if(c->series_n >= 8) break;
        const char *tagname = hml_json_str(s, "tag", NULL);
        hml_tag_t *tag = hml_tag_find(tagname);
        if(!tag) { HML_LOG_WARN("chart: unknown tag '%s'", tagname ? tagname : "(null)"); continue; }
        lv_color_t col = hml_ui_parse_color(hml_json_str(s, "color", NULL), lv_palette_main(LV_PALETTE_BLUE));
        c->series[c->series_n].ser = lv_chart_add_series(chart, col, LV_CHART_AXIS_PRIMARY_Y);
        c->series[c->series_n].tag = tag;
        c->series[c->series_n].scale = hml_json_num(s, "scale", 1);
        c->series_n++;
    }
    if(c->series_n == 0) { free(c); return; }
    uint32_t sample_ms = (uint32_t)hml_json_num(w, "sample_ms", 500);
    c->timer = lv_timer_create(chart_tick, sample_ms ? sample_ms : 500, c);
    c->next = s_charts;
    s_charts = c;
    chart_tick(c->timer);
}

/* ================================================================ widgets */

static void on_textarea_focused(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target_obj(e);
    screen_t *scr = lv_event_get_user_data(e);
    if(!scr->keyboard) return;
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(scr->keyboard, ta);
        lv_obj_remove_flag(scr->keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(scr->keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_keyboard_done(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target_obj(e);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    /* Do not touch the group focus here: re-focusing the textarea would send
     * LV_EVENT_FOCUSED and immediately re-open the keyboard. */
}

static lv_obj_t *build_widget(lv_obj_t *parent, const cJSON *w);

static void free_user_data_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void build_children(lv_obj_t *parent, const cJSON *w)
{
    const cJSON *children = cJSON_GetObjectItemCaseSensitive(w, "children");
    if(!cJSON_IsArray(children)) return;
    const cJSON *c;
    cJSON_ArrayForEach(c, children) build_widget(parent, c);
}

static void apply_layout(lv_obj_t *obj, const cJSON *w)
{
    const char *flex = hml_json_str(w, "flex", NULL);
    if(flex) {
        lv_flex_flow_t flow = LV_FLEX_FLOW_ROW;
        if(hml_strieq(flex, "column")) flow = LV_FLEX_FLOW_COLUMN;
        else if(hml_strieq(flex, "row_wrap")) flow = LV_FLEX_FLOW_ROW_WRAP;
        else if(hml_strieq(flex, "column_wrap")) flow = LV_FLEX_FLOW_COLUMN_WRAP;
        else if(hml_strieq(flex, "row_reverse")) flow = LV_FLEX_FLOW_ROW_REVERSE;
        else if(hml_strieq(flex, "column_reverse")) flow = LV_FLEX_FLOW_COLUMN_REVERSE;
        lv_obj_set_flex_flow(obj, flow);
        lv_obj_set_flex_align(obj,
                              parse_flex_align(hml_json_str(w, "flex_main", NULL), LV_FLEX_ALIGN_START),
                              parse_flex_align(hml_json_str(w, "flex_cross", NULL), LV_FLEX_ALIGN_START),
                              parse_flex_align(hml_json_str(w, "flex_track", NULL), LV_FLEX_ALIGN_START));
    }
    if(hml_json_has(w, "flex_grow")) lv_obj_set_flex_grow(obj, (uint8_t)hml_json_num(w, "flex_grow", 0));
}

static void apply_common(lv_obj_t *obj, const cJSON *w)
{
    const cJSON *jw = cJSON_GetObjectItemCaseSensitive(w, "w");
    const cJSON *jh = cJSON_GetObjectItemCaseSensitive(w, "h");
    if(jw) lv_obj_set_width(obj, parse_size(jw, LV_SIZE_CONTENT));
    if(jh) lv_obj_set_height(obj, parse_size(jh, LV_SIZE_CONTENT));

    const char *align = hml_json_str(w, "align", NULL);
    if(align) lv_obj_set_align(obj, parse_align(align));
    if(hml_json_has(w, "x")) lv_obj_set_x(obj, parse_size(cJSON_GetObjectItemCaseSensitive(w, "x"), 0));
    if(hml_json_has(w, "y")) lv_obj_set_y(obj, parse_size(cJSON_GetObjectItemCaseSensitive(w, "y"), 0));

    if(hml_json_bool(w, "hidden", false)) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if(hml_json_has(w, "clickable")) {
        if(hml_json_bool(w, "clickable", true)) lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        else lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    if(hml_json_has(w, "scrollable") && !hml_json_bool(w, "scrollable", true)) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    }
    const char *sb = hml_json_str(w, "scrollbar", NULL);
    if(sb) lv_obj_set_scrollbar_mode(obj, hml_strieq(sb, "off") ? LV_SCROLLBAR_MODE_OFF : hml_strieq(sb, "on") ? LV_SCROLLBAR_MODE_ON : hml_strieq(sb, "active") ? LV_SCROLLBAR_MODE_ACTIVE : LV_SCROLLBAR_MODE_AUTO);
    if(hml_json_bool(w, "checkable", false)) lv_obj_add_flag(obj, LV_OBJ_FLAG_CHECKABLE);
    if(hml_json_bool(w, "checked", false)) lv_obj_add_state(obj, LV_STATE_CHECKED);
    if(hml_json_bool(w, "disabled", false)) lv_obj_add_state(obj, LV_STATE_DISABLED);

    apply_layout(obj, w);
    apply_styles(obj, w);
}

static lv_obj_t *build_widget(lv_obj_t *parent, const cJSON *w)
{
    if(!cJSON_IsObject(w)) return NULL;
    const char *type = hml_json_str(w, "type", "panel");
    lv_obj_t *obj = NULL, *text_obj = NULL;
    widget_kind_t wk = W_PANEL;

    if(hml_strieq(type, "panel") || hml_strieq(type, "container") || hml_strieq(type, "obj")) {
        obj = lv_obj_create(parent);
    }
    else if(hml_strieq(type, "label")) {
        wk = W_LABEL;
        obj = lv_label_create(parent);
        lv_label_set_text(obj, hml_json_str(w, "text", ""));
        const char *lm = hml_json_str(w, "long_mode", NULL);
        if(lm) lv_label_set_long_mode(obj, hml_strieq(lm, "dots") ? LV_LABEL_LONG_MODE_DOTS : hml_strieq(lm, "scroll") ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : hml_strieq(lm, "clip") ? LV_LABEL_LONG_MODE_CLIP : LV_LABEL_LONG_MODE_WRAP);
        text_obj = obj;
    }
    else if(hml_strieq(type, "button")) {
        wk = W_BUTTON;
        obj = lv_button_create(parent);
        const char *text = hml_json_str(w, "text", NULL);
        if(text || hml_json_has(w, "bind")) {
            text_obj = lv_label_create(obj);
            lv_label_set_text(text_obj, text ? text : "");
            lv_obj_center(text_obj);
            const cJSON *ls = cJSON_GetObjectItemCaseSensitive(w, "style_label");
            if(ls) apply_style(text_obj, ls, LV_PART_MAIN);
        }
    }
    else if(hml_strieq(type, "switch")) {
        wk = W_SWITCH;
        obj = lv_switch_create(parent);
    }
    else if(hml_strieq(type, "checkbox")) {
        wk = W_CHECKBOX;
        obj = lv_checkbox_create(parent);
        lv_checkbox_set_text(obj, hml_json_str(w, "text", ""));
    }
    else if(hml_strieq(type, "slider")) {
        wk = W_SLIDER;
        obj = lv_slider_create(parent);
        lv_slider_set_range(obj, (int32_t)hml_json_num(w, "min", 0), (int32_t)hml_json_num(w, "max", 100));
        lv_slider_set_value(obj, (int32_t)hml_json_num(w, "value", 0), LV_ANIM_OFF);
    }
    else if(hml_strieq(type, "bar")) {
        wk = W_BAR;
        obj = lv_bar_create(parent);
        lv_bar_set_range(obj, (int32_t)hml_json_num(w, "min", 0), (int32_t)hml_json_num(w, "max", 100));
        lv_bar_set_value(obj, (int32_t)hml_json_num(w, "value", 0), LV_ANIM_OFF);
    }
    else if(hml_strieq(type, "arc")) {
        wk = W_ARC;
        obj = lv_arc_create(parent);
        lv_arc_set_range(obj, (int32_t)hml_json_num(w, "min", 0), (int32_t)hml_json_num(w, "max", 100));
        lv_arc_set_bg_angles(obj, (lv_value_precise_t)hml_json_num(w, "start_angle", 135), (lv_value_precise_t)hml_json_num(w, "end_angle", 45));
        lv_arc_set_rotation(obj, (int32_t)hml_json_num(w, "rotation", 0));
        lv_arc_set_value(obj, (int32_t)hml_json_num(w, "value", 0));
        if(!hml_json_bool(w, "adjustable", true)) {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_style(obj, NULL, LV_PART_KNOB);
        }
    }
    else if(hml_strieq(type, "led")) {
        wk = W_LED;
        obj = lv_led_create(parent);
        lv_led_set_color(obj, hml_ui_parse_color(hml_json_str(w, "color", NULL), lv_palette_main(LV_PALETTE_GREEN)));
        if(hml_json_bool(w, "on", false)) lv_led_on(obj); else lv_led_off(obj);
    }
    else if(hml_strieq(type, "dropdown")) {
        wk = W_DROPDOWN;
        obj = lv_dropdown_create(parent);
        char *opts = join_options(cJSON_GetObjectItemCaseSensitive(w, "options"));
        if(opts) { lv_dropdown_set_options(obj, opts); free(opts); }
        lv_dropdown_set_selected(obj, (uint32_t)hml_json_num(w, "selected", 0));
    }
    else if(hml_strieq(type, "roller")) {
        wk = W_ROLLER;
        obj = lv_roller_create(parent);
        char *opts = join_options(cJSON_GetObjectItemCaseSensitive(w, "options"));
        if(opts) { lv_roller_set_options(obj, opts, hml_json_bool(w, "infinite", false) ? LV_ROLLER_MODE_INFINITE : LV_ROLLER_MODE_NORMAL); free(opts); }
        lv_roller_set_visible_row_count(obj, (uint32_t)hml_json_num(w, "visible_rows", 3));
        lv_roller_set_selected(obj, (uint32_t)hml_json_num(w, "selected", 0), LV_ANIM_OFF);
    }
    else if(hml_strieq(type, "textarea")) {
        wk = W_TEXTAREA;
        obj = lv_textarea_create(parent);
        lv_textarea_set_text(obj, hml_json_str(w, "text", ""));
        const char *ph = hml_json_str(w, "placeholder", NULL);
        if(ph) lv_textarea_set_placeholder_text(obj, ph);
        lv_textarea_set_one_line(obj, hml_json_bool(w, "one_line", true));
        lv_textarea_set_password_mode(obj, hml_json_bool(w, "password", false));
        if(hml_json_has(w, "max_length")) lv_textarea_set_max_length(obj, (uint32_t)hml_json_num(w, "max_length", 0));
        const char *acc = hml_json_str(w, "accepted_chars", NULL);
        if(acc) lv_textarea_set_accepted_chars(obj, acc);
        if(s_group) lv_group_add_obj(s_group, obj);
        if(s_building) {
            lv_obj_add_event_cb(obj, on_textarea_focused, LV_EVENT_FOCUSED, s_building);
            lv_obj_add_event_cb(obj, on_textarea_focused, LV_EVENT_CLICKED, s_building);
            lv_obj_add_event_cb(obj, on_textarea_focused, LV_EVENT_DEFOCUSED, s_building);
            lv_obj_add_event_cb(obj, on_textarea_focused, LV_EVENT_READY, s_building);
            lv_obj_add_event_cb(obj, on_textarea_focused, LV_EVENT_CANCEL, s_building);
        }
        text_obj = obj;
    }
    else if(hml_strieq(type, "image")) {
        wk = W_IMAGE;
        obj = lv_image_create(parent);
        const char *src = hml_json_str(w, "src", NULL);
        if(src) {
            char path[640];
            if(src[1] == ':' || src[0] == '/') snprintf(path, sizeof(path), "A:%s", src);
            else snprintf(path, sizeof(path), "A:%s/%s", s_base_dir, src);
            lv_image_set_src(obj, path);
        }
        if(hml_json_has(w, "zoom")) lv_image_set_scale(obj, (uint32_t)(hml_json_num(w, "zoom", 1.0) * 256));
        if(hml_json_has(w, "rotation")) lv_image_set_rotation(obj, (int32_t)(hml_json_num(w, "rotation", 0) * 10));
    }
    else if(hml_strieq(type, "chart")) {
        wk = W_CHART;
        obj = lv_chart_create(parent);
        build_chart(obj, w);
    }
    else if(hml_strieq(type, "spinner")) {
        wk = W_SPINNER;
        obj = lv_spinner_create(parent);
        lv_spinner_set_anim_params(obj, (uint32_t)hml_json_num(w, "time", 1000), (uint32_t)hml_json_num(w, "angle", 60));
    }
    else if(hml_strieq(type, "keyboard")) {
        wk = W_KEYBOARD;
        obj = lv_keyboard_create(parent);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(obj, on_keyboard_done, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(obj, on_keyboard_done, LV_EVENT_CANCEL, NULL);
        if(s_building) s_building->keyboard = obj;
    }
    else if(hml_strieq(type, "line")) {
        wk = W_LINE;
        obj = lv_line_create(parent);
        const cJSON *pts = cJSON_GetObjectItemCaseSensitive(w, "points");
        int n = cJSON_IsArray(pts) ? cJSON_GetArraySize(pts) : 0;
        if(n >= 2) {
            lv_point_precise_t *p = calloc((size_t)n, sizeof(*p));
            int i = 0;
            const cJSON *pt;
            cJSON_ArrayForEach(pt, pts) {
                if(cJSON_IsArray(pt) && cJSON_GetArraySize(pt) >= 2) {
                    p[i].x = (lv_value_precise_t)cJSON_GetArrayItem(pt, 0)->valuedouble;
                    p[i].y = (lv_value_precise_t)cJSON_GetArrayItem(pt, 1)->valuedouble;
                    i++;
                }
            }
            lv_line_set_points_mutable(obj, p, (uint32_t)i);
            lv_obj_add_event_cb(obj, free_user_data_cb, LV_EVENT_DELETE, p);
        }
    }
    else {
        HML_LOG_WARN("unknown widget type '%s'", type);
        return NULL;
    }

    lv_obj_add_style(obj, &s_style_disabled, LV_STATE_DISABLED);
    apply_common(obj, w);
    parse_bindings(obj, text_obj, wk, w);
    parse_actions(obj, w);
    build_children(obj, w);
    return obj;
}

/* ================================================================ screens */

void hml_ui_init(lv_indev_t *keyboard_indev, const hml_ui_app_cb_t *cb)
{
    if(cb) s_app_cb = *cb;
    s_kb_indev = keyboard_indev;
    if(!s_group) {
        lv_style_init(&s_style_disabled);
        lv_style_set_opa(&s_style_disabled, LV_OPA_40);
        s_group = lv_group_create();
        lv_group_set_default(s_group);
        if(s_kb_indev) lv_indev_set_group(s_kb_indev, s_group);
    }
}

lv_obj_t *hml_ui_build_screen(const cJSON *screen_json, const char *base_dir)
{
    if(!cJSON_IsObject(screen_json)) return NULL;
    const char *name = hml_json_str(screen_json, "name", NULL);
    if(!name) { HML_LOG_WARN("screen without \"name\" skipped"); return NULL; }
    if(find_screen(name)) { HML_LOG_WARN("screen '%s' defined twice", name); return NULL; }
    if(s_screen_count >= MAX_SCREENS) { HML_LOG_WARN("too many screens"); return NULL; }
    if(base_dir) snprintf(s_base_dir, sizeof(s_base_dir), "%s", base_dir);

    screen_t *scr = &s_screens[s_screen_count];
    memset(scr, 0, sizeof(*scr));
    snprintf(scr->name, sizeof(scr->name), "%s", name);
    scr->obj = lv_obj_create(NULL);
    s_building = scr;

    apply_layout(scr->obj, screen_json);
    apply_styles(scr->obj, screen_json);
    if(hml_json_has(screen_json, "scrollable") && !hml_json_bool(screen_json, "scrollable", true)) {
        lv_obj_remove_flag(scr->obj, LV_OBJ_FLAG_SCROLLABLE);
    }
    build_children(scr->obj, screen_json);

    s_building = NULL;
    s_screen_count++;
    return scr->obj;
}

void hml_ui_unload_all(void)
{
    hml_tags_unsubscribe_group(HML_SUB_GROUP_UI);

    while(s_charts) { chart_ctx_t *n = s_charts->next; if(s_charts->timer) lv_timer_delete(s_charts->timer); free(s_charts); s_charts = n; }
    while(s_bindings) { binding_t *n = s_bindings->next; free(s_bindings); s_bindings = n; }
    while(s_action_lists) {
        action_list_t *n = s_action_lists->next;
        action_t *a = s_action_lists->first;
        while(a) { action_t *an = a->next; free(a); a = an; }
        free(s_action_lists);
        s_action_lists = n;
    }

    /* LVGL needs an active screen at all times: create a blank one, then delete ours. */
    lv_obj_t *blank = lv_obj_create(NULL);
    lv_screen_load(blank);
    for(int i = 0; i < s_screen_count; i++) {
        if(s_screens[i].obj) lv_obj_delete(s_screens[i].obj);
    }
    s_screen_count = 0;
    s_current = -1;
    s_history_n = 0;
    /* `blank` is deleted automatically when the next screen is loaded (see show_screen) */
}

static void delete_old_screen_cb(lv_event_t *e)
{
    (void)e;
}

bool hml_ui_show_screen(const char *name, const char *anim)
{
    screen_t *scr = find_screen(name);
    if(!scr) { HML_LOG_WARN("goto: unknown screen '%s'", name ? name : "(null)"); return false; }
    int idx = (int)(scr - s_screens);
    if(idx == s_current) return true;

    lv_obj_t *prev = lv_screen_active();
    bool prev_is_ours = false;
    for(int i = 0; i < s_screen_count; i++) if(s_screens[i].obj == prev) prev_is_ours = true;

    if(s_current >= 0) {
        if(s_history_n == HISTORY_LEN) { memmove(s_history, s_history + 1, sizeof(int) * (HISTORY_LEN - 1)); s_history_n--; }
        s_history[s_history_n++] = s_current;
    }
    s_current = idx;
    lv_screen_load_anim(scr->obj, parse_anim(anim), anim && !hml_strieq(anim, "none") ? 250 : 0, 0, !prev_is_ours);
    (void)delete_old_screen_cb;
    return true;
}

bool hml_ui_back(void)
{
    if(s_history_n == 0) return false;
    int idx = s_history[--s_history_n];
    if(idx < 0 || idx >= s_screen_count) return false;
    s_current = idx;
    lv_screen_load_anim(s_screens[idx].obj, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
    return true;
}

const char *hml_ui_current_screen(void)
{
    return s_current >= 0 ? s_screens[s_current].name : NULL;
}

int hml_ui_screen_count(void)
{
    return s_screen_count;
}
