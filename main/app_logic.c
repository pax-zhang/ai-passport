#include "app_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void app_time_format(int month, int day, int hour, int minute,
                     char *out, size_t n)
{
    if (!out || n == 0) return;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;
    if (day > 31) day = 31;
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;
    snprintf(out, n, "%d/%d %d:%02d", month, day, hour, minute);
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool app_ancs_date_text(const char *iso, char *out, size_t n)
{
    if (!out || n == 0) return false;
    out[0] = 0;
    if (!iso) return false;
    for (int i = 0; i < 15; i++) {
        if (!iso[i]) return false;
        if (i == 8) {
            if (iso[i] != 'T') return false;
        } else if (!is_digit(iso[i])) {
            return false;
        }
    }
    int month = (iso[4] - '0') * 10 + (iso[5] - '0');
    int day = (iso[6] - '0') * 10 + (iso[7] - '0');
    int hour = (iso[9] - '0') * 10 + (iso[10] - '0');
    int minute = (iso[11] - '0') * 10 + (iso[12] - '0');
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    if (hour > 23 || minute > 59) return false;
    app_time_format(month, day, hour, minute, out, n);
    return out[0] != 0;
}

bool app_notif_show_subtitle(const char *title, const char *subtitle)
{
    if (!subtitle || !subtitle[0]) return false;
    if (title && strcmp(title, subtitle) == 0) return false;
    return true;
}

bool app_dnd_in_range(int hour, int start, int end)
{
    if (hour < 0 || hour > 23) return false;
    if (start < 0 || start > 23) start = 21;
    if (end < 0 || end > 23) end = 8;
    if (start == end) return false;
    if (start < end) return hour >= start && hour < end;
    return hour >= start || hour < end;
}

void app_notif_q_init(app_notif_q_t *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
}

bool app_notif_q_push(app_notif_q_t *q, const app_notif_item_t *src)
{
    if (!q || !src) return false;
    if (q->count == APP_NOTIF_Q) {
        q->head = (q->head + 1) % APP_NOTIF_Q;
        q->count--;
    }
    int i = (q->head + q->count) % APP_NOTIF_Q;
    app_notif_item_t *it = &q->items[i];
    *it = *src;
    if (it->alert > APP_ALERT_URGENT) it->alert = APP_ALERT_URGENT;
    q->count++;
    return true;
}

bool app_notif_q_update(app_notif_q_t *q, const app_notif_item_t *it)
{
    if (!q || !it || !it->uid) return false;
    for (int i = 0; i < q->count; i++) {
        int idx = (q->head + i) % APP_NOTIF_Q;
        if (q->items[idx].uid != it->uid) continue;
        q->items[idx] = *it;
        if (q->items[idx].alert > APP_ALERT_URGENT) {
            q->items[idx].alert = APP_ALERT_URGENT;
        }
        return true;
    }
    return false;
}

const app_notif_item_t *app_notif_q_front(const app_notif_q_t *q)
{
    if (!q || q->count <= 0) return NULL;
    return &q->items[q->head];
}

const app_notif_item_t *app_notif_q_at(const app_notif_q_t *q, int i)
{
    if (!q || i < 0 || i >= q->count) return NULL;
    return &q->items[(q->head + i) % APP_NOTIF_Q];
}

void app_notif_q_pop(app_notif_q_t *q)
{
    if (!q || q->count <= 0) return;
    q->head = (q->head + 1) % APP_NOTIF_Q;
    q->count--;
}

int app_notif_q_count(const app_notif_q_t *q)
{
    return q ? q->count : 0;
}

void app_notif_q_drop_uid(app_notif_q_t *q, uint32_t uid)
{
    if (!q || q->count <= 0) return;
    int n = 0;
    for (int i = 0; i < q->count; i++) {
        int idx = (q->head + i) % APP_NOTIF_Q;
        if (q->items[idx].uid == uid) continue;
        int dst = (q->head + n) % APP_NOTIF_Q;
        if (dst != idx) q->items[dst] = q->items[idx];
        n++;
    }
    q->count = n;
}

int app_notif_acts(const app_notif_item_t *it, app_notif_act_t *out, int max)
{
    if (!it || !out || max <= 0) return 0;
    int n = 0;
    bool incoming = it->category == APP_CAT_INCOMING;
    if (!incoming && n < max) {
        out[n].kind = APP_NOTIF_ACT_CLOSE;
        out[n].label[0] = 0;
        n++;
    }
    uint8_t order_f[2];
    uint8_t order_k[2];
    if (incoming) {
        order_f[0] = APP_NOTIF_FLAG_POS;
        order_k[0] = APP_NOTIF_ACT_POS;
        order_f[1] = APP_NOTIF_FLAG_NEG;
        order_k[1] = APP_NOTIF_ACT_NEG;
    } else {
        order_f[0] = APP_NOTIF_FLAG_NEG;
        order_k[0] = APP_NOTIF_ACT_NEG;
        order_f[1] = APP_NOTIF_FLAG_POS;
        order_k[1] = APP_NOTIF_ACT_POS;
    }
    for (int i = 0; i < 2 && n < max; i++) {
        if (!(it->flags & order_f[i])) continue;
        out[n].kind = order_k[i];
        cpy(out[n].label, sizeof(out[n].label),
            order_k[i] == APP_NOTIF_ACT_POS ? it->pos_label : it->neg_label);
        n++;
    }
    if (n == 0 && n < max) {
        out[n].kind = APP_NOTIF_ACT_CLOSE;
        out[n].label[0] = 0;
        n++;
    }
    return n;
}

int app_notif_act_default(const app_notif_act_t *acts, int n, uint8_t category)
{
    if (!acts || n <= 0) return 0;
    if (category == APP_CAT_INCOMING) {
        for (int i = 0; i < n; i++) {
            if (acts[i].kind == APP_NOTIF_ACT_POS) return i;
        }
        for (int i = 0; i < n; i++) {
            if (acts[i].kind == APP_NOTIF_ACT_NEG) return i;
        }
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (acts[i].kind == APP_NOTIF_ACT_CLOSE) return i;
    }
    return 0;
}

static uint32_t rol32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr_be64(uint8_t *p, uint64_t v)
{
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

typedef struct {
    uint32_t h[5];
    uint64_t nbits;
    uint8_t buf[64];
    size_t nbuf;
} sha1_t;

static void sha1_init(sha1_t *s)
{
    s->h[0] = 0x67452301u;
    s->h[1] = 0xEFCDAB89u;
    s->h[2] = 0x98BADCFEu;
    s->h[3] = 0x10325476u;
    s->h[4] = 0xC3D2E1F0u;
    s->nbits = 0;
    s->nbuf = 0;
}

static void sha1_block(sha1_t *s, const uint8_t *blk)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = rd_be32(blk + i * 4);
    for (int i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

static void sha1_update(sha1_t *s, const uint8_t *p, size_t n)
{
    s->nbits += (uint64_t)n * 8;
    while (n) {
        size_t take = 64 - s->nbuf;
        if (take > n) take = n;
        memcpy(s->buf + s->nbuf, p, take);
        s->nbuf += take;
        p += take;
        n -= take;
        if (s->nbuf == 64) {
            sha1_block(s, s->buf);
            s->nbuf = 0;
        }
    }
}

static void sha1_final(sha1_t *s, uint8_t out[20])
{
    s->buf[s->nbuf++] = 0x80;
    if (s->nbuf > 56) {
        while (s->nbuf < 64) s->buf[s->nbuf++] = 0;
        sha1_block(s, s->buf);
        s->nbuf = 0;
    }
    while (s->nbuf < 56) s->buf[s->nbuf++] = 0;
    wr_be64(s->buf + 56, s->nbits);
    sha1_block(s, s->buf);
    for (int i = 0; i < 5; i++) wr_be32(out + i * 4, s->h[i]);
}

static void hmac_sha1(const uint8_t *key, size_t key_n,
                      const uint8_t *msg, size_t msg_n, uint8_t out[20])
{
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (key_n > 64) {
        sha1_t s;
        sha1_init(&s);
        sha1_update(&s, key, key_n);
        sha1_final(&s, k);
    } else {
        memcpy(k, key, key_n);
    }

    uint8_t ip[64], op[64];
    for (int i = 0; i < 64; i++) {
        ip[i] = (uint8_t)(k[i] ^ 0x36);
        op[i] = (uint8_t)(k[i] ^ 0x5c);
    }

    uint8_t inner[20];
    sha1_t s;
    sha1_init(&s);
    sha1_update(&s, ip, 64);
    sha1_update(&s, msg, msg_n);
    sha1_final(&s, inner);
    sha1_init(&s);
    sha1_update(&s, op, 64);
    sha1_update(&s, inner, 20);
    sha1_final(&s, out);
}

static int b32_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return 26 + (c - '2');
    return -1;
}

int app_b32_decode(const char *in, uint8_t *out, size_t out_n)
{
    if (!in || !out) return -1;
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (; *in; in++) {
        char c = *in;
        if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t' ||
            c == '-') {
            continue;
        }
        int v = b32_val(c);
        if (v < 0) return -1;
        acc = (acc << 5) | (uint32_t)v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (o >= out_n) return -1;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return (int)o;
}

uint32_t app_hotp(const uint8_t *key, size_t key_n, uint64_t counter, int digits)
{
    static const uint32_t MOD[9] = {
        1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000
    };
    if (!key || key_n == 0) return 0;
    if (digits < 6) digits = 6;
    if (digits > 8) digits = 8;
    uint8_t msg[8];
    wr_be64(msg, counter);
    uint8_t hs[20];
    hmac_sha1(key, key_n, msg, 8, hs);
    int off = hs[19] & 0x0f;
    uint32_t bin = ((uint32_t)(hs[off] & 0x7f) << 24) |
                   ((uint32_t)hs[off + 1] << 16) |
                   ((uint32_t)hs[off + 2] << 8) |
                   hs[off + 3];
    return bin % MOD[digits];
}

static void totp_defaults(app_totp_acct_t *a)
{
    if (!a->digits) a->digits = 6;
    if (a->digits != 6 && a->digits != 8) a->digits = 6;
    if (!a->period) a->period = 30;
    if (a->period < 15) a->period = 15;
    if (a->period > 120) a->period = 120;
}

bool app_totp_code(const app_totp_acct_t *a, uint64_t unix_sec,
                   char *out, size_t n, int *remain)
{
    if (out && n) out[0] = 0;
    if (remain) *remain = 0;
    if (!a || !a->secret[0]) return false;
    uint8_t key[64];
    int kn = app_b32_decode(a->secret, key, sizeof(key));
    if (kn <= 0) return false;
    int period = a->period ? a->period : 30;
    int digits = a->digits ? a->digits : 6;
    if (period < 1) period = 30;
    uint64_t p = (uint64_t)period;
    uint64_t ctr = unix_sec / p;
    int rem = (int)(p - (unix_sec % p));
    uint32_t code = app_hotp(key, (size_t)kn, ctr, digits);
    if (remain) *remain = rem;
    if (out && n) snprintf(out, n, "%0*u", digits, (unsigned)code);
    return true;
}

void app_totp_format_code(const char *digits, char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = 0;
    if (!digits || !digits[0]) return;
    size_t len = strlen(digits);
    size_t mid = len / 2;
    if (mid == 0 || len + 2 > n) {
        cpy(out, n, digits);
        return;
    }
    memcpy(out, digits, mid);
    out[mid] = ' ';
    memcpy(out + mid + 1, digits + mid, len - mid + 1);
}

void app_totp_mask(const char *secret, char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = 0;
    if (!secret || !secret[0]) return;
    size_t len = strlen(secret);
    if (len <= 4 || n < 9) {
        cpy(out, n, "****");
        return;
    }
    snprintf(out, n, "****%s", secret + len - 4);
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode_inplace(char *s)
{
    char *d = s;
    for (; *s; s++) {
        if (*s == '+') {
            *d++ = ' ';
            continue;
        }
        if (*s == '%' && s[1] && s[2]) {
            int h = hexv(s[1]), l = hexv(s[2]);
            if (h >= 0 && l >= 0) {
                *d++ = (char)((h << 4) | l);
                s += 2;
                continue;
            }
        }
        *d++ = *s;
    }
    *d = 0;
}

static bool istarts(const char *s, const char *p)
{
    if (!s || !p) return false;
    for (; *p; s++, p++) {
        char a = *s, b = *p;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static bool ieq(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (; *a || *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

static bool qget(const char *q, const char *key, char *out, size_t n)
{
    if (!q || !key || !out || n == 0) return false;
    out[0] = 0;
    size_t klen = strlen(key);
    while (*q) {
        const char *amp = strchr(q, '&');
        size_t seglen = amp ? (size_t)(amp - q) : strlen(q);
        if (seglen > klen && q[klen] == '=' && strncmp(q, key, klen) == 0) {
            size_t vlen = seglen - klen - 1;
            if (vlen + 1 > n) vlen = n - 1;
            memcpy(out, q + klen + 1, vlen);
            out[vlen] = 0;
            url_decode_inplace(out);
            return out[0] != 0;
        }
        if (!amp) break;
        q = amp + 1;
    }
    return false;
}

static bool norm_secret(const char *in, char *out, size_t n)
{
    if (!in || !out || n == 0) return false;
    size_t o = 0;
    for (; *in; in++) {
        char c = *in;
        if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t' ||
            c == '-') {
            continue;
        }
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (b32_val(c) < 0) return false;
        if (o + 1 >= n) return false;
        out[o++] = c;
    }
    out[o] = 0;
    return o > 0;
}

static bool apply_secret(app_totp_acct_t *acct, const char *secret)
{
    char norm[APP_TOTP_SECRET_LEN + 1];
    if (!norm_secret(secret, norm, sizeof(norm))) return false;
    uint8_t key[64];
    if (app_b32_decode(norm, key, sizeof(key)) <= 0) return false;
    cpy(acct->secret, sizeof(acct->secret), norm);
    return true;
}

static bool parse_otpauth(const char *uri, app_totp_acct_t *acct, bool fill_names)
{
    const char *p = uri;
    if (istarts(p, "otpauth://totp/")) p += 15;
    else return false;

    char label[96];
    const char *qmark = strchr(p, '?');
    size_t llen = qmark ? (size_t)(qmark - p) : strlen(p);
    if (llen >= sizeof(label)) llen = sizeof(label) - 1;
    memcpy(label, p, llen);
    label[llen] = 0;
    url_decode_inplace(label);

    const char *query = qmark ? qmark + 1 : "";
    char secret[APP_TOTP_SECRET_LEN + 8];
    char digits[8], period[8], algo[16];
    if (!qget(query, "secret", secret, sizeof(secret))) return false;
    if (!apply_secret(acct, secret)) return false;

    if (qget(query, "algorithm", algo, sizeof(algo))) {
        if (!ieq(algo, "SHA1") && !ieq(algo, "SHA-1")) return false;
    }
    if (qget(query, "digits", digits, sizeof(digits))) {
        int d = 0;
        for (const char *c = digits; *c; c++) {
            if (*c < '0' || *c > '9') return false;
            d = d * 10 + (*c - '0');
        }
        if (d != 6 && d != 8) return false;
        acct->digits = (uint8_t)d;
    }
    if (qget(query, "period", period, sizeof(period))) {
        int sec = 0;
        for (const char *c = period; *c; c++) {
            if (*c < '0' || *c > '9') return false;
            sec = sec * 10 + (*c - '0');
        }
        if (sec < 15 || sec > 120) return false;
        acct->period = (uint8_t)sec;
    }

    if (!fill_names) return true;

    char qiss[APP_TOTP_ISSUER_LEN + 1];
    qiss[0] = 0;
    qget(query, "issuer", qiss, sizeof(qiss));

    char pref[APP_TOTP_ISSUER_LEN + 1];
    char rest[APP_TOTP_LABEL_LEN + 1];
    app_totp_split_name(label, pref, sizeof(pref), rest, sizeof(rest));
    if (qiss[0]) cpy(acct->issuer, sizeof(acct->issuer), qiss);
    else if (pref[0]) cpy(acct->issuer, sizeof(acct->issuer), pref);
    if (rest[0]) cpy(acct->label, sizeof(acct->label), rest);
    else if (label[0] && !strchr(label, ':')) {
        if (acct->issuer[0]) {
            /* otpauth://totp/Example?secret=  → app Example, no account */
        } else {
            cpy(acct->issuer, sizeof(acct->issuer), label);
        }
    }
    return acct->secret[0] != 0;
}

bool app_totp_ingest(const char *text, app_totp_acct_t *acct, bool fill_names)
{
    if (!text || !acct) return false;
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') {
        text++;
    }
    if (!*text) return false;
    if (istarts(text, "otpauth://")) {
        return parse_otpauth(text, acct, fill_names);
    }
    if (!apply_secret(acct, text)) return false;
    return true;
}

void app_totp_split_name(const char *name, char *issuer, size_t issuer_n,
                         char *label, size_t label_n)
{
    if (issuer && issuer_n) issuer[0] = 0;
    if (label && label_n) label[0] = 0;
    if (!name || !name[0]) return;
    const char *colon = strchr(name, ':');
    if (colon && colon != name) {
        size_t il = (size_t)(colon - name);
        if (issuer && issuer_n) {
            if (il >= issuer_n) il = issuer_n - 1;
            memcpy(issuer, name, il);
            issuer[il] = 0;
        }
        if (label && label_n) cpy(label, label_n, colon + 1);
        return;
    }
    if (issuer && issuer_n) cpy(issuer, issuer_n, name);
}

static bool acct_ok(const app_totp_acct_t *a)
{
    if (!a || !a->secret[0]) return false;
    if (!a->issuer[0] && !a->label[0]) return false;
    uint8_t key[64];
    return app_b32_decode(a->secret, key, sizeof(key)) > 0;
}

static int icmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    for (;;) {
        unsigned char x = (unsigned char)*a++;
        unsigned char y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y - 'A' + 'a');
        if (x != y) return (int)x - (int)y;
        if (!x) return 0;
    }
}

static int cmp_acct(const void *x, const void *y)
{
    const app_totp_acct_t *a = x, *b = y;
    int c = icmp(a->issuer, b->issuer);
    if (c) return c;
    return icmp(a->label, b->label);
}

const char *app_totp_issuer(const app_totp_acct_t *a)
{
    return (a && a->issuer[0]) ? a->issuer : "";
}

const char *app_totp_label(const app_totp_acct_t *a)
{
    return (a && a->label[0]) ? a->label : "";
}

bool app_totp_same_group(const app_totp_acct_t *a, const app_totp_acct_t *b)
{
    if (!a || !b) return false;
    return icmp(a->issuer, b->issuer) == 0;
}

void app_totp_list_init(app_totp_list_t *l)
{
    if (!l) return;
    l->items = NULL;
    l->n = 0;
    l->cap = 0;
}

void app_totp_list_clear(app_totp_list_t *l)
{
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->n = 0;
    l->cap = 0;
}

void app_totp_list_sort(app_totp_list_t *l)
{
    if (!l || l->n < 2 || !l->items) return;
    qsort(l->items, l->n, sizeof(l->items[0]), cmp_acct);
}

int app_totp_list_find(const app_totp_list_t *l, const app_totp_acct_t *a)
{
    if (!l || !a || !l->items) return -1;
    for (uint16_t i = 0; i < l->n; i++) {
        if (strcmp(l->items[i].secret, a->secret) == 0 &&
            icmp(l->items[i].issuer, a->issuer) == 0 &&
            icmp(l->items[i].label, a->label) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool list_grow(app_totp_list_t *l)
{
    unsigned cap = l->cap ? (unsigned)l->cap * 2u : 4u;
    if (cap > 0xFFFFu || cap < l->cap) return false;
    app_totp_acct_t *next = realloc(l->items, cap * sizeof(*next));
    if (!next) return false;
    l->items = next;
    l->cap = (uint16_t)cap;
    return true;
}

bool app_totp_list_add(app_totp_list_t *l, const app_totp_acct_t *a)
{
    if (!l || !acct_ok(a)) return false;
    if (l->n >= l->cap && !list_grow(l)) return false;
    l->items[l->n] = *a;
    totp_defaults(&l->items[l->n]);
    l->n++;
    app_totp_list_sort(l);
    return true;
}

bool app_totp_list_update(app_totp_list_t *l, int i, const app_totp_acct_t *a)
{
    if (!l || !a || !l->items || i < 0 || i >= (int)l->n || !acct_ok(a)) {
        return false;
    }
    l->items[i] = *a;
    totp_defaults(&l->items[i]);
    app_totp_list_sort(l);
    return true;
}

bool app_totp_list_delete(app_totp_list_t *l, int i)
{
    if (!l || !l->items || i < 0 || i >= (int)l->n) return false;
    for (int k = i; k < (int)l->n - 1; k++) l->items[k] = l->items[k + 1];
    l->n--;
    memset(&l->items[l->n], 0, sizeof(l->items[0]));
    return true;
}

void app_dlog_init(app_dlog_t *l)
{
    app_dlog_clear(l);
}

void app_dlog_clear(app_dlog_t *l)
{
    if (!l) return;
    memset(l, 0, sizeof(*l));
}

static void dlog_commit(app_dlog_t *l, uint8_t next_cont)
{
    if (l->acc_n == 0) return;
    l->acc[l->acc_n] = 0;
    memcpy(l->line[l->head], l->acc, (size_t)l->acc_n + 1);
    l->cont[l->head] = l->acc_cont;
    l->head = (uint8_t)((l->head + 1) % APP_DLOG_N);
    if (l->count < APP_DLOG_N) l->count++;
    l->acc_n = 0;
    l->acc[0] = 0;
    l->acc_cont = next_cont;
}

void app_dlog_flush(app_dlog_t *l)
{
    if (l) dlog_commit(l, 0);
}

static void dlog_putc(app_dlog_t *l, char c)
{
    if (c == '\r') return;
    if (c == '\n') {
        dlog_commit(l, 0);
        return;
    }
    if (l->acc_n >= APP_DLOG_W - 1) dlog_commit(l, 1);
    l->acc[l->acc_n++] = c;
}

void app_dlog_feed(app_dlog_t *l, const char *s, int n)
{
    if (!l || !s || n <= 0) return;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (l->esc == 1) {
            l->esc = (c == '[') ? 2 : 0;
            continue;
        }
        if (l->esc == 2) {
            if (c >= 0x40 && c <= 0x7E) l->esc = 0;
            continue;
        }
        if (c == 0x1B) {
            l->esc = 1;
            continue;
        }
        dlog_putc(l, (char)c);
    }
}

int app_dlog_count(const app_dlog_t *l)
{
    return l ? l->count : 0;
}

static int dlog_idx(const app_dlog_t *l, int oldest_i)
{
    if (!l || oldest_i < 0 || oldest_i >= l->count) return -1;
    int i = (int)l->head - (int)l->count + oldest_i;
    i %= APP_DLOG_N;
    if (i < 0) i += APP_DLOG_N;
    return i;
}

void app_dlog_copy(const app_dlog_t *l, int oldest_i, char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = 0;
    int i = dlog_idx(l, oldest_i);
    if (i < 0) return;
    snprintf(out, n, "%s", l->line[i]);
}

bool app_dlog_cont(const app_dlog_t *l, int oldest_i)
{
    int i = dlog_idx(l, oldest_i);
    return i >= 0 && l->cont[i] != 0;
}
