#include "app_meow_set.h"

#include "app_i18n.h"
#include "app_ota.h"
#include "app_prefs.h"
#include "app_time.h"
#include "app_tone.h"
#include "app_ui.h"
#include "bsp_ble.h"
#include "bsp_display.h"
#include "bsp_wifi.h"
#include "ui_pixel.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COL_LCD  0xFFF7EA
#define COL_PIX  0x5B4636
#define COL_FACE 0xFF8C7A
#define COL_WHITE 0xFFFFFF
#define COL_MUTE 0x8A7460

#define WIFI_ROWS 7

typedef enum { WIFI_LIST = 0, WIFI_KB } wifi_view_t;

static lv_obj_t *s_box, *s_title, *s_hint, *s_body;
static lv_timer_t *s_timer;
static meow_set_id_t s_id;
static int s_sel;
static int s_y, s_mo, s_d, s_h, s_mi;

static wifi_view_t s_wifi_view;
static bsp_wifi_ap_t s_aps[BSP_WIFI_SCAN_MAX];
static int s_ap_n;
static char s_ssid[BSP_WIFI_SSID_MAX + 1];
static char s_pass[BSP_WIFI_PASS_MAX + 1];
static int s_kb_sel, s_kb_set;
static int s_hold_btn = -1;
static int s_hold_ms;
static lv_timer_t *s_hold_timer;
static TaskHandle_t s_task;
static volatile int s_req;
static volatile bool s_scanning;
static volatile bool s_wifi_page;

static bsp_ble_peer_t s_peers[BSP_BLE_PEER_MAX];
static int s_peer_n;
static int s_focus;
static uint32_t s_still_ms;

static const uint16_t SLEEP_OPT[] = { 0, 15, 30, 60, 120 };
#define SLEEP_N 5

static lv_obj_t *lab(lv_obj_t *p, const lv_font_t *font, int x, int y, int w)
{
    lv_obj_t *o = ui_pixel_label(p, "", font, COL_PIX);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    return o;
}

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
    return 2 + s_ap_n + 1 + (bsp_wifi_has_saved() ? 1 : 0);
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

static void paint_wifi(char *out, size_t n)
{
    if (s_wifi_view == WIFI_KB) {
        char vis[BSP_WIFI_PASS_MAX + 8];
        mask_pass(vis, sizeof(vis));
        app_kb_render(out, n, s_ssid, vis, s_kb_sel, s_kb_set);
        return;
    }
    int items = wifi_count();
    if (s_sel >= items) s_sel = items ? items - 1 : 0;
    int start = s_sel - WIFI_ROWS / 2;
    if (start < 0) start = 0;
    if (start + WIFI_ROWS > items) start = items > WIFI_ROWS ? items - WIFI_ROWS : 0;
    size_t used = 0;
    out[0] = 0;
    for (int i = start; i < items && i < start + WIFI_ROWS; i++) {
        char line[80];
        const char *m = (i == s_sel) ? ">" : " ";
        if (i == 0) {
            snprintf(line, sizeof(line), "%s %s  %s\n", m,
                     app_str(APP_STR_POWER), app_str_onoff(bsp_wifi_enabled()));
        } else if (i == 1) {
            snprintf(line, sizeof(line), "%s %s  %s\n", m,
                     app_str(APP_STR_AUTO), app_str_onoff(bsp_wifi_auto_connect()));
        } else if (i < 2 + s_ap_n) {
            int a = i - 2;
            char ssid[40];
            ui_pixel_utf8_copy(ssid, sizeof(ssid), s_aps[a].ssid);
            bool on = (bsp_wifi_state() == BSP_WIFI_CONNECTED &&
                       strcmp(s_aps[a].ssid, bsp_wifi_ssid()) == 0);
            snprintf(line, sizeof(line), "%s%s%s %ddB%s\n", m,
                     s_aps[a].open ? "" : "*", ssid, (int)s_aps[a].rssi,
                     on ? app_str(APP_STR_LINKED) : "");
        } else if (i == 2 + s_ap_n) {
            snprintf(line, sizeof(line), "%s %s\n", m, app_str(APP_STR_RESCAN));
        } else {
            snprintf(line, sizeof(line), "%s %s\n", m, app_str(APP_STR_FORGET));
        }
        size_t ln = strlen(line);
        if (used + ln >= n) break;
        memcpy(out + used, line, ln + 1);
        used += ln;
    }
}

static void paint_ble(char *out, size_t n)
{
    s_peer_n = bsp_ble_list_peers(s_peers, BSP_BLE_PEER_MAX);
    int items = 3 + s_peer_n + 1;
    if (s_sel >= items) s_sel = items ? items - 1 : 0;
    if (s_sel >= 3 && s_sel < 3 + s_peer_n) s_focus = s_sel - 3;
    size_t used = 0;
    out[0] = 0;
    int start = s_sel - 3;
    if (start < 0) start = 0;
    if (start + 7 > items) start = items > 7 ? items - 7 : 0;
    for (int i = start; i < items && i < start + 7; i++) {
        char line[80];
        const char *m = (i == s_sel) ? ">" : " ";
        if (i == 0) {
            snprintf(line, sizeof(line), "%s %s  %s\n", m,
                     app_str(APP_STR_POWER), app_str_onoff(bsp_ble_enabled()));
        } else if (i == 1) {
            snprintf(line, sizeof(line), "%s %s  %s\n", m,
                     app_str(APP_STR_STOP_ADV), app_str_onoff(bsp_ble_quiet()));
        } else if (i == 2) {
            snprintf(line, sizeof(line), "%s %s  %s\n", m,
                     app_str(APP_STR_ADVERTISE),
                     app_str_onoff(bsp_ble_adv_active()));
        } else if (i < 3 + s_peer_n) {
            int p = i - 3;
            char shown[36];
            const char *src = s_peers[p].name[0] ? s_peers[p].name : s_peers[p].addr;
            ui_pixel_utf8_copy(shown, sizeof(shown), src);
            snprintf(line, sizeof(line), "%s%s %s\n", m,
                     s_peers[p].connected ? "*" : " ", shown);
        } else {
            snprintf(line, sizeof(line), "%s %s\n", m, app_str(APP_STR_FORGET_SEL));
        }
        size_t ln = strlen(line);
        if (used + ln >= n) break;
        memcpy(out + used, line, ln + 1);
        used += ln;
    }
}

static int clock_rows(void)
{
    return app_prefs()->ntp_on ? 2 : 7;
}

static void paint_clock(char *out, size_t n)
{
    char now[24];
    app_time_now_text(now, sizeof(now));
    const app_prefs_t *p = app_prefs();
    if (s_sel >= clock_rows()) s_sel = clock_rows() - 1;
    if (p->ntp_on) {
        snprintf(out, n,
                 "%s %s\n"
                 "%s %s  %s%s\n"
                 "%s %s  %s\n",
                 app_str(APP_STR_NOW), now,
                 s_sel == 0 ? ">" : " ", app_str(APP_STR_NTP),
                 app_str_onoff(p->ntp_on),
                 app_time_ntp_synced() ? app_str(APP_STR_SYNC) : "",
                 s_sel == 1 ? ">" : " ", app_str(APP_STR_SERVER),
                 app_ntp_server(p->ntp_server));
        return;
    }
    snprintf(out, n,
             "%s %s\n"
             "%s %s  %s\n"
             "%s %s  %d\n"
             "%s %s  %d\n"
             "%s %s  %d\n"
             "%s %s  %d\n"
             "%s %s  %02d\n"
             "%s %s\n",
             app_str(APP_STR_NOW), now,
             s_sel == 0 ? ">" : " ", app_str(APP_STR_NTP),
             app_str_onoff(p->ntp_on),
             s_sel == 1 ? ">" : " ", app_str(APP_STR_YEAR), s_y,
             s_sel == 2 ? ">" : " ", app_str(APP_STR_MONTH), s_mo,
             s_sel == 3 ? ">" : " ", app_str(APP_STR_DAY), s_d,
             s_sel == 4 ? ">" : " ", app_str(APP_STR_HOUR), s_h,
             s_sel == 5 ? ">" : " ", app_str(APP_STR_MINUTE), s_mi,
             s_sel == 6 ? ">" : " ", app_str(APP_STR_SET_CLOCK));
}

static void paint_bed(char *out, size_t n)
{
    const app_prefs_t *p = app_prefs();
    snprintf(out, n,
             "%s %s  %02d:00\n"
             "%s %s  %02d:00\n",
             s_sel == 0 ? ">" : " ", app_str(APP_STR_BEDTIME), (int)p->meow_bed,
             s_sel == 1 ? ">" : " ", app_str(APP_STR_WAKE), (int)p->meow_wake);
}

static void paint_screen(char *out, size_t n)
{
    app_prefs_t *p = app_prefs();
    snprintf(out, n,
             "%s %s  %d%%\n"
             "%s %s  %s\n",
             s_sel == 0 ? ">" : " ", app_str(APP_STR_BRIGHTNESS), (int)p->brightness,
             s_sel == 1 ? ">" : " ", app_str(APP_STR_SLEEP),
             sleep_txt(p->sleep_sec));
}

static void paint_sound(char *out, size_t n)
{
    app_prefs_t *p = app_prefs();
    snprintf(out, n,
             "%s %s  %s\n"
             "%s %s  %d%%\n",
             s_sel == 0 ? ">" : " ", app_str(APP_STR_MUTE),
             app_str_onoff(p->muted),
             s_sel == 1 ? ">" : " ", app_str(APP_STR_VOLUME), (int)p->volume);
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
             "%s %s\n"
             "%s %s\n"
             "%s\n"
             "%s  %s\n"
             "%s  %s\n"
             "%s\n",
             s_sel == 0 ? ">" : " ", app_str(APP_STR_OTA_CHECK),
             s_sel == 1 ? ">" : " ", app_str(APP_STR_OTA_INSTALL),
             app_ota_channel(),
             app_str(APP_STR_OTA_NOW), app_ota_cur_ver(),
             app_str(APP_STR_OTA_LATEST), latest,
             stxt);
}

static void ota_choose(void)
{
    if (app_ota_busy()) return;
    if (s_sel == 0) app_ota_check();
    else if (s_sel == 1) app_ota_apply();
}

static void remember_current_ssid(void)
{
    const char *cur = bsp_wifi_ssid();
    int i;
    bsp_wifi_ap_t row;

    if (!cur[0]) return;
    for (i = 0; i < s_ap_n; i++) {
        if (strcmp(s_aps[i].ssid, cur) == 0) return;
    }
    if (s_ap_n >= BSP_WIFI_SCAN_MAX) return;
    memset(&row, 0, sizeof(row));
    strlcpy(row.ssid, cur, sizeof(row.ssid));
    memmove(&s_aps[1], &s_aps[0], (size_t)s_ap_n * sizeof(s_aps[0]));
    s_aps[0] = row;
    s_ap_n++;
}

static void paint(void)
{
    if (!s_box) return;
    s_still_ms = 0;
    app_str_id_t title = APP_STR_SETTINGS;
    const char *hint = app_str(APP_STR_MEOW_SUB_HINT);
    char body[900];
    body[0] = 0;
    switch (s_id) {
    case MEOW_SET_WIFI:
        title = APP_STR_WIFI;
        if (!bsp_wifi_enabled()) hint = app_str(APP_STR_WIFI_OFF);
        else if (s_scanning) hint = app_str(APP_STR_SCANNING);
        else if (s_wifi_view == WIFI_KB) hint = app_str(APP_STR_HOLD_SKIP);
        else if (bsp_wifi_state() == BSP_WIFI_CONNECTING) {
            static char conn[72];
            snprintf(conn, sizeof(conn), app_str(APP_STR_CONNECTING), bsp_wifi_ssid());
            hint = conn;
        }
        else if (bsp_wifi_state() == BSP_WIFI_CONNECTED) {
            static char up[72];
            char ip[20];
            bsp_wifi_ip(ip, sizeof(ip));
            snprintf(up, sizeof(up), "%s  %s", bsp_wifi_ssid(), ip);
            hint = up;
        }
        else if (bsp_wifi_state() == BSP_WIFI_FAILED) hint = app_str(APP_STR_FAIL_PASS);
        else hint = app_str(APP_STR_OK_CHOOSE);
        paint_wifi(body, sizeof(body));
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
        paint_ble(body, sizeof(body));
        break;
    case MEOW_SET_CLOCK:
        title = APP_STR_DATETIME;
        hint = app_str(app_prefs()->ntp_on ? APP_STR_CLOCK_HINT : APP_STR_CLOCK_SET_HINT);
        paint_clock(body, sizeof(body));
        break;
    case MEOW_SET_BED:
        title = APP_STR_PET_HOURS;
        hint = app_str(APP_STR_BED_HINT);
        paint_bed(body, sizeof(body));
        break;
    case MEOW_SET_SCREEN:
        title = APP_STR_SCREEN;
        hint = app_str(APP_STR_SCREEN_HINT);
        paint_screen(body, sizeof(body));
        break;
    case MEOW_SET_SOUND:
        title = APP_STR_SOUND;
        hint = app_str(APP_STR_SOUND_HINT);
        paint_sound(body, sizeof(body));
        break;
    case MEOW_SET_OTA:
        title = APP_STR_UPDATE;
        if (app_ota_state() == APP_OTA_APPLYING) hint = app_str(APP_STR_OTA_HOLD);
        else if (app_ota_state() == APP_OTA_CHECKING) hint = app_str(APP_STR_OTA_CHECKING);
        else hint = app_str(APP_STR_OK_CHOOSE);
        paint_ota(body, sizeof(body));
        break;
    }
    lv_label_set_text(s_title, app_str(title));
    lv_label_set_text(s_hint, hint);
    lv_label_set_text(s_body, body);
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    if (s_scanning || app_meow_set_busy() || s_id == MEOW_SET_WIFI) {
        paint();
        s_still_ms = 0;
        return;
    }
    s_still_ms += 250;
    if (s_still_ms >= 1000) {
        paint();
        s_still_ms = 0;
    }
}

static void wifi_task(void *arg)
{
    (void)arg;
    for (;;) {
        int req = s_req;
        if (req == 1) {
            s_req = 0;
            s_scanning = true;
            (void)bsp_wifi_ensure_started();
            bsp_wifi_cancel_connect();
            int n = bsp_wifi_scan(s_aps, BSP_WIFI_SCAN_MAX);
            s_ap_n = n < 0 ? 0 : n;
            remember_current_ssid();
            s_wifi_view = WIFI_LIST;
            s_scanning = false;
            if (!s_wifi_page && bsp_wifi_enabled() && bsp_wifi_auto_connect()) {
                (void)bsp_wifi_connect_saved();
            }
        } else if (req == 2) {
            s_req = 0;
            bsp_wifi_connect(s_ssid, s_pass);
        } else if (req == 3) {
            s_req = 0;
            bsp_wifi_forget();
            s_ap_n = 0;
            s_sel = 0;
            s_wifi_view = WIFI_LIST;
            if (bsp_wifi_enabled()) s_req = 1;
        } else {
            vTaskDelay(pdMS_TO_TICKS(40));
        }
    }
}

static void wifi_choose(void)
{
    if (s_scanning) return;
    if (!bsp_wifi_enabled() && s_sel != 0) return;
    if (s_sel == 0) {
        bool on = !bsp_wifi_enabled();
        bsp_wifi_set_enabled(on);
        if (on) {
            (void)bsp_wifi_ensure_started();
            s_req = 1;
        }
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
            s_req = 2;
        } else {
            s_kb_sel = 0;
            s_kb_set = 0;
            s_wifi_view = WIFI_KB;
        }
        return;
    }
    if (s_sel == 2 + s_ap_n) {
        s_req = 1;
        return;
    }
    s_req = 3;
}

static void ble_choose(void)
{
    if (s_sel == 0) {
        bsp_ble_set_enabled(!bsp_ble_enabled());
    } else if (s_sel == 1) {
        bsp_ble_set_quiet(!bsp_ble_quiet());
    } else if (s_sel == 2) {
        if (bsp_ble_enabled()) {
            if (bsp_ble_state() == BSP_BLE_WAIT_NOTIFY) {
                bsp_ble_resume_advertising();
            } else {
                bsp_ble_set_advertising(!bsp_ble_adv_active());
            }
        }
    } else if (s_sel < 3 + s_peer_n) {
        s_focus = s_sel - 3;
    } else if (s_peer_n > 0) {
        if (s_focus < 0) s_focus = 0;
        if (s_focus >= s_peer_n) s_focus = s_peer_n - 1;
        bsp_ble_forget_at(s_focus);
    } else {
        bsp_ble_unpair();
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
    if (p->ntp_on) {
        if (s_sel == 1) {
            p->ntp_server = (uint8_t)((p->ntp_server + 1) % APP_NTP_SERVER_N);
            app_prefs_save();
            app_time_ntp_restart();
        }
        return;
    }
    switch (s_sel) {
    case 1:
        s_y++;
        if (s_y > 2038) s_y = 2024;
        break;
    case 2:
        s_mo = s_mo >= 12 ? 1 : s_mo + 1;
        break;
    case 3:
        s_d = s_d >= 31 ? 1 : s_d + 1;
        break;
    case 4:
        s_h = (s_h + 1) % 24;
        break;
    case 5:
        s_mi = (s_mi + 1) % 60;
        break;
    case 6:
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
    } else {
        int i = sleep_idx(p->sleep_sec) + 1;
        if (i >= SLEEP_N) i = 0;
        p->sleep_sec = SLEEP_OPT[i];
    }
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
    } else {
        int v = (int)p->volume + 10;
        if (v > 100) v = 0;
        p->volume = (uint8_t)v;
        app_prefs_save();
        app_prefs_apply_audio();
        app_tone_play(APP_TONE_BEEP);
    }
}

static void hold_tick(lv_timer_t *t)
{
    int dir, step;

    (void)t;
    if (s_id != MEOW_SET_WIFI || s_wifi_view != WIFI_KB || s_hold_btn < 0) {
        return;
    }
    s_hold_ms += 80;
    if (s_hold_ms < 280) return;
    dir = (s_hold_btn == BSP_BTN_UP) ? -1 : 1;
    step = (s_hold_ms >= 800) ? KB_COLS : 1;
    move_sel(&s_kb_sel, KB_N, dir * step);
    paint();
}

void app_meow_set_close(void)
{
    bool leave_wifi = s_wifi_page;

    s_wifi_page = false;
    s_req = 0;
    s_hold_btn = -1;
    s_hold_ms = 0;
    if (s_hold_timer) {
        lv_timer_delete(s_hold_timer);
        s_hold_timer = NULL;
    }
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_box) {
        lv_obj_delete(s_box);
        s_box = NULL;
    }
    s_title = s_hint = s_body = NULL;
    if (leave_wifi && !s_scanning && bsp_wifi_enabled() &&
        bsp_wifi_auto_connect()) {
        (void)bsp_wifi_connect_saved();
    }
}

void app_meow_set_open(lv_obj_t *lcd, meow_set_id_t id)
{
    app_meow_set_close();
    if (!lcd) return;
    s_id = id;
    s_sel = 0;
    s_wifi_view = WIFI_LIST;
    s_kb_sel = 0;
    s_kb_set = 0;
    s_pass[0] = 0;
    s_ssid[0] = 0;
    s_focus = 0;
    app_time_get(&s_y, &s_mo, &s_d, &s_h, &s_mi);
    if (s_y < 2024) s_y = 2026;

    s_box = lv_obj_create(lcd);
    ui_pixel_strip_theme(s_box);
    int w = lv_obj_get_width(lcd);
    int h = lv_obj_get_height(lcd);
    lv_obj_set_pos(s_box, 0, 0);
    lv_obj_set_size(s_box, w, h);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_box, lv_color_hex(COL_LCD), 0);
    lv_obj_set_style_radius(s_box, 0, 0);

    lv_obj_t *bar = lv_obj_create(s_box);
    ui_pixel_strip_theme(bar);
    lv_obj_set_pos(bar, 16, 36);
    lv_obj_set_size(bar, w - 32, 28);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 14, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_FACE), 0);

    s_title = lab(bar, ui_pixel_font_14(), 8, 6, w - 48);
    lv_obj_set_style_text_color(s_title, lv_color_hex(COL_WHITE), 0);
    s_hint = lab(s_box, ui_pixel_font_14(), 16, 72, w - 32);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(COL_MUTE), 0);
    s_body = ui_pixel_label(s_box, "", ui_pixel_font_14(), COL_PIX);
    lv_obj_set_pos(s_body, 16, 96);
    lv_obj_set_width(s_body, w - 32);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);

    if (id == MEOW_SET_OTA) bsp_wifi_radio_resume();
    else if (id == MEOW_SET_WIFI) (void)bsp_wifi_ensure_started();
    if (id == MEOW_SET_WIFI) {
        s_wifi_page = true;
        if (!s_task) xTaskCreate(wifi_task, "meow_wifi", 4096, NULL, 4, &s_task);
        if (bsp_wifi_enabled()) s_req = 1;
    }
    s_hold_btn = -1;
    s_hold_ms = 0;
    s_hold_timer = lv_timer_create(hold_tick, 80, NULL);
    s_timer = lv_timer_create(on_tick, 250, NULL);
    paint();
}

bool app_meow_set_open_now(void)
{
    return s_box != NULL;
}

bool app_meow_set_busy(void)
{
    if (s_scanning) return true;
    if (s_id == MEOW_SET_WIFI && bsp_wifi_state() == BSP_WIFI_CONNECTING) return true;
    if (bsp_ble_state() == BSP_BLE_PAIRING) return true;
    if (app_ota_busy()) return true;
    return false;
}

bool app_meow_set_blocks_idle(void)
{
    if (app_meow_set_busy()) return true;
    if (!s_box) return false;
    if (s_id == MEOW_SET_SCREEN || s_id == MEOW_SET_SOUND) return false;
    return true;
}

void app_meow_set_tick(void)
{
    if (s_box) paint();
}

void app_meow_set_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_box) return;
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
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
            s_req = 2;
            s_wifi_view = WIFI_LIST;
            s_hold_btn = -1;
        } else if (r == 3 || r == 4) {
            s_wifi_view = WIFI_LIST;
            s_hold_btn = -1;
        }
        paint();
        return;
    }

    int n = 2;
    if (s_id == MEOW_SET_WIFI) n = wifi_count();
    else if (s_id == MEOW_SET_BLE) n = 3 + s_peer_n + 1;
    else if (s_id == MEOW_SET_CLOCK) n = clock_rows();
    else if (s_id == MEOW_SET_OTA) n = 2;
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
    switch (s_id) {
    case MEOW_SET_WIFI: wifi_choose(); break;
    case MEOW_SET_BLE: ble_choose(); break;
    case MEOW_SET_CLOCK: clock_choose(); break;
    case MEOW_SET_BED: bed_choose(); break;
    case MEOW_SET_SCREEN: screen_choose(); break;
    case MEOW_SET_SOUND: sound_choose(); break;
    case MEOW_SET_OTA: ota_choose(); break;
    }
    paint();
}
