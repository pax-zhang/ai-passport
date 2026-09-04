#include "app_web.h"

#include "app.h"
#include "app_bg.h"
#include "app_dial.h"
#include "app_prefs.h"
#include "app_time.h"
#include "bsp_wifi.h"
#include "qrcode.h"
#include "ui_pixel.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_web";

extern const uint8_t dial_html_start[] asm("_binary_dial_html_start");
extern const uint8_t dial_html_end[] asm("_binary_dial_html_end");

static lv_obj_t *s_screen;
static lv_obj_t *s_box;
static lv_obj_t *s_ssid;
static lv_obj_t *s_url;
static lv_obj_t *s_hint;
static lv_obj_t *s_qr;
static httpd_handle_t s_httpd;
static TaskHandle_t s_ap_task;
static TaskHandle_t s_dns_task;
static volatile bool s_want;
static volatile bool s_dns_on;
static int s_dns_sock = -1;
static QRCode s_qrcode;
static uint8_t s_qr_mod[128];
static bool s_qr_ok;
static char s_qr_text[36];

static bool json_int(const char *s, const char *k, int *out)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":", k);
    const char *p = strstr(s, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    *out = (int)v;
    return true;
}

static bool json_str(const char *s, const char *k, char *out, size_t n)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":\"", k);
    const char *p = strstr(s, pat);
    if (!p) return false;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}

static bool recv_body(httpd_req_t *req, char *buf, size_t n)
{
    int total = req->content_len;
    if (total <= 0 || total >= (int)n) return false;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, (size_t)(total - got));
        if (r <= 0) return false;
        got += r;
    }
    buf[got] = 0;
    return true;
}

static esp_err_t send_html(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)dial_html_start,
                           dial_html_end - dial_html_start);
}

static void json_esc(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; s && *s && o + 2 < n; s++) {
        if (*s == '"' || *s == '\\') {
            if (o + 3 >= n) break;
            out[o++] = '\\';
        }
        if ((uint8_t)*s < 32) continue;
        out[o++] = *s;
    }
    out[o] = 0;
}

static esp_err_t send_status(httpd_req_t *req)
{
    const app_prefs_t *p = app_prefs();
    char buf[192];
    char tz[80];
    json_esc(p->tz_name, tz, sizeof(tz));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    snprintf(buf, sizeof(buf),
             "{\"face\":%u,\"off\":%d,\"tz\":\"%s\",\"ok\":%u,\"cities\":[",
             p->face, (int)p->tz_off, tz, app_time_valid() ? 1u : 0u);
    if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < APP_CITY_N; i++) {
        const app_city_t *c = app_city(i);
        char zh[32], en[24];
        json_esc(c->zh, zh, sizeof(zh));
        json_esc(c->en, en, sizeof(en));
        snprintf(buf, sizeof(buf),
                 "%s{\"n\":\"%s\",\"e\":\"%s\",\"a\":\"%s\",\"o\":%d,\"r\":%u}",
                 i ? "," : "", zh, en, c->abbr, (int)c->off, c->region);
        if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(req, "],\"faces\":[") != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < APP_FACE_CUSTOM; i++) {
        const app_face_t *f = &p->faces[i];
        snprintf(buf, sizeof(buf),
                 "%s{\"city\":[%u,%u,%u,%u],\"elem\":["
                 "{\"x\":%d,\"y\":%d,\"s\":%u},"
                 "{\"x\":%d,\"y\":%d,\"s\":%u},"
                 "{\"x\":%d,\"y\":%d,\"s\":%u}]}",
                 i ? "," : "",
                 f->city[0], f->city[1], f->city[2], f->city[3],
                 (int)f->elem[0].x, (int)f->elem[0].y, f->elem[0].scale,
                 (int)f->elem[1].x, (int)f->elem[1].y, f->elem[1].scale,
                 (int)f->elem[2].x, (int)f->elem[2].y, f->elem[2].scale);
        if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    }
    const app_custom_t *cu = app_prefs_custom();
    snprintf(buf, sizeof(buf),
             "],\"custom\":{\"bg\":%u,\"canvas\":%u,\"comp\":[",
             cu->has_bg && app_bg_ok() ? 1u : 0u, (unsigned)cu->canvas);
    if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < APP_COMP_MAX; i++) {
        const app_comp_t *c = &cu->comp[i];
        snprintf(buf, sizeof(buf),
                 "%s{\"t\":%u,\"c\":%u,\"s\":%u,\"st\":%u,\"x\":%d,\"y\":%d,\"fg\":%u,\"ac\":%u,"
                 "\"fn\":%u,\"wt\":%u,\"sh\":%u,\"bo\":%u,\"rd\":%u}",
                 i ? "," : "", c->type, c->city, c->scale, c->style,
                 (int)c->x, (int)c->y, (unsigned)c->fg, (unsigned)c->acc,
                 c->font, c->weight, c->shadow, c->bg_opa, c->radius);
        if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(req, "]}}") != ESP_OK) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t recv_time(httpd_req_t *req)
{
    char raw[192];
    if (!recv_body(req, raw, sizeof(raw))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    int epoch = 0, off = 0;
    char tz[APP_TZ_NAME];
    tz[0] = 0;
    json_int(raw, "epoch", &epoch);
    json_int(raw, "off", &off);
    json_str(raw, "tz", tz, sizeof(tz));
    if (epoch > 0) app_time_set_utc((int64_t)epoch, (int16_t)off, tz);
    app_dial_request_reload();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static void parse_elem(const char *s, const char *key, app_elem_t *e)
{
    const char *p = strstr(s, key);
    if (!p) return;
    int x = e->x, y = e->y, sc = e->scale;
    json_int(p, "x", &x);
    json_int(p, "y", &y);
    json_int(p, "s", &sc);
    e->x = (int16_t)x;
    e->y = (int16_t)y;
    e->scale = (uint8_t)sc;
    if (e->scale < 50) e->scale = 50;
    if (e->scale > 200) e->scale = 200;
    if (e->x < 8) e->x = 8;
    if (e->x > 232) e->x = 232;
    if (e->y < 8) e->y = 8;
    if (e->y > 312) e->y = 312;
}

static esp_err_t recv_face(httpd_req_t *req)
{
    char raw[480];
    if (!recv_body(req, raw, sizeof(raw))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    int idx = 0;
    if (!json_int(raw, "i", &idx) || idx < 0 || idx >= APP_FACE_CUSTOM) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "i");
        return ESP_FAIL;
    }
    app_prefs_t *p = app_prefs();
    app_face_t *f = &p->faces[idx];
    const char *c = strstr(raw, "\"city\"");
    if (c) {
        int vals[4] = { f->city[0], f->city[1], f->city[2], f->city[3] };
        const char *q = strchr(c, '[');
        if (q) {
            for (int i = 0; i < 4; i++) {
                char *end = NULL;
                long v = strtol(q + 1, &end, 10);
                if (end == q + 1) break;
                if (v < 0) v = 0;
                if (v != APP_CITY_OFF && v >= APP_CITY_N) v = 0;
                vals[i] = (int)v;
                q = end;
                if (*q == ',') continue;
                break;
            }
        }
        for (int i = 0; i < 4; i++) f->city[i] = (uint8_t)vals[i];
    }
    parse_elem(raw, "\"elem\"", &f->elem[0]);
    const char *e1 = strstr(raw, "\"elem\"");
    if (e1) {
        const char *e2 = strstr(e1 + 6, "{\"x\"");
        if (e2) {
            parse_elem(e2, "{", &f->elem[0]);
            const char *e3 = strstr(e2 + 2, "{\"x\"");
            if (e3) {
                parse_elem(e3, "{", &f->elem[1]);
                const char *e4 = strstr(e3 + 2, "{\"x\"");
                if (e4) parse_elem(e4, "{", &f->elem[2]);
            }
        }
    }
    p->face = (uint8_t)idx;
    app_prefs_save();
    app_dial_request_reload();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t recv_active(httpd_req_t *req)
{
    char raw[64];
    if (!recv_body(req, raw, sizeof(raw))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    int idx = 0;
    if (json_int(raw, "i", &idx) && idx >= 0 && idx < APP_FACE_N) {
        app_prefs()->face = (uint8_t)idx;
        app_prefs_save();
        app_dial_request_reload();
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static void clamp_comp(app_comp_t *c)
{
    if (c->type > APP_COMP_XL) c->type = APP_COMP_NONE;
    if (c->scale < 50) c->scale = 50;
    if (c->scale > 200) c->scale = 200;
    if (c->type == APP_COMP_ANALOG) {
        if (c->font > APP_ANA_MAX) c->font = APP_ANA_CLASSIC;
    } else if (c->font > APP_FONT_LAT20) {
        c->font = APP_FONT_AUTO;
    }
    if (c->weight > 1) c->weight = 1;
    if (c->shadow > 3) c->shadow = 3;
    if (c->radius > 20) c->radius = 20;
    if (c->x < 8) c->x = 8;
    if (c->x > 232) c->x = 232;
    if (c->y < 8) c->y = 8;
    if (c->y > 312) c->y = 312;
    if (c->city >= APP_CITY_N && c->city != APP_CITY_OFF) c->city = 0;
}

static esp_err_t recv_custom(httpd_req_t *req)
{
    char raw[2048];
    if (!recv_body(req, raw, sizeof(raw))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    app_custom_t *cu = app_prefs_custom();
    int canvas = (int)cu->canvas;
    json_int(raw, "canvas", &canvas);
    cu->canvas = (uint32_t)canvas;
    const char *p = strstr(raw, "\"comp\"");
    int n = 0;
    if (p) p = strchr(p, '[');
    while (p && n < APP_COMP_MAX) {
        const char *q = strstr(p, "{\"t\"");
        if (!q) break;
        app_comp_t *c = &cu->comp[n];
        int t = 0, city = 0, sc = 100, st = 0, x = 120, y = 160, fg = 0xF5F5F7, ac = 0x1C1C1E;
        int fn = 0, wt = 0, sh = 0, bo = 0, rd = 8;
        json_int(q, "t", &t);
        json_int(q, "c", &city);
        json_int(q, "s", &sc);
        json_int(q, "st", &st);
        json_int(q, "x", &x);
        json_int(q, "y", &y);
        json_int(q, "fg", &fg);
        json_int(q, "ac", &ac);
        json_int(q, "fn", &fn);
        json_int(q, "wt", &wt);
        json_int(q, "sh", &sh);
        json_int(q, "bo", &bo);
        json_int(q, "rd", &rd);
        c->type = (uint8_t)t;
        c->city = (uint8_t)city;
        c->scale = (uint8_t)sc;
        c->style = (uint16_t)st;
        c->x = (int16_t)x;
        c->y = (int16_t)y;
        c->fg = (uint32_t)fg;
        c->acc = (uint32_t)ac;
        c->font = (uint8_t)fn;
        c->weight = (uint8_t)wt;
        c->shadow = (uint8_t)sh;
        c->bg_opa = (uint8_t)bo;
        c->radius = (uint8_t)rd;
        clamp_comp(c);
        n++;
        p = q + 4;
    }
    for (int i = n; i < APP_COMP_MAX; i++) {
        memset(&cu->comp[i], 0, sizeof(cu->comp[i]));
    }
    cu->n = (uint8_t)n;
    cu->has_bg = app_bg_ok() ? 1 : 0;
    app_prefs()->face = APP_FACE_CUSTOM;
    app_prefs_save();
    app_dial_request_reload();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t send_bg(httpd_req_t *req)
{
    const uint8_t *p = app_bg_blob();
    size_t n = app_bg_blob_len();
    if (!p || !n) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)p, (ssize_t)n);
}

static esp_err_t recv_bg(httpd_req_t *req)
{
    static const char MAGIC_RGB[8] = { 'D', 'I', 'A', 'L', 'B', 'G', '1', '6' };
    static const char MAGIC_GIF[8] = { 'D', 'I', 'A', 'L', 'B', 'G', 'I', 'F' };
    int want = req->content_len;
    if (want < 8 || want > (int)app_bg_max()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "size");
        return ESP_FAIL;
    }
    uint8_t head[8];
    int rh = httpd_req_recv(req, (char *)head, 8);
    if (rh != 8) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        return ESP_FAIL;
    }
    bool magic = memcmp(head, MAGIC_GIF, 8) == 0 || memcmp(head, MAGIC_RGB, 8) == 0;
    if (!magic && want != (int)APP_BG_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "size");
        return ESP_FAIL;
    }
    if (!app_bg_begin()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "erase");
        return ESP_FAIL;
    }
    if (!magic && !app_bg_feed(MAGIC_RGB, 8)) {
        app_bg_clear();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wr");
        return ESP_FAIL;
    }
    if (!app_bg_feed(head, 8)) {
        app_bg_clear();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wr");
        return ESP_FAIL;
    }
    uint8_t chunk[1024];
    int got = 8;
    while (got < want) {
        int n = want - got;
        if (n > (int)sizeof(chunk)) n = (int)sizeof(chunk);
        int r = httpd_req_recv(req, (char *)chunk, (size_t)n);
        if (r <= 0) {
            app_bg_clear();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        if (!app_bg_feed(chunk, (size_t)r)) {
            app_bg_clear();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wr");
            return ESP_FAIL;
        }
        got += r;
    }
    if (!app_bg_finish()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fin");
        return ESP_FAIL;
    }
    app_prefs_custom()->has_bg = 1;
    app_prefs()->face = APP_FACE_CUSTOM;
    app_prefs_save();
    app_dial_request_reload();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t clear_bg(httpd_req_t *req)
{
    app_bg_clear();
    app_prefs_custom()->has_bg = 0;
    app_prefs_save();
    app_dial_request_reload();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static uint32_t captive_ip(void)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip;
    if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr) {
        return ip.ip.addr;
    }
    return ESP_IP4TOADDR(192, 168, 4, 1);
}

static int dns_build(const uint8_t *req, int n, uint8_t *out, int max)
{
    if (n < 12 || n + 16 > max) return -1;
    memcpy(out, req, (size_t)n);
    out[2] |= 0x80;
    int i = 12;
    while (i < n) {
        uint8_t lab = out[i];
        if (lab == 0) {
            i++;
            break;
        }
        if (lab >= 0xC0) {
            i += 2;
            break;
        }
        i += lab + 1;
    }
    uint16_t typ = 0;
    if (i + 2 <= n) typ = ((uint16_t)out[i] << 8) | out[i + 1];
    out[6] = 0;
    out[7] = 0;
    if (typ != 1) return n;
    out[7] = 1;
    uint8_t *a = out + n;
    a[0] = 0xC0;
    a[1] = 0x0C;
    a[2] = 0;
    a[3] = 1;
    a[4] = 0;
    a[5] = 1;
    a[6] = 0;
    a[7] = 0;
    a[8] = 1;
    a[9] = 0x2C;
    a[10] = 0;
    a[11] = 4;
    uint32_t ip = captive_ip();
    memcpy(a + 12, &ip, 4);
    return n + 16;
}

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t rx[256];
    uint8_t tx[272];
    while (s_dns_on) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(53),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        s_dns_sock = sock;
        while (s_dns_on) {
            struct sockaddr_in src;
            socklen_t sl = sizeof(src);
            int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&src, &sl);
            if (n < 0) break;
            int m = dns_build(rx, n, tx, (int)sizeof(tx));
            if (m > 0) sendto(sock, tx, m, 0, (struct sockaddr *)&src, sl);
        }
        s_dns_sock = -1;
        close(sock);
    }
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void dns_stop(void)
{
    s_dns_on = false;
    if (s_dns_sock >= 0) shutdown(s_dns_sock, SHUT_RDWR);
}

static void dns_start(void)
{
    if (!bsp_wifi_ap_active() || s_dns_task) {
        if (s_dns_task) s_dns_on = true;
        return;
    }
    s_dns_on = true;
    if (xTaskCreate(dns_task, "web_dns", 2560, NULL, 5, &s_dns_task) != pdPASS) {
        s_dns_task = NULL;
        s_dns_on = false;
    }
}

static esp_err_t captive_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "200 OK");
    return send_html(req);
}

static void server_stop(void);

static void server_start(void)
{
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = APP_WEB_HTTP_PORT;
    cfg.ctrl_port = 32768;
    cfg.max_open_sockets = 7;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        s_httpd = NULL;
        ESP_LOGE(TAG, "httpd");
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = send_html },
        { .uri = "/s", .method = HTTP_GET, .handler = send_status },
        { .uri = "/t", .method = HTTP_POST, .handler = recv_time },
        { .uri = "/f", .method = HTTP_POST, .handler = recv_face },
        { .uri = "/a", .method = HTTP_POST, .handler = recv_active },
        { .uri = "/c", .method = HTTP_POST, .handler = recv_custom },
        { .uri = "/bg", .method = HTTP_GET, .handler = send_bg },
        { .uri = "/bg", .method = HTTP_POST, .handler = recv_bg },
        { .uri = "/bg", .method = HTTP_DELETE, .handler = clear_bg },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, captive_404);
    if (bsp_wifi_ap_active()) dns_start();
    bsp_wifi_ps_hold();
    ESP_LOGI(TAG, "http://192.168.4.1/");
}

static void server_stop(void)
{
    dns_stop();
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        bsp_wifi_ps_release();
    }
}

static void qr_paint(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN || !s_qr_ok) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    int bw = lv_area_get_width(&box);
    int bh = lv_area_get_height(&box);
    int n = s_qrcode.size;
    if (n <= 0) return;
    int cell = (bw < bh ? bw : bh) / n;
    if (cell < 1) cell = 1;
    int ox = box.x1 + (bw - cell * n) / 2;
    int oy = box.y1 + (bh - cell * n) / 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(0x111111);
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (!qrcode_getModule(&s_qrcode, x, y)) continue;
            lv_area_t a = {
                ox + x * cell, oy + y * cell,
                ox + (x + 1) * cell - 1, oy + (y + 1) * cell - 1,
            };
            lv_draw_rect(layer, &d, &a);
        }
    }
}

static void qr_refresh(void)
{
    if (!s_url) return;
    char url[36];
    if (!app_web_url(url, sizeof(url)) || !s_httpd) {
        if (s_hint) lv_label_set_text(s_hint, "正在开启热点…");
        lv_label_set_text(s_url, "");
        if (s_ssid) lv_label_set_text(s_ssid, "");
        return;
    }
    const char *ap = bsp_wifi_ap_ssid();
    if (s_ssid) lv_label_set_text(s_ssid, ap && ap[0] ? ap : "Passport");
    lv_label_set_text(s_url, url);
    if (s_hint) lv_label_set_text(s_hint, "连接热点后打开页面 · 长按关闭");
    if (!s_qr_ok || strcmp(s_qr_text, url) != 0) {
        strlcpy(s_qr_text, url, sizeof(s_qr_text));
        memset(s_qr_mod, 0, sizeof(s_qr_mod));
        s_qr_ok = qrcode_initText(&s_qrcode, s_qr_mod, 3, ECC_MEDIUM, url) >= 0;
    }
    if (s_qr) lv_obj_invalidate(s_qr);
}

static void ui_ensure(void)
{
    if (s_box || !s_screen) return;
    s_box = lv_obj_create(s_screen);
    ui_pixel_strip_theme(s_box);
    lv_obj_set_pos(s_box, 0, 0);
    lv_obj_set_size(s_box, 240, 320);
    lv_obj_set_style_bg_color(s_box, lv_color_hex(0x101218), 0);
    lv_obj_set_style_pad_all(s_box, 14, 0);

    s_ssid = lv_label_create(s_box);
    lv_obj_set_style_bg_opa(s_ssid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(s_ssid, ui_pixel_font_20(), 0);
    lv_obj_set_style_text_color(s_ssid, lv_color_hex(0xE8E6DF), 0);
    lv_obj_set_style_text_align(s_ssid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ssid, 212);
    lv_obj_align(s_ssid, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(s_ssid, "");

    s_hint = lv_label_create(s_box);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(s_hint, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x8B8A84), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint, 212);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text(s_hint, "正在开启热点…");

    s_qr = lv_obj_create(s_box);
    ui_pixel_strip_theme(s_qr);
    lv_obj_set_size(s_qr, 148, 148);
    lv_obj_set_style_bg_color(s_qr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(s_qr, 8, 0);
    lv_obj_align(s_qr, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_event_cb(s_qr, qr_paint, LV_EVENT_DRAW_MAIN, NULL);

    s_url = lv_label_create(s_box);
    lv_obj_set_style_bg_opa(s_url, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(s_url, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(s_url, lv_color_hex(0xC9A35A), 0);
    lv_obj_set_style_text_align(s_url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_url, 212);
    lv_obj_align(s_url, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_label_set_text(s_url, "");
}

static void ap_task(void *arg)
{
    (void)arg;
    if (s_want) {
        if (bsp_wifi_init() == ESP_OK) bsp_wifi_ap_start();
        if (s_want) {
            bsp_wifi_radio_resume();
            server_start();
        }
    }
    s_ap_task = NULL;
    vTaskDelete(NULL);
}

static void ap_kick(void)
{
    if (s_ap_task) return;
    if (bsp_wifi_ap_active() && s_httpd) {
        dns_start();
        return;
    }
    xTaskCreate(ap_task, "web_ap", 4096, NULL, 4, &s_ap_task);
}

bool app_web_url(char *buf, size_t n)
{
    if (!buf || n < 8) return false;
    char ip[20];
    if (bsp_wifi_ap_ip(ip, sizeof(ip)) != ESP_OK || !ip[0]) {
        strcpy(ip, "192.168.4.1");
    }
    snprintf(buf, n, "http://%s/", ip);
    return true;
}

void app_web_open(void)
{
    s_want = true;
    ui_ensure();
    if (s_box) lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    qr_refresh();
    ap_kick();
}

void app_web_close(void)
{
    s_want = false;
    if (s_box) lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    server_stop();
    if (bsp_wifi_ap_active()) bsp_wifi_ap_stop();
}

bool app_web_visible(void)
{
    return s_box && !lv_obj_has_flag(s_box, LV_OBJ_FLAG_HIDDEN);
}

bool app_web_keep_awake(void)
{
    return app_web_visible();
}

bool app_web_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!app_web_visible()) return false;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        app_web_close();
        return true;
    }
    return true;
}

void app_web_poll(void)
{
    if (!app_web_visible()) return;
    qr_refresh();
}

void app_web_init(lv_obj_t *screen)
{
    s_screen = screen;
}
