/* Simulation driver: generates waveforms into tags so screens can be developed
 * without a PLC.
 *
 * {
 *   "type": "sim",
 *   "items": [
 *     { "tag": "temp",  "mode": "sine",     "min": 15, "max": 35, "period_ms": 10000 },
 *     { "tag": "level", "mode": "ramp",     "min": 0,  "max": 100, "period_ms": 5000 },
 *     { "tag": "blink", "mode": "square",   "period_ms": 1000 },
 *     { "tag": "noise", "mode": "random",   "min": 0,  "max": 10, "period_ms": 500 },
 *     { "tag": "count", "mode": "counter",  "min": 0,  "max": 9,  "period_ms": 1000 },
 *     { "tag": "txt",   "mode": "cycle",    "values": ["Idle","Run","Alarm"], "period_ms": 2000 }
 *   ]
 * }
 */
#include "hml_driver.h"
#include "hml_tag.h"
#include "hml_os.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum { SIM_SINE, SIM_TRIANGLE, SIM_RAMP, SIM_SQUARE, SIM_RANDOM, SIM_COUNTER, SIM_CYCLE } sim_mode_t;

typedef struct {
    hml_tag_t *tag;
    sim_mode_t mode;
    double     min, max;
    uint32_t   period_ms;
    uint32_t   last_step;
    int        step;
    char     **values;
    int        value_count;
} sim_item_t;

typedef struct {
    sim_item_t *items;
    int         count;
    uint32_t    t0;
    lv_timer_t *timer;
} sim_ctx_t;

static void sim_tick(lv_timer_t *timer)
{
    hml_driver_t *d = lv_timer_get_user_data(timer);
    sim_ctx_t *c = d->ctx;
    uint32_t now = hml_millis();
    for(int i = 0; i < c->count; i++) {
        sim_item_t *it = &c->items[i];
        double period = it->period_ms ? (double)it->period_ms : 1000.0;
        double phase = fmod((double)(now - c->t0), period) / period; /* 0..1 */
        double span = it->max - it->min;
        switch(it->mode) {
            case SIM_SINE:
                hml_tag_set_num(it->tag, it->min + span * (0.5 + 0.5 * sin(2 * M_PI * phase)), d);
                break;
            case SIM_TRIANGLE:
                hml_tag_set_num(it->tag, it->min + span * (phase < 0.5 ? phase * 2 : 2 - phase * 2), d);
                break;
            case SIM_RAMP:
                hml_tag_set_num(it->tag, it->min + span * phase, d);
                break;
            case SIM_SQUARE:
                hml_tag_set_num(it->tag, phase < 0.5 ? it->max : it->min, d);
                break;
            case SIM_RANDOM:
            case SIM_COUNTER:
            case SIM_CYCLE:
                if(now - it->last_step >= it->period_ms) {
                    it->last_step = now;
                    it->step++;
                    if(it->mode == SIM_RANDOM) {
                        hml_tag_set_num(it->tag, it->min + span * ((double)rand() / RAND_MAX), d);
                    }
                    else if(it->mode == SIM_COUNTER) {
                        int lo = (int)it->min, hi = (int)it->max;
                        int n = hi - lo + 1;
                        if(n < 1) n = 1;
                        hml_tag_set_num(it->tag, lo + (it->step % n), d);
                    }
                    else if(it->value_count > 0) {
                        hml_tag_set_str(it->tag, it->values[it->step % it->value_count], d);
                    }
                }
                break;
        }
    }
}

static int sim_start(hml_driver_t *d)
{
    sim_ctx_t *c = d->ctx;
    c->t0 = hml_millis();
    c->timer = lv_timer_create(sim_tick, 50, d);
    return c->timer ? 0 : -1;
}

static void sim_stop(hml_driver_t *d)
{
    sim_ctx_t *c = d->ctx;
    if(c->timer) { lv_timer_delete(c->timer); c->timer = NULL; }
}

static void sim_destroy(hml_driver_t *d)
{
    sim_ctx_t *c = d->ctx;
    for(int i = 0; i < c->count; i++) {
        for(int k = 0; k < c->items[i].value_count; k++) free(c->items[i].values[k]);
        free(c->items[i].values);
    }
    free(c->items);
    free(c);
    free(d);
}

hml_driver_t *hml_driver_sim_create(const cJSON *cfg)
{
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(cfg, "items");
    if(!cJSON_IsArray(items)) { HML_LOG_WARN("sim: \"items\" array missing"); return NULL; }

    sim_ctx_t *c = calloc(1, sizeof(*c));
    hml_driver_t *d = calloc(1, sizeof(*d));
    if(!c || !d) { free(c); free(d); return NULL; }
    int n = cJSON_GetArraySize(items);
    c->items = calloc((size_t)(n ? n : 1), sizeof(sim_item_t));

    const cJSON *it;
    cJSON_ArrayForEach(it, items) {
        const char *tagname = hml_json_str(it, "tag", NULL);
        hml_tag_t *tag = hml_tag_find(tagname);
        if(!tag) { HML_LOG_WARN("sim: unknown tag '%s'", tagname ? tagname : "(null)"); continue; }
        sim_item_t *s = &c->items[c->count];
        s->tag = tag;
        const char *mode = hml_json_str(it, "mode", "sine");
        if(hml_strieq(mode, "sine")) s->mode = SIM_SINE;
        else if(hml_strieq(mode, "triangle")) s->mode = SIM_TRIANGLE;
        else if(hml_strieq(mode, "ramp") || hml_strieq(mode, "sawtooth")) s->mode = SIM_RAMP;
        else if(hml_strieq(mode, "square") || hml_strieq(mode, "toggle")) s->mode = SIM_SQUARE;
        else if(hml_strieq(mode, "random")) s->mode = SIM_RANDOM;
        else if(hml_strieq(mode, "counter")) s->mode = SIM_COUNTER;
        else if(hml_strieq(mode, "cycle")) s->mode = SIM_CYCLE;
        else { HML_LOG_WARN("sim: unknown mode '%s'", mode); continue; }

        double tmin = 0, tmax = 1;
        bool has_lim = hml_tag_limits(tag, &tmin, &tmax);
        s->min = hml_json_num(it, "min", has_lim ? tmin : 0);
        s->max = hml_json_num(it, "max", has_lim ? tmax : (hml_tag_type(tag) == HML_TYPE_BOOL ? 1 : 100));
        s->period_ms = (uint32_t)hml_json_num(it, "period_ms", 1000);
        if(s->period_ms == 0) s->period_ms = 1;

        const cJSON *vals = cJSON_GetObjectItemCaseSensitive(it, "values");
        if(cJSON_IsArray(vals)) {
            int vn = cJSON_GetArraySize(vals);
            s->values = calloc((size_t)(vn ? vn : 1), sizeof(char *));
            const cJSON *v;
            cJSON_ArrayForEach(v, vals) {
                char buf[HML_STR_MAX];
                if(cJSON_IsString(v)) snprintf(buf, sizeof(buf), "%s", v->valuestring);
                else if(cJSON_IsNumber(v)) snprintf(buf, sizeof(buf), "%g", v->valuedouble);
                else continue;
                s->values[s->value_count++] = hml_strdup(buf);
            }
        }
        c->count++;
    }

    d->type = "sim";
    d->ctx = c;
    d->start = sim_start;
    d->stop = sim_stop;
    d->destroy = sim_destroy;
    return d;
}
