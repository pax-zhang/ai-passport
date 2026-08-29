#include "app_farm_ui.h"

#include "app_farm.h"
#include "app_farm_img.h"
#include "app_farm_net.h"
#include "app_farm_web.h"
#include "app_i18n.h"
#include "app_meow_set.h"
#include "app_ota.h"
#include "app_prefs.h"
#include "app_time.h"
#include "app_tone.h"
#include "app_ui.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_ble.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pm.h"
#include "bsp_wifi.h"
#include "ui_pixel.h"

#include "esp_mac.h"
#include "lvgl.h"
#include "nvs.h"

#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LCD_W  240
#define LCD_H  320
#define NAV_H  46
#define PAGE_H (LCD_H - NAV_H)
#define HDR_H  48
#define XP_H   10
#define MSG_H  28

#define TAB_HOME  0
#define TAB_BAG   1
#define TAB_STEAL 2
#define TAB_SET   3
#define TAB_N     4

#define BAG_ITM  0
#define BAG_SHOP 1
#define STEAL_RAND 0
#define STEAL_FRI  1
#define STEAL_RANK 2
#define STEAL_MAIL 3
#define STEAL_RANK_MAX 10
#define STEAL_CAT_N 4
#define VISIT_NEXT    4
#define VISIT_TOOL_N  5

#define SET_N     10
#define SET_LANG  0
#define SET_ID    1
#define SET_NAME  2
#define SET_WIFI  3
#define SET_BLE   4
#define SET_CLOCK 5
#define SET_SCR   6
#define SET_SND   7
#define SET_OTA   8
#define SET_WIPE  9

#define COL_BG    0xF8E98D
#define COL_INK   0x60401F
#define COL_MUTE  0x8D6A36
#define COL_CORAL 0xF26D3D
#define COL_GOLD  0xFFC928
#define COL_COIN  0xFFF1A8
#define COL_COINX 0x9E6500
#define COL_GEM   0xDDF5FF
#define COL_GEMX  0x168DCC
#define COL_WHITE 0xFFFFFF
#define COL_CARD  0xFFF9D7
#define COL_GRASS 0x9DCF27
#define COL_GRASS2 0x78B91F
#define COL_DIRT  0xB96A22
#define COL_DIRT2 0x7B4318
#define COL_LOCK  0xD5B66A
#define COL_LEAF  0x4E9B22
#define COL_SKY   0xAEE5F0
#define COL_SKY2  0xFFF0A3
#define COL_ALERT 0xD93A28
#define COL_BUG   0x49301E
#define COL_WOOD  0xA85B24
#define COL_WOOD2 0x743716
#define COL_CREAM 0xFFF6C9

#define HOME_N  APP_FARM_TOOL_N
#define PLOT_COLS 3
#define IDLE_PERF_MS  2000u
#define IDLE_PAINT_MS 1000u
#define WIFI_WAKE_MS  30000u

#define WAIT_NONE    0
#define WAIT_REG     1
#define WAIT_SYNC    2
#define WAIT_PULL    3
#define WAIT_RANDOM  4
#define WAIT_VISIT   5
#define WAIT_STEAL   6
#define WAIT_FRIENDS 7
#define WAIT_ADD     8
#define WAIT_REMOVE  9
#define WAIT_RANK    10
#define WAIT_INBOX   11
#define WAIT_REPLY   12
#define WAIT_HELP    13

#define KB_NONE   0
#define KB_HOST   1
#define KB_FRIEND 2
#define KB_NAME   3

#define CONF_NONE   0
#define CONF_RESET  1
#define CONF_REMOVE 2

static const app_str_id_t SET_STR[SET_N] = {
    APP_STR_LANGUAGE, APP_STR_FARM_ID, APP_STR_MEOW_NAME, APP_STR_WIFI,
    APP_STR_BLUETOOTH, APP_STR_DATETIME, APP_STR_SCREEN, APP_STR_SOUND,
    APP_STR_UPDATE, APP_STR_FARM_RESET
};

static app_farm_t s_farm;
static bool s_ready;
static bool s_dirty;
static uint32_t s_saved_sec;
static bool s_asleep;
static uint32_t s_idle_ms, s_awake_ms, s_still_ms;
static bool s_wifi_wait, s_wake_skip;
static uint32_t s_tick_ms = 250;
static int s_sync_left;
static bool s_net_up;
static uint8_t s_list_ok;

static lv_obj_t *s_scr, *s_lcd, *s_ibar, *s_stage;
static lv_timer_t *s_timer, *s_kb_hold_tm;
static int s_sel, s_menu = -1, s_sub;
static int s_bag_cat, s_steal_cat;
static int s_plot_sel = -1;
static bool s_seed_pick;
static int s_kb_mode, s_kb_sel, s_kb_set, s_kb_hold_btn = -1, s_kb_hold_ms;
static char s_kb_buf[APP_FARM_HOST_MAX + 1];
static char s_flash[64];
static char s_flash_next[32];
static int s_flash_next_tone, s_flash_next_ticks;
static char s_line[80];
static int s_flash_left, s_blink;
static int s_wait;
static bool s_visit;
static bool s_visit_ro;
static app_farm_view_t s_guest;
static uint8_t s_quota[APP_FARM_ACT_N] = {
    APP_FARM_STEAL_DAY, APP_FARM_HELP_DAY, APP_FARM_HELP_DAY, APP_FARM_HELP_DAY
};
static int s_friend_act = -1;
static bool s_inbox_pick;
static bool s_reply_accept;
static int s_confirm;
static uint32_t s_confirm_id;
static int s_visit_src = STEAL_RAND;
static uint32_t s_add_id;
static app_farm_peer_t s_friends[APP_FARM_NET_PEER_MAX];
static app_farm_mail_t s_mail[APP_FARM_NET_PEER_MAX];
static app_farm_peer_t s_rank[APP_FARM_NET_PEER_MAX];
static int s_friend_n, s_mail_n, s_rank_n;
static bool s_want_back_hint;

static uint32_t now_sec(void)
{
    time_t t = time(NULL);
    if (t >= (time_t)1700000000) return (uint32_t)t;
    int64_t us = esp_timer_get_time();
    if (us < 0) us = 0;
    return 1u + (uint32_t)(us / 1000000);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("app", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "farm", &s_farm, sizeof(s_farm));
    nvs_commit(h);
    nvs_close(h);
    s_saved_sec = s_farm.last_sec;
    s_dirty = false;
}

static void load_mac_id(void)
{
    uint8_t mac[6];

    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        memset(mac, 0, sizeof(mac));
        mac[5] = 1;
    }
    s_farm.id = app_farm_id_from_mac(mac);
}

static void load_nvs(void)
{
    nvs_handle_t h;
    uint8_t raw[sizeof(app_farm_t)];
    size_t n = sizeof(raw);

    load_mac_id();
    app_farm_reset(&s_farm, now_sec(), s_farm.id, (uint8_t)(now_sec() ^ 0xA5));
    if (nvs_open("app", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "farm", raw, &n) == ESP_OK) {
        uint32_t id = s_farm.id;
        if (app_farm_import(&s_farm, raw, n)) {
            char old_host[APP_FARM_HOST_MAX + 1];

            if (s_farm.id == 0) s_farm.id = id;
            memcpy(old_host, s_farm.host, sizeof(old_host));
            strncpy(s_farm.host, APP_FARM_HOST_DEFAULT, sizeof(s_farm.host) - 1);
            s_farm.host[sizeof(s_farm.host) - 1] = 0;
            if (strcmp(old_host, s_farm.host) != 0) s_farm.token[0] = 0;
        }
    }
    nvs_close(h);
}

static bool back_hint_seen(void)
{
    nvs_handle_t h;
    uint8_t v = 0;

    if (nvs_open("app", NVS_READONLY, &h) != ESP_OK) return false;
    nvs_get_u8(h, "sys_okh", &v);
    nvs_close(h);
    return v != 0;
}

static void back_hint_mark(void)
{
    nvs_handle_t h;

    if (nvs_open("app", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "sys_okh", 1);
    nvs_commit(h);
    nvs_close(h);
}

static void mark_dirty(void)
{
    s_dirty = true;
    s_sync_left = 8;
}

static void sync_farm(void)
{
    uint32_t before = s_farm.last_sec;

    app_farm_advance(&s_farm, now_sec());
    if (s_farm.last_sec != before) s_dirty = true;
    if (s_dirty && s_farm.last_sec - s_saved_sec >= 20) save_nvs();
}

static lv_obj_t *px(lv_obj_t *p, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    ui_pixel_strip_theme(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    return o;
}

static void rrect(lv_layer_t *layer, int x, int y, int w, int h, int r, uint32_t c)
{
    lv_draw_rect_dsc_t rd;
    lv_area_t a;

    if (w < 2 || h < 2) return;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(c);
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = r;
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + h - 1;
    lv_draw_rect(layer, &rd, &a);
}

static void oval(lv_layer_t *layer, int x, int y, int w, int h, uint32_t c)
{
    int r = w < h ? w : h;
    rrect(layer, x, y, w, h, r / 2, c);
}

static void draw_txt(lv_layer_t *layer, int x, int y, int w, int h,
                    const char *s, uint32_t c, lv_text_align_t al)
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
    d.align = al;
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + h - 1;
    lv_draw_label(layer, &d, &a);
}

static void draw_img(lv_layer_t *layer, const lv_image_dsc_t *src, int x, int y)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;
    int w, h;

    if (!src) return;
    w = src->header.w;
    h = src->header.h;
    lv_draw_image_dsc_init(&d);
    d.src = src;
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + h - 1;
    lv_draw_image(layer, &d, &a);
}

static const lv_image_dsc_t *crop_img(int crop, int stage)
{
    if (crop < 0 || crop >= APP_FARM_CROP_N) crop = 0;
    if (stage == APP_FARM_ST_SEED) return &app_farm_crop_img[APP_FARM_IMG_SEED];
    if (stage == APP_FARM_ST_GROW) return &app_farm_crop_img[APP_FARM_IMG_SPROUT];
    if (stage == APP_FARM_ST_DEAD) return &app_farm_crop_img[APP_FARM_IMG_DEAD];
    return &app_farm_crop_img[APP_FARM_IMG_CARROT + crop];
}

static const char *crop_name(int id)
{
    if (id < 0 || id >= APP_FARM_CROP_N) return "";
    return app_str((app_str_id_t)(APP_STR_FARM_C0 + id));
}

static void fmt_dur(uint32_t sec, char *out, size_t n)
{
    uint32_t m, h;

    if (sec < 90) {
        snprintf(out, n, "%s", app_str(APP_STR_FARM_SOON));
        return;
    }
    m = (sec + 30u) / 60u;
    if (m < 60) {
        snprintf(out, n, app_str(APP_STR_FARM_DUR_M), (int)m);
        return;
    }
    h = m / 60u;
    m %= 60u;
    if (m == 0) snprintf(out, n, app_str(APP_STR_FARM_DUR_H), (int)h);
    else snprintf(out, n, app_str(APP_STR_FARM_DUR_HM), (int)h, (int)m);
}

static void fmt_when(uint32_t sec, char *out, size_t n)
{
    time_t t;
    struct tm tm;

    if (sec < 90 || time(NULL) < (time_t)1700000000) {
        fmt_dur(sec, out, n);
        return;
    }
    t = time(NULL) + (time_t)sec;
    localtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void hint_eta(char *out, size_t n, int crop, uint32_t sec)
{
    char when[24];

    fmt_when(sec, when, sizeof(when));
    snprintf(out, n, app_str(APP_STR_FARM_ETA_AT), crop_name(crop), when);
}

static int nearest_eta(int *crop, uint32_t *sec)
{
    int i, n, best = -1;
    uint32_t eta, min = 0xFFFFFFFFu;

    n = app_farm_plot_n((int)s_farm.level);
    for (i = 0; i < n; i++) {
        eta = app_farm_plot_eta_sec(&s_farm.plots[i]);
        if (eta == 0 || eta >= min) continue;
        min = eta;
        best = i;
    }
    if (best < 0) return -1;
    if (crop) *crop = (int)s_farm.plots[best].crop;
    if (sec) *sec = min;
    return best;
}

static void flash_for(const char *s, int tone, int ticks)
{
    s_flash_next[0] = 0;
    snprintf(s_flash, sizeof(s_flash), "%s", s ? s : "");
    s_flash_left = ticks;
    if (tone >= 0) app_tone_play(tone);
}

static void flash_after(const char *s, int tone, int ticks)
{
    snprintf(s_flash_next, sizeof(s_flash_next), "%s", s ? s : "");
    s_flash_next_tone = tone;
    s_flash_next_ticks = ticks;
}

static void note_level(uint8_t lv0)
{
    if (s_farm.level <= lv0) return;
    snprintf(s_line, sizeof(s_line), app_str(APP_STR_FARM_DID_LV), (int)s_farm.level);
    if (s_flash_left > 0 && s_flash[0]) flash_after(s_line, APP_TONE_CHIME, 10);
    else flash_for(s_line, APP_TONE_CHIME, 10);
}

static const char *net_busy_tip(void)
{
    switch (s_wait) {
    case WAIT_REG:
        return app_str(APP_STR_FARM_REGING);
    case WAIT_SYNC:
    case WAIT_PULL:
        return app_str(APP_STR_FARM_SYNCING);
    case WAIT_FRIENDS:
    case WAIT_RANK:
    case WAIT_INBOX:
    case WAIT_ADD:
    case WAIT_REMOVE:
    case WAIT_REPLY:
        return app_str(APP_STR_FARM_LOAD);
    case WAIT_VISIT:
        return app_str(APP_STR_FARM_ENTERING);
    case WAIT_STEAL:
        return app_str(APP_STR_FARM_STEALING);
    case WAIT_HELP:
        if (s_plot_sel == APP_FARM_ACT_WATER)
            return app_str(APP_STR_FARM_WATERING);
        if (s_plot_sel == APP_FARM_ACT_WEED)
            return app_str(APP_STR_FARM_WEEDING);
        if (s_plot_sel == APP_FARM_ACT_PEST)
            return app_str(APP_STR_FARM_PESTING);
        return app_str(APP_STR_FARM_LOAD);
    default:
        return app_str(APP_STR_FARM_LOOK);
    }
}

static void flash_wait(void)
{
    flash_for(net_busy_tip(), APP_TONE_BEEP, 8);
}

static bool net_started(bool ok, int wait, bool warn)
{
    if (!ok) {
        if (warn) {
            flash_for(app_str(app_farm_net_busy() ? APP_STR_FARM_BUSY
                                                  : APP_STR_FARM_FAIL),
                      APP_TONE_BEEP, 8);
        }
        return false;
    }
    s_wait = wait;
    if (warn) flash_wait();
    return true;
}

static const char *net_fail_tip(int wait, const char *d)
{
    if (d && strcmp(d, "wifi") == 0) return app_str(APP_STR_FARM_NEED_WIFI);
    if (d && strcmp(d, "host") == 0) return app_str(APP_STR_FARM_NEED_HOST);
    if (d && strcmp(d, "cool") == 0) return app_str(APP_STR_FARM_COOL);
    if (d && strcmp(d, "limit") == 0) return app_str(APP_STR_FARM_LIMIT);
    if (d && strcmp(d, "ripe") == 0) return app_str(APP_STR_FARM_NO_STEAL);
    if (d && strcmp(d, "none") == 0) {
        return app_str(wait == WAIT_RANDOM ? APP_STR_FARM_NO_RAND
                                           : APP_STR_FARM_NONE);
    }
    if (wait == WAIT_REG) return app_str(APP_STR_FARM_REG_FAIL);
    if (wait == WAIT_PULL || wait == WAIT_SYNC) return app_str(APP_STR_FARM_SYNC_FAIL);
    if (wait == WAIT_FRIENDS || wait == WAIT_RANK || wait == WAIT_INBOX) {
        return app_str(APP_STR_FARM_LOAD_FAIL);
    }
    return app_str(APP_STR_FARM_FAIL);
}

static void flash_res(app_farm_res_t r, uint16_t coins)
{
    switch (r) {
    case APP_FARM_OK:
        if (coins) {
            snprintf(s_line, sizeof(s_line), app_str(APP_STR_FARM_GOT), (int)coins);
            flash_for(s_line, APP_TONE_CHIME, 10);
        } else {
            flash_for(app_str(APP_STR_FARM_OK), APP_TONE_BEEP, 8);
        }
        break;
    case APP_FARM_NO_SEED: flash_for(app_str(APP_STR_FARM_NO_SEED), APP_TONE_BEEP, 8); break;
    case APP_FARM_NO_COIN: flash_for(app_str(APP_STR_FARM_NO_COIN), APP_TONE_BEEP, 8); break;
    case APP_FARM_LOCKED: flash_for(app_str(APP_STR_FARM_LOCK), APP_TONE_BEEP, 8); break;
    case APP_FARM_NEED_EMPTY: flash_for(app_str(APP_STR_FARM_NEED_EMPTY), APP_TONE_BEEP, 8); break;
    case APP_FARM_NEED_CROP: flash_for(app_str(APP_STR_FARM_EMPTY), APP_TONE_BEEP, 8); break;
    case APP_FARM_NEED_DRY: flash_for(app_str(APP_STR_FARM_NEED_WATER), APP_TONE_BEEP, 8); break;
    case APP_FARM_NEED_WEED: flash_for(app_str(APP_STR_FARM_NEED_WEED), APP_TONE_BEEP, 8); break;
    case APP_FARM_NEED_PEST: flash_for(app_str(APP_STR_FARM_NEED_PEST), APP_TONE_BEEP, 8); break;
    case APP_FARM_NEED_RIPE: flash_for(app_str(APP_STR_FARM_NEED_RIPE), APP_TONE_BEEP, 8); break;
    case APP_FARM_HAS_HAZ: flash_for(app_str(APP_STR_FARM_HAS_HAZ), APP_TONE_BEEP, 8); break;
    case APP_FARM_LOCKED_CROP: flash_for(app_str(APP_STR_FARM_LOCKED_CROP), APP_TONE_BEEP, 8); break;
    case APP_FARM_SELF: flash_for(app_str(APP_STR_FARM_SELF), APP_TONE_BEEP, 8); break;
    case APP_FARM_COOL: flash_for(app_str(APP_STR_FARM_COOL), APP_TONE_BEEP, 8); break;
    case APP_FARM_LIMIT: flash_for(app_str(APP_STR_FARM_LIMIT), APP_TONE_BEEP, 8); break;
    case APP_FARM_BUSY: flash_for(app_str(APP_STR_FARM_BUSY), APP_TONE_BEEP, 8); break;
    default: flash_for(app_str(APP_STR_FARM_FAIL), APP_TONE_BEEP, 8); break;
    }
}

static bool focused(void)
{
    return s_menu >= 0;
}

static bool wifi_up(void)
{
    return bsp_wifi_enabled() && bsp_wifi_state() == BSP_WIFI_CONNECTED;
}

static void close_menu(void)
{
    s_menu = -1;
    s_sub = 0;
    s_plot_sel = -1;
    s_seed_pick = false;
    s_friend_act = -1;
    s_inbox_pick = false;
    s_confirm = CONF_NONE;
    if (s_wait != WAIT_REMOVE) s_confirm_id = 0;
}

static int seed_list(uint8_t *out, int max)
{
    int n = 0, i;

    for (i = 0; i < APP_FARM_CROP_N && n < max; i++) {
        if (s_farm.seeds[i] > 0 && app_farm_crop_open(&s_farm, i)) {
            if (out) out[n] = (uint8_t)i;
            n++;
        }
    }
    return n;
}

static int shop_n(void)
{
    return APP_FARM_CROP_N;
}

static int steal_rows(void)
{
    if (s_steal_cat == STEAL_RAND) return 1;
    if (s_steal_cat == STEAL_FRI) return s_friend_n + 1;
    if (s_steal_cat == STEAL_MAIL) return s_mail_n > 0 ? s_mail_n : 1;
    if (s_rank_n <= 0) return 1;
    return s_rank_n > STEAL_RANK_MAX ? STEAL_RANK_MAX : s_rank_n;
}

static int inner_n(void)
{
    if (s_visit) {
        if (s_visit_ro) return app_farm_plot_n((int)s_guest.level);
        if (s_plot_sel >= 0) return app_farm_plot_n((int)s_guest.level);
        return VISIT_TOOL_N;
    }
    if (s_plot_sel >= 0) {
        return app_farm_plot_n((int)s_farm.level);
    }
    if (s_sel == TAB_HOME) {
        if (s_seed_pick) {
            int n = seed_list(NULL, APP_FARM_CROP_N);
            return n > 0 ? n : 1;
        }
        return HOME_N;
    }
    if (s_sel == TAB_BAG) {
        if (s_bag_cat == BAG_ITM) {
            int n = seed_list(NULL, APP_FARM_CROP_N);
            return n > 0 ? n : 1;
        }
        return shop_n();
    }
    if (s_sel == TAB_STEAL) {
        if (s_friend_act >= 0) return 2;
        return steal_rows();
    }
    if (s_sel == TAB_SET) return SET_N;
    return 0;
}

static void paint(void);

static void hide_obj(lv_obj_t *o, bool hid)
{
    if (!o) return;
    if (hid) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void clock_txt(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;

    if (t < (time_t)1700000000) {
        snprintf(out, n, "--:--");
        return;
    }
    localtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void draw_wifi(lv_layer_t *layer, int x, int y, uint32_t c)
{
    oval(layer, x + 4, y + 8, 4, 4, c);
    rrect(layer, x + 1, y + 4, 10, 2, 1, c);
    rrect(layer, x, y, 12, 2, 1, c);
}

static void draw_landscape(lv_layer_t *layer, int bx, int by, int h)
{
    int i;

    rrect(layer, bx, by, LCD_W, h, 0, COL_SKY2);
    rrect(layer, bx, by + 28, LCD_W, h - 28, 0, COL_GRASS);
    oval(layer, bx - 22, by + 12, 94, 54, 0x87C83A);
    oval(layer, bx + 38, by + 18, 104, 48, 0x91D044);
    oval(layer, bx + 126, by + 10, 126, 58, 0x72B92E);
    oval(layer, bx + 18, by + 8, 26, 12, COL_WHITE);
    oval(layer, bx + 34, by + 4, 32, 17, COL_WHITE);
    oval(layer, bx + 55, by + 9, 24, 11, COL_WHITE);
    oval(layer, bx + 188, by + 7, 22, 10, COL_WHITE);
    oval(layer, bx + 201, by + 3, 28, 15, COL_WHITE);
    for (i = 0; i < 8; i++) {
        int x = bx + 10 + i * 31;
        int y = by + h - 10 - (i & 1) * 4;
        rrect(layer, x, y, 2, 8, 1, COL_GRASS2);
        rrect(layer, x + 3, y + 3, 2, 5, 1, COL_LEAF);
    }
}

static void draw_panel_bg(lv_layer_t *layer, int bx, int by, int h)
{
    int i;

    rrect(layer, bx, by, LCD_W, h, 0, COL_BG);
    oval(layer, bx - 28, by - 20, 100, 62, 0xB9DC58);
    oval(layer, bx + 162, by - 18, 112, 58, 0x96CB3A);
    for (i = 0; i < 6; i++) {
        oval(layer, bx + 14 + i * 42, by + h - 12 - (i & 1) * 5,
             5, 5, (i & 1) ? COL_CORAL : COL_WHITE);
    }
}

static int txt_w(const char *s)
{
    lv_point_t sz = {0, 0};

    if (!s || !s[0]) return 0;
    lv_text_get_size(&sz, s, ui_pixel_font_14(), 0, 0, LCD_W, LV_TEXT_FLAG_EXPAND);
    return (int)sz.x;
}

static void name_fit(char *dst, size_t n, const char *src, int max_w)
{
    size_t i = 0, prev = 0, len;

    if (!dst || n == 0) return;
    dst[0] = 0;
    if (!src || max_w < 6) return;
    len = strlen(src);
    while (i < len && i + 1 < n) {
        unsigned char c = (unsigned char)src[i];
        size_t adv = 1;

        if (c >= 0xF0) adv = 4;
        else if (c >= 0xE0) adv = 3;
        else if (c >= 0xC0) adv = 2;
        if (i + adv > len || i + adv >= n) break;
        memcpy(dst, src, i + adv);
        dst[i + adv] = 0;
        if (txt_w(dst) > max_w) {
            dst[prev] = 0;
            break;
        }
        prev = i + adv;
        i += adv;
    }
}

static void draw_header(lv_layer_t *layer, int bx, int by)
{
    char clk[8], bat[12], lvbuf[8], nm[20];
    int soc = bsp_battery_soc();
    const char *title;
    int clk_on = app_time_ready(), wifi_on = bsp_wifi_enabled();
    int y = by + 5, lv, name_w, coin_w;
    unsigned long coins;

    title = s_visit ? (s_guest.name[0] ? s_guest.name : app_str(APP_STR_FARM_VISITING))
                    : (s_farm.name[0] ? s_farm.name : app_str(APP_STR_FARM));
    lv = s_visit ? (int)s_guest.level : (int)s_farm.level;
    coins = (unsigned long)(s_visit ? s_guest.coins : s_farm.coins);

    rrect(layer, bx + 5, y, 230, 38, 15, COL_WOOD2);
    rrect(layer, bx + 7, y + 2, 226, 34, 13, COL_CREAM);
    oval(layer, bx + 12, y + 7, 24, 24, COL_GOLD);
    oval(layer, bx + 17, y + 12, 14, 14, 0xFFAA16);

    name_w = 82;
    name_fit(nm, sizeof(nm), title, name_w);
    draw_txt(layer, bx + 42, y + 4, name_w, 14, nm, COL_INK, LV_TEXT_ALIGN_LEFT);

    snprintf(lvbuf, sizeof(lvbuf), "Lv%d", lv);
    snprintf(s_line, sizeof(s_line), "$%lu", coins);
    coin_w = txt_w(s_line) + 14;
    if (coin_w < 46) coin_w = 46;
    if (coin_w > 70) coin_w = 70;
    rrect(layer, bx + 42, y + 20, 38, 14, 7, COL_CORAL);
    draw_txt(layer, bx + 42, y + 20, 38, 14, lvbuf, COL_WHITE, LV_TEXT_ALIGN_CENTER);
    rrect(layer, bx + 84, y + 20, coin_w, 14, 7, COL_COIN);
    draw_txt(layer, bx + 84, y + 20, coin_w, 14, s_line, COL_COINX,
             LV_TEXT_ALIGN_CENTER);

    if (wifi_on) {
        uint32_t c = bsp_wifi_state() == BSP_WIFI_CONNECTED ? COL_LEAF : COL_MUTE;
        draw_wifi(layer, bx + 166, y + 8, c);
    }

    if (clk_on) {
        clock_txt(clk, sizeof(clk));
        draw_txt(layer, bx + 181, y + 5, 44, 14, clk, COL_INK, LV_TEXT_ALIGN_RIGHT);
    }
    if (soc < 0) snprintf(bat, sizeof(bat), "--");
    else snprintf(bat, sizeof(bat), "%d%%", soc > 100 ? 100 : soc);
    rrect(layer, bx + 171, y + 21, 54, 13, 7, COL_GEM);
    draw_txt(layer, bx + 171, y + 20, 54, 14, bat, COL_GEMX, LV_TEXT_ALIGN_CENTER);
}

static void draw_crop(lv_layer_t *layer, int cx, int cy, int crop, int stage)
{
    draw_img(layer, crop_img(crop, stage), cx - 20, cy - 16);
}

static void draw_tool(lv_layer_t *layer, int id, int x, int y, bool on)
{
    (void)on;
    if (id < 0 || id > APP_FARM_TOOL_CUT) id = 0;
    if (id == APP_FARM_TOOL_PLANT) {
        draw_img(layer, &app_farm_crop_img[APP_FARM_IMG_SPROUT], x - 6, y - 2);
        return;
    }
    draw_img(layer, &app_farm_ico_img[APP_FARM_ICO_SEED + id], x, y);
}

static void draw_lock(lv_layer_t *layer, int x, int y, uint32_t c)
{
    rrect(layer, x + 3, y, 2, 7, 1, c);
    rrect(layer, x + 9, y, 2, 7, 1, c);
    rrect(layer, x + 3, y, 8, 2, 1, c);
    rrect(layer, x, y + 6, 14, 11, 3, c);
    oval(layer, x + 5, y + 9, 4, 4, COL_WHITE);
}

static void draw_next(lv_layer_t *layer, int x, int y)
{
    rrect(layer, x, y + 11, 20, 6, 3, COL_LEAF);
    rrect(layer, x + 14, y + 6, 6, 16, 3, COL_LEAF);
    rrect(layer, x + 18, y + 9, 6, 10, 3, COL_LEAF);
}

static void draw_plot(lv_layer_t *layer, const app_farm_plot_t *p, int x, int y,
                     int w, int h, bool lock, bool sel)
{
    if (sel) {
        rrect(layer, x - 3, y - 3, w + 6, h + 6, 14, COL_WHITE);
        rrect(layer, x - 1, y - 1, w + 2, h + 2, 12, COL_GOLD);
    }
    if (lock) {
        rrect(layer, x, y + 4, w, h - 2, 11, COL_DIRT2);
        rrect(layer, x + 2, y + 2, w - 4, h - 4, 10, COL_LOCK);
        draw_lock(layer, x + (w - 14) / 2, y + (h - 18) / 2, COL_MUTE);
        return;
    }
    rrect(layer, x, y + 4, w, h - 2, 12, COL_DIRT2);
    rrect(layer, x + 2, y + 1, w - 4, h - 5, 11, COL_DIRT);
    oval(layer, x + 8, y + 8, w - 16, h - 15, 0xA95A1C);
    if (p->stage == APP_FARM_ST_EMPTY) {
        if (p->stolen) {
            draw_txt(layer, x, y + h / 2 - 7, w, 14, "!",
                     COL_ALERT, LV_TEXT_ALIGN_CENTER);
        }
        return;
    }
    if (p->stage == APP_FARM_ST_DEAD) {
        draw_crop(layer, x + w / 2, y + h / 2 - 1, (int)p->crop,
                  APP_FARM_ST_DEAD);
        return;
    }
    draw_crop(layer, x + w / 2, y + h / 2 - 1, (int)p->crop, (int)p->stage);
    if (p->weed) draw_img(layer, &app_farm_haz_img[APP_FARM_HAZ_WEED], x, y + h - 20);
    if (p->pest) draw_img(layer, &app_farm_haz_img[APP_FARM_HAZ_PEST], x + w - 20, y);
    if (p->dry) draw_img(layer, &app_farm_haz_img[APP_FARM_HAZ_DRY], x + w - 20, y + h - 20);
}

static void draw_plots(lv_layer_t *layer, int bx, int by, int top, int field_h,
                      const app_farm_plot_t *plots, int open, int mark)
{
    int i, col, row, x, y, n = APP_FARM_PLOT_N;
    int pw = 60, ph = 38, gapx = 12, gapy = 6;
    int ox, oy;

    if (field_h < 4 * ph + 3 * gapy) gapy = 3;
    ox = (LCD_W - PLOT_COLS * pw - (PLOT_COLS - 1) * gapx) / 2;
    oy = (field_h - 4 * ph - 3 * gapy) / 2;
    if (oy < 3) oy = 3;
    for (i = 0; i < n; i++) {
        col = i % PLOT_COLS;
        row = i / PLOT_COLS;
        x = bx + ox + col * (pw + gapx);
        y = by + top + oy + row * (ph + gapy);
        draw_plot(layer, &plots[i], x, y, pw, ph, i >= open, mark == i);
    }
}

static void draw_xp(lv_layer_t *layer, int bx, int by)
{
    int need = app_farm_xp_need((int)s_farm.level);
    int xp = (int)s_farm.xp;
    int x = bx + 12, y = by + HDR_H, w = 216, h = 7, fill;

    rrect(layer, x, y, w, h, 4, COL_WOOD2);
    rrect(layer, x + 1, y + 1, w - 2, h - 2, 3, COL_COIN);
    if (need <= 0) fill = w;
    else {
        if (xp > need) xp = need;
        fill = need ? (w * xp) / need : 0;
    }
    if (fill > w) fill = w;
    if (fill >= 6) rrect(layer, x + 1, y + 1, fill - 2, h - 2, 3, COL_LEAF);
    if (need <= 0) snprintf(s_line, sizeof(s_line), "MAX");
    else snprintf(s_line, sizeof(s_line), "%d/%d", (int)s_farm.xp, need);
    draw_txt(layer, x, y - 4, w, 14, s_line, COL_INK, LV_TEXT_ALIGN_CENTER);
}

static void draw_home_tools(lv_layer_t *layer, int bx, int by)
{
    int i, x, cx;

    rrect(layer, bx, by + PAGE_H, LCD_W, NAV_H, 0, COL_WOOD2);
    rrect(layer, bx + 3, by + PAGE_H + 2, LCD_W - 6, NAV_H - 5, 16, COL_CREAM);
    for (i = 0; i < HOME_N; i++) {
        bool lit = focused() &&
                   (s_seed_pick ? i == APP_FARM_TOOL_SEED :
                    (s_plot_sel >= 0 ? i == s_plot_sel : s_sub == i));
        x = bx + 4 + i * 39;
        cx = x + 19;
        if (lit) {
            oval(layer, cx - 18, by + PAGE_H + 4, 36, 36, COL_GOLD);
            oval(layer, cx - 15, by + PAGE_H + 7, 30, 30, COL_WHITE);
        }
        draw_tool(layer, i, cx - 14, by + PAGE_H + 8, lit);
    }
}

static void draw_seed_popup(lv_layer_t *layer, int bx, int by)
{
    uint8_t ids[APP_FARM_CROP_N];
    int n = seed_list(ids, APP_FARM_CROP_N);
    int i, cell = 36, bw, bh = 48, x0, y0, crop, sel;

    if (n <= 0) return;
    if (s_sub < 0) s_sub = 0;
    if (s_sub >= n) s_sub = n - 1;
    bw = n * cell + 12;
    if (bw > 228) bw = 228;
    x0 = bx + (LCD_W - bw) / 2;
    y0 = by + HDR_H + XP_H + 82;
    rrect(layer, x0 - 3, y0 - 3, bw + 6, bh + 6, 14, COL_WOOD2);
    rrect(layer, x0, y0, bw, bh, 11, COL_CREAM);
    for (i = 0; i < n; i++) {
        crop = (int)ids[i];
        sel = (i == s_sub);
        x0 = bx + (LCD_W - bw) / 2 + 6 + i * cell;
        if (sel) oval(layer, x0 + 3, y0 + 5, 34, 34, COL_GOLD);
        draw_img(layer, crop_img(crop, APP_FARM_ST_RIPE), x0, y0 + 8);
    }
    crop = (int)ids[s_sub];
    {
        char dur[16];
        const app_farm_crop_info_t *c = app_farm_crop(crop);

        fmt_dur(c ? c->grow_sec : 0, dur, sizeof(dur));
        snprintf(s_line, sizeof(s_line), "%s x%d  %s",
                 crop_name(crop), app_farm_seed_n(&s_farm, crop), dur);
    }
    draw_txt(layer, bx + 8, y0 + bh + 7, 224, 14, s_line, COL_INK,
             LV_TEXT_ALIGN_CENTER);
}

static void draw_flash(lv_layer_t *layer, int bx, int by)
{
    int y = PAGE_H - MSG_H + 3;

    if (s_flash_left <= 0 || !s_flash[0]) return;
    rrect(layer, bx + 8, by + y, 224, 24, 12, COL_WOOD2);
    rrect(layer, bx + 11, by + y - 2, 218, 22, 11, COL_CREAM);
    draw_txt(layer, bx + 10, by + y + 3, 220, 14, s_flash, COL_ALERT,
             LV_TEXT_ALIGN_CENTER);
}

static void draw_hint(lv_layer_t *layer, int bx, int by, const char *text)
{
    int y = by + PAGE_H - MSG_H + 3;

    if (s_flash_left > 0 || !text || !text[0]) return;
    rrect(layer, bx + 12, y, 216, 22, 11, COL_CREAM);
    draw_txt(layer, bx + 16, y + 3, 208, 14, text, COL_MUTE,
             LV_TEXT_ALIGN_CENTER);
}

static void draw_confirm(lv_layer_t *layer, int bx, int by)
{
    const char *title;

    if (s_confirm == CONF_NONE) return;
    title = app_str(s_confirm == CONF_RESET ? APP_STR_FARM_RESET_ASK
                                            : APP_STR_FARM_DELETE_ASK);
    rrect(layer, bx + 16, by + 98, 208, 76, 16, COL_WOOD2);
    rrect(layer, bx + 20, by + 94, 200, 72, 14, COL_CREAM);
    draw_txt(layer, bx + 28, by + 111, 184, 18, title, COL_ALERT,
             LV_TEXT_ALIGN_CENTER);
    draw_txt(layer, bx + 28, by + 137, 184, 16,
             app_str(APP_STR_FARM_CONFIRM_HINT), COL_INK, LV_TEXT_ALIGN_CENTER);
}

static const char *status_text(void)
{
    if (s_flash_left > 0 && s_flash[0]) return s_flash;
    if (app_ota_state() == APP_OTA_APPLYING) {
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_OTA_APPLYING),
                 app_ota_progress());
        return s_line;
    }
    if (app_ota_prompt()) {
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_OTA_NEW),
                 app_ota_new_ver());
        return s_line;
    }
    if (bsp_ble_state() == BSP_BLE_PAIRING) {
        snprintf(s_line, sizeof(s_line), "%s %06lu", app_str(APP_STR_BT_CODE),
                 (unsigned long)bsp_ble_passkey());
        return s_line;
    }
    if (app_farm_net_busy()) return net_busy_tip();
    return "";
}

static void page_home(lv_layer_t *layer, int bx, int by)
{
    int mark = -1;
    int top = HDR_H + XP_H;
    int field = PAGE_H - MSG_H - top;

    draw_landscape(layer, bx, by, LCD_H);
    draw_header(layer, bx, by);
    draw_xp(layer, bx, by);
    if (s_plot_sel >= 0) mark = s_sub;
    draw_plots(layer, bx, by, top, field, s_farm.plots,
               app_farm_plot_n((int)s_farm.level), mark);
    draw_home_tools(layer, bx, by);
    if (s_seed_pick) {
        draw_seed_popup(layer, bx, by);
        draw_hint(layer, bx, by, app_str(APP_STR_FARM_PICK_SEED));
    } else if (s_plot_sel >= 0) {
        const app_farm_plot_t *p = &s_farm.plots[s_sub];
        uint32_t eta = app_farm_plot_eta_sec(p);

        if (eta) {
            hint_eta(s_line, sizeof(s_line), (int)p->crop, eta);
            draw_hint(layer, bx, by, s_line);
        } else {
            draw_hint(layer, bx, by, app_str(APP_STR_FARM_PICK_PLOT));
        }
    } else if (focused()) {
        static const app_str_id_t TN[HOME_N] = {
            APP_STR_FARM_SEED, APP_STR_FARM_PLANT, APP_STR_FARM_WATER,
            APP_STR_FARM_WEED, APP_STR_FARM_PEST, APP_STR_FARM_CUT
        };
        snprintf(s_line, sizeof(s_line), "%s  %s", app_str(TN[s_sub]),
                 app_str(APP_STR_FARM_OK_SELECT));
        draw_hint(layer, bx, by, s_line);
    } else if (app_farm_care_need(&s_farm) == APP_FARM_TOOL_CUT) {
        draw_hint(layer, bx, by, app_str(APP_STR_FARM_CARE_CUT));
    } else {
        int crop;
        uint32_t eta;

        if (nearest_eta(&crop, &eta) >= 0) {
            hint_eta(s_line, sizeof(s_line), crop, eta);
            draw_hint(layer, bx, by, s_line);
        } else {
            draw_hint(layer, bx, by, app_str(APP_STR_FARM_ENTER_HINT));
        }
    }
}

static void page_visit(lv_layer_t *layer, int bx, int by)
{
    static const int ICO[VISIT_TOOL_N] = {
        APP_FARM_ICO_HAND, APP_FARM_ICO_WATER, APP_FARM_ICO_WEED,
        APP_FARM_ICO_PEST, APP_FARM_ICO_STEAL
    };
    static const app_str_id_t TN[VISIT_TOOL_N] = {
        APP_STR_FARM_HAND, APP_STR_FARM_WATER, APP_STR_FARM_WEED,
        APP_STR_FARM_PEST, APP_STR_FARM_NEXT
    };
    int i, mark = -1, act;

    draw_landscape(layer, bx, by, LCD_H);
    draw_header(layer, bx, by);
    if (s_visit_ro) {
        mark = s_sub;
    } else if (s_plot_sel >= 0 && s_sub >= 0 &&
        app_farm_guest_can(&s_guest, s_sub, s_plot_sel)) {
        mark = s_sub;
    }
    draw_plots(layer, bx, by, HDR_H, PAGE_H - MSG_H - HDR_H, s_guest.plots,
               app_farm_plot_n((int)s_guest.level), mark);
    if (s_visit_ro) {
        draw_hint(layer, bx, by, app_str(APP_STR_FARM_VIEW_ONLY));
        return;
    }

    act = s_plot_sel >= 0 ? s_plot_sel : s_sub;
    rrect(layer, bx, by + PAGE_H, LCD_W, NAV_H, 0, COL_WOOD2);
    rrect(layer, bx + 3, by + PAGE_H + 2, LCD_W - 6, NAV_H - 5, 16, COL_CREAM);
    for (i = 0; i < VISIT_TOOL_N; i++) {
        int x = bx + 4 + i * 47;
        int cx = x + 22;
        bool on = (i == act);
        if (on) oval(layer, cx - 18, by + PAGE_H + 4, 36, 36, COL_GOLD);
        if (i == VISIT_NEXT) {
            draw_next(layer, cx - 12, by + PAGE_H + 7);
        } else {
            draw_img(layer, &app_farm_ico_img[ICO[i]],
                     cx - app_farm_ico_img[ICO[i]].header.w / 2, by + PAGE_H + 2);
        }
        if (i < APP_FARM_ACT_N) {
            snprintf(s_line, sizeof(s_line), "%u", (unsigned)s_quota[i]);
            draw_txt(layer, x, by + PAGE_H + 26, 44, 16, s_line,
                     COL_INK, LV_TEXT_ALIGN_CENTER);
        }
    }
    if (s_plot_sel >= 0) {
        draw_hint(layer, bx, by, app_str(APP_STR_FARM_PICK_PLOT));
    } else if (act >= 0 && act < VISIT_TOOL_N) {
        snprintf(s_line, sizeof(s_line), "%s  %s", app_str(TN[act]),
                 app_str(APP_STR_FARM_OK_SELECT));
        draw_hint(layer, bx, by, s_line);
    }
}

static void draw_cat_tabs(lv_layer_t *layer, int bx, int by,
                         const app_str_id_t *labs, int n, int cur)
{
    int i, tw = n >= 4 ? 49 : (n == 2 ? 98 : 68);

    for (i = 0; i < n; i++) {
        int x = bx + 10 + i * (tw + (n >= 4 ? 9 : 24));
        bool on = (i == cur);
        rrect(layer, x, by + HDR_H + 6, tw, 25, 8, COL_WOOD2);
        rrect(layer, x + 2, by + HDR_H + 8, tw - 4, 21, 7,
              on ? COL_GOLD : COL_CREAM);
        draw_txt(layer, x, by + HDR_H + 10, tw, 16, app_str(labs[i]),
                 COL_INK, LV_TEXT_ALIGN_CENTER);
    }
}

static void page_bag(lv_layer_t *layer, int bx, int by)
{
    static const app_str_id_t CATS[2] = { APP_STR_FARM_BAG, APP_STR_FARM_SHOP };
    uint8_t ids[APP_FARM_CROP_N];
    int n, i, start, vis = 5;

    draw_panel_bg(layer, bx, by, PAGE_H);
    draw_header(layer, bx, by);
    draw_cat_tabs(layer, bx, by, CATS, 2, s_bag_cat);
    draw_hint(layer, bx, by, app_str(focused() ? APP_STR_FARM_CATEGORY_HINT
                                               : APP_STR_FARM_ENTER_HINT));
    if (s_bag_cat == BAG_ITM) n = seed_list(ids, APP_FARM_CROP_N);
    else n = shop_n();
    if (n == 0) {
        draw_txt(layer, bx + 16, by + HDR_H + 80, 208, 16, app_str(APP_STR_FARM_EMPTY),
                 COL_MUTE, LV_TEXT_ALIGN_CENTER);
        return;
    }
    start = focused() ? s_sub - 2 : 0;
    if (start < 0) start = 0;
    if (start > n - vis) start = n > vis ? n - vis : 0;
    for (i = 0; i < vis; i++) {
        int idx = start + i, y, crop;
        bool sel, lock;
        const app_farm_crop_info_t *c;

        if (idx >= n) break;
        y = by + HDR_H + 40 + i * 32;
        sel = focused() && idx == s_sub;
        crop = (s_bag_cat == BAG_ITM) ? ids[idx] : idx;
        c = app_farm_crop(crop);
        lock = s_bag_cat == BAG_SHOP && !app_farm_crop_open(&s_farm, crop);
        rrect(layer, bx + 14, y + 2, 212, 30, 11, COL_WOOD2);
        rrect(layer, bx + 16, y, 208, 28, 10, sel ? COL_GOLD : COL_CARD);
        draw_img(layer, crop_img(crop, APP_FARM_ST_RIPE), bx + 18, y - 2);
        if (s_bag_cat == BAG_ITM) {
            char dur[16];

            fmt_dur(c ? c->grow_sec : 0, dur, sizeof(dur));
            snprintf(s_line, sizeof(s_line), "%s  x%d  %s", crop_name(crop),
                     app_farm_seed_n(&s_farm, crop), dur);
        } else if (lock) {
            char dur[16];

            fmt_dur(c ? c->grow_sec : 0, dur, sizeof(dur));
            snprintf(s_line, sizeof(s_line), "%s  %s  Lv%d", crop_name(crop),
                     dur, c ? (int)c->unlock_lv : 0);
        } else {
            char dur[16];

            fmt_dur(c ? c->grow_sec : 0, dur, sizeof(dur));
            snprintf(s_line, sizeof(s_line), "%s  %s  $%u", crop_name(crop),
                     dur, c ? (unsigned)c->seed_cost : 0);
        }
        draw_txt(layer, bx + 62, y + 6, 154, 16, s_line,
                 lock ? COL_MUTE : COL_INK,
                 LV_TEXT_ALIGN_LEFT);
    }
}

static void sort_friends(void)
{
    int i, j;

    for (i = 0; i < s_friend_n - 1; i++) {
        for (j = i + 1; j < s_friend_n; j++) {
            if (s_friends[j].coins > s_friends[i].coins ||
                (s_friends[j].coins == s_friends[i].coins &&
                 s_friends[j].level > s_friends[i].level)) {
                app_farm_peer_t t = s_friends[i];
                s_friends[i] = s_friends[j];
                s_friends[j] = t;
            }
        }
    }
}

static void peer_line(char *out, size_t n, const app_farm_peer_t *p, const char *tag)
{
    char fitted[20];
    const char *nm;

    name_fit(fitted, sizeof(fitted), p->name[0] ? p->name : "-", 76);
    nm = fitted;

    if (tag && tag[0]) {
        snprintf(out, n, "%s  %s", nm, tag);
    } else {
        snprintf(out, n, "%s  Lv%d  $%lu", nm, (int)p->level,
                 (unsigned long)p->coins);
    }
}

static void page_steal(lv_layer_t *layer, int bx, int by)
{
    static const app_str_id_t CATS[STEAL_CAT_N] = {
        APP_STR_FARM_RAND, APP_STR_FARM_FRIEND, APP_STR_FARM_RANK,
        APP_STR_FARM_INBOX
    };
    int n, i, start, vis = 5;

    draw_panel_bg(layer, bx, by, PAGE_H);
    draw_header(layer, bx, by);
    draw_cat_tabs(layer, bx, by, CATS, STEAL_CAT_N, s_steal_cat);
    draw_hint(layer, bx, by, app_str(focused() ? APP_STR_FARM_STEAL_HINT
                                               : APP_STR_FARM_ENTER_HINT));

    if (!wifi_up()) {
        draw_txt(layer, bx + 16, by + HDR_H + 80, 208, 16, app_str(APP_STR_FARM_NEED_WIFI),
                 COL_MUTE, LV_TEXT_ALIGN_CENTER);
        return;
    }

    if (s_friend_act >= 0) {
        static const app_str_id_t ACT[2] = { APP_STR_FARM_ENTER, APP_STR_FARM_DEL };
        static const app_str_id_t INB[2] = { APP_STR_FARM_ACCEPT, APP_STR_FARM_DECLINE };
        const app_str_id_t *lab = s_inbox_pick ? INB : ACT;
        for (i = 0; i < 2; i++) {
            bool sel = s_sub == i;
            rrect(layer, bx + 14, by + HDR_H + 52 + i * 38, 212, 32, 11, COL_WOOD2);
            rrect(layer, bx + 16, by + HDR_H + 50 + i * 38, 208, 30, 10,
                  sel ? COL_GOLD : COL_CARD);
            draw_txt(layer, bx + 26, by + HDR_H + 56 + i * 38, 188, 16, app_str(lab[i]),
                     COL_INK, LV_TEXT_ALIGN_LEFT);
        }
        return;
    }

    if (s_steal_cat == STEAL_RAND) {
        bool sel = focused();
        rrect(layer, bx + 13, by + HDR_H + 63, 214, 42, 14, COL_WOOD2);
        rrect(layer, bx + 16, by + HDR_H + 60, 208, 40, 12,
              sel ? COL_GOLD : COL_CARD);
        draw_txt(layer, bx + 24, by + HDR_H + 72, 192, 16, app_str(APP_STR_FARM_RAND),
                 COL_INK, LV_TEXT_ALIGN_CENTER);
        return;
    }

    n = steal_rows();
    if ((s_steal_cat == STEAL_RANK && s_rank_n == 0) ||
        (s_steal_cat == STEAL_MAIL && s_mail_n == 0)) {
        uint8_t bit = s_steal_cat == STEAL_MAIL ? 4u : 2u;
        app_str_id_t empty = s_steal_cat == STEAL_MAIL
            ? APP_STR_FARM_NO_MAIL : APP_STR_FARM_NO_RANK;
        draw_txt(layer, bx + 16, by + HDR_H + 80, 208, 16,
                 (!(s_list_ok & bit) && (s_farm.token[0] || app_farm_net_busy()))
                     ? app_str(APP_STR_FARM_LOAD)
                     : app_str(empty),
                 COL_MUTE, LV_TEXT_ALIGN_CENTER);
        return;
    }
    start = (s_steal_cat == STEAL_RANK || focused()) ? s_sub - 2 : 0;
    if (start < 0) start = 0;
    if (start > n - vis) start = n > vis ? n - vis : 0;
    for (i = 0; i < vis; i++) {
        int idx = start + i, y;
        bool sel;
        const char *lab = s_line;

        if (idx >= n) break;
        y = by + HDR_H + 40 + i * 32;
        sel = focused() && idx == s_sub;
        rrect(layer, bx + 14, y + 2, 212, 30, 11, COL_WOOD2);
        rrect(layer, bx + 16, y, 208, 28, 10, sel ? COL_GOLD : COL_CARD);
        if (s_steal_cat == STEAL_FRI) {
            if (idx == s_friend_n) {
                lab = app_str(APP_STR_FARM_ADD);
            } else {
                peer_line(s_line, sizeof(s_line), &s_friends[idx], NULL);
            }
        } else if (s_steal_cat == STEAL_MAIL) {
            const app_farm_mail_t *m = &s_mail[idx];
            char fitted[20];
            const char *nm;
            const char *tag = app_str(APP_STR_FARM_INVITE);

            name_fit(fitted, sizeof(fitted), m->name[0] ? m->name : "-", 70);
            nm = fitted;
            if (m->kind == APP_FARM_MAIL_STEAL) tag = app_str(APP_STR_FARM_MAIL_STEAL);
            else if (m->kind == APP_FARM_MAIL_WATER) tag = app_str(APP_STR_FARM_MAIL_WATER);
            else if (m->kind == APP_FARM_MAIL_WEED) tag = app_str(APP_STR_FARM_MAIL_WEED);
            else if (m->kind == APP_FARM_MAIL_PEST) tag = app_str(APP_STR_FARM_MAIL_PEST);
            if (m->kind == APP_FARM_MAIL_STEAL && m->got) {
                snprintf(s_line, sizeof(s_line), "%s  %s -%u", nm, tag,
                         (unsigned)m->got);
            } else {
                snprintf(s_line, sizeof(s_line), "%s  %s", nm, tag);
            }
        } else {
            char body[56];

            peer_line(body, sizeof(body), &s_rank[idx], NULL);
            snprintf(s_line, sizeof(s_line), "%d. %s", idx + 1, body);
            lab = s_line;
        }
        draw_txt(layer, bx + 26, y + 6, 188, 16, lab,
                 COL_INK, LV_TEXT_ALIGN_LEFT);
    }
}

static void page_set(lv_layer_t *layer, int bx, int by)
{
    int n = SET_N, start, vis = 6, i;

    draw_panel_bg(layer, bx, by, PAGE_H);
    draw_header(layer, bx, by);
    draw_hint(layer, bx, by, app_str(focused() ? APP_STR_FARM_HOLD_BACK
                                               : APP_STR_FARM_ENTER_HINT));
    start = focused() ? s_sub - 3 : 0;
    if (start < 0) start = 0;
    if (start > n - vis) start = n > vis ? n - vis : 0;
    for (i = 0; i < vis; i++) {
        int idx = start + i, y;
        bool sel;
        const char *lab;

        if (idx >= n) break;
        y = by + HDR_H + 8 + i * 32;
        sel = focused() && idx == s_sub;
        rrect(layer, bx + 14, y + 2, 212, 30, 11, COL_WOOD2);
        rrect(layer, bx + 16, y, 208, 28, 10, sel ? COL_GOLD : COL_CARD);
        lab = app_str(SET_STR[idx]);
        if (idx == SET_LANG) {
            snprintf(s_line, sizeof(s_line), "%s  %s", lab, app_lang_name(app_lang()));
            lab = s_line;
        } else if (idx == SET_ID) {
            snprintf(s_line, sizeof(s_line), "%s  %lu", lab,
                     (unsigned long)s_farm.id);
            lab = s_line;
        } else if (idx == SET_NAME) {
            snprintf(s_line, sizeof(s_line), "%s  %s", lab,
                     s_farm.name[0] ? s_farm.name : app_str(APP_STR_FARM));
            lab = s_line;
        }
        draw_txt(layer, bx + 26, y + 6, 188, 16, lab,
                 COL_INK, LV_TEXT_ALIGN_LEFT);
    }
}

static void page_kb(lv_layer_t *layer, int bx, int by)
{
    const char *const *keys;
    const char *title, *head;
    int i, cols, n, kw, kh = 22, x0, y0 = by + 88;

    if (s_kb_mode == KB_HOST) {
        title = app_str(APP_STR_FARM_HOST);
        head = app_str(APP_STR_FARM_HOST_HINT);
    } else if (s_kb_mode == KB_NAME) {
        title = app_str(APP_STR_MEOW_NAME);
        head = app_str(APP_STR_MEOW_NAME_HINT);
    } else {
        title = app_str(APP_STR_FARM_ADD);
        head = app_str(APP_STR_FARM_ID_HINT);
    }

    if (s_kb_mode == KB_FRIEND) {
        keys = app_kb_id_keys();
        cols = KB_ID_COLS;
        n = KB_ID_N;
        kw = 52;
        x0 = bx + 16;
    } else {
        keys = app_kb_keys(s_kb_set);
        cols = KB_COLS;
        n = KB_N;
        kw = 36;
        x0 = bx + 12;
    }

    draw_panel_bg(layer, bx, by, LCD_H);
    rrect(layer, bx + 12, by + 7, 216, 44, 13, COL_WOOD2);
    rrect(layer, bx + 15, by + 9, 210, 40, 11, COL_CREAM);
    draw_txt(layer, bx + 16, by + 11, 208, 18, title,
             COL_INK, LV_TEXT_ALIGN_CENTER);
    draw_txt(layer, bx + 16, by + 29, 208, 16, head, COL_MUTE, LV_TEXT_ALIGN_CENTER);
    rrect(layer, bx + 14, by + 59, 212, 26, 13, COL_WOOD2);
    rrect(layer, bx + 16, by + 57, 208, 24, 12, COL_WHITE);
    draw_txt(layer, bx + 24, by + 61, 192, 16,
             s_kb_buf[0] ? s_kb_buf : app_str(APP_STR_EMPTY),
             s_kb_buf[0] ? COL_INK : COL_MUTE, LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < n; i++) {
        int c = i % cols, r = i / cols;
        int x = x0 + c * kw, y = y0 + r * kh;
        bool sel = (i == s_kb_sel);

        if (!keys[i][0]) continue;
        rrect(layer, x, y + 2, kw - 2, kh - 2, 7, COL_WOOD2);
        rrect(layer, x + 1, y, kw - 4, kh - 3, 6, sel ? COL_GOLD : COL_CREAM);
        draw_txt(layer, x, y + 2, kw - 2, 16, keys[i],
                 COL_INK, LV_TEXT_ALIGN_CENTER);
    }
}

static void ibar_draw(lv_event_t *e)
{
    static const int TAB_ICO[TAB_N] = {
        APP_FARM_ICO_HOME, APP_FARM_ICO_BAG, APP_FARM_ICO_STEAL, APP_FARM_ICO_SET
    };
    lv_layer_t *layer;
    lv_area_t box;
    int i;

    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    if (app_meow_set_open_now()) return;
    layer = lv_event_get_layer(e);
    lv_obj_get_coords(lv_event_get_target(e), &box);
    rrect(layer, box.x1, box.y1, LCD_W, NAV_H, 0, COL_WOOD2);
    rrect(layer, box.x1 + 3, box.y1 + 2, LCD_W - 6, NAV_H - 5, 16, COL_CREAM);
    for (i = 0; i < TAB_N; i++) {
        int x = box.x1 + 8 + i * 58;
        int cx = x + 26;
        bool on = (i == s_sel);
        if (on) {
            oval(layer, cx - 19, box.y1 + 4, 38, 38, COL_GOLD);
            oval(layer, cx - 16, box.y1 + 7, 32, 32, COL_WHITE);
        }
        draw_img(layer, &app_farm_ico_img[TAB_ICO[i]], cx - 14, box.y1 + 8);
    }
}

static void stage_draw(lv_event_t *e)
{
    lv_layer_t *layer;
    lv_area_t box;
    int bx, by;

    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    if (app_meow_set_open_now()) return;
    layer = lv_event_get_layer(e);
    lv_obj_get_coords(lv_event_get_target(e), &box);
    bx = box.x1;
    by = box.y1;
    if (app_farm_web_shown()) {
        app_farm_web_draw(layer, bx, by);
        draw_flash(layer, bx, by);
        return;
    }
    if (s_kb_mode) page_kb(layer, bx, by);
    else if (s_visit) page_visit(layer, bx, by);
    else if (s_sel == TAB_BAG) page_bag(layer, bx, by);
    else if (s_sel == TAB_STEAL) page_steal(layer, bx, by);
    else if (s_sel == TAB_SET) page_set(layer, bx, by);
    else page_home(layer, bx, by);
    draw_flash(layer, bx, by);
    draw_confirm(layer, bx, by);
    if (app_ota_prompt() || app_ota_state() == APP_OTA_APPLYING) {
        const char *hint = app_ota_state() == APP_OTA_APPLYING
            ? app_str(APP_STR_OTA_HOLD) : app_str(APP_STR_OTA_READY);
        rrect(layer, bx + 16, by + 88, 208, 52, 12, COL_CORAL);
        draw_txt(layer, bx + 24, by + 94, 192, 16, status_text(),
                 COL_WHITE, LV_TEXT_ALIGN_CENTER);
        draw_txt(layer, bx + 24, by + 114, 192, 16, hint,
                 COL_WHITE, LV_TEXT_ALIGN_CENTER);
    }
}

static void paint(void)
{
    int n;
    bool set, kb, qr, home;

    if (!s_lcd) return;
    n = inner_n();
    if (focused() && n > 0 && s_sub >= n) s_sub = n - 1;
    set = app_meow_set_open_now();
    kb = s_kb_mode != KB_NONE;
    qr = app_farm_web_shown();
    home = s_sel == TAB_HOME && !s_visit && focused();
    hide_obj(s_ibar, set || kb || s_visit || qr || home);
    hide_obj(s_stage, set);
    if (s_stage) lv_obj_set_size(s_stage, LCD_W,
                                (kb || s_visit || qr || home) ? LCD_H : PAGE_H);
    if (s_scr) lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_BG), 0);
    if (s_lcd) lv_obj_set_style_bg_color(s_lcd, lv_color_hex(COL_BG), 0);
    if (s_stage) lv_obj_invalidate(s_stage);
    if (s_ibar && !set) lv_obj_invalidate(s_ibar);
}

static bool net_ready(bool warn)
{
    if (!wifi_up()) {
        if (warn) flash_for(app_str(APP_STR_FARM_NEED_WIFI), APP_TONE_BEEP, 8);
        return false;
    }
    if (!s_farm.host[0]) {
        if (warn) flash_for(app_str(APP_STR_FARM_NEED_HOST), APP_TONE_BEEP, 8);
        return false;
    }
    return true;
}

static void ask_register(bool warn)
{
    uint8_t mac[6];

    if (!net_ready(warn)) return;
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return;
    net_started(app_farm_net_register(&s_farm, mac), WAIT_REG, warn);
}

static void ask_sync(void)
{
    if (!s_farm.host[0] || !s_farm.token[0]) return;
    if (!wifi_up()) return;
    net_started(app_farm_net_sync(&s_farm), WAIT_SYNC, false);
}

static void ask_pull(bool warn)
{
    if (!net_ready(warn)) return;
    net_started(app_farm_net_pull(&s_farm), WAIT_PULL, warn);
}

static void net_hello(void)
{
    if (!s_farm.host[0] || app_farm_net_busy()) return;
    if (s_farm.token[0]) ask_pull(false);
    else ask_register(false);
}

static void kb_hold_stop(void)
{
    s_kb_hold_btn = -1;
    s_kb_hold_ms = 0;
    if (s_kb_hold_tm) {
        lv_timer_delete(s_kb_hold_tm);
        s_kb_hold_tm = NULL;
    }
}

static void kb_hold_cb(lv_timer_t *t)
{
    int dir, step;

    (void)t;
    if (!s_kb_mode || s_kb_hold_btn < 0) return;
    s_kb_hold_ms += 80;
    if (s_kb_hold_ms < 280) return;
    dir = (s_kb_hold_btn == BSP_BTN_UP) ? -1 : 1;
    if (s_kb_mode == KB_FRIEND) {
        step = (s_kb_hold_ms >= 800) ? KB_ID_COLS : 1;
        app_kb_id_move(&s_kb_sel, dir * step);
    } else {
        step = (s_kb_hold_ms >= 800) ? KB_COLS : 1;
        app_ui_move(&s_kb_sel, KB_N, dir * step);
    }
    paint();
}

static void kb_open(int mode)
{
    s_kb_mode = mode;
    s_kb_sel = 0;
    s_kb_set = 0;
    s_kb_buf[0] = 0;
    if (mode == KB_HOST && s_farm.host[0]) {
        strncpy(s_kb_buf, s_farm.host, sizeof(s_kb_buf) - 1);
    } else if (mode == KB_NAME && s_farm.name[0]) {
        strncpy(s_kb_buf, s_farm.name, APP_FARM_NAME_MAX);
        s_kb_buf[APP_FARM_NAME_MAX] = 0;
    } else if (mode == KB_FRIEND) {
        while (!app_kb_id_keys()[s_kb_sel][0]) s_kb_sel++;
    }
    if (!s_kb_hold_tm) s_kb_hold_tm = lv_timer_create(kb_hold_cb, 80, NULL);
}

static void kb_cancel(void)
{
    app_farm_web_close();
    s_kb_mode = KB_NONE;
    kb_hold_stop();
}

static void start_add(uint32_t id)
{
    if (!net_ready(true)) return;
    s_add_id = id;
    net_started(app_farm_net_add(&s_farm, id), WAIT_ADD, true);
}

static void kb_commit(void)
{
    if (s_kb_mode == KB_HOST) {
        strncpy(s_farm.host, s_kb_buf, sizeof(s_farm.host) - 1);
        s_farm.host[sizeof(s_farm.host) - 1] = 0;
        mark_dirty();
        save_nvs();
        kb_cancel();
        if (s_farm.host[0]) ask_register(true);
        return;
    }
    if (s_kb_mode == KB_NAME) {
        app_farm_set_name(&s_farm, s_kb_buf);
        mark_dirty();
        save_nvs();
        kb_cancel();
        return;
    }
    if (s_kb_mode == KB_FRIEND) {
        unsigned long id = strtoul(s_kb_buf, NULL, 10);
        kb_cancel();
        if (id < 100000ul || id > 999999ul) {
            flash_for(app_str(APP_STR_FARM_FAIL), APP_TONE_BEEP, 8);
            return;
        }
        start_add((uint32_t)id);
    }
}

static app_str_id_t tool_none_str(int tool)
{
    if (tool == APP_FARM_TOOL_PLANT) return APP_STR_FARM_NO_PLANT;
    if (tool == APP_FARM_TOOL_WATER) return APP_STR_FARM_NO_WATER;
    if (tool == APP_FARM_TOOL_WEED) return APP_STR_FARM_NO_WEED;
    if (tool == APP_FARM_TOOL_PEST) return APP_STR_FARM_NO_PEST;
    if (tool == APP_FARM_TOOL_CUT) return APP_STR_FARM_NO_CUT;
    return APP_STR_FARM_FAIL;
}

static void flash_no_plot(int tool)
{
    flash_for(app_str(tool_none_str(tool)), APP_TONE_BEEP, 8);
}

static int first_tool_plot(int tool)
{
    int plot;

    if (tool == APP_FARM_TOOL_PLANT) {
        if (app_farm_seed_n(&s_farm, (int)s_farm.pick) == 0 ||
            !app_farm_crop_open(&s_farm, (int)s_farm.pick)) {
            flash_res(APP_FARM_NO_SEED, 0);
            return -1;
        }
    }
    plot = app_farm_next_plot(&s_farm, tool, -1, 0);
    if (plot < 0) flash_no_plot(tool);
    return plot;
}

static bool enter_plant_plots(void)
{
    int plot = first_tool_plot(APP_FARM_TOOL_PLANT);

    if (plot < 0) return false;
    s_plot_sel = APP_FARM_TOOL_PLANT;
    s_sub = plot;
    return true;
}

static void move_plot(int dir)
{
    int p;

    if (s_visit) {
        p = app_farm_next_guest(&s_guest, s_plot_sel, s_sub, dir);
        if (p >= 0) s_sub = p;
        return;
    }
    p = app_farm_next_plot(&s_farm, s_plot_sel, s_sub, dir);
    if (p >= 0) s_sub = p;
}

static void apply_tool(int plot)
{
    app_farm_res_t r = APP_FARM_NONE;
    uint16_t coins = 0;
    uint8_t lv0 = s_farm.level;
    int crop = (int)s_farm.pick;
    int plot_crop = (int)s_farm.plots[plot].crop;
    int tool = s_plot_sel;
    int next;
    const app_farm_crop_info_t *c;

    if (tool == APP_FARM_TOOL_PLANT) r = app_farm_plant(&s_farm, plot, crop);
    else if (tool == APP_FARM_TOOL_WATER) r = app_farm_water(&s_farm, plot);
    else if (tool == APP_FARM_TOOL_WEED) r = app_farm_weed(&s_farm, plot);
    else if (tool == APP_FARM_TOOL_PEST) r = app_farm_pest(&s_farm, plot);
    else if (tool == APP_FARM_TOOL_CUT) r = app_farm_cut(&s_farm, plot, &coins);
    if (r != APP_FARM_OK) {
        flash_res(r, 0);
        return;
    }
    if (tool == APP_FARM_TOOL_CUT && coins == 0) {
        flash_for(app_str(APP_STR_FARM_WITHER), APP_TONE_BEEP, 8);
    } else if (tool == APP_FARM_TOOL_PLANT) {
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_FARM_DID_PLANT),
                 crop_name(crop));
        flash_for(s_line, APP_TONE_CHIME, 10);
    } else if (tool == APP_FARM_TOOL_WATER) {
        char when[24], ready[32];
        uint32_t eta = app_farm_plot_eta_sec(&s_farm.plots[plot]);

        fmt_when(eta, when, sizeof(when));
        snprintf(ready, sizeof(ready), app_str(APP_STR_FARM_ETA), when);
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_FARM_DID_WATER), ready);
        flash_for(s_line, APP_TONE_CHIME, 8);
    } else if (tool == APP_FARM_TOOL_WEED) {
        flash_for(app_str(APP_STR_FARM_DID_WEED), APP_TONE_CHIME, 8);
    } else if (tool == APP_FARM_TOOL_PEST) {
        flash_for(app_str(APP_STR_FARM_DID_PEST), APP_TONE_CHIME, 8);
    } else if (tool == APP_FARM_TOOL_CUT) {
        c = app_farm_crop(plot_crop);
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_FARM_DID_CUT),
                 crop_name(plot_crop), c ? (int)c->xp : 0, (int)coins);
        flash_for(s_line, APP_TONE_CHIME, 10);
    }
    note_level(lv0);
    mark_dirty();
    next = app_farm_next_plot(&s_farm, tool, plot, 1);
    if (next < 0) {
        s_sub = tool;
        s_plot_sel = -1;
    } else {
        s_sub = next;
    }
}

static void home_ok(void)
{
    uint8_t ids[APP_FARM_CROP_N];
    int n, i, idx, plot;

    if (s_plot_sel >= 0) {
        apply_tool(s_sub);
        return;
    }
    if (s_seed_pick) {
        n = seed_list(ids, APP_FARM_CROP_N);
        if (n == 0) {
            s_seed_pick = false;
            s_sub = APP_FARM_TOOL_SEED;
            flash_res(APP_FARM_NO_SEED, 0);
            return;
        }
        if (s_sub < 0) s_sub = 0;
        if (s_sub >= n) s_sub = n - 1;
        s_farm.pick = ids[s_sub];
        s_seed_pick = false;
        mark_dirty();
        if (!enter_plant_plots()) s_sub = APP_FARM_TOOL_SEED;
        return;
    }
    if (s_sub == APP_FARM_TOOL_SEED) {
        n = seed_list(ids, APP_FARM_CROP_N);
        if (n == 0) {
            flash_res(APP_FARM_NO_SEED, 0);
            return;
        }
        idx = 0;
        for (i = 0; i < n; i++) {
            if (ids[i] == s_farm.pick) {
                idx = i;
                break;
            }
        }
        s_seed_pick = true;
        s_sub = idx;
        return;
    }
    plot = first_tool_plot(s_sub);
    if (plot < 0) return;
    s_plot_sel = s_sub;
    s_sub = plot;
}

static void bag_ok(void)
{
    uint8_t ids[APP_FARM_CROP_N];
    int n, crop;
    app_farm_res_t r;

    if (s_bag_cat == BAG_ITM) {
        n = seed_list(ids, APP_FARM_CROP_N);
        if (n == 0) {
            flash_res(APP_FARM_NO_SEED, 0);
            return;
        }
        s_farm.pick = ids[s_sub];
        mark_dirty();
        s_sel = TAB_HOME;
        s_menu = TAB_HOME;
        s_seed_pick = false;
        if (!enter_plant_plots()) {
            s_plot_sel = -1;
            s_sub = APP_FARM_TOOL_SEED;
        }
        return;
    }
    crop = s_sub;
    r = app_farm_buy(&s_farm, crop, 1);
    if (r == APP_FARM_OK) {
        const app_farm_crop_info_t *c = app_farm_crop(crop);
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_FARM_DID_BUY),
                 crop_name(crop), c ? (int)c->seed_cost : 0);
        flash_for(s_line, APP_TONE_CHIME, 8);
        mark_dirty();
    } else {
        flash_res(r, 0);
    }
}

static uint32_t friend_id_at(int idx)
{
    if (idx == s_friend_n) return 0;
    return s_friends[idx].id;
}

static void steal_ok(void)
{
    uint32_t id;

    if (s_steal_cat == STEAL_RAND) {
        if (!net_ready(true)) return;
        s_visit_ro = false;
        if (net_started(app_farm_net_random(&s_farm), WAIT_RANDOM, true)) {
            s_visit_src = STEAL_RAND;
        }
        return;
    }
    if (s_friend_act >= 0) {
        id = (uint32_t)s_friend_act;
        if (s_inbox_pick) {
            if (!net_ready(true)) return;
            s_reply_accept = (s_sub == 0);
            net_started(app_farm_net_reply(&s_farm, id, s_reply_accept),
                        WAIT_REPLY, true);
            return;
        }
        if (s_sub == 0) {
            if (!net_ready(true)) return;
            s_visit_ro = false;
            if (net_started(app_farm_net_visit(&s_farm, id), WAIT_VISIT, true)) {
                s_visit_src = STEAL_FRI;
            }
        } else {
            s_confirm = CONF_REMOVE;
            s_confirm_id = id;
        }
        return;
    }
    if (s_steal_cat == STEAL_RANK) {
        if (s_rank_n == 0 || s_sub >= s_rank_n) return;
        if (!net_ready(true)) return;
        s_visit_ro = true;
        if (net_started(app_farm_net_visit(&s_farm, s_rank[s_sub].id),
                        WAIT_VISIT, true)) {
            s_visit_src = STEAL_RANK;
        }
        return;
    }
    if (s_steal_cat == STEAL_MAIL) {
        if (s_mail_n == 0 || s_sub >= s_mail_n) return;
        if (s_mail[s_sub].kind == APP_FARM_MAIL_FRI) {
            s_inbox_pick = true;
            s_friend_act = (int)s_mail[s_sub].from;
            s_sub = 0;
            return;
        }
        if (!net_ready(true)) return;
        s_visit_ro = false;
        if (net_started(app_farm_net_visit(&s_farm, s_mail[s_sub].from),
                        WAIT_VISIT, true)) {
            s_visit_src = STEAL_MAIL;
        }
        return;
    }
    if (s_sub == s_friend_n) {
        kb_open(KB_FRIEND);
        return;
    }
    s_inbox_pick = false;
    s_friend_act = (int)friend_id_at(s_sub);
    s_sub = 0;
}

static void set_ok(void)
{
    if (s_sub == SET_LANG) {
        app_lang_t lang = app_lang() == APP_LANG_ZH ? APP_LANG_EN : APP_LANG_ZH;
        app_lang_set(lang);
        app_prefs()->lang = (uint8_t)lang;
        app_prefs_save_lang();
        flash_for(app_lang_name(lang), APP_TONE_BEEP, 8);
        return;
    }
    if (s_sub == SET_ID) {
        snprintf(s_line, sizeof(s_line), "%lu", (unsigned long)s_farm.id);
        flash_for(s_line, APP_TONE_BEEP, 16);
        return;
    }
    if (s_sub == SET_NAME) {
        kb_open(KB_NAME);
        return;
    }
    if (s_sub == SET_WIFI) app_meow_set_open(s_lcd, MEOW_SET_WIFI);
    else if (s_sub == SET_BLE) app_meow_set_open(s_lcd, MEOW_SET_BLE);
    else if (s_sub == SET_CLOCK) app_meow_set_open(s_lcd, MEOW_SET_CLOCK);
    else if (s_sub == SET_SCR) app_meow_set_open(s_lcd, MEOW_SET_SCREEN);
    else if (s_sub == SET_SND) app_meow_set_open(s_lcd, MEOW_SET_SOUND);
    else if (s_sub == SET_OTA) app_meow_set_open(s_lcd, MEOW_SET_OTA);
    else if (s_sub == SET_WIPE) {
        s_confirm = CONF_RESET;
    }
}

static void confirm_ok(void)
{
    if (s_confirm == CONF_RESET) {
        s_confirm = CONF_NONE;
        app_farm_wipe(&s_farm, now_sec());
        mark_dirty();
        save_nvs();
        flash_for(app_str(APP_STR_FARM_RESET_DONE), APP_TONE_CHIME, 10);
        return;
    }
    if (s_confirm == CONF_REMOVE) {
        if (!net_ready(true)) return;
        if (net_started(app_farm_net_remove(&s_farm, s_confirm_id),
                        WAIT_REMOVE, true)) {
            s_confirm = CONF_NONE;
        }
    }
}

static uint32_t next_peer_id(const app_farm_peer_t *list, int n)
{
    int i, k;
    uint32_t id;

    if (!list || n <= 0) return 0;
    for (i = 0; i < n; i++) {
        if (list[i].id != s_guest.id) continue;
        for (k = 1; k <= n; k++) {
            id = list[(i + k) % n].id;
            if (id && id != s_farm.id && id != s_guest.id) return id;
        }
        return 0;
    }
    for (i = 0; i < n; i++) {
        id = list[i].id;
        if (id && id != s_farm.id && id != s_guest.id) return id;
    }
    return 0;
}

static uint32_t next_mail_id(void)
{
    int i, k;

    for (i = 0; i < s_mail_n; i++) {
        if (s_mail[i].from != s_guest.id) continue;
        for (k = 1; k <= s_mail_n; k++) {
            uint32_t id = s_mail[(i + k) % s_mail_n].from;
            if (id && id != s_farm.id && id != s_guest.id) return id;
        }
    }
    return 0;
}

static void visit_next(void)
{
    uint32_t id = 0;

    if (!net_ready(true)) return;
    if (s_visit_src == STEAL_RAND) {
        net_started(app_farm_net_random(&s_farm), WAIT_RANDOM, true);
        return;
    }
    if (s_visit_src == STEAL_RANK) id = next_peer_id(s_rank, s_rank_n);
    else if (s_visit_src == STEAL_MAIL) id = next_mail_id();
    else id = next_peer_id(s_friends, s_friend_n);
    if (!id) {
        flash_for(app_str(APP_STR_FARM_NONE), APP_TONE_BEEP, 8);
        return;
    }
    net_started(app_farm_net_visit(&s_farm, id), WAIT_VISIT, true);
}

static void visit_ok(void)
{
    int plot, act;
    static const app_str_id_t NONE[APP_FARM_ACT_N] = {
        APP_STR_FARM_NO_STEAL, APP_STR_FARM_NO_WATER,
        APP_STR_FARM_NO_WEED, APP_STR_FARM_NO_PEST
    };

    if (s_visit_ro) return;
    if (s_plot_sel < 0) {
        act = s_sub;
        if (act == VISIT_NEXT) {
            visit_next();
            return;
        }
        if (act < 0 || act >= APP_FARM_ACT_N) act = 0;
        if (s_quota[act] == 0) {
            flash_for(app_str(APP_STR_FARM_LIMIT), APP_TONE_BEEP, 8);
            return;
        }
        plot = app_farm_next_guest(&s_guest, act, -1, 0);
        if (plot < 0) {
            flash_for(app_str(NONE[act]), APP_TONE_BEEP, 8);
            return;
        }
        s_plot_sel = act;
        s_sub = plot;
        return;
    }
    act = s_plot_sel;
    if (s_sub < 0 || !app_farm_guest_can(&s_guest, s_sub, act)) {
        flash_for(app_str(NONE[act < APP_FARM_ACT_N ? act : 0]), APP_TONE_BEEP, 8);
        return;
    }
    if (!net_ready(true)) return;
    if (act == APP_FARM_ACT_STEAL) {
        net_started(app_farm_net_steal(&s_farm, s_guest.id, s_sub),
                    WAIT_STEAL, true);
    } else {
        net_started(app_farm_net_help(&s_farm, s_guest.id, s_sub, act),
                    WAIT_HELP, true);
    }
}

static void refresh_lists(void)
{
    if (!s_farm.host[0] || !s_farm.token[0]) return;
    if (!wifi_up()) return;
    if (app_farm_net_busy()) return;
    if (s_steal_cat == STEAL_FRI) {
        if (s_list_ok & 1u) return;
        net_started(app_farm_net_friends(&s_farm), WAIT_FRIENDS, false);
    } else if (s_steal_cat == STEAL_RANK) {
        if (s_list_ok & 2u) return;
        net_started(app_farm_net_rank(&s_farm), WAIT_RANK, false);
    } else if (s_steal_cat == STEAL_MAIL) {
        if (s_list_ok & 4u) return;
        net_started(app_farm_net_inbox(&s_farm), WAIT_INBOX, false);
    }
}

static void handle_net(void)
{
    app_farm_net_st_t st = app_farm_net_state();
    int wait = s_wait;
    uint16_t coins;
    int lost;

    if (st == APP_FARM_NET_BUSY || wait == WAIT_NONE) return;
    s_wait = WAIT_NONE;
    if (st != APP_FARM_NET_OK) {
        const char *d = app_farm_net_detail();
        flash_for(net_fail_tip(wait, d), APP_TONE_BEEP, 8);
        if ((wait == WAIT_PULL || wait == WAIT_SYNC) && strstr(d, "401")) {
            s_farm.token[0] = 0;
            mark_dirty();
            save_nvs();
            ask_register(false);
        }
        if (wait == WAIT_VISIT) s_visit_ro = false;
        if (wait == WAIT_RANK) s_list_ok |= 2u;
        else if (wait == WAIT_FRIENDS) s_list_ok |= 1u;
        else if (wait == WAIT_INBOX) s_list_ok |= 4u;
        app_farm_net_clear();
        if (s_sel == TAB_STEAL && focused()) refresh_lists();
        return;
    }
    if (wait == WAIT_REG) {
        if (app_farm_net_take_token(s_farm.token, sizeof(s_farm.token))) {
            mark_dirty();
            save_nvs();
            ask_sync();
        }
    } else if (wait == WAIT_PULL || wait == WAIT_SYNC) {
        lost = app_farm_apply_remote(&s_farm, app_farm_net_view());
        if (wait == WAIT_PULL) {
            s_mail_n = app_farm_net_mail(s_mail, APP_FARM_NET_PEER_MAX);
            s_list_ok |= 4u;
        }
        if (lost) {
            flash_for(app_str(APP_STR_FARM_LOST), APP_TONE_ALARM, 12);
            mark_dirty();
        }
        save_nvs();
        if (wait == WAIT_PULL) ask_sync();
    } else if (wait == WAIT_RANDOM || wait == WAIT_VISIT) {
        s_guest = *app_farm_net_view();
        app_farm_net_quota(s_quota);
        if (s_guest.id && s_guest.id != s_farm.id) {
            bool stay = s_visit;
            s_visit = true;
            s_menu = TAB_STEAL;
            s_plot_sel = -1;
            if (!stay) s_sub = 0;
        } else {
            s_visit_ro = false;
            flash_for(app_str(wait == WAIT_RANDOM ? APP_STR_FARM_NO_RAND
                                                  : APP_STR_FARM_NONE),
                       APP_TONE_BEEP, 8);
        }
    } else if (wait == WAIT_STEAL) {
        uint8_t lv0 = s_farm.level;

        coins = app_farm_net_last_coins();
        s_farm.coins += coins;
        app_farm_add_xp(&s_farm, 3);
        s_guest = *app_farm_net_view();
        {
            uint8_t before = s_quota[APP_FARM_ACT_STEAL];
            app_farm_net_quota(s_quota);
            if (before > 0 && s_quota[APP_FARM_ACT_STEAL] >= before) {
                s_quota[APP_FARM_ACT_STEAL] = (uint8_t)(before - 1);
            }
        }
        s_sub = app_farm_next_guest(&s_guest, APP_FARM_ACT_STEAL, s_sub, 1);
        if (s_sub < 0) {
            s_sub = APP_FARM_ACT_STEAL;
            s_plot_sel = -1;
        }
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_FARM_DID_STEAL), (int)coins);
        flash_for(s_line, APP_TONE_CHIME, 10);
        note_level(lv0);
        mark_dirty();
    } else if (wait == WAIT_HELP) {
        uint8_t lv0 = s_farm.level;

        s_guest = *app_farm_net_view();
        {
            int act = s_plot_sel;
            uint8_t before = (act >= 0 && act < APP_FARM_ACT_N) ? s_quota[act] : 0;
            app_farm_net_quota(s_quota);
            if (act >= 0 && act < APP_FARM_ACT_N && before > 0 &&
                s_quota[act] >= before) {
                s_quota[act] = (uint8_t)(before - 1);
            }
        }
        app_farm_add_xp(&s_farm, 1);
        s_sub = app_farm_next_guest(&s_guest, s_plot_sel, s_sub, 1);
        if (s_sub < 0) {
            s_sub = s_plot_sel >= 0 ? s_plot_sel : 0;
            s_plot_sel = -1;
        }
        flash_for(app_str(APP_STR_FARM_DID_HELP), APP_TONE_CHIME, 8);
        note_level(lv0);
        mark_dirty();
    } else if (wait == WAIT_FRIENDS) {
        s_friend_n = app_farm_net_peers(s_friends, APP_FARM_NET_PEER_MAX);
        sort_friends();
        s_list_ok |= 1u;
    } else if (wait == WAIT_RANK) {
        s_rank_n = app_farm_net_peers(s_rank, APP_FARM_NET_PEER_MAX);
        if (s_rank_n > STEAL_RANK_MAX) s_rank_n = STEAL_RANK_MAX;
        s_list_ok |= 2u;
    } else if (wait == WAIT_INBOX) {
        s_mail_n = app_farm_net_mail(s_mail, APP_FARM_NET_PEER_MAX);
        s_list_ok |= 4u;
    } else if (wait == WAIT_ADD) {
        if (app_farm_net_already() || app_farm_net_linked()) {
            if (app_farm_friend_add(&s_farm, s_add_id) == APP_FARM_OK) mark_dirty();
            flash_for(app_str(APP_STR_FARM_OK), APP_TONE_CHIME, 8);
        } else {
            flash_for(app_str(APP_STR_FARM_ADDED), APP_TONE_CHIME, 8);
        }
        s_list_ok &= ~1u;
    } else if (wait == WAIT_REPLY) {
        if (s_reply_accept) {
            if (app_farm_friend_add(&s_farm, (uint32_t)s_friend_act) == APP_FARM_OK) {
                mark_dirty();
            }
        }
        s_friend_act = -1;
        s_inbox_pick = false;
        s_sub = 0;
        flash_for(app_str(s_reply_accept ? APP_STR_FARM_ACCEPT
                                         : APP_STR_FARM_DECLINE),
                  APP_TONE_CHIME, 8);
        s_list_ok &= ~5u;
    } else if (wait == WAIT_REMOVE) {
        if (s_confirm_id) {
            app_farm_friend_del(&s_farm, s_confirm_id);
            mark_dirty();
        }
        s_confirm_id = 0;
        s_friend_act = -1;
        s_sub = 0;
        s_list_ok &= ~1u;
        flash_for(app_str(APP_STR_FARM_DELETE_DONE), APP_TONE_CHIME, 8);
    }
    app_farm_net_clear();
    if (s_sel == TAB_STEAL && focused()) refresh_lists();
}

static bool idle_hold(void)
{
    if (app_meow_set_blocks_idle()) return true;
    if (s_kb_mode) return true;
    if (app_farm_web_shown()) return true;
    if (app_ota_state() == APP_OTA_APPLYING) return true;
    if (bsp_ble_state() == BSP_BLE_PAIRING) return true;
    return false;
}

static void tune_pm(void)
{
    bool hot = (s_idle_ms < IDLE_PERF_MS) || idle_hold();
    bsp_pm_set_perf(hot);
}

static void sleep_now(void)
{
    if (s_asleep) return;
    if (s_dirty) save_nvs();
    s_asleep = true;
    s_wifi_wait = false;
    s_awake_ms = 0;
    if (bsp_ble_state() != BSP_BLE_PAIRING) (void)bsp_ble_set_enabled(false);
    bsp_display_backlight(0);
    lv_refr_now(NULL);
    bsp_lvgl_flush_enable(false);
    bsp_display_sleep(true);
    bsp_audio_standby();
    bsp_wifi_radio_suspend();
    bsp_button_sleep_gpio(true);
    bsp_lvgl_tick_enable(false);
    bsp_pm_set_sleeping(true);
}

static void wake_now(void)
{
    s_idle_ms = 0;
    s_still_ms = 0;
    if (!s_asleep) return;
    s_asleep = false;
    s_awake_ms = 0;
    s_wifi_wait = true;
    bsp_pm_set_perf(true);
    bsp_pm_set_sleeping(false);
    bsp_button_sleep_gpio(false);
    bsp_lvgl_tick_enable(true);
    bsp_display_sleep(false);
    bsp_lvgl_flush_enable(true);
    lv_obj_invalidate(s_scr);
    lv_refr_now(NULL);
    app_prefs_apply_display();
    s_tick_ms = 250;
    if (s_timer) lv_timer_set_period(s_timer, 250);
}

static void on_gpio_wake(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    s_wake_skip = true;
    wake_now();
    bsp_lvgl_unlock();
}

static uint32_t tick_ms(void)
{
    static uint32_t last;
    uint32_t now = now_ms(), dt;

    if (last == 0) last = now;
    dt = now - last;
    last = now;
    return dt;
}

static void on_tick(lv_timer_t *t)
{
    uint32_t dt = tick_ms();
    uint16_t lim;
    bool flash_tick;

    (void)t;
    if (!s_ready) {
        load_nvs();
        s_ready = true;
        s_want_back_hint = !back_hint_seen();
        sync_farm();
        save_nvs();
        if (wifi_up()) {
            net_hello();
            s_net_up = true;
        }
    } else {
        sync_farm();
        bool up = wifi_up();
        if (up && !s_net_up) {
            s_list_ok = 0;
            net_hello();
            if (s_sel == TAB_STEAL && focused()) refresh_lists();
        }
        s_net_up = up;
    }
    if (s_ready && s_want_back_hint) {
        s_want_back_hint = false;
        back_hint_mark();
        flash_for(app_str(APP_STR_FARM_HOLD_BACK), APP_TONE_CHIME, 16);
    }
    handle_net();
    app_farm_web_poll();
    if (s_sel == TAB_STEAL && focused() && !s_visit) refresh_lists();
    if (s_sync_left > 0 && --s_sync_left == 0 && s_dirty) ask_sync();
    flash_tick = s_flash_left > 0;
    if (s_flash_left > 0) s_flash_left--;
    if (s_flash_left <= 0 && s_flash_next[0]) {
        snprintf(s_flash, sizeof(s_flash), "%s", s_flash_next);
        s_flash_left = s_flash_next_ticks;
        s_flash_next[0] = 0;
        if (s_flash_next_tone >= 0) app_tone_play(s_flash_next_tone);
        flash_tick = true;
    }
    s_blink ^= 1;
    app_time_tick();
    if (app_meow_set_open_now()) app_meow_set_tick();
    app_ota_tick(!s_asleep && !app_meow_set_open_now() && s_kb_mode == KB_NONE);

    if (!s_asleep) {
        s_awake_ms += dt;
        if (flash_tick) {
            paint();
            s_still_ms = 0;
        } else {
            s_still_ms += dt;
            if (s_still_ms >= IDLE_PAINT_MS) {
                paint();
                s_still_ms = 0;
            }
        }
    }

    lim = app_prefs()->sleep_sec;
    if (!s_asleep && s_wifi_wait && s_awake_ms >= WIFI_WAKE_MS) {
        bool will_sleep = (lim != 0) && !idle_hold() &&
                          (s_idle_ms + dt >= (uint32_t)lim * 1000);
        if (!will_sleep) {
            s_wifi_wait = false;
            if (bsp_wifi_enabled()) bsp_wifi_radio_resume();
        }
    }
    if (lim == 0 || s_asleep) {
        tune_pm();
        return;
    }
    if (idle_hold()) {
        s_idle_ms = 0;
        tune_pm();
        return;
    }
    s_idle_ms += dt;
    tune_pm();
    if (s_idle_ms >= (uint32_t)lim * 1000) sleep_now();
}

void app_farm_start(void)
{
    s_scr = lv_obj_create(NULL);
    ui_pixel_strip_theme(s_scr);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_font(s_scr, ui_pixel_font_14(), 0);

    s_lcd = px(s_scr, 0, 0, LCD_W, LCD_H, COL_BG);
    lv_obj_set_style_radius(s_lcd, 0, 0);
    lv_obj_set_style_border_width(s_lcd, 0, 0);

    s_stage = lv_obj_create(s_lcd);
    ui_pixel_strip_theme(s_stage);
    lv_obj_set_pos(s_stage, 0, 0);
    lv_obj_set_size(s_stage, LCD_W, PAGE_H);
    lv_obj_set_style_bg_opa(s_stage, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_stage, lv_color_hex(COL_BG), 0);
    lv_obj_add_event_cb(s_stage, stage_draw, LV_EVENT_DRAW_MAIN, NULL);

    s_ibar = lv_obj_create(s_lcd);
    ui_pixel_strip_theme(s_ibar);
    lv_obj_set_pos(s_ibar, 0, PAGE_H);
    lv_obj_set_size(s_ibar, LCD_W, NAV_H);
    lv_obj_set_style_bg_opa(s_ibar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ibar, lv_color_hex(COL_WHITE), 0);
    lv_obj_add_event_cb(s_ibar, ibar_draw, LV_EVENT_DRAW_MAIN, NULL);

    s_sel = 0;
    s_menu = -1;
    s_asleep = false;
    bsp_button_set_wake_cb(on_gpio_wake);
    if (bsp_ble_state() != BSP_BLE_PAIRING) (void)bsp_ble_set_enabled(false);
    app_farm_net_init();
    app_ota_init();
    s_timer = lv_timer_create(on_tick, 250, NULL);
    lv_screen_load(s_scr);
    paint();
}

void app_farm_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    int n;

    if (s_asleep) {
        if (ev == BSP_BTN_PRESS || ev == BSP_BTN_CLICK) {
            wake_now();
            s_wake_skip = true;
        }
        return;
    }
    s_idle_ms = 0;
    s_still_ms = 0;
    tune_pm();
    if (s_wake_skip) {
        if (ev == BSP_BTN_CLICK) s_wake_skip = false;
        return;
    }

    if (app_farm_web_shown()) {
        if (app_farm_web_key(btn, ev)) paint();
        return;
    }

    if (bsp_ble_state() == BSP_BLE_PAIRING && bsp_ble_pair_needs_confirm()) {
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            bsp_ble_pair_reply(true);
            flash_for(NULL, APP_TONE_BEEP, 4);
            return;
        }
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            bsp_ble_pair_reply(false);
            flash_for(NULL, APP_TONE_BEEP, 4);
            return;
        }
    }
    if (app_ota_state() == APP_OTA_APPLYING && !app_meow_set_open_now()) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) app_ota_cancel();
        return;
    }
    if (app_ota_prompt() && !app_meow_set_open_now()) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            app_ota_apply();
            paint();
            return;
        }
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            app_ota_skip();
            paint();
            return;
        }
        return;
    }

    if (s_kb_mode) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            kb_cancel();
            paint();
            return;
        }
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            if (ev == BSP_BTN_PRESS) {
                s_kb_hold_btn = (int)btn;
                s_kb_hold_ms = 0;
                if (s_kb_mode == KB_FRIEND) {
                    app_kb_id_move(&s_kb_sel, btn == BSP_BTN_UP ? -1 : 1);
                } else {
                    app_ui_move(&s_kb_sel, KB_N, btn == BSP_BTN_UP ? -1 : 1);
                }
                paint();
            } else if (ev == BSP_BTN_RELEASE && s_kb_hold_btn == (int)btn) {
                s_kb_hold_btn = -1;
                s_kb_hold_ms = 0;
            } else if (ev == BSP_BTN_CLICK) {
                /* already moved on press */
            }
            return;
        }
        if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) return;
        {
            size_t cap = s_kb_mode == KB_NAME ? (APP_FARM_NAME_MAX + 1)
                                              : sizeof(s_kb_buf);
            int r = s_kb_mode == KB_FRIEND
                ? app_kb_id_click(s_kb_buf, sizeof(s_kb_buf), s_kb_sel)
                : app_kb_click(s_kb_buf, cap, &s_kb_sel, &s_kb_set);
            if (r == 3) kb_cancel();
            else if (r == 2) kb_commit();
            else if (r == 4) {
                if (!wifi_up()) {
                    flash_for(app_str(APP_STR_FARM_NEED_WIFI), APP_TONE_BEEP, 8);
                } else {
                    app_farm_web_open(s_kb_buf, cap,
                                      s_kb_mode == KB_NAME ? "name" : "host",
                                      paint);
                }
            }
        }
        paint();
        return;
    }

    if (app_meow_set_open_now()) {
        app_meow_set_on_key(btn, ev);
        paint();
        return;
    }

    if (s_confirm != CONF_NONE) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            s_confirm = CONF_NONE;
            if (s_wait != WAIT_REMOVE) s_confirm_id = 0;
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            confirm_ok();
        }
        paint();
        return;
    }

    if (s_visit) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            if (s_plot_sel >= 0) {
                s_sub = s_plot_sel;
                s_plot_sel = -1;
            } else {
                s_visit = false;
                s_visit_ro = false;
                s_plot_sel = -1;
                s_friend_act = -1;
                s_inbox_pick = false;
                s_menu = TAB_STEAL;
                s_sel = TAB_STEAL;
                s_sub = 0;
            }
            paint();
            return;
        }
        if (s_visit_ro) {
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) {
                app_ui_move(&s_sub, app_farm_plot_n((int)s_guest.level), -1);
            } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
                app_ui_move(&s_sub, app_farm_plot_n((int)s_guest.level), 1);
            }
            paint();
            return;
        }
        if (ev != BSP_BTN_CLICK) return;
        if (s_plot_sel >= 0) {
            if (btn == BSP_BTN_UP) move_plot(-1);
            else if (btn == BSP_BTN_DOWN) move_plot(1);
            else if (btn == BSP_BTN_OK) visit_ok();
        } else {
            n = VISIT_TOOL_N;
            if (btn == BSP_BTN_UP) app_ui_move(&s_sub, n, -1);
            else if (btn == BSP_BTN_DOWN) app_ui_move(&s_sub, n, 1);
            else if (btn == BSP_BTN_OK) visit_ok();
        }
        paint();
        return;
    }

    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (s_friend_act >= 0) {
            s_friend_act = -1;
            s_inbox_pick = false;
            s_sub = 0;
        } else if (s_seed_pick) {
            s_seed_pick = false;
            s_sub = APP_FARM_TOOL_SEED;
        } else if (s_plot_sel >= 0) {
            s_sub = s_plot_sel;
            s_plot_sel = -1;
        } else if (focused()) {
            close_menu();
        }
        paint();
        return;
    }

    if (ev == BSP_BTN_LONG && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        int dir = btn == BSP_BTN_UP ? -1 : 1;
        if (s_sel == TAB_BAG && focused()) {
            s_bag_cat = (s_bag_cat + 2 + dir) % 2;
            s_sub = 0;
        } else if (s_sel == TAB_STEAL && focused() && s_friend_act < 0) {
            s_steal_cat = (s_steal_cat + STEAL_CAT_N + dir) % STEAL_CAT_N;
            s_sub = 0;
            refresh_lists();
        }
        paint();
        return;
    }

    if (ev != BSP_BTN_CLICK) return;
    if (!focused()) {
        if (btn == BSP_BTN_UP) s_sel = (s_sel + TAB_N - 1) % TAB_N;
        else if (btn == BSP_BTN_DOWN) s_sel = (s_sel + 1) % TAB_N;
        else if (btn == BSP_BTN_OK) {
            s_menu = s_sel;
            s_sub = 0;
            s_plot_sel = -1;
            s_seed_pick = false;
            s_friend_act = -1;
            s_inbox_pick = false;
            if (s_sel == TAB_STEAL) {
                s_list_ok = 0;
                refresh_lists();
            }
        }
        paint();
        return;
    }

    n = inner_n();
    if (n < 1) n = 1;
    if (s_sel == TAB_HOME && s_plot_sel >= 0) {
        if (btn == BSP_BTN_UP) move_plot(-1);
        else if (btn == BSP_BTN_DOWN) move_plot(1);
        else if (btn == BSP_BTN_OK) home_ok();
        paint();
        return;
    }
    if (btn == BSP_BTN_UP) app_ui_move(&s_sub, n, -1);
    else if (btn == BSP_BTN_DOWN) app_ui_move(&s_sub, n, 1);
    else if (btn == BSP_BTN_OK) {
        if (s_sel == TAB_HOME) home_ok();
        else if (s_sel == TAB_BAG) bag_ok();
        else if (s_sel == TAB_STEAL) {
            if (!wifi_up() && s_friend_act < 0) {
                flash_for(app_str(APP_STR_FARM_NEED_WIFI), APP_TONE_BEEP, 8);
            } else {
                steal_ok();
            }
        }
        else if (s_sel == TAB_SET) set_ok();
    }
    paint();
}
