#pragma once

#include "app_logic.h"
#include <stdint.h>

#define APP_TONE_OFF    0
#define APP_TONE_BEEP   1
#define APP_TONE_DOUBLE 2
#define APP_TONE_CHIME  3
#define APP_TONE_TRIPLE 4
#define APP_TONE_ALARM  5
#define APP_TONE_MEOW   6

#define APP_NTP_SERVER_N 4

typedef struct {
    uint8_t brightness;   // 10..100, step 10
    uint16_t sleep_sec;   // 0 = never
    uint8_t lock_on;      // 1 = show lock after sleep/idle
    uint8_t lock_stay;    // 1 = keep backlight on while locked
    uint8_t lock_time;    // 1 = show clock on lock screen
    uint8_t lock_wx;      // 1 = show weather on lock screen
    uint8_t lock_quote;   // 1 = show verse on lock screen
    uint8_t volume;       // 0..100, step 10
    uint8_t muted;        // 1 = silence all output
    uint8_t tone_msg;
    uint8_t tone_alert;
    uint8_t ntp_on;
    uint8_t ntp_server;   // index into app_ntp_server()
    uint8_t auto_hide;    // 0, 5, 10, 20
    uint8_t lang;         // app_lang_t
    char wx_city[32 + 1];
    int32_t wx_lat_e4;    // latitude * 10000
    int32_t wx_lon_e4;    // longitude * 10000
    uint16_t wx_interval; // minutes: 15/30/60/180
    uint8_t wx_imperial;  // 0 = C/kmh, 1 = F/mph
    uint8_t meow_bed;     // 0..23 DND start, default 21
    uint8_t meow_wake;    // 0..23 DND end, default 8; equal to bed = off
    uint8_t ota_auto;     // 1 = check for updates in background
    uint8_t theme;
    uint8_t notif_def;    // 未命中规则时的档位,含 APP_ALERT_DROP
    uint8_t kw_ver;       // 规则语义版本,见 APP_RULE_VER
    uint8_t kw_n;
    app_kw_t kw[APP_KW_MAX];
} app_prefs_t;

void app_prefs_load(void);
void app_prefs_save(void);
void app_prefs_flush(void);
void app_prefs_tick(void);
void app_prefs_save_lang(void);
app_prefs_t *app_prefs(void);
app_totp_list_t *app_totp_store(void);
bool app_totp_persist(void);

const char *app_ntp_server(int index);
void app_prefs_apply_display(void);
void app_prefs_apply_audio(void);
