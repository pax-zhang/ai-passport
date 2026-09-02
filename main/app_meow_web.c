#include "app_meow_web.h"

#include "app_i18n.h"
#include "app_prefs.h"
#include "app_tone.h"
#include "bsp_wifi.h"
#include "qrcode.h"
#include "ui_pixel.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "meow_web";

static const char PAGE[] =
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Meow</title>"
    "<style>"
    "body{margin:0;background:#FFF7EA;font-family:-apple-system,system-ui,sans-serif;color:#5B4636}"
    "main{max-width:480px;margin:24px auto;background:#FFFFFF;"
    "padding:20px;border-radius:16px}"
    "h1{margin:0 0 8px;font-size:22px;font-weight:600}"
    "p{margin:0 0 12px;color:#8A7460;font-size:15px}"
    "textarea{width:100%;min-height:140px;font-size:20px;padding:12px;"
    "border:0;background:#FFF3E0;border-radius:12px;box-sizing:border-box}"
    "button{width:100%;margin-top:12px;padding:14px;font-size:17px;"
    "font-weight:600;background:#FFC857;border:0;border-radius:12px}"
    "#ok{min-height:1.4em;color:#5C9A3A}"
    "</style>"
    "<main><h1>Meow</h1><p id=st></p>"
    "<form method=post action=/t>"
    "<textarea name=t id=t></textarea>"
    "<button type=submit>Send</button></form>"
    "<p id=ok></p></main>"
    "<script>"
    "let T={ok:'Sent',fail:'Failed'};"
    "const f=document.querySelector('form');"
    "const el=document.getElementById('t');"
    "let composing=false;"
    "el.addEventListener('compositionstart',()=>composing=true);"
    "el.addEventListener('compositionend',()=>composing=false);"
    "f.onsubmit=async e=>{"
    "e.preventDefault();"
    "if(composing){el.blur();"
    "await new Promise(r=>el.addEventListener('compositionend',r,{once:true}));}"
    "else el.blur();"
    "const t=el.value;"
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

static char s_pending[161];
static bool s_have;
static bool s_fresh;

static char *s_buf;
static size_t s_cap;
static void (*s_refresh)(void);

static lv_obj_t *s_qr_box, *s_qr_title, *s_qr_draw, *s_qr_url, *s_qr_hint;
static QRCode s_qr;
static uint8_t s_qr_mod[128];
static char s_qr_text[36];
static bool s_qr_ok;
static bool s_qr_shown;

static void mu_ensure(void)
{
    if (!s_mu) s_mu = xSemaphoreCreateMutex();
}

bool app_meow_web_url(char *buf, size_t n)
{
    if (!buf || n < 32) return false;
    buf[0] = 0;
    if (bsp_wifi_state() != BSP_WIFI_CONNECTED) return false;
    char ip[20];
    if (bsp_wifi_ip(ip, sizeof(ip)) != ESP_OK) return false;
    if (!ip[0] || strcmp(ip, "0.0.0.0") == 0) return false;
    snprintf(buf, n, "http://%s:%d/", ip, APP_MEOW_WEB_PORT);
    return true;
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
    while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
    char *p = s;
    while (*p == ' ' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void store_text(const char *src)
{
    if (!src) return;
    char tmp[161];
    ui_pixel_utf8_copy(tmp, sizeof(tmp), src);
    trim_ws(tmp);
    if (!tmp[0]) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    ui_pixel_utf8_copy(s_pending, sizeof(s_pending), tmp);
    s_have = true;
    s_fresh = true;
    xSemaphoreGive(s_mu);
}

static void qr_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    if (!s_qr_ok) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0x000000);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    dsc.outline_width = 0;
    dsc.shadow_width = 0;

    int n = s_qr.size;
    int quiet = 2;
    int inner = n + quiet * 2;
    int w = (int)lv_area_get_width(&coords);
    int h = (int)lv_area_get_height(&coords);
    int scale = (w < h ? w : h) / inner;
    if (scale < 1) return;
    int ox = coords.x1 + (w - inner * scale) / 2;
    int oy = coords.y1 + (h - inner * scale) / 2;

    for (int y = 0; y < n; y++) {
        int x = 0;
        while (x < n) {
            if (!qrcode_getModule(&s_qr, (uint8_t)x, (uint8_t)y)) {
                x++;
                continue;
            }
            int x0 = x;
            while (x < n && qrcode_getModule(&s_qr, (uint8_t)x, (uint8_t)y)) x++;
            lv_area_t a;
            a.x1 = ox + (x0 + quiet) * scale;
            a.y1 = oy + (y + quiet) * scale;
            a.x2 = ox + (x + quiet) * scale - 1;
            a.y2 = a.y1 + scale - 1;
            lv_draw_rect(layer, &dsc, &a);
        }
    }
}

static void qr_refresh(void)
{
    if (!s_qr_title) return;
    lv_label_set_text(s_qr_title, app_str(APP_STR_QR_TITLE));
    char url[36];
    if (!app_meow_web_url(url, sizeof(url))) {
        s_qr_ok = false;
        s_qr_text[0] = 0;
        lv_label_set_text(s_qr_url, "");
        lv_label_set_text(s_qr_hint, app_str(APP_STR_QR_NEED));
        if (s_qr_draw) lv_obj_invalidate(s_qr_draw);
        return;
    }
    lv_label_set_text(s_qr_url, url);
    lv_label_set_text(s_qr_hint, app_str(APP_STR_QR_HINT));
    if (s_qr_ok && strcmp(s_qr_text, url) == 0) return;
    strlcpy(s_qr_text, url, sizeof(s_qr_text));
    memset(s_qr_mod, 0, sizeof(s_qr_mod));
    s_qr_ok = qrcode_initText(&s_qr, s_qr_mod, 3, ECC_MEDIUM, url) >= 0;
    if (s_qr_draw) lv_obj_invalidate(s_qr_draw);
}

static esp_err_t send_html(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_status(httpd_req_t *req)
{
    char json[512];
    xSemaphoreTake(s_mu, portMAX_DELAY);
    const char *f = (s_buf && s_cap) ? app_str(APP_STR_MEOW_NAME) : "";
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
    if (total < 0 || total > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too long");
        return ESP_FAIL;
    }
    char raw[513];
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, raw + got, (size_t)(total - got));
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        got += n;
    }
    raw[got] = 0;

    char ctype[64] = { 0 };
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    char *text = raw;
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
    if (s_httpd) return;
    uint32_t now = xTaskGetTickCount();
    if (s_retry_at && now < s_retry_at) return;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = APP_MEOW_WEB_PORT;
    cfg.ctrl_port = 32768;
    cfg.max_open_sockets = 2;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 4096;
    cfg.max_uri_handlers = 8;
    esp_err_t e = httpd_start(&s_httpd, &cfg);
    if (e != ESP_OK) {
        s_httpd = NULL;
        s_retry_at = now + pdMS_TO_TICKS(8000);
        ESP_LOGE(TAG, "httpd start %s", esp_err_to_name(e));
        return;
    }
    s_retry_at = 0;
    httpd_register_uri_handler(s_httpd, &URI_ROOT);
    httpd_register_uri_handler(s_httpd, &URI_STATUS);
    httpd_register_uri_handler(s_httpd, &URI_POST);
    httpd_register_uri_handler(s_httpd, &URI_FORM);
    bsp_wifi_ps_hold();
    char ip[20] = { 0 };
    bsp_wifi_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "http://%s:%d/", ip[0] ? ip : "0.0.0.0", APP_MEOW_WEB_PORT);
}

void app_meow_web_init(lv_obj_t *screen)
{
    mu_ensure();
    if (!screen || s_qr_box) return;

    s_qr_box = lv_obj_create(screen);
    ui_pixel_strip_theme(s_qr_box);
    lv_obj_set_pos(s_qr_box, 10, 16);
    lv_obj_set_size(s_qr_box, 220, 288);
    ui_pixel_card_style(s_qr_box, UI_PAPER, UI_LINE);
    lv_obj_set_style_pad_all(s_qr_box, 8, 0);

    s_qr_title = lv_label_create(s_qr_box);
    lv_obj_set_style_text_font(s_qr_title, ui_pixel_font_20(), 0);
    lv_obj_set_style_text_color(s_qr_title, lv_color_hex(UI_INK), 0);
    lv_obj_set_width(s_qr_title, 196);
    lv_label_set_long_mode(s_qr_title, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_qr_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_qr_draw = lv_obj_create(s_qr_box);
    ui_pixel_strip_theme(s_qr_draw);
    lv_obj_set_size(s_qr_draw, 136, 136);
    lv_obj_set_style_bg_opa(s_qr_draw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_qr_draw, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_qr_draw, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_add_event_cb(s_qr_draw, qr_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    s_qr_url = lv_label_create(s_qr_box);
    lv_obj_set_style_text_font(s_qr_url, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(s_qr_url, lv_color_hex(UI_INK), 0);
    lv_obj_set_width(s_qr_url, 196);
    lv_label_set_long_mode(s_qr_url, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_qr_url, LV_ALIGN_TOP_LEFT, 0, 176);

    s_qr_hint = lv_label_create(s_qr_box);
    lv_obj_set_style_text_font(s_qr_hint, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(s_qr_hint, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_set_width(s_qr_hint, 196);
    lv_label_set_long_mode(s_qr_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_qr_hint, LV_ALIGN_TOP_LEFT, 0, 204);

    lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
}

void app_meow_web_set_target(char *buf, size_t cap, void (*refresh)(void))
{
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_buf = buf;
    s_cap = cap;
    s_refresh = refresh;
    xSemaphoreGive(s_mu);
}

void app_meow_web_clear_target(void)
{
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_buf = NULL;
    s_cap = 0;
    s_refresh = NULL;
    xSemaphoreGive(s_mu);
}

void app_meow_web_qr_open(void)
{
    if (!s_qr_box) return;
    qr_refresh();
    s_qr_shown = true;
    lv_obj_move_foreground(s_qr_box);
    lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    if (bsp_wifi_state() == BSP_WIFI_CONNECTED) server_start();
}

void app_meow_web_qr_close(void)
{
    if (s_qr_box) lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    s_qr_shown = false;
}

bool app_meow_web_qr_visible(void)
{
    return s_qr_shown;
}

bool app_meow_web_qr_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_qr_shown) return false;
    if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
        app_meow_web_qr_close();
        return true;
    }
    return true;
}

void app_meow_web_poll(void)
{
    if (bsp_wifi_state() == BSP_WIFI_CONNECTED && (s_qr_shown || s_httpd)) {
        server_start();
    }
    if (s_qr_shown) qr_refresh();
    if (!s_mu) return;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    bool have = s_have && s_pending[0];
    bool target = s_buf && s_cap;
    bool fresh = s_fresh;
    char next[161];
    next[0] = 0;
    void (*refresh)(void) = s_refresh;
    char *buf = s_buf;
    size_t cap = s_cap;
    if (have && target) {
        strlcpy(next, s_pending, sizeof(next));
        s_have = false;
        s_pending[0] = 0;
        s_fresh = false;
    }
    xSemaphoreGive(s_mu);

    if (!have || !target) return;
    ui_pixel_utf8_copy(buf, cap, next);
    if (s_qr_shown) app_meow_web_qr_close();
    if (refresh) refresh();
    if (fresh) app_tone_play((int)app_prefs()->tone_msg);
}
