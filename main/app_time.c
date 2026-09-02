#include "app_time.h"

#include "app_i18n.h"
#include "app_logic.h"
#include "app_prefs.h"
#include "bsp_wifi.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "app_time";
static bool s_ntp;
static bool s_synced;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "NTP synced");
}

void app_time_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    s_ntp = false;
    s_synced = false;
}

void app_time_ntp_restart(void)
{
    if (s_ntp) {
        esp_netif_sntp_deinit();
        s_ntp = false;
    }
    s_synced = false;
}

void app_time_tick(void)
{
    bool want = app_prefs()->ntp_on && bsp_wifi_state() == BSP_WIFI_CONNECTED;
    if (want && !s_ntp) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(app_ntp_server(app_prefs()->ntp_server));
        cfg.sync_cb = on_sync;
        if (esp_netif_sntp_init(&cfg) == ESP_OK) {
            s_ntp = true;
            s_synced = false;
            ESP_LOGI(TAG, "NTP start %s", app_ntp_server(app_prefs()->ntp_server));
        }
    } else if (!want && s_ntp) {
        app_time_ntp_restart();
    }
}

void app_time_now_text(char *out, size_t n)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    app_time_format(t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, out, n);
}

void app_time_lock_date(char *out, size_t n)
{
    if (!out || n == 0) return;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int w = t.tm_wday;
    if (w < 0 || w > 6) w = 0;
    if (app_lang() == APP_LANG_ZH) {
        static const char *const WD[] = {
            "日", "一", "二", "三", "四", "五", "六",
        };
        snprintf(out, n, "%04d.%02d.%02d 周%s",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, WD[w]);
    } else {
        static const char *const WD[] = {
            "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
        };
        snprintf(out, n, "%04d.%02d.%02d %s",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, WD[w]);
    }
}

void app_time_lock_clock(char *out, size_t n)
{
    if (!out || n == 0) return;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(out, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

void app_time_get(int *year, int *month, int *day, int *hour, int *minute)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (year) *year = t.tm_year + 1900;
    if (month) *month = t.tm_mon + 1;
    if (day) *day = t.tm_mday;
    if (hour) *hour = t.tm_hour;
    if (minute) *minute = t.tm_min;
}

void app_time_set(int year, int month, int day, int hour, int minute)
{
    if (year < 2020) year = 2020;
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t ts = mktime(&t);
    if (ts == (time_t)-1) return;
    struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

bool app_time_ntp_synced(void)
{
    return s_synced;
}

bool app_time_valid(void)
{
    if (s_synced) return true;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year + 1900 >= 2024;
}
