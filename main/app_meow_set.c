#include "app_meow_set.h"

#include "app.h"
#include "app_i18n.h"
#include "app_net.h"
#include "app_ota.h"
#include "app_prefs.h"
#include "app_time.h"
#include "app_tone.h"
#include "app_ui.h"
#include "app_web.h"
#include "app_wifi_flow.h"
#include "bsp_ble.h"
#include "bsp_display.h"
#include "bsp_wifi.h"
#include "ui_pixel.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIFI_ROWS 7

static const char *TAG = "meow_set";

typedef enum { WIFI_LIST = 0, WIFI_KB } wifi_view_t;

static lv_obj_t *s_box, *s_card, *s_title, *s_hint, *s_body;
static lv_obj_t *s_mini_qr, *s_mini_url;
static lv_obj_t *s_rows[8], *s_labs[8], *s_metas[8];
static lv_timer_t *s_timer;
static meow_set_id_t s_id;
static int s_sel;
static int s_y, s_mo, s_d, s_h, s_mi;
static bool s_adj;
static int s_confirm;

static wifi_view_t s_wifi_view;
static bsp_wifi_ap_t s_aps[BSP_WIFI_SCAN_MAX];
static int s_ap_n;
static char s_ssid[BSP_WIFI_SSID_MAX + 1];
static char s_pass[BSP_WIFI_PASS_MAX + 1];
static int s_kb_sel, s_kb_set;
static int s_hold_btn = -1;
static int s_hold_ms;
static lv_timer_t *s_hold_timer;
static lv_timer_t *s_ble_defer;
static int s_ble_kick;
static TaskHandle_t s_task;
static volatile int s_req;
static volatile bool s_scanning;
static volatile bool s_paint_req;
static volatile uint32_t s_wifi_epoch;

static bsp_ble_peer_t s_peers[BSP_BLE_PEER_MAX];
static int s_peer_n;
static int s_focus;
static uint32_t s_still_ms;

static const uint16_t SLEEP_OPT[] = { 0, 30, 60, 120, 300 };
#define SLEEP_N 5

static void paint(void);
static void ble_kick_adv(void);

static void move_sel(int *sel, int n, int d)
{
    app_ui_move(sel, n, d);
}

static int sleep_idx(uint16_t sec)
{
    for (int i = 0; i < SLEEP_N; i++) if (SLEEP_OPT[i] == sec) return i;
    return 2;
}

static const char *sleep_txt(uint16_t sec)
{
    if (sec == 0) return app_str(APP_STR_NEVER);
    static char buf[8];
    snprintf(buf, sizeof(buf), "%ds", (int)sec);
    return buf;
}

static int wifi_count(void)
{
    return app_wifi_item_count(s_ap_n, bsp_wifi_has_saved());
}

static void mask_pass(char *out, size_t n)
{
    size_t len = strlen(s_pass);
    if (len == 0) {
        snprintf(out, n, "%s", app_str(APP_STR_EMPTY));
        return;
    }
    size_t i = 0;
    for (; i + 1 < len && i + 1 < n; i++) out[i] = '*';
    if (i < n - 1) out[i++] = s_pass[len - 1];
    out[i] = 0;
}

static int clock_rows(void)
{
    return app_prefs()->ntp_on ? 2 : 8;
}

static const char *ota_err_str(void)
{
    switch (app_ota_err()) {
    case APP_OTA_E_WIFI: return app_str(APP_STR_MEOW_NEED_WIFI);
    case APP_OTA_E_LOWBAT: return app_str(APP_STR_OTA_LOWBAT);
    case APP_OTA_E_HASH: return app_str(APP_STR_OTA_HASH);
    case APP_OTA_E_CANCEL: return app_str(APP_STR_OTA_FAIL);
    case APP_OTA_E_PARSE:
    case APP_OTA_E_NET: return app_str(APP_STR_OTA_NET);
    default: return app_str(APP_STR_OTA_FAIL);
    }
}

static void paint_ota(char *out, size_t n)
{
    const char *latest = app_ota_new_ver();
    const char *stxt = "";

    if (!latest[0]) latest = app_str(APP_STR_EMPTY);
    switch (app_ota_state()) {
    case APP_OTA_CHECKING: stxt = app_str(APP_STR_OTA_CHECKING); break;
    case APP_OTA_LATEST: stxt = app_str(APP_STR_OTA_OK); break;
    case APP_OTA_AVAILABLE: stxt = app_str(APP_STR_OTA_READY); break;
    case APP_OTA_FAIL: stxt = ota_err_str(); break;
    case APP_OTA_APPLYING: stxt = app_str(APP_STR_OTA_HOLD); break;
    default: break;
    }
    if (app_ota_state() == APP_OTA_APPLYING) {
        snprintf(out, n, "%s\n%d%%\n", stxt, app_ota_progress());
        return;
    }
    snprintf(out, n,
             "%s %s  %s\n"
             "%s %s\n"
             "%s %s\n"
             "%s\n"
             "%s  %s\n"
             "%s  %s\n"
             "%s\n",
             s_sel == 0 ? ">" : " ", app_str(APP_STR_AUTO),
             app_str_onoff(app_prefs()->ota_auto),
             s_sel == 1 ? ">" : " ", app_str(APP_STR_OTA_CHECK),
             s_sel == 2 ? ">" : " ", app_str(APP_STR_OTA_INSTALL),
             app_ota_channel(),
             app_str(APP_STR_OTA_NOW), app_ota_cur_ver(),
             app_str(APP_STR_OTA_LATEST), latest,
             stxt);
}

static void ota_choose(void)
{
    if (s_sel == 0) {
        app_prefs_t *p = app_prefs();
        p->ota_auto = !p->ota_auto;
        app_prefs_save();
        return;
    }
    if (app_ota_busy()) return;
    if (s_sel == 1) app_ota_check();
    else if (s_sel == 2) app_ota_apply();
}

static void rows_clear(void)
{
    for (int i = 0; i < 8; i++) {
        if (s_rows[i]) lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void row_show(int slot, const char *label, const char *meta, bool selected)
{
    if (slot < 0 || slot >= 8) return;
    if (!s_rows[slot]) {
        s_rows[slot] = lv_obj_create(s_card);
        ui_pixel_strip_theme(s_rows[slot]);
        lv_obj_set_pos(s_rows[slot], 0, 40 + slot * 30);
        lv_obj_set_size(s_rows[slot], APP_CONTENT_W, 26);
        lv_obj_set_style_radius(s_rows[slot], UI_RADIUS_SM, 0);
        lv_obj_set_style_border_width(s_rows[slot], 0, 0);
        lv_obj_set_style_clip_corner(s_rows[slot], false, 0);
        lv_obj_set_style_bg_opa(s_rows[slot], LV_OPA_COVER, 0);
        s_labs[slot] = lv_label_create(s_rows[slot]);
        lv_obj_set_style_text_font(s_labs[slot], ui_pixel_font_cjk(), 0);
        lv_obj_set_style_text_color(s_labs[slot], lv_color_hex(UI_TEXT), 0);
        lv_label_set_long_mode(s_labs[slot], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_labs[slot], APP_CONTENT_W - 96);
        lv_obj_align(s_labs[slot], LV_ALIGN_LEFT_MID, 8, 0);
        s_metas[slot] = lv_label_create(s_rows[slot]);
        lv_obj_set_style_text_font(s_metas[slot], ui_pixel_font_cjk(), 0);
        lv_obj_set_style_text_color(s_metas[slot], lv_color_hex(UI_MUTE), 0);
        lv_label_set_long_mode(s_metas[slot], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_metas[slot], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(s_metas[slot], 76);
        lv_obj_align(s_metas[slot], LV_ALIGN_RIGHT_MID, -8, 0);
    }
    lv_obj_remove_flag(s_rows[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(s_rows[slot], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_rows[slot],
                              lv_color_hex(selected ? UI_FILL : UI_CARD), 0);
    lv_label_set_text(s_labs[slot], label ? label : "");
    lv_label_set_text(s_metas[slot], meta ? meta : "");
}

static bool paint_rows(void)
{
    rows_clear();
    if (s_wifi_view == WIFI_KB) return false;
    int slot = 0;
    char meta[48];

    if (s_confirm) {
        row_show(0, app_str(APP_STR_TOTP_CANCEL), "", s_sel == 0);
        row_show(1, s_confirm == 2 ? app_str(APP_STR_FORGET_SEL)
                                   : app_str(APP_STR_FORGET), "", s_sel == 1);
        return true;
    }

    if (s_id == MEOW_SET_WIFI) {
        int items = wifi_count();
        if (s_sel >= items) s_sel = items ? items - 1 : 0;
        app_list_t win = { .sel = s_sel, .n = items, .rows = WIFI_ROWS };
        app_list_reveal(&win);
        int start = win.top;
        for (int i = start; i < items && slot < WIFI_ROWS; i++, slot++) {
            if (i == 0) {
                row_show(slot, app_str(APP_STR_POWER),
                         app_str_onoff(bsp_wifi_enabled()), i == s_sel);
            } else if (i == 1) {
                row_show(slot, app_str(APP_STR_AUTO),
                         app_str_onoff(bsp_wifi_auto_connect()), i == s_sel);
            } else if (i < 2 + s_ap_n) {
                int a = i - 2;
                char ssid[40];
                ui_pixel_utf8_copy(ssid, sizeof(ssid), s_aps[a].ssid);
                bool on = (bsp_wifi_state() == BSP_WIFI_CONNECTED &&
                           strcmp(s_aps[a].ssid, bsp_wifi_ssid()) == 0);
                snprintf(meta, sizeof(meta), "%ddB%s", (int)s_aps[a].rssi,
                         on ? app_str(APP_STR_LINKED) : "");
                row_show(slot, ssid[0] ? ssid : "ssid", meta, i == s_sel);
            } else if (i == 2 + s_ap_n) {
                row_show(slot, app_str(APP_STR_RESCAN), "", i == s_sel);
            } else {
                row_show(slot, app_str(APP_STR_FORGET), "", i == s_sel);
            }
        }
        return true;
    }

    if (s_id == MEOW_SET_BLE) {
        s_peer_n = bsp_ble_list_peers(s_peers, BSP_BLE_PEER_MAX);
        int items = 2 + s_peer_n + 1;
        if (s_sel >= items) s_sel = items ? items - 1 : 0;
        if (s_sel >= 2 && s_sel < 2 + s_peer_n) s_focus = s_sel - 2;
        app_list_t win = { .sel = s_sel, .n = items, .rows = 7 };
        app_list_reveal(&win);
        int start = win.top;
        for (int i = start; i < items && slot < 7; i++, slot++) {
            if (i == 0) {
                row_show(slot, app_str(APP_STR_POWER),
                         app_str_onoff(bsp_ble_enabled()), i == s_sel);
            } else if (i == 1) {
                row_show(slot, app_str(APP_STR_ADVERTISE),
                         app_str_onoff(bsp_ble_adv_active()), i == s_sel);
            } else if (i < 2 + s_peer_n) {
                int k = i - 2;
                char name[32];
                ui_pixel_utf8_copy(name, sizeof(name),
                                   s_peers[k].name[0] ? s_peers[k].name
                                                      : s_peers[k].addr);
                row_show(slot, name[0] ? name : "peer",
                         s_peers[k].connected ? "LINK" : "",
                         i == s_sel);
            } else {
                row_show(slot, app_str(APP_STR_FORGET_SEL), "", i == s_sel);
            }
        }
        return true;
    }

    if (s_id == MEOW_SET_CLOCK) {
        const app_prefs_t *p = app_prefs();
        if (s_sel >= clock_rows()) s_sel = clock_rows() - 1;
        row_show(0, app_str(APP_STR_NTP), app_str_onoff(p->ntp_on), s_sel == 0);
        row_show(1, app_str(APP_STR_SERVER), app_ntp_server(p->ntp_server), s_sel == 1);
        if (!p->ntp_on) {
            snprintf(meta, sizeof(meta), "%d", s_y);
            row_show(2, app_str(APP_STR_YEAR), meta, s_sel == 2);
            snprintf(meta, sizeof(meta), "%02d", s_mo);
            row_show(3, app_str(APP_STR_MONTH), meta, s_sel == 3);
            snprintf(meta, sizeof(meta), "%02d", s_d);
            row_show(4, app_str(APP_STR_DAY), meta, s_sel == 4);
            snprintf(meta, sizeof(meta), "%02d", s_h);
            row_show(5, app_str(APP_STR_HOUR), meta, s_sel == 5);
            snprintf(meta, sizeof(meta), "%02d", s_mi);
            row_show(6, app_str(APP_STR_MINUTE), meta, s_sel == 6);
            row_show(7, app_str(APP_STR_SET_CLOCK), "GO", s_sel == 7);
        }
        return true;
    }

    if (s_id == MEOW_SET_BED) {
        const app_prefs_t *p = app_prefs();
        snprintf(meta, sizeof(meta), "%02d:00", (int)p->meow_bed);
        row_show(0, app_str(APP_STR_BEDTIME), meta, s_sel == 0);
        snprintf(meta, sizeof(meta), "%02d:00", (int)p->meow_wake);
        row_show(1, app_str(APP_STR_WAKE), meta, s_sel == 1);
        return true;
    }

    if (s_id == MEOW_SET_SCREEN) {
        const app_prefs_t *p = app_prefs();
        snprintf(meta, sizeof(meta), "%d%%", (int)p->brightness);
        row_show(0, app_str(APP_STR_BRIGHTNESS), meta, s_sel == 0);
        row_show(1, app_str(APP_STR_SLEEP), sleep_txt(p->sleep_sec), s_sel == 1);
        row_show(2, app_str(APP_STR_LOCK), app_str_onoff(p->lock_on), s_sel == 2);
        row_show(3, app_str(APP_STR_LOCK_STAY), app_str_onoff(p->lock_stay), s_sel == 3);
        return true;
    }

    if (s_id == MEOW_SET_LOCK) {
        const app_prefs_t *p = app_prefs();
        row_show(0, app_str(APP_STR_LOCK_TIME), app_str_onoff(p->lock_time), s_sel == 0);
        row_show(1, app_str(APP_STR_WX), app_str_onoff(p->lock_wx), s_sel == 1);
        row_show(2, app_str(APP_STR_LOCK_QUOTE), app_str_onoff(p->lock_quote), s_sel == 2);
        return true;
    }

    if (s_id == MEOW_SET_SOUND) {
        const app_prefs_t *p = app_prefs();
        static const app_str_id_t tones[] = {
            APP_STR_TONE_OFF, APP_STR_TONE_BEEP, APP_STR_TONE_DOUBLE,
            APP_STR_TONE_CHIME, APP_STR_TONE_TRIPLE, APP_STR_TONE_ALARM,
        };
        int msg = p->tone_msg <= APP_TONE_ALARM ? p->tone_msg : APP_TONE_BEEP;
        int alert = p->tone_alert <= APP_TONE_ALARM ? p->tone_alert : APP_TONE_ALARM;
        row_show(0, app_str(APP_STR_MUTE), app_str_onoff(p->muted), s_sel == 0);
        snprintf(meta, sizeof(meta), "%d%%", (int)p->volume);
        row_show(1, app_str(APP_STR_VOLUME), meta, s_sel == 1);
        row_show(2, app_str(APP_STR_MESSAGE), app_str(tones[msg]), s_sel == 2);
        row_show(3, app_str(APP_STR_ALERT_TONE), app_str(tones[alert]), s_sel == 3);
        return true;
    }

    return false;
}

static void paint(void)
{
    if (!s_box) return;
    s_still_ms = 0;
    if (s_id == MEOW_SET_WIFI && s_scanning) {
        lv_label_set_text(s_title, app_str(APP_STR_WIFI));
        lv_label_set_text(s_hint, app_str(APP_STR_SCANNING));
        paint_rows();
        if (s_body) lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        app_kb_hide();
        app_web_mini_qr_show(s_mini_qr, s_mini_url, false);
        app_web_clear_target();
        return;
    }
    app_str_id_t title = APP_STR_SETTINGS;
    const char *hint = app_str(APP_STR_OK_CHOOSE);
    if (s_confirm) hint = app_str(APP_STR_OK_CHOOSE);
    else if (s_adj) hint = app_str(APP_STR_HINT_ADJ);
    char body[400];
    body[0] = 0;
    switch (s_id) {
    case MEOW_SET_WIFI:
        title = APP_STR_WIFI;
        if (!bsp_wifi_enabled()) hint = app_str(APP_STR_WIFI_OFF);
        else if (s_scanning) hint = app_str(APP_STR_SCANNING);
        else if (s_task || bsp_wifi_state() == BSP_WIFI_CONNECTING) {
            static char conn[40];
            const char *id = s_ssid[0] ? s_ssid : bsp_wifi_ssid();
            snprintf(conn, sizeof(conn), app_str(APP_STR_CONNECTING), id);
            hint = conn;
        }
        else if (s_wifi_view == WIFI_KB) hint = app_str(APP_STR_HOLD_SKIP);
        else if (bsp_wifi_state() == BSP_WIFI_FAILED) hint = app_str(APP_STR_FAIL_PASS);
        else hint = app_str(APP_STR_OK_CHOOSE);
        break;
    case MEOW_SET_BLE:
        title = APP_STR_BLUETOOTH;
        if (!bsp_ble_enabled()) hint = app_str(APP_STR_BT_OFF);
        else if (bsp_ble_state() == BSP_BLE_PAIRING && bsp_ble_passkey()) {
            static char pair[24];
            snprintf(pair, sizeof(pair), "%s %06lu", app_str(APP_STR_BT_CODE),
                     (unsigned long)bsp_ble_passkey());
            hint = pair;
        } else {
            hint = bsp_ble_name();
        }
        break;
    case MEOW_SET_CLOCK:
        title = APP_STR_DATETIME;
        hint = app_str(app_prefs()->ntp_on ? APP_STR_CLOCK_HINT : APP_STR_CLOCK_SET_HINT);
        break;
    case MEOW_SET_BED:
        title = APP_STR_PET_HOURS;
        hint = app_str(APP_STR_BED_HINT);
        break;
    case MEOW_SET_SCREEN:
        title = APP_STR_SCREEN;
        hint = app_str(APP_STR_SCREEN_HINT);
        break;
    case MEOW_SET_LOCK:
        title = APP_STR_LOCK;
        hint = app_str(APP_STR_OK_CHOOSE);
        break;
    case MEOW_SET_SOUND:
        title = APP_STR_SOUND;
        hint = app_str(APP_STR_SOUND_HINT);
        break;
    case MEOW_SET_OTA:
        title = APP_STR_UPDATE;
        if (app_ota_state() == APP_OTA_APPLYING) hint = app_str(APP_STR_OTA_HOLD);
        else if (app_ota_state() == APP_OTA_CHECKING) hint = app_str(APP_STR_OTA_CHECKING);
        else hint = app_str(APP_STR_OK_CHOOSE);
        paint_ota(body, sizeof(body));
        break;
    }
    if (s_confirm) hint = app_str(APP_STR_OK_CHOOSE);
    else if (s_adj) hint = app_str(APP_STR_HINT_ADJ);
    lv_label_set_text(s_title, app_str(title));
    lv_label_set_text(s_hint, hint);
    if (s_id == MEOW_SET_WIFI && s_wifi_view == WIFI_KB) {
        bool web = bsp_wifi_state() == BSP_WIFI_CONNECTED;
        rows_clear();
        if (s_body) lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        if (s_hint) lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        if (s_ssid[0]) lv_label_set_text(s_title, s_ssid);
        char vis[BSP_WIFI_PASS_MAX + 8];
        mask_pass(vis, sizeof(vis));
        app_kb_show(s_card, vis, s_kb_sel, s_kb_set,
                    web ? APP_WEB_MINI_H + 6 : 4);
        app_web_mini_qr_bind(s_card, &s_mini_qr, &s_mini_url);
        app_web_mini_qr_show(s_mini_qr, s_mini_url, web);
        app_web_set_target("password", s_pass, sizeof(s_pass), paint);
        return;
    }
    app_kb_hide();
    if (s_hint) lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    bool rows = paint_rows();
    if (s_body) {
        if (rows) lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_body, body);
        }
    }
    app_web_mini_qr_show(s_mini_qr, s_mini_url, false);
    app_web_clear_target();
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    if (s_paint_req) {
        s_paint_req = false;
        paint();
        s_still_ms = 0;
        return;
    }
    if (s_scanning) {
        s_still_ms = 0;
        return;
    }
    if (app_meow_set_busy()) {
        s_still_ms += 500;
        if (s_still_ms >= 1000) {
            paint();
            s_still_ms = 0;
        }
        return;
    }
    s_still_ms += 500;
    if (s_still_ms >= 1000) {
        paint();
        s_still_ms = 0;
    }
}

static void wifi_task(void *arg)
{
    uint32_t epoch = (uint32_t)(uintptr_t)arg;
    int req = s_req;
    s_req = 0;
    if (req == 1) {
        bool ble = bsp_ble_stack_up();
        int n = -1;
        s_scanning = true;
        s_paint_req = true;
        if (ble) {
            bsp_ble_suspend();
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        bsp_wifi_ap_t aps[BSP_WIFI_SCAN_MAX];
        bsp_wifi_radio_resume();
        bsp_wifi_ps_hold();
        if (!app_net_heap_ready(12 * 1024)) {
            ESP_LOGW(TAG, "堆不足,跳过扫描");
        } else if (app_net_acquire(APP_NET_SCAN, 3000)) {
            n = bsp_wifi_scan(aps, BSP_WIFI_SCAN_MAX);
            app_net_release(APP_NET_SCAN);
        } else {
            ESP_LOGW(TAG, "网络忙,跳过扫描");
        }
        bsp_wifi_ps_release();
        s_scanning = false;
        if (ble && bsp_ble_enabled() && !bsp_ble_stack_up() && !app_ota_busy()) {
            bsp_ble_resume();
        }
        if (app_wifi_scan_result_current(epoch, s_wifi_epoch,
                                         s_box && s_id == MEOW_SET_WIFI)) {
            s_ap_n = n < 0 ? 0 : n;
            if (s_ap_n > 0) memcpy(s_aps, aps, (size_t)s_ap_n * sizeof(s_aps[0]));
            s_wifi_view = WIFI_LIST;
            s_paint_req = true;
        }
    } else if (req == 2) {
        char ssid[BSP_WIFI_SSID_MAX + 1];
        char pass[BSP_WIFI_PASS_MAX + 1];
        strlcpy(ssid, s_ssid, sizeof(ssid));
        strlcpy(pass, s_pass, sizeof(pass));
        bsp_wifi_radio_resume();
        bsp_wifi_connect(ssid, pass);
    } else if (req == 3) {
        bsp_wifi_forget();
        if (app_wifi_scan_result_current(epoch, s_wifi_epoch,
                                         s_box && s_id == MEOW_SET_WIFI)) {
            s_ap_n = 0;
            s_sel = 0;
            s_wifi_view = WIFI_LIST;
            s_paint_req = true;
        }
    }
    ESP_LOGI(TAG, "wifi task done stack=%u",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    s_task = NULL;
    vTaskDelete(NULL);
}

static bool wifi_start(int req)
{
    if (s_task || req < 1 || req > 3) return false;
    s_req = req;
    uint32_t epoch = s_wifi_epoch;
    if (xTaskCreate(wifi_task, "meow_wifi", 4096, (void *)(uintptr_t)epoch, 4,
                    &s_task) != pdPASS) {
        s_task = NULL;
        s_req = 0;
        s_scanning = false;
        return false;
    }
    return true;
}

static void wifi_choose(void)
{
    if (s_scanning) return;
    if (!bsp_wifi_enabled() && s_sel != 0) return;
    if (s_sel == 0) {
        bool on = !bsp_wifi_enabled();
        if (on) bsp_wifi_radio_resume();
        bsp_wifi_set_enabled(on);
        if (on) wifi_start(1);
        return;
    }
    if (s_sel == 1) {
        bsp_wifi_set_auto_connect(!bsp_wifi_auto_connect());
        return;
    }
    if (s_sel < 2 + s_ap_n) {
        int a = s_sel - 2;
        strlcpy(s_ssid, s_aps[a].ssid, sizeof(s_ssid));
        s_pass[0] = 0;
        bsp_wifi_saved_pass(s_ssid, s_pass, sizeof(s_pass));
        if (s_aps[a].open) {
            wifi_start(2);
        } else {
            s_kb_sel = 0;
            s_kb_set = 0;
            s_wifi_view = WIFI_KB;
        }
        return;
    }
    if (s_sel == 2 + s_ap_n) {
        wifi_start(1);
        return;
    }
    s_confirm = 1;
    s_sel = 0;
}

static void ble_choose(void)
{
    if (s_sel == 0) {
        bool on = !bsp_ble_enabled();
        bsp_ble_set_enabled(on);
        if (on) ble_kick_adv();
    } else if (s_sel == 1) {
        if (bsp_ble_enabled()) {
            if (bsp_ble_state() == BSP_BLE_WAIT_NOTIFY) {
                bsp_ble_resume_advertising();
            } else if (bsp_ble_adv_active()) {
                bsp_ble_set_advertising(false);
            } else {
                ble_kick_adv();
            }
        }
    } else if (s_sel < 2 + s_peer_n) {
        s_focus = s_sel - 2;
    } else if (s_peer_n > 0) {
        if (s_focus < 0) s_focus = 0;
        if (s_focus >= s_peer_n) s_focus = s_peer_n - 1;
        s_confirm = 2;
        s_sel = 0;
    } else {
        s_confirm = 2;
        s_sel = 0;
    }
}

static void clock_choose(void)
{
    app_prefs_t *p = app_prefs();
    if (s_sel == 0) {
        p->ntp_on = !p->ntp_on;
        app_prefs_save();
        app_time_ntp_restart();
        s_sel = 0;
        return;
    }
    if (s_sel == 1) {
        p->ntp_server = (uint8_t)((p->ntp_server + 1) % APP_NTP_SERVER_N);
        app_prefs_save();
        app_time_ntp_restart();
        return;
    }
    switch (s_sel) {
    case 2:
        s_y++;
        if (s_y > 2038) s_y = 2024;
        break;
    case 3:
        s_mo = s_mo >= 12 ? 1 : s_mo + 1;
        break;
    case 4:
        s_d = s_d >= 31 ? 1 : s_d + 1;
        break;
    case 5:
        s_h = (s_h + 1) % 24;
        break;
    case 6:
        s_mi = (s_mi + 1) % 60;
        break;
    case 7:
        app_time_set(s_y, s_mo, s_d, s_h, s_mi);
        break;
    default:
        break;
    }
}

static void bed_choose(void)
{
    app_prefs_t *p = app_prefs();
    if (s_sel == 0) p->meow_bed = (uint8_t)((p->meow_bed + 1) % 24);
    else p->meow_wake = (uint8_t)((p->meow_wake + 1) % 24);
    app_prefs_save();
}

static void screen_choose(void)
{
    app_prefs_t *p = app_prefs();
    if (s_sel == 0) {
        int b = (int)p->brightness + 10;
        if (b > 100) b = 10;
        p->brightness = (uint8_t)b;
        bsp_display_backlight(p->brightness);
    } else if (s_sel == 1) {
        int i = sleep_idx(p->sleep_sec) + 1;
        if (i >= SLEEP_N) i = 0;
        p->sleep_sec = SLEEP_OPT[i];
    } else if (s_sel == 2) {
        p->lock_on = !p->lock_on;
    } else {
        p->lock_stay = !p->lock_stay;
    }
    app_prefs_save();
}

static void lock_choose(void)
{
    app_prefs_t *p = app_prefs();
    if (s_sel == 0) p->lock_time = !p->lock_time;
    else if (s_sel == 1) p->lock_wx = !p->lock_wx;
    else p->lock_quote = !p->lock_quote;
    app_prefs_save();
}

static void sound_choose(void)
{
    app_prefs_t *p = app_prefs();
    if (s_sel == 0) {
        p->muted = !p->muted;
        app_prefs_save();
        app_prefs_apply_audio();
        if (!p->muted) app_tone_play(APP_TONE_BEEP);
    } else if (s_sel == 1) {
        int v = (int)p->volume + 10;
        if (v > 100) v = 0;
        p->volume = (uint8_t)v;
        app_prefs_save();
        app_prefs_apply_audio();
        app_tone_play(APP_TONE_BEEP);
    } else if (s_sel == 2) {
        p->tone_msg = (uint8_t)((p->tone_msg + 1) % (APP_TONE_ALARM + 1));
        app_prefs_save();
        app_tone_play(p->tone_msg);
    } else {
        p->tone_alert = (uint8_t)((p->tone_alert + 1) % (APP_TONE_ALARM + 1));
        app_prefs_save();
        app_tone_play(p->tone_alert);
    }
}

static void hold_tick(lv_timer_t *t)
{
    int dir, step;

    (void)t;
    if (s_id != MEOW_SET_WIFI || s_wifi_view != WIFI_KB || s_hold_btn < 0) {
        return;
    }
    s_hold_ms += 120;
    if (s_hold_ms < 280) return;
    dir = (s_hold_btn == BSP_BTN_UP) ? -1 : 1;
    step = (s_hold_ms >= 800) ? KB_COLS : 1;
    move_sel(&s_kb_sel, KB_N, dir * step);
    paint();
}

static bool set_modal_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    app_meow_set_on_key(btn, ev);
    if (!app_meow_set_open_now()) app_shell_reload();
    return true;
}

void app_meow_set_init(void)
{
    static const app_modal_t modal = {
        .visible = app_meow_set_open_now,
        .key = set_modal_key,
        .prio = 40,
    };
    app_shell_register_modal(&modal);
}

void app_meow_set_close(void)
{
    bool was_ble = s_box && s_id == MEOW_SET_BLE;
    bool restore_ble = s_box && (s_id == MEOW_SET_WIFI || s_id == MEOW_SET_OTA) &&
                       bsp_ble_enabled() && !s_task;
    app_prefs_flush();
    s_adj = false;
    s_confirm = 0;
    s_wifi_epoch++;
    if (s_scanning) bsp_wifi_scan_cancel();
    s_scanning = false;
    s_req = 0;
    s_hold_btn = -1;
    s_hold_ms = 0;
    if (s_hold_timer) {
        lv_timer_delete(s_hold_timer);
        s_hold_timer = NULL;
    }
    s_ble_kick = 0;
    if (s_ble_defer) {
        lv_timer_delete(s_ble_defer);
        s_ble_defer = NULL;
    }
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_box) {
        lv_obj_delete(s_box);
        s_box = NULL;
        if (!app_lock_visible()) app_shell_page_obscure(false);
    }
    app_web_clear_target();
    app_web_qr_close();
    app_kb_hide();
    s_card = NULL;
    s_mini_qr = s_mini_url = NULL;
    s_title = s_hint = s_body = NULL;
    for (int i = 0; i < 8; i++) s_rows[i] = s_labs[i] = s_metas[i] = NULL;
    if (was_ble) app_web_suspend_for_ota(false);
    if (restore_ble && !app_settings_open_now() && !bsp_ble_stack_up() &&
        !app_ota_busy()) {
        bsp_ble_resume();
    }
}

static void ble_defer_cb(lv_timer_t *t)
{
    (void)t;
    s_ble_defer = NULL;
    if (!s_box || s_id != MEOW_SET_BLE || !bsp_ble_enabled()) return;
    if (app_ota_busy()) return;
    if (s_ble_kick == 0) {
        app_web_suspend_for_ota(true);
        bsp_ble_resume();
        s_ble_kick = 1;
        s_ble_defer = lv_timer_create(ble_defer_cb, 400, NULL);
        lv_timer_set_repeat_count(s_ble_defer, 1);
        paint();
        return;
    }
    if (!bsp_ble_synced()) {
        if (s_ble_kick < 12) {
            s_ble_kick++;
            bsp_ble_resume();
            s_ble_defer = lv_timer_create(ble_defer_cb, 400, NULL);
            lv_timer_set_repeat_count(s_ble_defer, 1);
        }
        paint();
        return;
    }
    bsp_ble_set_advertising(true);
    paint();
}

static void ble_kick_adv(void)
{
    s_ble_kick = 0;
    if (s_ble_defer) {
        lv_timer_delete(s_ble_defer);
        s_ble_defer = NULL;
    }
    s_ble_defer = lv_timer_create(ble_defer_cb, 50, NULL);
    lv_timer_set_repeat_count(s_ble_defer, 1);
}

void app_meow_set_open(lv_obj_t *lcd, meow_set_id_t id)
{
    app_meow_set_close();
    if (!lcd) return;
    app_shell_page_vacate();
    s_id = id;
    s_sel = 0;
    s_adj = false;
    s_confirm = 0;
    s_wifi_view = WIFI_LIST;
    s_kb_sel = 0;
    s_kb_set = 0;
    s_pass[0] = 0;
    s_ssid[0] = 0;
    s_mini_qr = s_mini_url = NULL;
    s_focus = 0;
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_labs, 0, sizeof(s_labs));
    memset(s_metas, 0, sizeof(s_metas));
    app_time_get(&s_y, &s_mo, &s_d, &s_h, &s_mi);
    if (s_y < 2024) s_y = 2026;

    s_box = lv_obj_create(lcd);
    ui_pixel_strip_theme(s_box);
    lv_obj_set_pos(s_box, APP_VIEW_X, APP_BODY_Y);
    lv_obj_set_size(s_box, APP_VIEW_W, APP_BODY_H);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_box, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_radius(s_box, 0, 0);
    lv_obj_set_style_border_width(s_box, 0, 0);

    s_card = s_box;
    s_title = app_ui_page_title(s_card, app_str(APP_STR_SETTINGS));
    s_hint = app_ui_footer(s_card, "");
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_MUTE), 0);
    s_body = app_ui_body(s_card, 40);
    lv_obj_set_pos(s_body, 0, 40);
    lv_obj_set_width(s_body, APP_CONTENT_W);
    lv_obj_set_style_text_color(s_body, lv_color_hex(UI_TEXT), 0);
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);

    if (id == MEOW_SET_WIFI || id == MEOW_SET_OTA) {
        bsp_wifi_radio_resume();
    }
    if (id == MEOW_SET_WIFI) {
        s_scanning = false;
        s_paint_req = false;
    }
    s_hold_btn = -1;
    s_hold_ms = 0;
    s_hold_timer = lv_timer_create(hold_tick, 120, NULL);
    s_timer = lv_timer_create(on_tick, 500, NULL);
    paint();
    if (id == MEOW_SET_BLE && bsp_ble_enabled()) ble_kick_adv();
}

void app_meow_set_suspend(bool hide)
{
    if (!s_box) return;
    if (hide) lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
}

bool app_meow_set_open_now(void)
{
    return s_box != NULL;
}

bool app_meow_set_ble_now(void)
{
    return s_box != NULL && s_id == MEOW_SET_BLE;
}

bool app_meow_set_busy(void)
{
    if (s_scanning) return true;
    if (s_id == MEOW_SET_WIFI &&
        (s_task || bsp_wifi_state() == BSP_WIFI_CONNECTING)) return true;
    if (bsp_ble_state() == BSP_BLE_PAIRING) return true;
    if (app_ota_busy()) return true;
    return false;
}

static int wrapi(int v, int lo, int hi, int dir)
{
    v += dir;
    if (v > hi) v = lo;
    if (v < lo) v = hi;
    return v;
}

static bool row_adj(void)
{
    if (s_id == MEOW_SET_BED) return true;
    if (s_id == MEOW_SET_SCREEN) return s_sel <= 1;
    if (s_id == MEOW_SET_SOUND) return s_sel >= 1;
    if (s_id == MEOW_SET_CLOCK) return s_sel >= 1 && s_sel <= 6;
    return false;
}

static void bump(int dir)
{
    app_prefs_t *p = app_prefs();
    if (s_id == MEOW_SET_BED) {
        if (s_sel == 0) p->meow_bed = (uint8_t)wrapi((int)p->meow_bed, 0, 23, dir);
        else p->meow_wake = (uint8_t)wrapi((int)p->meow_wake, 0, 23, dir);
        app_prefs_save();
        return;
    }
    if (s_id == MEOW_SET_SCREEN) {
        if (s_sel == 0) {
            p->brightness = (uint8_t)(wrapi((int)p->brightness / 10, 1, 10, dir) * 10);
            bsp_display_backlight(p->brightness);
        } else {
            p->sleep_sec = SLEEP_OPT[wrapi(sleep_idx(p->sleep_sec), 0, SLEEP_N - 1, dir)];
        }
        app_prefs_save();
        return;
    }
    if (s_id == MEOW_SET_SOUND) {
        if (s_sel == 1) {
            p->volume = (uint8_t)(wrapi((int)p->volume / 10, 0, 10, dir) * 10);
            app_prefs_save();
            app_prefs_apply_audio();
            app_tone_play(APP_TONE_BEEP);
        } else if (s_sel == 2) {
            p->tone_msg = (uint8_t)wrapi((int)p->tone_msg, 0, APP_TONE_ALARM, dir);
            app_prefs_save();
            app_tone_play(p->tone_msg);
        } else {
            p->tone_alert = (uint8_t)wrapi((int)p->tone_alert, 0, APP_TONE_ALARM, dir);
            app_prefs_save();
            app_tone_play(p->tone_alert);
        }
        return;
    }
    if (s_id == MEOW_SET_CLOCK) {
        if (s_sel == 1) {
            p->ntp_server = (uint8_t)wrapi((int)p->ntp_server, 0, APP_NTP_SERVER_N - 1, dir);
            app_prefs_save();
            app_time_ntp_restart();
        } else if (s_sel == 2) s_y = wrapi(s_y, 2024, 2038, dir);
        else if (s_sel == 3) s_mo = wrapi(s_mo, 1, 12, dir);
        else if (s_sel == 4) s_d = wrapi(s_d, 1, 31, dir);
        else if (s_sel == 5) s_h = wrapi(s_h, 0, 23, dir);
        else if (s_sel == 6) s_mi = wrapi(s_mi, 0, 59, dir);
    }
}

void app_meow_set_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_box) return;
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (s_adj) {
            s_adj = false;
            paint();
            return;
        }
        if (s_confirm) {
            s_confirm = 0;
            s_sel = 0;
            paint();
            return;
        }
        if (s_id == MEOW_SET_OTA && app_ota_state() == APP_OTA_APPLYING) {
            app_ota_cancel();
            paint();
            return;
        }
        if (s_id == MEOW_SET_WIFI && s_wifi_view == WIFI_KB) {
            s_wifi_view = WIFI_LIST;
            s_hold_btn = -1;
            s_hold_ms = 0;
            paint();
            return;
        }
        if (app_ota_state() == APP_OTA_APPLYING) return;
        app_meow_set_close();
        return;
    }
    if (s_id == MEOW_SET_WIFI && s_wifi_view == WIFI_KB &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        if (ev == BSP_BTN_PRESS) {
            s_hold_btn = (int)btn;
            s_hold_ms = 0;
            move_sel(&s_kb_sel, KB_N, btn == BSP_BTN_UP ? -1 : 1);
            paint();
        } else if (ev == BSP_BTN_RELEASE && s_hold_btn == (int)btn) {
            s_hold_btn = -1;
            s_hold_ms = 0;
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (s_id == MEOW_SET_WIFI && s_scanning) return;
    if (s_id == MEOW_SET_OTA && app_ota_state() == APP_OTA_APPLYING) return;

    if (s_id == MEOW_SET_WIFI && s_wifi_view == WIFI_KB) {
        if (btn != BSP_BTN_OK) return;
        int r = app_kb_click(s_pass, sizeof(s_pass), &s_kb_sel, &s_kb_set);
        if (r == 2) {
            wifi_start(2);
            s_wifi_view = WIFI_LIST;
            s_hold_btn = -1;
        } else if (r == 3) {
            s_wifi_view = WIFI_LIST;
            s_hold_btn = -1;
        } else if (r == 4) {
            app_web_qr_open();
        }
        paint();
        return;
    }

    if (s_confirm) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            move_sel(&s_sel, 2, btn == BSP_BTN_UP ? -1 : 1);
            paint();
            return;
        }
        if (btn != BSP_BTN_OK) return;
        if (s_sel == 1) {
            if (s_confirm == 1) wifi_start(3);
            else if (s_peer_n > 0) {
                if (s_focus < 0) s_focus = 0;
                if (s_focus >= s_peer_n) s_focus = s_peer_n - 1;
                bsp_ble_forget_at(s_focus);
            } else {
                bsp_ble_unpair();
            }
        }
        s_confirm = 0;
        s_sel = 0;
        paint();
        return;
    }

    if (s_adj) {
        if (btn == BSP_BTN_UP) bump(-1);
        else if (btn == BSP_BTN_DOWN) bump(1);
        else if (btn == BSP_BTN_OK) s_adj = false;
        paint();
        return;
    }

    int n = 2;
    if (s_id == MEOW_SET_WIFI) n = wifi_count();
    else if (s_id == MEOW_SET_BLE) n = 2 + s_peer_n + 1;
    else if (s_id == MEOW_SET_CLOCK) n = clock_rows();
    else if (s_id == MEOW_SET_SCREEN) n = 4;
    else if (s_id == MEOW_SET_LOCK) n = 3;
    else if (s_id == MEOW_SET_SOUND) n = 4;
    else if (s_id == MEOW_SET_OTA) n = 3;
    if (n < 1) n = 1;

    if (btn == BSP_BTN_UP) {
        move_sel(&s_sel, n, -1);
        paint();
        return;
    }
    if (btn == BSP_BTN_DOWN) {
        move_sel(&s_sel, n, 1);
        paint();
        return;
    }
    if (btn != BSP_BTN_OK) return;
    if (row_adj()) {
        s_adj = true;
        paint();
        return;
    }
    switch (s_id) {
    case MEOW_SET_WIFI: wifi_choose(); break;
    case MEOW_SET_BLE: ble_choose(); break;
    case MEOW_SET_CLOCK: clock_choose(); break;
    case MEOW_SET_BED: bed_choose(); break;
    case MEOW_SET_SCREEN: screen_choose(); break;
    case MEOW_SET_LOCK: lock_choose(); break;
    case MEOW_SET_SOUND: sound_choose(); break;
    case MEOW_SET_OTA: ota_choose(); break;
    }
    paint();
}
