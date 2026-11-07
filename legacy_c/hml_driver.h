/* Data drivers: pluggable sources/sinks that move values between the tag store
 * and the outside world (PLC, simulation, ...). Each driver is created from a
 * JSON object in the project's "drivers" array, keyed by its "type". */
#ifndef HML_DRIVER_H
#define HML_DRIVER_H

#include <stdbool.h>
#include "cJSON.h"

typedef struct hml_driver hml_driver_t;

struct hml_driver {
    const char *type;
    char        name[32];
    void       *ctx;
    int  (*start)(hml_driver_t *d);    /* called once on the UI thread, LVGL is initialised */
    void (*stop)(hml_driver_t *d);     /* called on the UI thread, LVGL still alive */
    void (*destroy)(hml_driver_t *d);  /* free ctx and the driver itself */
};

typedef hml_driver_t *(*hml_driver_factory_t)(const cJSON *cfg);

void hml_drivers_register(const char *type, hml_driver_factory_t factory);
void hml_drivers_register_builtins(void);

/* Create and start every driver in the JSON array. Returns number started. */
int  hml_drivers_load(const cJSON *array);
void hml_drivers_stop_all(void);

/* Built-in factories */
hml_driver_t *hml_driver_sim_create(const cJSON *cfg);
hml_driver_t *hml_driver_modbus_tcp_create(const cJSON *cfg);

/* JSON helpers shared by drivers and the UI loader */
const char *hml_json_str(const cJSON *obj, const char *key, const char *dflt);
double      hml_json_num(const cJSON *obj, const char *key, double dflt);
bool        hml_json_bool(const cJSON *obj, const char *key, bool dflt);
bool        hml_json_has(const cJSON *obj, const char *key);

#endif
