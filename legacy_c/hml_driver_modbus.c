/* Modbus TCP client driver.
 *
 * {
 *   "type": "modbus_tcp",
 *   "host": "192.168.0.10", "port": 502, "unit_id": 1,
 *   "poll_ms": 200, "timeout_ms": 1000,
 *   "status_tag": "plc_online",            // optional bool tag: connection state
 *   "map": [
 *     { "tag": "speed", "area": "holding", "addr": 0, "dtype": "u16", "scale": 1, "offset": 0 },
 *     { "tag": "temp",  "area": "input",   "addr": 10, "dtype": "s16", "scale": 0.1 },
 *     { "tag": "flow",  "area": "holding", "addr": 20, "dtype": "float", "swap_words": false },
 *     { "tag": "run",   "area": "coil",    "addr": 0 },
 *     { "tag": "fault", "area": "discrete","addr": 3 },
 *     { "tag": "cmd",   "area": "holding", "addr": 5, "access": "wo" }   // write only, never polled
 *   ]
 * }
 *
 * area:   coil (FC1/FC5) | discrete (FC2) | holding (FC3/FC6/FC16) | input (FC4)
 * dtype:  bool | u16 | s16 | u32 | s32 | float   (32-bit types occupy two registers, big-endian word order
 *         unless "swap_words" is true)
 * access: "rw" (default for coil/holding) | "ro" | "wo"
 *
 * Reads run on a worker thread; UI writes to a mapped tag are queued and sent by the same thread.
 */
#include "hml_driver.h"
#include "hml_tag.h"
#include "hml_os.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET sock_t;
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_close closesocket
#  define sock_errno WSAGetLastError()
#else
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <errno.h>
typedef int sock_t;
#  define SOCK_INVALID (-1)
#  define sock_close close
#  define sock_errno errno
#endif

typedef enum { AREA_COIL, AREA_DISCRETE, AREA_HOLDING, AREA_INPUT } mb_area_t;
typedef enum { DT_BOOL, DT_U16, DT_S16, DT_U32, DT_S32, DT_FLOAT } mb_dtype_t;

typedef struct {
    hml_tag_t *tag;
    mb_area_t  area;
    mb_dtype_t dtype;
    uint16_t   addr;
    double     scale, offset;
    bool       readable, writable, swap_words;
    int        pending_writes;   /* guarded by qlock: reads are ignored while a write is queued */
} mb_item_t;

typedef struct {
    int    item;
    double value;
} mb_write_t;

#define WRITE_QUEUE_LEN 64

typedef struct {
    char       host[128];
    uint16_t   port;
    uint8_t    unit;
    uint32_t   poll_ms, timeout_ms;
    hml_tag_t *status_tag;

    mb_item_t *items;
    int        item_count;

    hml_mutex_t  qlock;
    mb_write_t   queue[WRITE_QUEUE_LEN];
    int          q_head, q_tail;

    hml_thread_t thread;
    volatile bool running;
    sock_t       sock;
    uint16_t     tid;
    bool         online;
    hml_driver_t *drv;
} mb_ctx_t;

/* ---------------------------------------------------------------- sockets */

static bool sock_startup(void)
{
#ifdef _WIN32
    static bool done;
    if(!done) {
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        done = true;
    }
#endif
    return true;
}

static void set_timeout(sock_t s, uint32_t ms)
{
#ifdef _WIN32
    DWORD tv = ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv = { .tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static bool mb_connect(mb_ctx_t *c)
{
    struct addrinfo hints, *res = NULL;
    char portstr[8];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%u", c->port);
    if(getaddrinfo(c->host, portstr, &hints, &res) != 0 || !res) return false;

    sock_t s = SOCK_INVALID;
    for(struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if(s == SOCK_INVALID) continue;
        set_timeout(s, c->timeout_ms);
        if(connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        sock_close(s);
        s = SOCK_INVALID;
    }
    freeaddrinfo(res);
    if(s == SOCK_INVALID) return false;
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    c->sock = s;
    return true;
}

static void mb_disconnect(mb_ctx_t *c)
{
    if(c->sock != SOCK_INVALID) { sock_close(c->sock); c->sock = SOCK_INVALID; }
}

static bool send_all(sock_t s, const uint8_t *buf, int len)
{
    while(len > 0) {
        int n = send(s, (const char *)buf, len, 0);
        if(n <= 0) return false;
        buf += n; len -= n;
    }
    return true;
}

static bool recv_all(sock_t s, uint8_t *buf, int len)
{
    while(len > 0) {
        int n = recv(s, (char *)buf, len, 0);
        if(n <= 0) return false;
        buf += n; len -= n;
    }
    return true;
}

/* Send a PDU, receive the response PDU. Returns response PDU length or -1. */
static int mb_transact(mb_ctx_t *c, const uint8_t *pdu, int pdu_len, uint8_t *resp, int resp_max)
{
    uint8_t frame[7 + 260];
    uint16_t tid = ++c->tid;
    frame[0] = tid >> 8; frame[1] = tid & 0xFF;
    frame[2] = 0; frame[3] = 0;
    frame[4] = (uint8_t)((pdu_len + 1) >> 8); frame[5] = (uint8_t)((pdu_len + 1) & 0xFF);
    frame[6] = c->unit;
    memcpy(frame + 7, pdu, (size_t)pdu_len);
    if(!send_all(c->sock, frame, 7 + pdu_len)) return -1;

    uint8_t hdr[7];
    if(!recv_all(c->sock, hdr, 7)) return -1;
    int len = ((hdr[4] << 8) | hdr[5]) - 1;
    if(len < 1 || len > resp_max) return -1;
    if(!recv_all(c->sock, resp, len)) return -1;
    if(((hdr[0] << 8) | hdr[1]) != tid) return -1;
    if(resp[0] & 0x80) {
        HML_LOG_WARN("modbus: exception fc=0x%02x code=%d", resp[0] & 0x7F, len > 1 ? resp[1] : 0);
        return -2;
    }
    return len;
}

/* ---------------------------------------------------------------- encode/decode */

static int dtype_regs(mb_dtype_t dt)
{
    return (dt == DT_U32 || dt == DT_S32 || dt == DT_FLOAT) ? 2 : 1;
}

static double decode_regs(const mb_item_t *it, const uint8_t *b)
{
    uint16_t w0 = (uint16_t)((b[0] << 8) | b[1]);
    uint16_t w1 = (uint16_t)((b[2] << 8) | b[3]);
    if(it->swap_words) { uint16_t t = w0; w0 = w1; w1 = t; }
    uint32_t u32 = ((uint32_t)w0 << 16) | w1;
    switch(it->dtype) {
        case DT_U16: return w0;
        case DT_S16: return (int16_t)w0;
        case DT_U32: return u32;
        case DT_S32: return (int32_t)u32;
        case DT_FLOAT: { float f; memcpy(&f, &u32, 4); return f; }
        default: return w0 != 0;
    }
}

static void encode_regs(const mb_item_t *it, double raw, uint8_t *b)
{
    uint32_t u32 = 0;
    switch(it->dtype) {
        case DT_U16: u32 = (uint16_t)(uint32_t)llround(raw); break;
        case DT_S16: u32 = (uint16_t)(int16_t)llround(raw); break;
        case DT_U32: u32 = (uint32_t)llround(raw); break;
        case DT_S32: u32 = (uint32_t)(int32_t)llround(raw); break;
        case DT_FLOAT: { float f = (float)raw; memcpy(&u32, &f, 4); break; }
        default: u32 = raw != 0; break;
    }
    uint16_t w0, w1;
    if(dtype_regs(it->dtype) == 2) {
        w0 = (uint16_t)(u32 >> 16); w1 = (uint16_t)(u32 & 0xFFFF);
        if(it->swap_words) { uint16_t t = w0; w0 = w1; w1 = t; }
    }
    else { w0 = (uint16_t)(u32 & 0xFFFF); w1 = 0; }
    b[0] = w0 >> 8; b[1] = w0 & 0xFF; b[2] = w1 >> 8; b[3] = w1 & 0xFF;
}

/* ---------------------------------------------------------------- read / write */

static bool item_write_pending(mb_ctx_t *c, mb_item_t *it)
{
    hml_mutex_lock(&c->qlock);
    bool p = it->pending_writes > 0;
    hml_mutex_unlock(&c->qlock);
    return p;
}

static bool mb_read_item(mb_ctx_t *c, mb_item_t *it)
{
    uint8_t pdu[5], resp[260];
    int nregs = dtype_regs(it->dtype);
    bool skip_update = item_write_pending(c, it);
    pdu[1] = it->addr >> 8; pdu[2] = it->addr & 0xFF;
    pdu[3] = 0;
    if(it->area == AREA_COIL || it->area == AREA_DISCRETE) {
        pdu[0] = it->area == AREA_COIL ? 0x01 : 0x02;
        pdu[4] = 1;
        int n = mb_transact(c, pdu, 5, resp, sizeof(resp));
        if(n == -1) return false;
        if(n >= 3 && !skip_update) hml_tag_set_num(it->tag, (resp[2] & 1) ? 1 : 0, c->drv);
        return true;
    }
    pdu[0] = it->area == AREA_HOLDING ? 0x03 : 0x04;
    pdu[4] = (uint8_t)nregs;
    int n = mb_transact(c, pdu, 5, resp, sizeof(resp));
    if(n == -1) return false;
    if(n >= 2 + nregs * 2 && !skip_update) {
        uint8_t b[4] = { 0, 0, 0, 0 };
        memcpy(b, resp + 2, (size_t)nregs * 2);
        double raw = decode_regs(it, b);
        hml_tag_set_num(it->tag, raw * it->scale + it->offset, c->drv);
    }
    return true;
}

static bool mb_write_item(mb_ctx_t *c, mb_item_t *it, double value)
{
    uint8_t pdu[16], resp[260];
    pdu[1] = it->addr >> 8; pdu[2] = it->addr & 0xFF;
    if(it->area == AREA_COIL) {
        pdu[0] = 0x05;
        pdu[3] = value != 0 ? 0xFF : 0x00; pdu[4] = 0x00;
        return mb_transact(c, pdu, 5, resp, sizeof(resp)) != -1;
    }
    if(it->area != AREA_HOLDING) return true;
    double raw = it->scale != 0 ? (value - it->offset) / it->scale : value;
    uint8_t b[4];
    encode_regs(it, raw, b);
    if(dtype_regs(it->dtype) == 1) {
        pdu[0] = 0x06;
        pdu[3] = b[0]; pdu[4] = b[1];
        return mb_transact(c, pdu, 5, resp, sizeof(resp)) != -1;
    }
    pdu[0] = 0x10;
    pdu[3] = 0; pdu[4] = 2; pdu[5] = 4;
    memcpy(pdu + 6, b, 4);
    return mb_transact(c, pdu, 10, resp, sizeof(resp)) != -1;
}

static void set_online(mb_ctx_t *c, bool on)
{
    if(c->online == on) return;
    c->online = on;
    HML_LOG_INFO("modbus %s:%u %s", c->host, c->port, on ? "connected" : "disconnected");
    if(c->status_tag) hml_tag_set_num(c->status_tag, on ? 1 : 0, c->drv);
}

static bool queue_pop(mb_ctx_t *c, mb_write_t *w)
{
    bool ok = false;
    hml_mutex_lock(&c->qlock);
    if(c->q_head != c->q_tail) {
        *w = c->queue[c->q_head];
        c->q_head = (c->q_head + 1) % WRITE_QUEUE_LEN;
        ok = true;
    }
    hml_mutex_unlock(&c->qlock);
    return ok;
}

static void mb_thread(void *arg)
{
    mb_ctx_t *c = arg;
    uint32_t backoff = 500;
    while(c->running) {
        if(c->sock == SOCK_INVALID) {
            if(!mb_connect(c)) {
                set_online(c, false);
                /* sleep in small slices so stop() is responsive */
                for(uint32_t t = 0; t < backoff && c->running; t += 50) hml_sleep_ms(50);
                if(backoff < 5000) backoff *= 2;
                continue;
            }
            backoff = 500;
            set_online(c, true);
        }

        uint32_t cycle_start = hml_millis();
        bool ok = true;

        /* pending writes first so operator actions feel immediate */
        mb_write_t w;
        while(ok && queue_pop(c, &w)) {
            ok = mb_write_item(c, &c->items[w.item], w.value);
            hml_mutex_lock(&c->qlock);
            c->items[w.item].pending_writes--;
            hml_mutex_unlock(&c->qlock);
        }

        for(int i = 0; ok && i < c->item_count && c->running; i++) {
            if(!c->items[i].readable) continue;
            ok = mb_read_item(c, &c->items[i]);
        }

        if(!ok) {
            HML_LOG_WARN("modbus: communication error (%d), reconnecting", (int)sock_errno);
            mb_disconnect(c);
            set_online(c, false);
            continue;
        }

        uint32_t elapsed = hml_millis() - cycle_start;
        uint32_t wait = elapsed < c->poll_ms ? c->poll_ms - elapsed : 0;
        for(uint32_t t = 0; t < wait && c->running; t += 10) hml_sleep_ms(wait - t < 10 ? wait - t : 10);
    }
    mb_disconnect(c);
    set_online(c, false);
}

/* UI thread: a mapped tag changed. Queue a write unless we caused it. */
static void on_tag_changed(hml_tag_t *tag, const hml_value_t *v, void *source, void *user)
{
    mb_ctx_t *c = user;
    if(source == c->drv) return;
    for(int i = 0; i < c->item_count; i++) {
        mb_item_t *it = &c->items[i];
        if(it->tag != tag || !it->writable) continue;
        hml_mutex_lock(&c->qlock);
        int next = (c->q_tail + 1) % WRITE_QUEUE_LEN;
        if(next != c->q_head) {
            c->queue[c->q_tail].item = i;
            c->queue[c->q_tail].value = (v->type == HML_TYPE_STRING) ? atof(v->str) : v->num;
            c->q_tail = next;
            it->pending_writes++;
        }
        else HML_LOG_WARN("modbus: write queue full, dropping write to '%s'", hml_tag_name(tag));
        hml_mutex_unlock(&c->qlock);
    }
}

/* ---------------------------------------------------------------- driver glue */

static int mb_start(hml_driver_t *d)
{
    mb_ctx_t *c = d->ctx;
    for(int i = 0; i < c->item_count; i++) {
        if(c->items[i].writable) hml_tag_subscribe(c->items[i].tag, HML_SUB_GROUP_DRIVER, on_tag_changed, c);
    }
    c->running = true;
    return hml_thread_start(&c->thread, mb_thread, c);
}

static void mb_stop(hml_driver_t *d)
{
    mb_ctx_t *c = d->ctx;
    if(!c->running) return;
    c->running = false;
    hml_thread_join(&c->thread);
}

static void mb_destroy(hml_driver_t *d)
{
    mb_ctx_t *c = d->ctx;
    hml_mutex_destroy(&c->qlock);
    free(c->items);
    free(c);
    free(d);
}

hml_driver_t *hml_driver_modbus_tcp_create(const cJSON *cfg)
{
    if(!sock_startup()) { HML_LOG_ERROR("modbus: socket init failed"); return NULL; }
    const cJSON *map = cJSON_GetObjectItemCaseSensitive(cfg, "map");
    if(!cJSON_IsArray(map)) { HML_LOG_WARN("modbus: \"map\" array missing"); return NULL; }

    mb_ctx_t *c = calloc(1, sizeof(*c));
    hml_driver_t *d = calloc(1, sizeof(*d));
    if(!c || !d) { free(c); free(d); return NULL; }
    snprintf(c->host, sizeof(c->host), "%s", hml_json_str(cfg, "host", "127.0.0.1"));
    c->port = (uint16_t)hml_json_num(cfg, "port", 502);
    c->unit = (uint8_t)hml_json_num(cfg, "unit_id", 1);
    c->poll_ms = (uint32_t)hml_json_num(cfg, "poll_ms", 200);
    c->timeout_ms = (uint32_t)hml_json_num(cfg, "timeout_ms", 1000);
    c->status_tag = hml_tag_find(hml_json_str(cfg, "status_tag", NULL));
    c->sock = SOCK_INVALID;
    c->drv = d;
    hml_mutex_init(&c->qlock);

    int n = cJSON_GetArraySize(map);
    c->items = calloc((size_t)(n ? n : 1), sizeof(mb_item_t));
    const cJSON *m;
    cJSON_ArrayForEach(m, map) {
        const char *tagname = hml_json_str(m, "tag", NULL);
        hml_tag_t *tag = hml_tag_find(tagname);
        if(!tag) { HML_LOG_WARN("modbus: unknown tag '%s'", tagname ? tagname : "(null)"); continue; }
        mb_item_t *it = &c->items[c->item_count];
        it->tag = tag;
        const char *area = hml_json_str(m, "area", "holding");
        if(hml_strieq(area, "coil")) it->area = AREA_COIL;
        else if(hml_strieq(area, "discrete") || hml_strieq(area, "di")) it->area = AREA_DISCRETE;
        else if(hml_strieq(area, "input") || hml_strieq(area, "ir")) it->area = AREA_INPUT;
        else it->area = AREA_HOLDING;

        const char *dt = hml_json_str(m, "dtype", NULL);
        if(it->area == AREA_COIL || it->area == AREA_DISCRETE) it->dtype = DT_BOOL;
        else if(!dt || hml_strieq(dt, "u16") || hml_strieq(dt, "uint16")) it->dtype = DT_U16;
        else if(hml_strieq(dt, "s16") || hml_strieq(dt, "int16")) it->dtype = DT_S16;
        else if(hml_strieq(dt, "u32") || hml_strieq(dt, "uint32")) it->dtype = DT_U32;
        else if(hml_strieq(dt, "s32") || hml_strieq(dt, "int32")) it->dtype = DT_S32;
        else if(hml_strieq(dt, "float") || hml_strieq(dt, "f32") || hml_strieq(dt, "real")) it->dtype = DT_FLOAT;
        else { HML_LOG_WARN("modbus: unknown dtype '%s'", dt); it->dtype = DT_U16; }

        it->addr = (uint16_t)hml_json_num(m, "addr", 0);
        it->scale = hml_json_num(m, "scale", 1);
        it->offset = hml_json_num(m, "offset", 0);
        it->swap_words = hml_json_bool(m, "swap_words", false);
        bool can_write = (it->area == AREA_COIL || it->area == AREA_HOLDING);
        const char *access = hml_json_str(m, "access", can_write ? "rw" : "ro");
        it->readable = !hml_strieq(access, "wo");
        it->writable = can_write && !hml_strieq(access, "ro");
        c->item_count++;
    }

    d->type = "modbus_tcp";
    d->ctx = c;
    d->start = mb_start;
    d->stop = mb_stop;
    d->destroy = mb_destroy;
    return d;
}
