#include "app_farm_web.h"

#include "app_i18n.h"
#include "bsp_wifi.h"
#include "qrcode.h"
#include "ui_pixel.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "farm_web";

#define PORT 8080
#define TEXT_MAX 160

static const char PAGE[] =
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Farm</title>"
    "<style>"
    "body{margin:0;background:#FFF7EA;font-family:sans-serif;color:#5B4636}"
    "main{max-width:480px;margin:24px auto;background:#fff;"
    "padding:16px;border:4px solid #5B4636}"
    "h1{margin:0 0 8px;font-size:22px}"
    "p{margin:0 0 12px;color:#8A7460;font-size:14px}"
    "textarea{width:100%;min-height:140px;font-size:20px;padding:10px;"
    "border:3px solid #5B4636;box-sizing:border-box}"
    "button{width:100%;margin-top:12px;padding:14px;font-size:18px;"
    "font-weight:700;background:#FF8C7A;border:3px solid #5B4636;color:#fff}"
    "#ok{min-height:1.4em;color:#55951D}"
    "</style>"
    "<main><h1>Farm</h1><p id=st></p>"
    "<form method=post action=/t>"
    "<textarea name=t id=t></textarea>"
    "<button type=submit>Send</button></form>"
    "<p id=ok></p></main>"
    "<script>"
    "let T={ok:'Sent',fail:'Failed'};"
    "const f=document.querySelector('form');"
    "f.onsubmit=async e=>{"
    "e.preventDefault();"
    "const t=document.getElementById('t').value;"
    "try{"
    "const r=await fetch('/t',{method:'POST',"
    "headers:{'Content-Type':'text/plain;charset=utf-8'},body:t});"
    "document.getElementById('ok').textContent=r.ok?T.ok:T.fail;"
    "}catch(err){document.getElementById('ok').textContent=T.fail;}"
    "};"
    "async function st(){try{"
    "const j=await(await fetch('/s')).json();"
    "document.documentElement.lang=j.lang||'en';"
    "T.ok=j.ok;T.fail=j.fail;"
    "document.querySelector('button').textContent=j.send;"
    "document.getElementById('t').placeholder=j.ph;"
    "document.getElementById('st').textContent=j.field?"
    "j.busy.replace('%s',j.field):j.idle;"
    "}catch(e){}}"
    "st();setInterval(st,2000);"
    "</script>";

static httpd_handle_t s_httpd;
static SemaphoreHandle_t s_mu;
static uint32_t s_retry_at;

static char s_field[16];
static char *s_buf;
static size_t s_cap;
static void (*s_refresh)(void);

static char s_pending[TEXT_MAX + 1];
static bool s_have;
static bool s_shown;

static QRCode s_qr;
static uint8_t s_qr_mod[128];
static char s_qr_text[36];
static bool s_qr_ok;

static void mu_ensure(void)
{
    if (!s_mu) s_mu = xSemaphoreCreateMutex();
}

static bool make_url(char *buf, size_t n)
{
    char ip[20];

    if (!buf || n < 32) return false;
    buf[0] = 0;
    if (bsp_wifi_state() != BSP_WIFI_CONNECTED) return false;
    if (bsp_wifi_ip(ip, sizeof(ip)) != ESP_OK) return false;
    if (!ip[0] || strcmp(ip, "0.0.0.0") == 0) return false;
    snprintf(buf, n, "http://%s:%d/", ip, PORT);
    return true;
}

static const char *field_lab(void)
{
    if (strcmp(s_field, "name") == 0) return app_str(APP_STR_MEOW_NAME);
    if (strcmp(s_field, "host") == 0) return app_str(APP_STR_FARM_HOST);
    return s_field[0] ? s_field : app_str(APP_STR_WEB_FIELD);
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s)
{
    char *d = s;

    for (; *s; s++) {
        if (*s == '+') {
            *d++ = ' ';
            continue;
        }
        if (*s == '%' && s[1] && s[2]) {
            int h = hex(s[1]), l = hex(s[2]);
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

static void trim_ws(char *s)
{
    char *e = s + strlen(s);
    char *p;

    while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
    p = s;
    while (*p == ' ' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void store_text(const char *src)
{
    char tmp[TEXT_MAX * 3 + 8];

    if (!src) return;
    ui_pixel_utf8_copy(tmp, sizeof(tmp), src);
    trim_ws(tmp);
    if (!tmp[0]) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    ui_pixel_utf8_copy(s_pending, sizeof(s_pending), tmp);
    s_have = true;
    xSemaphoreGive(s_mu);
}

static void qr_refresh(void)
{
    char url[36];

    if (!make_url(url, sizeof(url))) {
        s_qr_ok = false;
        s_qr_text[0] = 0;
        return;
    }
    if (s_qr_ok && strcmp(s_qr_text, url) == 0) return;
    strlcpy(s_qr_text, url, sizeof(s_qr_text));
    memset(s_qr_mod, 0, sizeof(s_qr_mod));
    s_qr_ok = qrcode_initText(&s_qr, s_qr_mod, 3, ECC_MEDIUM, url) >= 0;
}

static esp_err_t send_html(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_status(httpd_req_t *req)
{
    char json[768];
    const char *f;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    f = (s_buf && s_field[0]) ? field_lab() : "";
    snprintf(json, sizeof(json),
             "{\"lang\":\"%s\",\"field\":\"%s\",\"idle\":\"%s\","
             "\"busy\":\"%s\",\"send\":\"%s\",\"ph\":\"%s\","
             "\"ok\":\"%s\",\"fail\":\"%s\"}",
             app_lang_html(), f,
             app_str(APP_STR_WEB_NO_PAGE),
             app_str(APP_STR_WEB_BUSY),
             app_str(APP_STR_WEB_SEND),
             app_str(APP_STR_WEB_PLACEHOLDER),
             app_str(APP_STR_WEB_SENT),
             app_str(APP_STR_WEB_FAIL));
    xSemaphoreGive(s_mu);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t recv_text(httpd_req_t *req)
{
    int total = req->content_len;
    char raw[513];
    char ctype[64] = { 0 };
    char *text;
    int got = 0;

    if (total < 0 || total > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too long");
        return ESP_FAIL;
    }
    while (got < total) {
        int n = httpd_req_recv(req, raw + got, (size_t)(total - got));
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        got += n;
    }
    raw[got] = 0;
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    text = raw;
    if (strstr(ctype, "application/x-www-form-urlencoded")) {
        url_decode(raw);
        if (!strncmp(raw, "t=", 2)) text = raw + 2;
        char *amp = strchr(text, '&');
        if (amp) *amp = 0;
    }
    store_text(text);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static const httpd_uri_t URI_ROOT = {
    .uri = "/", .method = HTTP_GET, .handler = send_html,
};
static const httpd_uri_t URI_STATUS = {
    .uri = "/s", .method = HTTP_GET, .handler = send_status,
};
static const httpd_uri_t URI_POST = {
    .uri = "/t", .method = HTTP_POST, .handler = recv_text,
};
static const httpd_uri_t URI_FORM = {
    .uri = "/", .method = HTTP_POST, .handler = recv_text,
};

static void server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    esp_err_t e;
    uint32_t now = xTaskGetTickCount();
    char ip[20] = { 0 };

    if (s_httpd) return;
    if (s_retry_at && now < s_retry_at) return;
    cfg.server_port = PORT;
    cfg.ctrl_port = 32768;
    cfg.max_open_sockets = 2;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 4096;
    cfg.max_uri_handlers = 8;
    e = httpd_start(&s_httpd, &cfg);
    if (e != ESP_OK) {
        s_httpd = NULL;
        s_retry_at = now + pdMS_TO_TICKS(8000);
        ESP_LOGE(TAG, "httpd %s", esp_err_to_name(e));
        return;
    }
    s_retry_at = 0;
    httpd_register_uri_handler(s_httpd, &URI_ROOT);
    httpd_register_uri_handler(s_httpd, &URI_STATUS);
    httpd_register_uri_handler(s_httpd, &URI_POST);
    httpd_register_uri_handler(s_httpd, &URI_FORM);
    bsp_wifi_ps_hold();
    bsp_wifi_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "http://%s:%d/", ip[0] ? ip : "0.0.0.0", PORT);
}

static void rrect(lv_layer_t *layer, int x, int y, int w, int h, int r, uint32_t c)
{
    lv_draw_rect_dsc_t d;
    lv_area_t a;

    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(c);
    d.bg_opa = LV_OPA_COVER;
    d.radius = r;
    d.border_width = 0;
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + h - 1;
    lv_draw_rect(layer, &d, &a);
}

static void draw_txt(lv_layer_t *layer, int x, int y, int w, int h,
                    const char *s, uint32_t c)
{
    lv_draw_label_dsc_t d;
    lv_area_t a;

    if (!s || !s[0] || w < 2 || h < 2) return;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = ui_pixel_font_14();
    d.color = lv_color_hex(c);
    d.opa = LV_OPA_COVER;
    d.align = LV_TEXT_ALIGN_CENTER;
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + h - 1;
    lv_draw_label(layer, &d, &a);
}

void app_farm_web_open(char *buf, size_t cap, const char *field,
                       void (*refresh)(void))
{
    mu_ensure();
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_buf = buf;
    s_cap = cap;
    s_refresh = refresh;
    s_field[0] = 0;
    if (field) {
        size_t i = 0;
        for (; field[i] && i + 1 < sizeof(s_field); i++) {
            char c = field[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) s_field[i] = c;
            else break;
        }
        s_field[i] = 0;
    }
    s_have = false;
    s_pending[0] = 0;
    xSemaphoreGive(s_mu);
    s_shown = true;
    qr_refresh();
    if (bsp_wifi_state() == BSP_WIFI_CONNECTED) server_start();
}

void app_farm_web_close(void)
{
    s_shown = false;
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_buf = NULL;
    s_cap = 0;
    s_refresh = NULL;
    s_field[0] = 0;
    s_have = false;
    s_pending[0] = 0;
    xSemaphoreGive(s_mu);
}

bool app_farm_web_shown(void)
{
    return s_shown;
}

bool app_farm_web_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_shown) return false;
    if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
        app_farm_web_close();
        return true;
    }
    return true;
}

void app_farm_web_poll(void)
{
    char next[TEXT_MAX + 1];
    char *buf;
    size_t cap;
    void (*refresh)(void) = NULL;
    bool have;

    if (s_shown) qr_refresh();
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    have = s_have && s_pending[0];
    next[0] = 0;
    if (have) strlcpy(next, s_pending, sizeof(next));
    buf = s_buf;
    cap = s_cap;
    refresh = s_refresh;
    if (have) {
        s_have = false;
        s_pending[0] = 0;
    }
    xSemaphoreGive(s_mu);
    if (!have || !buf || !cap) return;
    ui_pixel_utf8_copy(buf, cap, next);
    s_shown = false;
    if (refresh) refresh();
}

void app_farm_web_draw(lv_layer_t *layer, int bx, int by)
{
    const char *url = s_qr_ok ? s_qr_text : app_str(APP_STR_QR_NEED);
    int n, inner, scale, ox, oy, y, x;
    lv_draw_rect_dsc_t dsc;

    rrect(layer, bx, by, 240, 320, 0, 0xFFF7EA);
    draw_txt(layer, bx + 16, by + 12, 208, 18, app_str(APP_STR_QR_TITLE), 0x5B4636);
    rrect(layer, bx + 52, by + 40, 136, 136, 8, 0xFFFFFF);
    if (s_qr_ok) {
        n = s_qr.size;
        inner = n + 4;
        scale = 136 / inner;
        if (scale < 1) scale = 1;
        ox = bx + 52 + (136 - inner * scale) / 2;
        oy = by + 40 + (136 - inner * scale) / 2;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(0x5B4636);
        dsc.bg_opa = LV_OPA_COVER;
        dsc.radius = 0;
        dsc.border_width = 0;
        for (y = 0; y < n; y++) {
            x = 0;
            while (x < n) {
                if (!qrcode_getModule(&s_qr, (uint8_t)x, (uint8_t)y)) {
                    x++;
                    continue;
                }
                int x0 = x;
                lv_area_t a;
                while (x < n && qrcode_getModule(&s_qr, (uint8_t)x, (uint8_t)y)) x++;
                a.x1 = ox + (x0 + 2) * scale;
                a.y1 = oy + (y + 2) * scale;
                a.x2 = ox + (x + 2) * scale - 1;
                a.y2 = a.y1 + scale - 1;
                lv_draw_rect(layer, &dsc, &a);
            }
        }
    }
    draw_txt(layer, bx + 8, by + 184, 224, 16, url, 0x8A7460);
    draw_txt(layer, bx + 8, by + 208, 224, 32, app_str(APP_STR_QR_HINT), 0x8A7460);
}
