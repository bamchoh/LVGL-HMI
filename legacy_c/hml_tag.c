#include "hml_tag.h"
#include "hml_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    hml_tag_cb_t cb;
    void        *user;
    int          group;
} sub_t;

struct hml_tag {
    char        name[64];
    hml_type_t  type;
    hml_value_t value;
    bool        has_limits;
    double      min, max;
    bool        dirty;
    void       *last_source;
    sub_t      *subs;
    int         sub_count, sub_cap;
};

static hml_tag_t **s_tags;
static int         s_count, s_cap;
static hml_mutex_t s_lock;
static bool        s_inited;

void hml_tags_init(void)
{
    if(s_inited) return;
    hml_mutex_init(&s_lock);
    s_inited = true;
}

void hml_tags_deinit(void)
{
    if(!s_inited) return;
    for(int i = 0; i < s_count; i++) {
        free(s_tags[i]->subs);
        free(s_tags[i]);
    }
    free(s_tags);
    s_tags = NULL; s_count = s_cap = 0;
    hml_mutex_destroy(&s_lock);
    s_inited = false;
}

static void coerce(hml_tag_t *t, const hml_value_t *in, hml_value_t *out)
{
    out->type = t->type;
    out->str[0] = '\0';
    out->num = 0;
    switch(t->type) {
        case HML_TYPE_STRING:
            if(in->type == HML_TYPE_STRING) {
                strncpy(out->str, in->str, HML_STR_MAX - 1);
                out->str[HML_STR_MAX - 1] = '\0';
            }
            else {
                hml_value_format(in, NULL, out->str, HML_STR_MAX);
            }
            break;
        case HML_TYPE_BOOL:
            if(in->type == HML_TYPE_STRING) out->num = (hml_strieq(in->str, "true") || atof(in->str) != 0) ? 1 : 0;
            else out->num = (in->num != 0) ? 1 : 0;
            break;
        case HML_TYPE_INT:
        case HML_TYPE_FLOAT: {
            double n = (in->type == HML_TYPE_STRING) ? atof(in->str) : in->num;
            if(t->has_limits) {
                if(n < t->min) n = t->min;
                if(n > t->max) n = t->max;
            }
            if(t->type == HML_TYPE_INT) n = (double)(long long)llround(n);
            out->num = n;
            break;
        }
    }
}

hml_tag_t *hml_tag_create(const char *name, hml_type_t type, const hml_value_t *init,
                          bool has_limits, double min, double max)
{
    if(!name || !*name) return NULL;
    hml_tag_t *existing = hml_tag_find(name);
    if(existing) {
        HML_LOG_WARN("tag '%s' defined twice, keeping the first", name);
        return existing;
    }
    hml_tag_t *t = calloc(1, sizeof(*t));
    if(!t) return NULL;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->type = type;
    t->has_limits = has_limits;
    t->min = min; t->max = max;
    t->value.type = type;
    if(init) coerce(t, init, &t->value);
    t->dirty = false;   /* initial values are pulled by the UI; never pushed to drivers */

    hml_mutex_lock(&s_lock);
    if(s_count == s_cap) {
        int ncap = s_cap ? s_cap * 2 : 32;
        hml_tag_t **n = realloc(s_tags, (size_t)ncap * sizeof(*n));
        if(!n) { hml_mutex_unlock(&s_lock); free(t); return NULL; }
        s_tags = n; s_cap = ncap;
    }
    s_tags[s_count++] = t;
    hml_mutex_unlock(&s_lock);
    return t;
}

hml_tag_t *hml_tag_find(const char *name)
{
    if(!name) return NULL;
    hml_mutex_lock(&s_lock);
    hml_tag_t *r = NULL;
    for(int i = 0; i < s_count; i++) {
        if(strcmp(s_tags[i]->name, name) == 0) { r = s_tags[i]; break; }
    }
    hml_mutex_unlock(&s_lock);
    return r;
}

int        hml_tag_count(void)                  { return s_count; }
hml_tag_t *hml_tag_at(int idx)                  { return (idx >= 0 && idx < s_count) ? s_tags[idx] : NULL; }
const char *hml_tag_name(const hml_tag_t *t)    { return t->name; }
hml_type_t  hml_tag_type(const hml_tag_t *t)    { return t->type; }
bool hml_tag_limits(const hml_tag_t *t, double *min, double *max)
{
    if(min) *min = t->min;
    if(max) *max = t->max;
    return t->has_limits;
}

void hml_tag_get(hml_tag_t *t, hml_value_t *out)
{
    hml_mutex_lock(&s_lock);
    *out = t->value;
    hml_mutex_unlock(&s_lock);
}

double hml_tag_get_num(hml_tag_t *t)
{
    hml_mutex_lock(&s_lock);
    double n = (t->type == HML_TYPE_STRING) ? atof(t->value.str) : t->value.num;
    hml_mutex_unlock(&s_lock);
    return n;
}

bool hml_tag_set(hml_tag_t *t, const hml_value_t *v, void *source)
{
    hml_value_t nv;
    coerce(t, v, &nv);
    hml_mutex_lock(&s_lock);
    bool changed = (t->type == HML_TYPE_STRING) ? strcmp(t->value.str, nv.str) != 0
                                                : t->value.num != nv.num;
    if(changed) {
        t->value = nv;
        t->dirty = true;
        t->last_source = source;
    }
    hml_mutex_unlock(&s_lock);
    return changed;
}

bool hml_tag_set_num(hml_tag_t *t, double num, void *source)
{
    hml_value_t v;
    hml_value_set_num(&v, HML_TYPE_FLOAT, num);
    return hml_tag_set(t, &v, source);
}

bool hml_tag_set_str(hml_tag_t *t, const char *s, void *source)
{
    hml_value_t v;
    hml_value_set_str(&v, s);
    return hml_tag_set(t, &v, source);
}

void hml_tag_subscribe(hml_tag_t *t, int group, hml_tag_cb_t cb, void *user)
{
    hml_mutex_lock(&s_lock);
    if(t->sub_count == t->sub_cap) {
        int ncap = t->sub_cap ? t->sub_cap * 2 : 4;
        sub_t *n = realloc(t->subs, (size_t)ncap * sizeof(*n));
        if(!n) { hml_mutex_unlock(&s_lock); return; }
        t->subs = n; t->sub_cap = ncap;
    }
    t->subs[t->sub_count].cb = cb;
    t->subs[t->sub_count].user = user;
    t->subs[t->sub_count].group = group;
    t->sub_count++;
    hml_mutex_unlock(&s_lock);
}

void hml_tags_unsubscribe_group(int group)
{
    hml_mutex_lock(&s_lock);
    for(int i = 0; i < s_count; i++) {
        hml_tag_t *t = s_tags[i];
        int w = 0;
        for(int r = 0; r < t->sub_count; r++) {
            if(t->subs[r].group != group) t->subs[w++] = t->subs[r];
        }
        t->sub_count = w;
    }
    hml_mutex_unlock(&s_lock);
}

void hml_tags_dispatch(bool force_all)
{
    /* Snapshot under lock, deliver outside the lock (callbacks may write tags). */
    for(int i = 0; i < s_count; i++) {
        hml_tag_t *t = s_tags[i];
        hml_value_t v;
        void *src;
        sub_t *subs;
        int n;

        hml_mutex_lock(&s_lock);
        if(!t->dirty && !force_all) { hml_mutex_unlock(&s_lock); continue; }
        if(!force_all) t->dirty = false;   /* forced refresh keeps pending driver notifications */
        v = t->value;
        src = force_all ? NULL : t->last_source;
        n = t->sub_count;
        subs = n ? malloc((size_t)n * sizeof(sub_t)) : NULL;
        if(subs) memcpy(subs, t->subs, (size_t)n * sizeof(sub_t));
        hml_mutex_unlock(&s_lock);

        if(!subs) continue;
        for(int s = 0; s < n; s++) {
            /* a forced refresh is for the UI only: drivers must not re-write unchanged values */
            if(force_all && subs[s].group != HML_SUB_GROUP_UI) continue;
            subs[s].cb(t, &v, src, subs[s].user);
        }
        free(subs);
    }
}

/* ---------------------------------------------------------------- values */

void hml_value_set_num(hml_value_t *v, hml_type_t type, double num)
{
    v->type = type;
    v->num = num;
    v->str[0] = '\0';
}

void hml_value_set_str(hml_value_t *v, const char *s)
{
    v->type = HML_TYPE_STRING;
    v->num = 0;
    strncpy(v->str, s ? s : "", HML_STR_MAX - 1);
    v->str[HML_STR_MAX - 1] = '\0';
}

hml_type_t hml_type_from_name(const char *name, hml_type_t dflt)
{
    if(!name) return dflt;
    if(hml_strieq(name, "bool") || hml_strieq(name, "boolean")) return HML_TYPE_BOOL;
    if(hml_strieq(name, "int") || hml_strieq(name, "integer")) return HML_TYPE_INT;
    if(hml_strieq(name, "float") || hml_strieq(name, "real") || hml_strieq(name, "number")) return HML_TYPE_FLOAT;
    if(hml_strieq(name, "string") || hml_strieq(name, "text")) return HML_TYPE_STRING;
    return dflt;
}

const char *hml_type_name(hml_type_t t)
{
    switch(t) {
        case HML_TYPE_BOOL: return "bool";
        case HML_TYPE_INT: return "int";
        case HML_TYPE_FLOAT: return "float";
        default: return "string";
    }
}

/* Find the single printf conversion in fmt; returns the conversion char or 0. */
static char find_conversion(const char *fmt)
{
    for(const char *p = fmt; *p; p++) {
        if(*p != '%') continue;
        p++;
        if(*p == '%') continue;
        while(*p && strchr("-+ #0123456789.lhjzt", *p)) p++;
        return *p;
    }
    return 0;
}

void hml_value_format(const hml_value_t *v, const char *fmt, char *out, size_t n)
{
    if(!out || n == 0) return;
    if(!fmt || !*fmt) {
        switch(v->type) {
            case HML_TYPE_BOOL:   snprintf(out, n, "%s", v->num != 0 ? "true" : "false"); break;
            case HML_TYPE_INT:    snprintf(out, n, "%lld", (long long)v->num); break;
            case HML_TYPE_FLOAT:  snprintf(out, n, "%g", v->num); break;
            default:              snprintf(out, n, "%s", v->str); break;
        }
        return;
    }
    char conv = find_conversion(fmt);
    /* Rebuild the format with any length modifier stripped so we control the argument type. */
    char f2[160];
    size_t j = 0;
    for(const char *p = fmt; *p && j < sizeof(f2) - 4; p++) {
        if(*p == '%' && p[1] != '%') {
            f2[j++] = '%'; p++;
            while(*p && strchr("-+ #0123456789.", *p) && j < sizeof(f2) - 4) f2[j++] = *p++;
            while(*p && strchr("lhjzt", *p)) p++;              /* drop length modifiers */
            if(*p && strchr("diuxXo", *p)) { f2[j++] = 'l'; f2[j++] = 'l'; }
            if(*p) f2[j++] = *p;
            else break;
        }
        else if(*p == '%') { f2[j++] = '%'; f2[j++] = '%'; p++; }
        else f2[j++] = *p;
    }
    f2[j] = '\0';

    double num = (v->type == HML_TYPE_STRING) ? atof(v->str) : v->num;
    switch(conv) {
        case 'd': case 'i':
            snprintf(out, n, f2, (long long)llround(num)); break;
        case 'u': case 'x': case 'X': case 'o':
            snprintf(out, n, f2, (unsigned long long)(long long)llround(num)); break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            snprintf(out, n, f2, num); break;
        case 's': {
            char tmp[HML_STR_MAX];
            if(v->type == HML_TYPE_STRING) snprintf(tmp, sizeof(tmp), "%s", v->str);
            else hml_value_format(v, NULL, tmp, sizeof(tmp));
            snprintf(out, n, f2, tmp);
            break;
        }
        case 'c':
            snprintf(out, n, f2, (int)num); break;
        default: /* no conversion: literal text */
            snprintf(out, n, "%s", fmt); break;
    }
}

bool hml_value_parse(hml_type_t type, const char *text, hml_value_t *out)
{
    if(!text) return false;
    switch(type) {
        case HML_TYPE_STRING: hml_value_set_str(out, text); return true;
        case HML_TYPE_BOOL:
            if(hml_strieq(text, "true") || hml_strieq(text, "on") || hml_strieq(text, "1")) { hml_value_set_num(out, type, 1); return true; }
            if(hml_strieq(text, "false") || hml_strieq(text, "off") || hml_strieq(text, "0")) { hml_value_set_num(out, type, 0); return true; }
            return false;
        default: {
            char *end = NULL;
            double d = strtod(text, &end);
            if(end == text) return false;
            hml_value_set_num(out, type, d);
            return true;
        }
    }
}
