#include "hml_driver.h"
#include "hml_os.h"
#include <string.h>
#include <stdlib.h>

#define MAX_FACTORIES 16
#define MAX_DRIVERS   32

typedef struct { const char *type; hml_driver_factory_t factory; } factory_t;

static factory_t     s_factories[MAX_FACTORIES];
static int           s_factory_count;
static hml_driver_t *s_drivers[MAX_DRIVERS];
static int           s_driver_count;

void hml_drivers_register(const char *type, hml_driver_factory_t factory)
{
    if(s_factory_count >= MAX_FACTORIES) return;
    s_factories[s_factory_count].type = type;
    s_factories[s_factory_count].factory = factory;
    s_factory_count++;
}

void hml_drivers_register_builtins(void)
{
    hml_drivers_register("sim", hml_driver_sim_create);
    hml_drivers_register("modbus_tcp", hml_driver_modbus_tcp_create);
}

int hml_drivers_load(const cJSON *array)
{
    int started = 0;
    if(!cJSON_IsArray(array)) return 0;
    const cJSON *cfg;
    cJSON_ArrayForEach(cfg, array) {
        const char *type = hml_json_str(cfg, "type", NULL);
        if(!type) { HML_LOG_WARN("driver without \"type\" skipped"); continue; }
        if(!hml_json_bool(cfg, "enabled", true)) { HML_LOG_INFO("driver '%s' disabled", type); continue; }
        hml_driver_factory_t f = NULL;
        for(int i = 0; i < s_factory_count; i++) {
            if(hml_strieq(s_factories[i].type, type)) { f = s_factories[i].factory; break; }
        }
        if(!f) { HML_LOG_WARN("unknown driver type '%s'", type); continue; }
        if(s_driver_count >= MAX_DRIVERS) { HML_LOG_WARN("too many drivers"); break; }
        hml_driver_t *d = f(cfg);
        if(!d) { HML_LOG_WARN("driver '%s' failed to initialise", type); continue; }
        const char *name = hml_json_str(cfg, "name", type);
        strncpy(d->name, name, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';
        if(d->start && d->start(d) != 0) {
            HML_LOG_WARN("driver '%s' failed to start", d->name);
            if(d->destroy) d->destroy(d);
            continue;
        }
        s_drivers[s_driver_count++] = d;
        HML_LOG_INFO("driver '%s' (%s) started", d->name, d->type);
        started++;
    }
    return started;
}

void hml_drivers_stop_all(void)
{
    for(int i = s_driver_count - 1; i >= 0; i--) {
        hml_driver_t *d = s_drivers[i];
        if(d->stop) d->stop(d);
        if(d->destroy) d->destroy(d);
    }
    s_driver_count = 0;
}

/* ---------------------------------------------------------------- JSON helpers */

const char *hml_json_str(const cJSON *obj, const char *key, const char *dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : dflt;
}

double hml_json_num(const cJSON *obj, const char *key, double dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if(cJSON_IsNumber(v)) return v->valuedouble;
    if(cJSON_IsBool(v)) return cJSON_IsTrue(v) ? 1 : 0;
    if(cJSON_IsString(v)) return atof(v->valuestring);
    return dflt;
}

bool hml_json_bool(const cJSON *obj, const char *key, bool dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if(cJSON_IsBool(v)) return cJSON_IsTrue(v);
    if(cJSON_IsNumber(v)) return v->valuedouble != 0;
    if(cJSON_IsString(v)) return hml_strieq(v->valuestring, "true");
    return dflt;
}

bool hml_json_has(const cJSON *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(obj, key) != NULL;
}
