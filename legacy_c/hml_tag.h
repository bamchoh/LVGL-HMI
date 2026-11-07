/* Tag store: named, typed process variables shared between the UI and data drivers.
 *
 * - hml_tag_set() may be called from any thread.
 * - Subscribers are invoked on the UI thread from hml_tags_dispatch().
 * - Each write carries a "source" pointer so a writer can ignore its own echo.
 */
#ifndef HML_TAG_H
#define HML_TAG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HML_STR_MAX 128

typedef enum {
    HML_TYPE_BOOL,
    HML_TYPE_INT,
    HML_TYPE_FLOAT,
    HML_TYPE_STRING,
} hml_type_t;

typedef struct {
    hml_type_t type;
    double     num;              /* bool / int / float payload */
    char       str[HML_STR_MAX]; /* string payload */
} hml_value_t;

typedef struct hml_tag hml_tag_t;

typedef void (*hml_tag_cb_t)(hml_tag_t *tag, const hml_value_t *v, void *source, void *user);

/* Subscription groups: lets the UI drop all of its subscriptions on hot reload
 * without touching the drivers'. */
enum { HML_SUB_GROUP_DRIVER = 0, HML_SUB_GROUP_UI = 1 };

void       hml_tags_init(void);
void       hml_tags_deinit(void);

hml_tag_t *hml_tag_create(const char *name, hml_type_t type, const hml_value_t *init,
                          bool has_limits, double min, double max);
hml_tag_t *hml_tag_find(const char *name);
int        hml_tag_count(void);
hml_tag_t *hml_tag_at(int idx);

const char *hml_tag_name(const hml_tag_t *t);
hml_type_t  hml_tag_type(const hml_tag_t *t);
bool        hml_tag_limits(const hml_tag_t *t, double *min, double *max);

void   hml_tag_get(hml_tag_t *t, hml_value_t *out);
double hml_tag_get_num(hml_tag_t *t);

/* Set and mark dirty. Value is coerced to the tag's type and clamped to its limits.
 * Returns true if the stored value changed. */
bool hml_tag_set(hml_tag_t *t, const hml_value_t *v, void *source);
bool hml_tag_set_num(hml_tag_t *t, double num, void *source);
bool hml_tag_set_str(hml_tag_t *t, const char *s, void *source);

void hml_tag_subscribe(hml_tag_t *t, int group, hml_tag_cb_t cb, void *user);
void hml_tags_unsubscribe_group(int group);

/* UI thread: deliver pending changes to subscribers. If force_all is set every
 * tag is delivered to UI subscribers only (used right after screens are (re)built);
 * pending driver notifications are kept for the next normal dispatch. */
void hml_tags_dispatch(bool force_all);

/* ---- value helpers ---- */
void        hml_value_set_num(hml_value_t *v, hml_type_t type, double num);
void        hml_value_set_str(hml_value_t *v, const char *s);
hml_type_t  hml_type_from_name(const char *name, hml_type_t dflt);
const char *hml_type_name(hml_type_t t);
/* printf-style format with a single conversion (%d %x %f %s ...). NULL fmt = sensible default. */
void        hml_value_format(const hml_value_t *v, const char *fmt, char *out, size_t n);
/* Parse text into a value of the given type ("true", "12.5", ...). */
bool        hml_value_parse(hml_type_t type, const char *text, hml_value_t *out);

#endif
