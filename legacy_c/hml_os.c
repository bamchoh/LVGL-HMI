#include "hml_os.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32

void hml_mutex_init(hml_mutex_t *m)    { InitializeCriticalSection(&m->cs); }
void hml_mutex_destroy(hml_mutex_t *m) { DeleteCriticalSection(&m->cs); }
void hml_mutex_lock(hml_mutex_t *m)    { EnterCriticalSection(&m->cs); }
void hml_mutex_unlock(hml_mutex_t *m)  { LeaveCriticalSection(&m->cs); }

typedef struct { hml_thread_fn_t fn; void *arg; } thread_boot_t;

static DWORD WINAPI thread_tramp(LPVOID p)
{
    thread_boot_t b = *(thread_boot_t *)p;
    free(p);
    b.fn(b.arg);
    return 0;
}

int hml_thread_start(hml_thread_t *t, hml_thread_fn_t fn, void *arg)
{
    thread_boot_t *b = malloc(sizeof(*b));
    if(!b) return -1;
    b->fn = fn; b->arg = arg;
    t->h = CreateThread(NULL, 0, thread_tramp, b, 0, NULL);
    if(!t->h) { free(b); return -1; }
    return 0;
}

void hml_thread_join(hml_thread_t *t)
{
    if(!t->h) return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    t->h = NULL;
}

void hml_sleep_ms(uint32_t ms) { Sleep(ms); }
uint32_t hml_millis(void)      { return (uint32_t)GetTickCount64(); }

#else /* POSIX */

#include <unistd.h>
#include <stdlib.h>

void hml_mutex_init(hml_mutex_t *m)    { pthread_mutex_init(&m->m, NULL); }
void hml_mutex_destroy(hml_mutex_t *m) { pthread_mutex_destroy(&m->m); }
void hml_mutex_lock(hml_mutex_t *m)    { pthread_mutex_lock(&m->m); }
void hml_mutex_unlock(hml_mutex_t *m)  { pthread_mutex_unlock(&m->m); }

typedef struct { hml_thread_fn_t fn; void *arg; } thread_boot_t;

static void *thread_tramp(void *p)
{
    thread_boot_t b = *(thread_boot_t *)p;
    free(p);
    b.fn(b.arg);
    return NULL;
}

int hml_thread_start(hml_thread_t *t, hml_thread_fn_t fn, void *arg)
{
    thread_boot_t *b = malloc(sizeof(*b));
    if(!b) return -1;
    b->fn = fn; b->arg = arg;
    if(pthread_create(&t->t, NULL, thread_tramp, b) != 0) { free(b); return -1; }
    return 0;
}

void hml_thread_join(hml_thread_t *t) { pthread_join(t->t, NULL); }

void hml_sleep_ms(uint32_t ms) { usleep(ms * 1000u); }
uint32_t hml_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

#endif

bool hml_strieq(const char *a, const char *b)
{
    if(!a || !b) return false;
    while(*a && *b) {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

char *hml_strdup(const char *s)
{
    if(!s) s = "";
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    if(r) memcpy(r, s, n);
    return r;
}

void hml_log(const char *level, const char *fmt, ...)
{
    va_list ap;
    printf("[hml %-5s] ", level);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}
