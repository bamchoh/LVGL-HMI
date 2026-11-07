/* Tiny OS abstraction: mutex, thread, sleep, monotonic clock (Win32 + POSIX). */
#ifndef HML_OS_H
#define HML_OS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
typedef struct { CRITICAL_SECTION cs; } hml_mutex_t;
typedef struct { HANDLE h; } hml_thread_t;
#else
#  include <pthread.h>
typedef struct { pthread_mutex_t m; } hml_mutex_t;
typedef struct { pthread_t t; } hml_thread_t;
#endif

typedef void (*hml_thread_fn_t)(void *arg);

void hml_mutex_init(hml_mutex_t *m);
void hml_mutex_destroy(hml_mutex_t *m);
void hml_mutex_lock(hml_mutex_t *m);
void hml_mutex_unlock(hml_mutex_t *m);

int  hml_thread_start(hml_thread_t *t, hml_thread_fn_t fn, void *arg);
void hml_thread_join(hml_thread_t *t);

void     hml_sleep_ms(uint32_t ms);
uint32_t hml_millis(void);

/* Case-insensitive string equality (NULL-safe). */
bool hml_strieq(const char *a, const char *b);
/* malloc-backed string copy (NULL -> empty string). */
char *hml_strdup(const char *s);

/* Logging to stdout with a prefix. */
void hml_log(const char *level, const char *fmt, ...);
#define HML_LOG_INFO(...)  hml_log("INFO", __VA_ARGS__)
#define HML_LOG_WARN(...)  hml_log("WARN", __VA_ARGS__)
#define HML_LOG_ERROR(...) hml_log("ERROR", __VA_ARGS__)

#endif
