#include "app_notif_store.h"

#include <string.h>

#define STORE_MAGIC 0x3146534Eu  // "NSF1"

typedef struct {
    uint32_t magic;
    uint16_t rec_size;
    uint16_t n;
} store_hdr_t;

static void cpy(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0) return;
    dst[0] = 0;
    if (!src) return;
    size_t i = 0;
    while (src[i] && i + 1 < n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int phys_of(const app_notif_store_t *s, int newest_i)
{
    int i = s->head - 1 - newest_i;
    i %= APP_NOTIF_STORE_N;
    if (i < 0) i += APP_NOTIF_STORE_N;
    return i;
}

void app_notif_store_init(app_notif_store_t *s)
{
    app_notif_store_clear(s);
}

void app_notif_store_clear(app_notif_store_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

int app_notif_store_count(const app_notif_store_t *s)
{
    return s ? s->count : 0;
}

const app_notif_rec_t *app_notif_store_at(const app_notif_store_t *s, int newest_i)
{
    if (!s || newest_i < 0 || newest_i >= s->count) return NULL;
    return &s->items[phys_of(s, newest_i)];
}

int app_notif_store_find_uid(const app_notif_store_t *s, uint32_t uid)
{
    if (!s || !uid) return -1;
    for (int i = 0; i < s->count; i++) {
        if (s->items[phys_of(s, i)].uid == uid) return i;
    }
    return -1;
}

bool app_notif_store_push(app_notif_store_t *s, const app_notif_rec_t *r)
{
    if (!s || !r) return false;
    int at = app_notif_store_find_uid(s, r->uid);
    if (at >= 0) {
        s->items[phys_of(s, at)] = *r;
        return true;
    }
    s->items[s->head] = *r;
    s->head = (s->head + 1) % APP_NOTIF_STORE_N;
    if (s->count < APP_NOTIF_STORE_N) s->count++;
    return true;
}

bool app_notif_store_remove(app_notif_store_t *s, int newest_i)
{
    if (!s || newest_i < 0 || newest_i >= s->count) return false;
    int phys = phys_of(s, newest_i);
    int last = phys_of(s, 0);
    while (phys != last) {
        int next = phys + 1;
        if (next >= APP_NOTIF_STORE_N) next = 0;
        s->items[phys] = s->items[next];
        phys = next;
    }
    memset(&s->items[last], 0, sizeof(s->items[0]));
    s->head = last;
    s->count--;
    return true;
}

bool app_notif_store_remove_uid(app_notif_store_t *s, uint32_t uid)
{
    int at = app_notif_store_find_uid(s, uid);
    if (at < 0) return false;
    return app_notif_store_remove(s, at);
}

int app_notif_store_unread(const app_notif_store_t *s)
{
    if (!s) return 0;
    int n = 0;
    for (int i = 0; i < s->count; i++) {
        if (s->items[phys_of(s, i)].unread) n++;
    }
    return n;
}

bool app_notif_store_mark_read(app_notif_store_t *s, int newest_i)
{
    if (!s || newest_i < 0 || newest_i >= s->count) return false;
    app_notif_rec_t *r = &s->items[phys_of(s, newest_i)];
    if (!r->unread) return false;
    r->unread = 0;
    return true;
}

bool app_notif_store_mark_unread(app_notif_store_t *s, int newest_i)
{
    if (!s || newest_i < 0 || newest_i >= s->count) return false;
    app_notif_rec_t *r = &s->items[phys_of(s, newest_i)];
    if (r->unread) return false;
    r->unread = 1;
    return true;
}

bool app_notif_store_mark_read_uid(app_notif_store_t *s, uint32_t uid)
{
    return app_notif_store_mark_read(s, app_notif_store_find_uid(s, uid));
}

void app_notif_store_mark_all_read(app_notif_store_t *s)
{
    if (!s) return;
    for (int i = 0; i < s->count; i++) s->items[phys_of(s, i)].unread = 0;
}

const char *app_notif_rec_key(const app_notif_rec_t *r)
{
    if (!r) return "";
    if (r->app_id[0]) return r->app_id;
    if (r->app_name[0]) return r->app_name;
    return "";
}

void app_notif_rec_label(const app_notif_rec_t *r, char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = 0;
    if (!r) return;
    if (r->app_name[0]) {
        cpy(out, n, r->app_name);
        return;
    }
    if (!r->app_id[0]) return;
    const char *last = r->app_id;
    for (const char *p = r->app_id; *p; p++) {
        if (*p == '.') last = p + 1;
    }
    cpy(out, n, last[0] ? last : r->app_id);
}

int app_notif_store_apps(const app_notif_store_t *s, app_notif_group_t *out, int max)
{
    if (!s || !out || max < 1) return 0;
    int n = 0;
    for (int i = 0; i < s->count; i++) {
        const app_notif_rec_t *r = app_notif_store_at(s, i);
        const char *key = app_notif_rec_key(r);
        int found = -1;
        for (int j = 0; j < n; j++) {
            if (strcmp(out[j].app_id, key) == 0) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            out[found].count++;
            continue;
        }
        if (n >= max) continue;
        memset(&out[n], 0, sizeof(out[n]));
        cpy(out[n].app_id, sizeof(out[n].app_id), key);
        app_notif_rec_label(r, out[n].app_name, sizeof(out[n].app_name));
        out[n].count = 1;
        n++;
    }
    return n;
}

int app_notif_store_cats(const app_notif_store_t *s, app_notif_group_t *out, int max)
{
    if (!s || !out || max < 1) return 0;
    int n = 0;
    for (int i = 0; i < s->count; i++) {
        const app_notif_rec_t *r = app_notif_store_at(s, i);
        int found = -1;
        for (int j = 0; j < n; j++) {
            if (out[j].category == r->category) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            out[found].count++;
            continue;
        }
        if (n >= max) continue;
        memset(&out[n], 0, sizeof(out[n]));
        out[n].category = r->category;
        out[n].count = 1;
        n++;
    }
    return n;
}

int app_notif_store_match_app(const app_notif_store_t *s, const char *app_id,
                              int *idx, int max)
{
    if (!s || !idx || max < 1) return 0;
    if (!app_id) app_id = "";
    int n = 0;
    for (int i = 0; i < s->count && n < max; i++) {
        if (strcmp(app_notif_rec_key(app_notif_store_at(s, i)), app_id) == 0) {
            idx[n++] = i;
        }
    }
    return n;
}

int app_notif_store_match_cat(const app_notif_store_t *s, uint8_t category,
                              int *idx, int max)
{
    if (!s || !idx || max < 1) return 0;
    int n = 0;
    for (int i = 0; i < s->count && n < max; i++) {
        if (app_notif_store_at(s, i)->category == category) idx[n++] = i;
    }
    return n;
}

size_t app_notif_store_blob_size(const app_notif_store_t *s)
{
    int n = app_notif_store_count(s);
    return sizeof(store_hdr_t) + (size_t)n * sizeof(app_notif_rec_t);
}

size_t app_notif_store_serialize(const app_notif_store_t *s, void *buf, size_t n)
{
    if (!s || !buf) return 0;
    size_t need = app_notif_store_blob_size(s);
    if (n < need) return 0;
    store_hdr_t h = {
        .magic = STORE_MAGIC,
        .rec_size = (uint16_t)sizeof(app_notif_rec_t),
        .n = (uint16_t)s->count,
    };
    memcpy(buf, &h, sizeof(h));
    uint8_t *p = (uint8_t *)buf + sizeof(h);
    for (int i = s->count - 1; i >= 0; i--) {  // 旧到新
        memcpy(p, app_notif_store_at(s, i), sizeof(app_notif_rec_t));
        p += sizeof(app_notif_rec_t);
    }
    return need;
}

bool app_notif_store_deserialize(app_notif_store_t *s, const void *buf, size_t n)
{
    if (!s || !buf || n < sizeof(store_hdr_t)) return false;
    store_hdr_t h;
    memcpy(&h, buf, sizeof(h));
    if (h.magic != STORE_MAGIC) return false;
    if (h.rec_size != sizeof(app_notif_rec_t)) return false;
    if (n < sizeof(h) + (size_t)h.n * sizeof(app_notif_rec_t)) return false;
    app_notif_store_clear(s);
    const uint8_t *p = (const uint8_t *)buf + sizeof(h);
    for (uint16_t i = 0; i < h.n; i++) {
        app_notif_rec_t r;
        memcpy(&r, p, sizeof(r));
        p += sizeof(r);
        s->items[s->head] = r;
        s->head = (s->head + 1) % APP_NOTIF_STORE_N;
        if (s->count < APP_NOTIF_STORE_N) s->count++;
    }
    return true;
}
