#include "app.h"
#include "app_i18n.h"
#include "app_logic.h"
#include "app_ui.h"
#include "ui_pixel.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WIN 11
#define SEP "--//----------------"

static app_dlog_t s_dlog;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev;
static bool s_hooked;

static lv_obj_t *s_title, *s_hint, *s_body;
static lv_timer_t *s_timer;
static int s_top;
static bool s_follow;
static bool s_clear_ask;

static int hook(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int n = s_prev ? s_prev(fmt, ap2) : vprintf(fmt, ap2);
    va_end(ap2);

    char tmp[160];
    int m = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (m < 0) return n;
    if (m >= (int)sizeof(tmp)) m = (int)sizeof(tmp) - 1;

    portENTER_CRITICAL(&s_mux);
    app_dlog_feed(&s_dlog, tmp, m);
    if (m >= (int)sizeof(tmp) - 1) app_dlog_flush(&s_dlog);
    portEXIT_CRITICAL(&s_mux);
    return n;
}

void app_logs_start(void)
{
    if (s_hooked) return;
    app_dlog_init(&s_dlog);
    s_prev = esp_log_set_vprintf(hook);
    s_hooked = true;
}

static int window_last(int start, int n, const uint8_t *cont)
{
    int vis = 0;
    int last = start - 1;
    for (int i = start; i < n && vis < WIN; i++) {
        if (i > start && !cont[i]) {
            if (vis + 1 >= WIN) break;
            vis++;
        }
        vis++;
        last = i;
    }
    return last;
}

static void paint(void)
{
    if (s_title) lv_label_set_text(s_title, app_str(APP_STR_LOG));
    if (!s_hint || !s_body) return;

    char lines[WIN][APP_DLOG_W];
    uint8_t cont[APP_DLOG_N];
    int n;
    int top;

    portENTER_CRITICAL(&s_mux);
    n = app_dlog_count(&s_dlog);
    memset(cont, 0, sizeof(cont));
    for (int i = 0; i < n; i++) {
        cont[i] = app_dlog_cont(&s_dlog, i) ? 1 : 0;
    }
    int follow_top = 0;
    for (int s = 0; s < n; s++) {
        if (window_last(s, n, cont) >= n - 1) follow_top = s;
    }
    if (s_follow || s_top > follow_top) s_top = follow_top;
    if (s_top < 0) s_top = 0;
    top = s_top;
    int shown = 0;
    int vis = 0;
    for (int i = top; i < n && vis < WIN; i++) {
        if (i > top && !cont[i]) {
            if (vis + 1 >= WIN) break;
            vis++;
        }
        vis++;
        shown++;
    }
    for (int i = 0; i < shown; i++) {
        app_dlog_copy(&s_dlog, top + i, lines[i], sizeof(lines[i]));
    }
    portEXIT_CRITICAL(&s_mux);

    lv_label_set_text(s_hint, n == 0 ? app_str(APP_STR_LOG_NONE)
                                     : (s_clear_ask ? app_str(APP_STR_HINT_CLEAR)
                                                    : app_str(APP_STR_LOG_HINT)));

    char buf[WIN * (APP_DLOG_W + sizeof(SEP)) + 8];
    size_t o = 0;
    buf[0] = 0;
    vis = 0;
    for (int i = 0; i < shown && vis < WIN; i++) {
        if (i > 0 && !cont[top + i]) {
            if (vis + 1 >= WIN) break;
            int w = snprintf(buf + o, sizeof(buf) - o, "%s\n", SEP);
            if (w < 0) break;
            o += (size_t)w;
            vis++;
            if (o >= sizeof(buf)) break;
        }
        int w = snprintf(buf + o, sizeof(buf) - o, "%s\n", lines[i]);
        if (w < 0) break;
        o += (size_t)w;
        vis++;
        if (o >= sizeof(buf)) break;
    }
    lv_label_set_text(s_body, buf);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    paint();
}

void app_logs_enter(lv_obj_t *p)
{
    s_follow = true;
    s_top = 0;
    s_clear_ask = false;
    lv_obj_t *card = app_ui_card(p);
    s_title = app_ui_title(card, app_str(APP_STR_LOG));
    s_hint = app_ui_hint(card);
    s_body = app_ui_body(card, 48);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(UI_TEXT), 0);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_CLIP);

    s_timer = lv_timer_create(tick, 400, NULL);
    paint();
}

void app_logs_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_title = s_hint = s_body = NULL;
}

void app_logs_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_OK) {
        if (!s_clear_ask) {
            s_clear_ask = true;
            paint();
            return;
        }
        portENTER_CRITICAL(&s_mux);
        app_dlog_clear(&s_dlog);
        portEXIT_CRITICAL(&s_mux);
        s_follow = true;
        s_top = 0;
        s_clear_ask = false;
        paint();
        return;
    }
    s_clear_ask = false;

    portENTER_CRITICAL(&s_mux);
    int n = app_dlog_count(&s_dlog);
    uint8_t cont[APP_DLOG_N];
    memset(cont, 0, sizeof(cont));
    for (int i = 0; i < n; i++) {
        cont[i] = app_dlog_cont(&s_dlog, i) ? 1 : 0;
    }
    int follow_top = 0;
    for (int s = 0; s < n; s++) {
        if (window_last(s, n, cont) >= n - 1) follow_top = s;
    }
    portEXIT_CRITICAL(&s_mux);

    if (btn == BSP_BTN_UP) {
        if (s_top > 0) {
            s_top--;
            s_follow = false;
        }
        paint();
    } else if (btn == BSP_BTN_DOWN) {
        if (s_top < follow_top) s_top++;
        if (s_top >= follow_top) s_follow = true;
        paint();
    }
}
