#include "app.h"

#include "app_i18n.h"
#include "app_net.h"
#include "app_prefs.h"
#include "app_ui.h"
#include "app_web.h"
#include "bsp_wifi.h"
#include "ui_pixel.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "wx";

static SemaphoreHandle_t s_mu;
static TaskHandle_t s_task;
static uint32_t s_retry_at;

#define WX_BUF 3072
#define WX_SET_N 6
#define WX_DAYS 4
#define HUD_BG       UI_BG
#define HUD_PANEL    UI_CARD
#define HUD_LINE     UI_LINE
#define HUD_CYAN     UI_CYAN
#define HUD_VIOLET   UI_VIOLET
#define HUD_MUTE     UI_MUTE

typedef enum { VIEW_MAIN = 0, VIEW_SET, VIEW_KB } view_t;

typedef struct {
    const char *en;
    const char *zh;
    int32_t lat_e4;
    int32_t lon_e4;
    int citykey; /* 中国天气网编号,0=无 */
} wx_city_t;

static const wx_city_t CITIES[] = {
    { "Shanghai", "上海", 312304, 1214737, 101020100 },
    { "Beijing", "北京", 399042, 1164074, 101010100 },
    { "Guangzhou", "广州", 231292, 1132644, 101280101 },
    { "Shenzhen", "深圳", 225431, 1140579, 101280601 },
    { "Hangzhou", "杭州", 302743, 1201551, 101210101 },
    { "Chengdu", "成都", 306724, 1040660, 101270101 },
    { "Wuhan", "武汉", 305924, 1143054, 101200101 },
    { "Nanjing", "南京", 320606, 1187969, 101190101 },
    { "Xian", "西安", 342657, 1089542, 101110101 },
    { "Hong Kong", "香港", 223196, 1141694, 101320101 },
    { "Taipei", "台北", 250328, 1215654, 101340101 },
    { "Tokyo", "东京", 356766, 1396500, 0 },
    { "Singapore", "新加坡", 13520, 1038198, 0 },
    { "London", "伦敦", 515072, -1278, 0 },
    { "New York", "纽约", 407128, -740060, 0 },
};
#define CITY_N (int)(sizeof(CITIES) / sizeof(CITIES[0]))

static const uint16_t IV[] = { 15, 30, 60, 180 };

typedef struct {
    float tmin;
    float tmax;
    float feels;
    float wind;
    float uv;
    int wmo;
    uint8_t ok;
} wx_day_t;

typedef struct {
    float temp;
    float feels;
    float tmin;
    float tmax;
    float wind;
    float uv;
    int rh;
    int wmo;
    uint8_t ok;     // 0 none, 1 ok, 2 fail
    uint8_t busy;
    uint8_t day_n;
    wx_day_t day[WX_DAYS];
    int64_t ok_us;
    int64_t fail_us;
} wx_snap_t;

static wx_snap_t s_snap;
static volatile int s_req; // 1=forecast 2=geocode
static volatile bool s_paused;
static char s_lookup[33];
static char s_http[WX_BUF];
static int64_t s_hold_off_us;
static volatile bool s_geo_fail;

static lv_obj_t *s_title, *s_hint, *s_body, *s_icon, *s_temp;
static lv_obj_t *s_card, *s_mini_qr, *s_mini_url;
static lv_obj_t *s_set_rows[WX_SET_N], *s_set_labs[WX_SET_N], *s_set_metas[WX_SET_N];
static lv_timer_t *s_timer;
static view_t s_view;
static int s_sel, s_kb_sel, s_kb_set, s_icon_wmo = -1, s_day;
static char s_custom[33];
static int s_hold_btn = -1;
static int s_hold_ms;
static lv_timer_t *s_hold_timer;

static int32_t to_e4(float v)
{
    return (int32_t)(v * 10000.0f + (v < 0 ? -0.5f : 0.5f));
}

static const char *city_label(const wx_city_t *c)
{
    return app_lang() == APP_LANG_ZH ? c->zh : c->en;
}

static int city_index(void)
{
    const app_prefs_t *p = app_prefs();
    for (int i = 0; i < CITY_N; i++) {
        if (p->wx_lat_e4 == CITIES[i].lat_e4 &&
            p->wx_lon_e4 == CITIES[i].lon_e4) return i;
    }
    return -1;
}

static int city_key(void)
{
    int i = city_index();
    if (i >= 0) return CITIES[i].citykey;
    const app_prefs_t *p = app_prefs();
    for (i = 0; i < CITY_N; i++) {
        if (strcmp(p->wx_city, CITIES[i].en) == 0 ||
            strcmp(p->wx_city, CITIES[i].zh) == 0) {
            return CITIES[i].citykey;
        }
    }
    return 0;
}

static int city_by_name(const char *q)
{
    if (!q || !q[0]) return -1;
    for (int i = 0; i < CITY_N; i++) {
        if (strcmp(q, CITIES[i].en) == 0 || strcmp(q, CITIES[i].zh) == 0) {
            return i;
        }
    }
    return -1;
}

static int iv_index(uint16_t m)
{
    for (int i = 0; i < 4; i++) if (IV[i] == m) return i;
    return 1;
}

static lv_obj_t *pix(lv_obj_t *p, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    if (!o) return NULL;
    ui_pixel_strip_theme(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    return o;
}

static int wx_kind(int wmo)
{
    if (wmo <= 0) return 0;
    if (wmo == 1) return 1;
    if (wmo == 2) return 2;
    if (wmo == 3) return 3;
    if (wmo == 45 || wmo == 48) return 4;
    if (wmo >= 51 && wmo <= 57) return 5;
    if ((wmo >= 61 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) return 6;
    if ((wmo >= 71 && wmo <= 77) || (wmo >= 85 && wmo <= 86)) return 7;
    if (wmo >= 95) return 8;
    return 2;
}

static app_str_id_t cond_id(int wmo)
{
    switch (wx_kind(wmo)) {
    case 0: return APP_STR_WX_CLEAR;
    case 1: return APP_STR_WX_MAINLY;
    case 2: return APP_STR_WX_CLOUD;
    case 3: return APP_STR_WX_OVERCAST;
    case 4: return APP_STR_WX_FOG;
    case 5: return APP_STR_WX_DRIZZLE;
    case 6: return APP_STR_WX_RAIN;
    case 7: return APP_STR_WX_SNOW;
    default: return APP_STR_WX_STORM;
    }
}

static int cloth_level(float feels, int wmo, float wind_kmh)
{
    if (wx_kind(wmo) >= 6) feels -= 2.0f;
    if (wind_kmh > 28.0f) feels -= 2.0f;
    if (feels >= 33.0f) return 0;
    if (feels >= 28.0f) return 1;
    if (feels >= 23.0f) return 2;
    if (feels >= 18.0f) return 3;
    if (feels >= 13.0f) return 4;
    if (feels >= 8.0f) return 5;
    if (feels >= 0.0f) return 6;
    return 7;
}

static app_str_id_t uv_id(float uv)
{
    if (uv < 3.0f) return APP_STR_WX_UV_LOW;
    if (uv < 6.0f) return APP_STR_WX_UV_MOD;
    if (uv < 8.0f) return APP_STR_WX_UV_HIGH;
    return APP_STR_WX_UV_VHIGH;
}

void app_wx_draw_icon(lv_obj_t *p, int wmo)
{
    if (!p) return;
    lv_obj_clean(p);
    if (wmo < 0) return;
    int k = wx_kind(wmo);
    if (k <= 1) {
        pix(p, 18, 2, 4, 6, HUD_VIOLET);
        pix(p, 18, 32, 4, 6, HUD_VIOLET);
        pix(p, 2, 18, 6, 4, HUD_VIOLET);
        pix(p, 32, 18, 6, 4, HUD_VIOLET);
        pix(p, 8, 8, 4, 4, HUD_CYAN);
        pix(p, 28, 8, 4, 4, HUD_CYAN);
        pix(p, 8, 28, 4, 4, HUD_CYAN);
        pix(p, 28, 28, 4, 4, HUD_CYAN);
        pix(p, 12, 12, 16, 16, HUD_CYAN);
    }
    if (k == 1 || k >= 2) {
        uint32_t cld = (k == 7) ? 0xB9D9E8 : HUD_LINE;
        pix(p, 8, 16, 28, 12, cld);
        pix(p, 14, 10, 16, 10, cld);
        pix(p, 4, 20, 8, 8, cld);
        pix(p, 28, 20, 10, 8, cld);
    }
    if (k == 4) {
        pix(p, 6, 30, 8, 3, HUD_VIOLET);
        pix(p, 18, 34, 10, 3, HUD_VIOLET);
        pix(p, 28, 30, 8, 3, HUD_VIOLET);
    }
    if (k == 5 || k == 6 || k == 8) {
        pix(p, 12, 30, 3, 8, HUD_CYAN);
        pix(p, 20, 32, 3, 8, HUD_CYAN);
        pix(p, 28, 30, 3, 8, HUD_CYAN);
    }
    if (k == 7) {
        pix(p, 12, 30, 6, 3, HUD_CYAN);
        pix(p, 22, 34, 6, 3, HUD_CYAN);
        pix(p, 16, 36, 6, 3, HUD_CYAN);
    }
    if (k == 8) {
        pix(p, 20, 18, 4, 14, HUD_VIOLET);
        pix(p, 16, 24, 12, 4, HUD_VIOLET);
    }
}

static void pct_enc(char *out, size_t n, const char *s)
{
    size_t o = 0;
    for (; *s && o + 1 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out[o++] = (char)c;
        } else {
            if (o + 4 >= n) break;
            o += (size_t)snprintf(out + o, n - o, "%%%02X", c);
        }
    }
    out[o] = 0;
}

static bool json_num(const char *s, const char *key, float *out)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(s, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '[' || *p == '"') p++;
    char *end = NULL;
    float v = strtof(p, &end);
    if (end == p) return false;
    *out = v;
    return true;
}

static bool json_nums(const char *s, const char *key, float *out, int max, int *n)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(s, pat);
    if (!p || !out || max <= 0) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    int i = 0;
    if (*p != '[') {
        char *end = NULL;
        float v = strtof(p, &end);
        if (end == p) return false;
        out[0] = v;
        if (n) *n = 1;
        return true;
    }
    p++;
    while (i < max && *p && *p != ']') {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        if (*p == 'n') break;
        char *end = NULL;
        float v = strtof(p, &end);
        if (end == p) break;
        out[i++] = v;
        p = end;
    }
    if (n) *n = i;
    return i > 0;
}

static bool first_num(const char *s, float *out)
{
    if (!s || !out) return false;
    while (*s && *s != '-' && *s != '+' && (*s < '0' || *s > '9')) s++;
    if (!*s) return false;
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s) return false;
    *out = v;
    return true;
}

static bool json_str(const char *s, const char *key, char *out, size_t n)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(s, pat);
    if (!p) return false;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = 0;
    return i > 0;
}

static int type_to_wmo(const char *t)
{
    if (!t || !t[0]) return 2;
    if (strstr(t, "雷") || strstr(t, "storm") || strstr(t, "thunder")) return 95;
    if (strstr(t, "雪") || strstr(t, "snow") || strstr(t, "sleet")) return 71;
    if (strstr(t, "雾") || strstr(t, "霾") || strstr(t, "fog") || strstr(t, "mist")) {
        return 45;
    }
    if (strstr(t, "雨") || strstr(t, "rain") || strstr(t, "drizzle") ||
        strstr(t, "shower")) return 61;
    if (strstr(t, "阴") || strstr(t, "overcast")) return 3;
    if (strstr(t, "云") || strstr(t, "cloud")) return 2;
    if (strstr(t, "晴") || strstr(t, "sun") || strstr(t, "clear") ||
        strstr(t, "fair")) return 0;
    return 2;
}

static int parse_url(const char *url, char *host, size_t host_n,
                     const char **path, int *port)
{
    if (!url || strncmp(url, "http://", 7) != 0) return -1;
    url += 7;
    const char *slash = strchr(url, '/');
    const char *host_end = slash ? slash : url + strlen(url);
    const char *colon = NULL;
    for (const char *p = url; p < host_end; p++) {
        if (*p == ':') { colon = p; break; }
    }
    const char *h_end = colon ? colon : host_end;
    size_t hl = (size_t)(h_end - url);
    if (hl == 0 || hl + 1 > host_n) return -1;
    memcpy(host, url, hl);
    host[hl] = 0;
    *port = 80;
    if (colon) {
        int p = atoi(colon + 1);
        if (p > 0) *port = p;
    }
    *path = slash ? slash : "/";
    return 0;
}

static int sock_read(int fd, char *p, int n)
{
    int got = 0;
    while (got < n) {
        int r = recv(fd, p + got, (size_t)(n - got), 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return got ? got : -1;
        }
        if (r == 0) break;
        got += r;
    }
    return got;
}

static int sock_read_until(int fd, char *buf, int cap, const char *mark, int *used)
{
    int n = 0;
    size_t mlen = strlen(mark);
    while (n + 1 < cap) {
        int r = recv(fd, buf + n, (size_t)(cap - 1 - n), 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            *used = n;
            return -1;
        }
        if (r == 0) break;
        n += r;
        buf[n] = 0;
        if (n >= (int)mlen) {
            char *hit = strstr(buf, mark);
            if (hit) {
                *used = n;
                return 0;
            }
        }
    }
    *used = n;
    return -1;
}

static int http_get(const char *url, char *buf, size_t cap)
{
    if (!url || !buf || cap < 8) return -1;
    buf[0] = 0;
    char host[64];
    const char *path = "/";
    int port = 80;
    if (parse_url(url, host, sizeof(host), &path, &port) != 0) {
        ESP_LOGW(TAG, "bad url %.48s", url);
        return -1;
    }

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
    };
    struct addrinfo *ai = NULL;
    char port_s[8];
    snprintf(port_s, sizeof(port_s), "%d", port);
    int g = getaddrinfo(host, port_s, &hints, &ai);
    if (g != 0 || !ai) {
        ESP_LOGW(TAG, "dns %s g=%d heap=%u", host, g,
                 (unsigned)esp_get_free_heap_size());
        return -1;
    }

    int fd = socket(ai->ai_family, ai->ai_socktype, 0);
    if (fd < 0) {
        freeaddrinfo(ai);
        ESP_LOGW(TAG, "socket fail heap=%u", (unsigned)esp_get_free_heap_size());
        return -1;
    }
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int c = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (c != 0) {
        close(fd);
        vTaskDelay(pdMS_TO_TICKS(400));
        fd = socket(ai->ai_family, ai->ai_socktype, 0);
        if (fd >= 0) {
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            c = connect(fd, ai->ai_addr, ai->ai_addrlen);
        }
    }
    freeaddrinfo(ai);
    if (fd < 0 || c != 0) {
        if (fd >= 0) close(fd);
        ESP_LOGW(TAG, "connect %s:%d fail errno=%d", host, port, errno);
        return -1;
    }

    int req_n = snprintf(buf, cap,
                         "GET %s HTTP/1.0\r\n"
                         "Host: %s\r\n"
                         "User-Agent: Mozilla/5.0\r\n"
                         "Accept: */*\r\n"
                         "\r\n",
                         path, host);
    if (req_n <= 0 || req_n >= (int)cap ||
        send(fd, buf, (size_t)req_n, 0) != req_n) {
        close(fd);
        ESP_LOGW(TAG, "send fail %s errno=%d", host, errno);
        return -1;
    }

    int hdr_n = 0;
    if (sock_read_until(fd, buf, (int)cap, "\r\n\r\n", &hdr_n) != 0) {
        close(fd);
        ESP_LOGW(TAG, "hdr fail %s n=%d errno=%d", host, hdr_n, errno);
        return -1;
    }

    int status = 0;
    const char *sp = strchr(buf, ' ');
    if (sp) status = atoi(sp + 1);
    if (status < 200 || status > 299) {
        close(fd);
        ESP_LOGW(TAG, "http status=%d %s", status, host);
        return -1;
    }

    int extra = 0;
    const char *end = strstr(buf, "\r\n\r\n");
    if (end) {
        extra = hdr_n - (int)(end - buf) - 4;
        if (extra < 0) extra = 0;
        if (extra > 0 && extra < (int)cap) {
            memmove(buf, end + 4, (size_t)extra);
        } else {
            extra = 0;
        }
    }

    int body = extra;
    for (;;) {
        int room = (int)cap - 1 - body;
        if (room <= 0) break;
        int r = sock_read(fd, buf + body, room);
        if (r < 0) {
            if (body == 0) {
                close(fd);
                ESP_LOGW(TAG, "body fail %s errno=%d", host, errno);
                return -1;
            }
            break;
        }
        if (r == 0) break;
        body += r;
    }
    buf[body] = 0;
    close(fd);
    if (body <= 0) {
        ESP_LOGW(TAG, "empty body %s", host);
        return -1;
    }
    return body;
}

static int s_radio;

static void radio_enter(void)
{
    if (s_radio++ == 0) {
        bsp_wifi_ps_hold();
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

static void radio_leave(void)
{
    if (s_radio > 0 && --s_radio == 0) bsp_wifi_ps_release();
}

static void snap_ok(wx_snap_t *out, float t, float feels, float tmin, float tmax,
                    float wind, float uv, int rh, int wmo)
{
    memset(out->day, 0, sizeof(out->day));
    out->temp = t;
    out->feels = feels;
    out->tmin = tmin;
    out->tmax = tmax;
    out->wind = wind;
    out->uv = uv;
    out->rh = rh;
    out->wmo = wmo;
    out->ok = 1;
    out->ok_us = esp_timer_get_time();
    out->day[0].tmin = tmin;
    out->day[0].tmax = tmax;
    out->day[0].feels = feels;
    out->day[0].wind = wind;
    out->day[0].uv = uv;
    out->day[0].wmo = wmo;
    out->day[0].ok = 1;
    out->day_n = 1;
}

static void parse_daily(const char *daily, wx_snap_t *out)
{
    if (!daily || !out) return;
    float tmax[WX_DAYS], tmin[WX_DAYS], wmo[WX_DAYS];
    float wind[WX_DAYS], uv[WX_DAYS], feels[WX_DAYS];
    int nmax = 0, nmin = 0, nw = 0, nwind = 0, nuv = 0, nfeels = 0;
    json_nums(daily, "temperature_2m_max", tmax, WX_DAYS, &nmax);
    json_nums(daily, "temperature_2m_min", tmin, WX_DAYS, &nmin);
    json_nums(daily, "weather_code", wmo, WX_DAYS, &nw);
    json_nums(daily, "wind_speed_10m_max", wind, WX_DAYS, &nwind);
    json_nums(daily, "uv_index_max", uv, WX_DAYS, &nuv);
    json_nums(daily, "apparent_temperature_max", feels, WX_DAYS, &nfeels);
    int n = nmax;
    if (nmin < n) n = nmin;
    if (nw > 0 && nw < n) n = nw;
    if (n < 1) return;
    if (n > WX_DAYS) n = WX_DAYS;
    for (int i = 0; i < n; i++) {
        out->day[i].tmax = tmax[i];
        out->day[i].tmin = tmin[i];
        out->day[i].wmo = (i < nw) ? (int)(wmo[i] + 0.5f) : out->wmo;
        out->day[i].wind = (i < nwind) ? wind[i] : 0;
        out->day[i].uv = (i < nuv) ? uv[i] : -1;
        out->day[i].feels = (i < nfeels) ? feels[i] : tmax[i];
        out->day[i].ok = 1;
    }
    out->day_n = (uint8_t)n;
    out->tmin = tmin[0];
    out->tmax = tmax[0];
}

static bool parse_forecast(const char *js, wx_snap_t *out)
{
    const char *cur = strstr(js, "\"current\":");
    if (!cur) return false;
    const char *daily = strstr(js, "\"daily\":");
    float t = 0, feels = 0, wmo = 0, rh = 0, wind = 0, uv = 0, tmin = 0, tmax = 0;
    if (!json_num(cur, "temperature_2m", &t)) return false;
    if (!json_num(cur, "apparent_temperature", &feels)) feels = t;
    if (!json_num(cur, "weather_code", &wmo)) wmo = 0;
    if (!json_num(cur, "relative_humidity_2m", &rh)) rh = 0;
    if (!json_num(cur, "wind_speed_10m", &wind)) wind = 0;
    if (!json_num(cur, "uv_index", &uv)) uv = -1;
    if (daily) {
        json_num(daily, "temperature_2m_min", &tmin);
        json_num(daily, "temperature_2m_max", &tmax);
    }
    snap_ok(out, t, feels, tmin, tmax, wind, uv, (int)(rh + 0.5f),
            (int)(wmo + 0.5f));
    if (daily) parse_daily(daily, out);
    return true;
}

static const char *next_json_obj(const char *p, char *buf, size_t cap)
{
    const char *a = strchr(p, '{');
    if (!a) return NULL;
    int depth = 0;
    for (const char *b = a; *b; b++) {
        if (*b == '{') depth++;
        else if (*b == '}') {
            if (--depth == 0) {
                size_t n = (size_t)(b - a + 1);
                if (n >= cap) n = cap - 1;
                memcpy(buf, a, n);
                buf[n] = 0;
                return b + 1;
            }
        }
    }
    return NULL;
}

static bool fill_itboy_day(const char *obj, wx_day_t *d)
{
    char high[24] = { 0 }, low[24] = { 0 }, type[24] = { 0 }, fl[16] = { 0 };
    json_str(obj, "high", high, sizeof(high));
    json_str(obj, "low", low, sizeof(low));
    json_str(obj, "type", type, sizeof(type));
    json_str(obj, "fl", fl, sizeof(fl));
    float tmax = 0, tmin = 0, flv = 0;
    if (!first_num(high, &tmax) || !first_num(low, &tmin)) return false;
    if (tmax < tmin) {
        float x = tmax;
        tmax = tmin;
        tmin = x;
    }
    int lv = 2;
    if (first_num(fl, &flv)) lv = (int)(flv + 0.5f);
    static const float bft[] = { 0, 3, 9, 15, 25, 35, 45, 56, 68 };
    if (lv < 0) lv = 0;
    if (lv > 8) lv = 8;
    d->tmax = tmax;
    d->tmin = tmin;
    d->feels = tmax;
    d->wind = bft[lv];
    d->uv = -1;
    d->wmo = type_to_wmo(type);
    d->ok = 1;
    return true;
}

static void parse_itboy_days(const char *js, wx_snap_t *out)
{
    const char *p = strstr(js, "\"forecast\"");
    if (!p) return;
    char obj[480];
    int n = 0;
    while (n < WX_DAYS) {
        p = next_json_obj(p, obj, sizeof(obj));
        if (!p) break;
        if (!fill_itboy_day(obj, &out->day[n])) continue;
        n++;
    }
    if (n > 0) {
        out->day_n = (uint8_t)n;
        out->tmin = out->day[0].tmin;
        out->tmax = out->day[0].tmax;
    }
}

static bool parse_itboy(const char *js, wx_snap_t *out)
{
    if (!strstr(js, "\"data\"")) return false;
    float t = 0, rh = 0, tmin = 0, tmax = 0, wind = 9;
    if (!json_num(js, "wendu", &t)) return false;
    json_num(js, "shidu", &rh);
    char high[24] = { 0 }, low[24] = { 0 }, type[24] = { 0 }, fl[16] = { 0 };
    json_str(js, "high", high, sizeof(high));
    json_str(js, "low", low, sizeof(low));
    json_str(js, "type", type, sizeof(type));
    json_str(js, "fl", fl, sizeof(fl));
    first_num(high, &tmax);
    first_num(low, &tmin);
    int lv = 2;
    float flv = 0;
    if (first_num(fl, &flv)) lv = (int)(flv + 0.5f);
    static const float bft[] = { 0, 3, 9, 15, 25, 35, 45, 56, 68 };
    if (lv < 0) lv = 0;
    if (lv > 8) lv = 8;
    wind = bft[lv];
    if (tmax < tmin) { float x = tmax; tmax = tmin; tmin = x; }
    snap_ok(out, t, t, tmin, tmax, wind, -1, (int)(rh + 0.5f), type_to_wmo(type));
    parse_itboy_days(js, out);
    return true;
}

static bool parse_wttr(const char *s, wx_snap_t *out)
{
    if (!s || strstr(s, "Unknown") || strstr(s, "not available") ||
        strstr(s, "ERROR")) return false;
    char tmp[192];
    strlcpy(tmp, s, sizeof(tmp));
    char *f[8] = { 0 };
    int n = 0;
    char *p = tmp;
    while (n < 8 && p) {
        f[n++] = p;
        char *bar = strchr(p, '|');
        if (!bar) break;
        *bar = 0;
        p = bar + 1;
    }
    if (n < 5) return false;
    float t = 0, feels = 0, rh = 0, wind = 0, uv = -1;
    if (!first_num(f[0], &t)) return false;
    if (!first_num(f[1], &feels)) feels = t;
    first_num(f[2], &rh);
    first_num(f[3], &wind);
    if (n > 5 && f[5]) first_num(f[5], &uv);
    snap_ok(out, t, feels, t, t, wind, uv, (int)(rh + 0.5f),
            type_to_wmo(f[4]));
    return true;
}

static bool parse_geo(const char *js, char *name, size_t name_n,
                      int32_t *lat_e4, int32_t *lon_e4)
{
    if (!strstr(js, "\"results\"")) return false;
    float lat = 0, lon = 0;
    if (!json_num(js, "latitude", &lat) || !json_num(js, "longitude", &lon)) {
        return false;
    }
    char nbuf[48];
    if (!json_str(js, "name", nbuf, sizeof(nbuf))) return false;
    ui_pixel_utf8_copy(name, name_n, nbuf);
    *lat_e4 = to_e4(lat);
    *lon_e4 = to_e4(lon);
    return name[0] != 0;
}

static void set_busy(bool on)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_snap.busy = on ? 1 : 0;
    xSemaphoreGive(s_mu);
}

static bool try_itboy(wx_snap_t *out)
{
    int key = city_key();
    if (key <= 0) return false;
    char url[96];
    snprintf(url, sizeof(url),
             "http://t.weather.itboy.net/api/weather/city/%d", key);
    int n = http_get(url, s_http, sizeof(s_http));
    if (n > 0 && parse_itboy(s_http, out)) {
        ESP_LOGI(TAG, "itboy ok %.1f wmo=%d days=%d", out->temp, out->wmo, out->day_n);
        return true;
    }
    ESP_LOGW(TAG, "itboy fail n=%d %.80s", n, n > 0 ? s_http : "");
    return false;
}

static bool try_openmeteo(wx_snap_t *out)
{
    app_prefs_t *p = app_prefs();
    if (p->wx_lat_e4 == 0 && p->wx_lon_e4 == 0) return false;
    char url[400];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?"
             "latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,"
             "apparent_temperature,weather_code,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
             "apparent_temperature_max,wind_speed_10m_max,uv_index_max"
             "&forecast_days=4&timezone=auto",
             p->wx_lat_e4 / 10000.0f, p->wx_lon_e4 / 10000.0f);
    int n = http_get(url, s_http, sizeof(s_http));
    if (n > 0 && parse_forecast(s_http, out)) {
        ESP_LOGI(TAG, "om ok %.1f wmo=%d days=%d", out->temp, out->wmo, out->day_n);
        return true;
    }
    ESP_LOGW(TAG, "om fail n=%d %.80s", n, n > 0 ? s_http : "");
    return false;
}

static bool try_wttr(wx_snap_t *out)
{
    app_prefs_t *p = app_prefs();
    char enc[96];
    const char *q = p->wx_city[0] ? p->wx_city : "Shanghai";
    pct_enc(enc, sizeof(enc), q);
    char url[160];
    snprintf(url, sizeof(url),
             "http://wttr.in/%s?m&format=%%t%%7C%%f%%7C%%h%%7C%%w%%7C%%C%%7C%%u", enc);
    int n = http_get(url, s_http, sizeof(s_http));
    if (n > 0 && parse_wttr(s_http, out)) {
        ESP_LOGI(TAG, "wttr ok %.1f wmo=%d", out->temp, out->wmo);
        return true;
    }
    ESP_LOGW(TAG, "wttr fail n=%d %.80s", n, n > 0 ? s_http : "");
    return false;
}

static void fetch_forecast(void)
{
    set_busy(true);
    wx_snap_t next = { 0 };
    xSemaphoreTake(s_mu, portMAX_DELAY);
    next = s_snap;
    xSemaphoreGive(s_mu);
    size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "fetch heap=%u blk=%u",
             (unsigned)esp_get_free_heap_size(), (unsigned)blk);
    if (!app_net_heap_ready(10 * 1024) ||
        !app_net_acquire(APP_NET_WEATHER, 0)) {
        ESP_LOGW(TAG, "skip fetch, keep cache");
        s_hold_off_us = esp_timer_get_time() + 15LL * 1000000;
        next.busy = 0;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        s_snap = next;
        xSemaphoreGive(s_mu);
        return;
    }
    radio_enter();
    bool ok = try_itboy(&next) || try_openmeteo(&next) || try_wttr(&next);
    radio_leave();
    app_net_release(APP_NET_WEATHER);
    if (!ok) {
        next.fail_us = esp_timer_get_time();
        if (next.ok != 1) next.ok = 2;
        s_hold_off_us = next.fail_us + 120LL * 1000000;
        ESP_LOGW(TAG, "forecast fail keep=%d", next.ok == 1);
    }
    next.busy = 0;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_snap = next;
    xSemaphoreGive(s_mu);
}

static void fetch_geocode(const char *q)
{
    s_geo_fail = false;
    int preset = city_by_name(q);
    if (preset >= 0) {
        app_prefs_t *p = app_prefs();
        ui_pixel_utf8_copy(p->wx_city, sizeof(p->wx_city),
                           city_label(&CITIES[preset]));
        p->wx_lat_e4 = CITIES[preset].lat_e4;
        p->wx_lon_e4 = CITIES[preset].lon_e4;
        app_prefs_save();
        fetch_forecast();
        return;
    }
    char enc[96];
    pct_enc(enc, sizeof(enc), q);
    char url[220];
    snprintf(url, sizeof(url),
             "http://geocoding-api.open-meteo.com/v1/search?name=%s"
             "&count=1&language=%s",
             enc, app_lang() == APP_LANG_ZH ? "zh" : "en");
    set_busy(true);
    if (!app_net_heap_ready(10 * 1024) ||
        !app_net_acquire(APP_NET_WEATHER, 0)) {
        s_geo_fail = true;
        set_busy(false);
        s_hold_off_us = esp_timer_get_time() + 15LL * 1000000;
        return;
    }
    radio_enter();
    int n = http_get(url, s_http, sizeof(s_http));
    char name[33] = { 0 };
    int32_t lat = 0, lon = 0;
    bool ok = n > 0 && parse_geo(s_http, name, sizeof(name), &lat, &lon);
    if (ok) {
        app_prefs_t *p = app_prefs();
        ui_pixel_utf8_copy(p->wx_city, sizeof(p->wx_city), name);
        p->wx_lat_e4 = lat;
        p->wx_lon_e4 = lon;
        app_prefs_save();
    } else {
        ESP_LOGW(TAG, "geocode fail %.32s", q);
        s_geo_fail = true;
        set_busy(false);
    }
    radio_leave();
    app_net_release(APP_NET_WEATHER);
    if (ok) fetch_forecast();
}

static bool due_now(const wx_snap_t *s)
{
    int64_t now = esp_timer_get_time();
    if (now < s_hold_off_us) return false;
    uint16_t iv = app_prefs()->wx_interval;
    if (iv < 15) iv = 30;
    int64_t span = (int64_t)iv * 60 * 1000000LL;
    if (s->ok != 1) {
        if (s->fail_us && now - s->fail_us < 120LL * 1000000) return false;
        return true;
    }
    return now - s->ok_us >= span;
}

static void wx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task running");
    if (!s_paused && bsp_wifi_state() == BSP_WIFI_CONNECTED) {
        int req = s_req;
        s_req = 0;
        wx_snap_t snap;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        snap = s_snap;
        xSemaphoreGive(s_mu);
        if (req == 2 && s_lookup[0]) {
            char q[33];
            strlcpy(q, s_lookup, sizeof(q));
            fetch_geocode(q);
        } else if (req == 1 || due_now(&snap)) {
            fetch_forecast();
        }
    }
    ESP_LOGI(TAG, "task done stack=%u",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    s_task = NULL;
    vTaskDelete(NULL);
}

void app_wx_start(void)
{
    if (s_task || s_paused || !bsp_wifi_enabled()) return;
    uint32_t now = xTaskGetTickCount();
    if (s_retry_at && now < s_retry_at) return;
    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
        if (!s_mu) return;
    }
    wx_snap_t snap;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    snap = s_snap;
    xSemaphoreGive(s_mu);
    if (!s_req && !due_now(&snap)) return;
    if (bsp_wifi_state() != BSP_WIFI_CONNECTED) {
        bsp_wifi_radio_resume();
        return;
    }
    /* BLE 在线时连续块常 <10KB,开任务也只会 skip fetch,反而把 min heap 打穿。 */
    if (!app_net_heap_ready(12 * 1024)) {
        s_retry_at = now + pdMS_TO_TICKS(15000);
        return;
    }
    if (xTaskCreate(wx_task, "wx", 4096, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        s_retry_at = now + pdMS_TO_TICKS(8000);
        ESP_LOGW(TAG, "wx task fail heap=%u blk=%u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        return;
    }
    s_retry_at = 0;
    ESP_LOGI(TAG, "wx task ok heap=%u", (unsigned)esp_get_free_heap_size());
}

void app_wx_pause(bool on)
{
    s_paused = on;
}

void app_wx_stop(void)
{
    s_paused = true;
}

static void apply_city(int i)
{
    if (i < 0 || i >= CITY_N) i = 0;
    s_geo_fail = false;
    app_prefs_t *p = app_prefs();
    ui_pixel_utf8_copy(p->wx_city, sizeof(p->wx_city), city_label(&CITIES[i]));
    p->wx_lat_e4 = CITIES[i].lat_e4;
    p->wx_lon_e4 = CITIES[i].lon_e4;
    app_prefs_save();
    s_day = 0;
    s_req = 1;
}

static int disp_temp(float c)
{
    if (app_prefs()->wx_imperial) return (int)(c * 9.0f / 5.0f + 32.0f + (c < 0 ? -0.5f : 0.5f));
    return (int)(c + (c < 0 ? -0.5f : 0.5f));
}

static int disp_wind(float kmh)
{
    if (app_prefs()->wx_imperial) return (int)(kmh * 0.621371f + 0.5f);
    return (int)(kmh + 0.5f);
}

bool app_wx_lock_line(char *out, size_t n)
{
    if (!out || n == 0) return false;
    out[0] = 0;
    if (!bsp_wifi_enabled() || !s_mu) return false;

    const app_prefs_t *p = app_prefs();
    int ci = city_index();
    const char *cname = ci >= 0 ? city_label(&CITIES[ci]) : p->wx_city;
    if (!cname[0]) cname = app_str(APP_STR_WX);

    wx_snap_t snap;
    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    snap = s_snap;
    xSemaphoreGive(s_mu);

    if (snap.ok != 1) return false;
    snprintf(out, n, "%s  %d\xc2\xb0%s  %s",
             cname, disp_temp(snap.temp),
             p->wx_imperial ? "F" : "C",
             app_str(cond_id(snap.wmo)));
    return true;
}

bool app_wx_brief(char *out, size_t n)
{
    if (!out || n == 0) return false;
    out[0] = 0;
    if (!bsp_wifi_enabled() || !s_mu) return false;
    const app_prefs_t *p = app_prefs();
    int ci = city_index();
    const char *cname = ci >= 0 ? city_label(&CITIES[ci]) : p->wx_city;
    wx_snap_t snap;
    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    snap = s_snap;
    xSemaphoreGive(s_mu);
    if (snap.ok != 1 || !cname[0]) return false;
    snprintf(out, n, "%s %d\xc2\xb0", cname, disp_temp(snap.temp));
    return true;
}

bool app_wx_lock_card(char *city, size_t cn, char *temp, size_t tn,
                      char *sub, size_t sn)
{
    if (!city || !temp || !sub || cn == 0 || tn == 0 || sn == 0) return false;
    city[0] = temp[0] = sub[0] = 0;
    if (!bsp_wifi_enabled() || !s_mu) return false;
    const app_prefs_t *p = app_prefs();
    int ci = city_index();
    const char *cname = ci >= 0 ? city_label(&CITIES[ci]) : p->wx_city;
    wx_snap_t snap;
    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    snap = s_snap;
    xSemaphoreGive(s_mu);
    if (snap.ok != 1) return false;
    ui_pixel_utf8_copy(city, cn, cname[0] ? cname : app_str(APP_STR_WX));
    snprintf(temp, tn, "%d\xc2\xb0", disp_temp(snap.temp));
    snprintf(sub, sn, "%s", app_str(cond_id(snap.wmo)));
    return true;
}

int app_wx_wmo(void)
{
    if (!s_mu) return -1;
    wx_snap_t snap;
    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) != pdTRUE) return -1;
    snap = s_snap;
    xSemaphoreGive(s_mu);
    return snap.ok == 1 ? snap.wmo : -1;
}

static void fmt_iv(char *out, size_t n, uint16_t m)
{
    if (m >= 60 && m % 60 == 0) snprintf(out, n, "%dh", m / 60);
    else snprintf(out, n, "%dm", m);
}

static void set_rows_hide(void)
{
    for (int i = 0; i < WX_SET_N; i++) {
        if (s_set_rows[i]) lv_obj_add_flag(s_set_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_row(int i, const char *label, const char *meta)
{
    if (i < 0 || i >= WX_SET_N || !s_card) return;
    if (!s_set_rows[i]) {
        s_set_rows[i] = app_ui_row(s_card, 0, 44 + i * 37, 200, 32);
        s_set_labs[i] = lv_label_create(s_set_rows[i]);
        lv_obj_set_style_text_font(s_set_labs[i], ui_pixel_font_14(), 0);
        lv_obj_set_style_text_color(s_set_labs[i], lv_color_hex(UI_TEXT), 0);
        lv_obj_align(s_set_labs[i], LV_ALIGN_LEFT_MID, 7, 0);
        s_set_metas[i] = lv_label_create(s_set_rows[i]);
        lv_obj_set_style_text_font(s_set_metas[i], ui_pixel_font_14(), 0);
        lv_obj_set_style_text_color(s_set_metas[i], lv_color_hex(UI_MUTE), 0);
        lv_obj_set_width(s_set_metas[i], 108);
        lv_obj_set_style_text_align(s_set_metas[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_set_metas[i], LV_LABEL_LONG_CLIP);
        lv_obj_align(s_set_metas[i], LV_ALIGN_RIGHT_MID, -7, 0);
    }
    lv_obj_remove_flag(s_set_rows[i], LV_OBJ_FLAG_HIDDEN);
    bool selected = s_sel == i;
    app_ui_select(s_set_rows[i], selected, HUD_CYAN);
    lv_obj_set_style_text_color(s_set_labs[i], lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_color(s_set_metas[i],
                                lv_color_hex(selected ? UI_TEXT : HUD_MUTE), 0);
    lv_label_set_text(s_set_labs[i], label);
    lv_label_set_text(s_set_metas[i], meta);
}

static void wx_day_title(int off, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (off <= 0) {
        snprintf(out, n, "%s", app_str(APP_STR_WX_TODAY));
        return;
    }
    if (off == 1) {
        snprintf(out, n, "%s", app_str(APP_STR_WX_TOMORROW));
        return;
    }
    time_t ts = time(NULL) + (time_t)off * 86400;
    struct tm t;
    localtime_r(&ts, &t);
    int w = t.tm_wday;
    if (w < 0 || w > 6) w = 0;
    if (app_lang() == APP_LANG_ZH) {
        static const char *const ZH[] = {
            "星期日", "星期一", "星期二", "星期三",
            "星期四", "星期五", "星期六",
        };
        snprintf(out, n, "%s %d/%d", ZH[w], t.tm_mon + 1, t.tm_mday);
    } else {
        static const char *const EN[] = {
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
        };
        snprintf(out, n, "%s %d/%d", EN[w], t.tm_mon + 1, t.tm_mday);
    }
}

static void wx_day_short(int off, char *out, size_t n)
{
    if (off == 0) {
        snprintf(out, n, "%s", app_lang() == APP_LANG_ZH ? "今" : "Now");
        return;
    }
    if (off == 1) {
        snprintf(out, n, "%s", app_lang() == APP_LANG_ZH ? "明" : "Next");
        return;
    }
    time_t ts = time(NULL) + (time_t)off * 86400;
    struct tm t;
    localtime_r(&ts, &t);
    static const char *const ZH[] = { "日", "一", "二", "三", "四", "五", "六" };
    static const char *const EN[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    int w = t.tm_wday;
    if (w < 0 || w > 6) w = 0;
    snprintf(out, n, "%s", app_lang() == APP_LANG_ZH ? ZH[w] : EN[w]);
}

static void paint(void)
{
    if (!s_hint || !s_body) return;
    lv_obj_set_y(s_body, s_view == VIEW_MAIN ? 92 : 44);
    wx_snap_t snap;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    snap = s_snap;
    xSemaphoreGive(s_mu);

    if (s_view == VIEW_KB) {
        set_rows_hide();
        if (s_body) lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        if (s_hint) lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        if (s_icon) lv_obj_add_flag(s_icon, LV_OBJ_FLAG_HIDDEN);
        if (s_temp) lv_obj_add_flag(s_temp, LV_OBJ_FLAG_HIDDEN);
        if (s_title) lv_label_set_text(s_title, app_str(APP_STR_WX_CUSTOM));
        bool wifi = bsp_wifi_state() == BSP_WIFI_CONNECTED;
        app_kb_show(s_card, s_custom, s_kb_sel, s_kb_set,
                    wifi ? APP_WEB_MINI_H + 6 : 4);
        app_web_mini_qr_bind(s_card, &s_mini_qr, &s_mini_url);
        app_web_mini_qr_show(s_mini_qr, s_mini_url, wifi);
        app_web_set_target("city", s_custom, sizeof(s_custom), paint);
        return;
    }

    app_kb_hide();
    if (s_hint) lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    app_web_clear_target();
    app_web_mini_qr_show(s_mini_qr, s_mini_url, false);
    if (s_body) lv_obj_set_height(s_body, LV_SIZE_CONTENT);
    if (s_view == VIEW_SET) {
        lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        if (s_icon) lv_obj_add_flag(s_icon, LV_OBJ_FLAG_HIDDEN);
        if (s_temp) lv_obj_add_flag(s_temp, LV_OBJ_FLAG_HIDDEN);
        if (s_title) lv_label_set_text(s_title, app_str(APP_STR_WX_SET));
        lv_label_set_text(s_hint, app_str(APP_STR_HINT_OPEN));
        const app_prefs_t *p = app_prefs();
        int ci = city_index();
        const char *cname = ci >= 0 ? city_label(&CITIES[ci]) : p->wx_city;
        char iv[8];
        fmt_iv(iv, sizeof(iv), p->wx_interval);
        set_row(0, app_str(APP_STR_WX_CITY), cname);
        set_row(1, app_str(APP_STR_WX_CUSTOM), "KEYBOARD / QR");
        set_row(2, app_str(APP_STR_WX_INTERVAL), iv);
        set_row(3, app_str(APP_STR_WX_UNITS),
                app_str(p->wx_imperial ? APP_STR_WX_IMPERIAL : APP_STR_WX_METRIC));
        set_row(4, app_str(APP_STR_WX_REFRESH), "GO");
        set_row(5, app_str(APP_STR_WX_DONE), "GO");
        return;
    }

    set_rows_hide();
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    const app_prefs_t *p = app_prefs();
    int ci = city_index();
    const char *cname = ci >= 0 ? city_label(&CITIES[ci]) : p->wx_city;
    int day_n = snap.day_n > 0 ? (int)snap.day_n : (snap.ok == 1 ? 1 : 0);
    int day = s_day;
    if (day < 0 || day >= day_n) day = 0;
    char dname[32];
    wx_day_title(day, dname, sizeof(dname));
    if (s_title) {
        if (cname[0] && day_n > 1) {
            lv_label_set_text_fmt(s_title, "%s  %s", cname, dname);
        } else {
            lv_label_set_text(s_title, cname[0] ? cname : app_str(APP_STR_WX));
        }
    }
    if (s_geo_fail && !snap.busy) {
        lv_label_set_text(s_hint, app_str(APP_STR_WX_CITY_NOT_FOUND));
    } else if (bsp_wifi_state() != BSP_WIFI_CONNECTED) {
        lv_label_set_text(s_hint, app_str(APP_STR_WX_NEED_WIFI));
    } else if (snap.busy) {
        lv_label_set_text(s_hint, app_str(APP_STR_WX_UPDATING));
    } else if (snap.ok != 1) {
        lv_label_set_text(s_hint, app_str(APP_STR_WX_FAIL));
    } else if (day_n > 1) {
        lv_label_set_text(s_hint, app_str(APP_STR_WX_HINT_DAYS));
    } else {
        lv_label_set_text(s_hint, app_str(APP_STR_HINT_WX_SET));
    }

    if (s_icon) lv_obj_remove_flag(s_icon, LV_OBJ_FLAG_HIDDEN);
    if (s_temp) lv_obj_remove_flag(s_temp, LV_OBJ_FLAG_HIDDEN);

    if (snap.ok == 1) {
        int wmo = snap.wmo;
        float tshow = snap.temp;
        float feels_v = snap.feels;
        float tmin = snap.tmin;
        float tmax = snap.tmax;
        float wind_v = snap.wind;
        float uv_v = snap.uv;
        int rh = snap.rh;
        bool now = (day == 0);
        if (day > 0 && day < day_n && snap.day[day].ok) {
            const wx_day_t *d = &snap.day[day];
            wmo = d->wmo;
            tshow = d->tmax;
            feels_v = d->feels;
            tmin = d->tmin;
            tmax = d->tmax;
            wind_v = d->wind;
            uv_v = d->uv;
            rh = -1;
            now = false;
        }
        if (wmo != s_icon_wmo) {
            app_wx_draw_icon(s_icon, wmo);
            s_icon_wmo = wmo;
        }
        lv_label_set_text_fmt(s_temp, "%d\xc2\xb0%s", disp_temp(tshow),
                              p->wx_imperial ? "F" : "C");
        int cl = cloth_level(feels_v, wmo, wind_v);
        char feels[40], hum[24], wind[32], uv[40];
        if (now) {
            snprintf(feels, sizeof(feels), app_str(APP_STR_WX_FEELS),
                     disp_temp(feels_v));
            snprintf(hum, sizeof(hum), app_str(APP_STR_WX_HUM), rh);
        } else {
            feels[0] = 0;
            hum[0] = 0;
        }
        snprintf(wind, sizeof(wind), app_str(APP_STR_WX_WIND),
                 disp_wind(wind_v), p->wx_imperial ? "mph" : "km/h");
        if (uv_v >= 0) {
            snprintf(uv, sizeof(uv), app_str(APP_STR_WX_UV),
                     uv_v, app_str(uv_id(uv_v)));
        } else {
            uv[0] = 0;
        }
        char buf[420];
        if (now) {
            snprintf(buf, sizeof(buf),
                     "[ %s ]\n----------------\n%s  %d/%d\xc2\xb0\n%s  %s\n%s\n%s  %s\n",
                     app_str(cond_id(wmo)),
                     feels, disp_temp(tmin), disp_temp(tmax),
                     hum, wind, uv,
                     app_str(APP_STR_WX_CLOTH),
                     app_str((app_str_id_t)(APP_STR_WX_CL0 + cl)));
            size_t used = strlen(buf);
            for (int i = 0; i < day_n && i < WX_DAYS && used + 24 < sizeof(buf); i++) {
                if (!snap.day[i].ok) continue;
                char day_s[8];
                wx_day_short(i, day_s, sizeof(day_s));
                int wrote = snprintf(buf + used, sizeof(buf) - used,
                                     "%s %d/%d\xc2\xb0%s",
                                     day_s,
                                     disp_temp(snap.day[i].tmax),
                                     disp_temp(snap.day[i].tmin),
                                     (i % 2) ? "\n" : "  ");
                if (wrote < 0 || (size_t)wrote >= sizeof(buf) - used) break;
                used += (size_t)wrote;
            }
        } else {
            snprintf(buf, sizeof(buf),
                     "[ %s ]\n----------------\n%d/%d\xc2\xb0\n%s\n%s\n%s  %s\n",
                     app_str(cond_id(wmo)),
                     disp_temp(tmax), disp_temp(tmin),
                     wind, uv,
                     app_str(APP_STR_WX_CLOTH),
                     app_str((app_str_id_t)(APP_STR_WX_CL0 + cl)));
        }
        lv_label_set_text(s_body, buf);
    } else {
        if (s_icon_wmo != -2) {
            lv_obj_clean(s_icon);
            s_icon_wmo = -2;
        }
        lv_label_set_text(s_temp, "--");
        lv_label_set_text(s_body, snap.busy ? app_str(APP_STR_WX_UPDATING)
                                            : (bsp_wifi_state() == BSP_WIFI_CONNECTED
                                                   ? app_str(APP_STR_WX_FAIL)
                                                   : app_str(APP_STR_WX_NEED_WIFI)));
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (s_view == VIEW_MAIN) paint();
}

static void hold_tick(lv_timer_t *t)
{
    (void)t;
    if (s_view != VIEW_KB || s_hold_btn < 0) return;
    s_hold_ms += 120;
    if (s_hold_ms < 280) return;
    int dir = (s_hold_btn == BSP_BTN_UP) ? -1 : 1;
    int step = (s_hold_ms >= 800) ? KB_COLS : 1;
    app_ui_move(&s_kb_sel, KB_N, dir * step);
    paint();
}

// 长按 OK 逐层退子视图,退到主视图再交给页栈。
static bool wx_back(void)
{
    if (s_view == VIEW_KB) {
        s_view = VIEW_SET;
        app_web_qr_close();
    } else if (s_view == VIEW_SET) {
        s_view = VIEW_MAIN;
    } else {
        return false;
    }
    paint();
    return true;
}

void app_wx_enter(lv_obj_t *p)
{
    app_shell_set_back(wx_back);
    s_view = VIEW_MAIN;
    s_sel = 0;
    s_day = 0;
    s_icon_wmo = -1;
    s_custom[0] = 0;
    lv_obj_t *card = app_ui_card(p);
    s_card = card;
    lv_obj_set_style_bg_color(card, lv_color_hex(HUD_BG), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    s_mini_qr = s_mini_url = NULL;
    memset(s_set_rows, 0, sizeof(s_set_rows));
    memset(s_set_labs, 0, sizeof(s_set_labs));
    memset(s_set_metas, 0, sizeof(s_set_metas));
    s_title = app_ui_title(card, app_str(APP_STR_WX));
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_width(s_title, 200);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_CLIP);
    s_hint = app_ui_hint(card);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(HUD_MUTE), 0);
    s_icon = lv_obj_create(card);
    ui_pixel_strip_theme(s_icon);
    lv_obj_set_pos(s_icon, 0, 48);
    lv_obj_set_size(s_icon, 40, 40);
    lv_obj_set_style_bg_opa(s_icon, LV_OPA_TRANSP, 0);
    s_temp = lv_label_create(card);
    lv_obj_set_style_text_font(s_temp, ui_pixel_font_20(), 0);
    lv_obj_set_style_text_color(s_temp, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_border_width(s_temp, 0, 0);
    lv_obj_set_style_pad_hor(s_temp, 0, 0);
    lv_obj_set_style_pad_ver(s_temp, 0, 0);
    lv_obj_align(s_temp, LV_ALIGN_TOP_LEFT, 48, 52);
    s_body = app_ui_body(card, 92);
    lv_obj_set_style_text_color(s_body, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_body, lv_color_hex(HUD_PANEL), 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_radius(s_body, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(s_body, 8, 0);
    s_timer = lv_timer_create(tick, 1000, NULL);
    s_hold_timer = lv_timer_create(hold_tick, 120, NULL);
    if (s_mu) {
        wx_snap_t snap;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        snap = s_snap;
        xSemaphoreGive(s_mu);
        if (snap.ok != 1) s_req = 1;
    } else {
        s_req = 1;
    }
    paint();
}

void app_wx_exit(void)
{
    s_hold_btn = -1;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_hold_timer) { lv_timer_delete(s_hold_timer); s_hold_timer = NULL; }
    app_web_clear_target();
    app_web_qr_close();
    app_kb_hide();
    s_title = s_hint = s_body = s_icon = s_temp = NULL;
    s_card = s_mini_qr = s_mini_url = NULL;
    memset(s_set_rows, 0, sizeof(s_set_rows));
    memset(s_set_labs, 0, sizeof(s_set_labs));
    memset(s_set_metas, 0, sizeof(s_set_metas));
}

static void do_set(void)
{
    app_prefs_t *p = app_prefs();
    if (s_sel == 0) {
        int i = city_index();
        apply_city((i < 0 ? 0 : i + 1) % CITY_N);
    } else if (s_sel == 1) {
        s_geo_fail = false;
        s_view = VIEW_KB;
        s_kb_sel = 0;
        s_kb_set = 0;
        s_custom[0] = 0;
        paint();
        return;
    } else if (s_sel == 2) {
        int i = (iv_index(p->wx_interval) + 1) % 4;
        p->wx_interval = IV[i];
        app_prefs_save();
    } else if (s_sel == 3) {
        p->wx_imperial = !p->wx_imperial;
        app_prefs_save();
    } else if (s_sel == 4) {
        s_hold_off_us = 0;
        s_req = 1;
        s_view = VIEW_MAIN;
    } else {
        s_view = VIEW_MAIN;
    }
    paint();
}

void app_wx_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_view == VIEW_KB && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        if (ev == BSP_BTN_PRESS) {
            s_hold_btn = (int)btn;
            s_hold_ms = 0;
            app_ui_move(&s_kb_sel, KB_N, btn == BSP_BTN_UP ? -1 : 1);
            paint();
        } else if (ev == BSP_BTN_RELEASE && s_hold_btn == (int)btn) {
            s_hold_btn = -1;
            s_hold_ms = 0;
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (s_view == VIEW_KB) {
        if (btn != BSP_BTN_OK) return;
        int r = app_kb_click(s_custom, sizeof(s_custom), &s_kb_sel, &s_kb_set);
        if (r == 2 && s_custom[0]) {
            strlcpy(s_lookup, s_custom, sizeof(s_lookup));
            s_req = 2;
            s_view = VIEW_MAIN;
            s_day = 0;
            app_web_qr_close();
        } else if (r == 3) {
            s_view = VIEW_SET;
            app_web_qr_close();
        } else if (r == 4) {
            app_web_qr_open();
        }
        paint();
        return;
    }
    if (s_view == VIEW_SET) {
        if (btn == BSP_BTN_UP) { app_ui_move(&s_sel, WX_SET_N, -1); paint(); return; }
        if (btn == BSP_BTN_DOWN) { app_ui_move(&s_sel, WX_SET_N, 1); paint(); return; }
        if (btn == BSP_BTN_OK) do_set();
        return;
    }
    if (btn == BSP_BTN_OK) {
        s_view = VIEW_SET;
        s_sel = 0;
        paint();
        return;
    }
    if (btn != BSP_BTN_UP && btn != BSP_BTN_DOWN) return;
    int n = 1;
    if (s_mu) {
        wx_snap_t snap;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        snap = s_snap;
        xSemaphoreGive(s_mu);
        n = snap.day_n > 0 ? (int)snap.day_n : 1;
    }
    if (n <= 1) return;
    s_day += (btn == BSP_BTN_DOWN) ? 1 : -1;
    if (s_day < 0) s_day = n - 1;
    if (s_day >= n) s_day = 0;
    paint();
}
