#pragma once

#include <stddef.h>
#include <stdbool.h>

void app_time_init(void);
void app_time_tick(void);                 // call ~1s; starts/stops NTP from Wi-Fi + prefs
void app_time_now_text(char *out, size_t n);
void app_time_lock_date(char *out, size_t n);
void app_time_lock_clock(char *out, size_t n);
void app_time_set(int year, int month, int day, int hour, int minute);
void app_time_get(int *year, int *month, int *day, int *hour, int *minute);
bool app_time_ntp_synced(void);
bool app_time_valid(void);
void app_time_ntp_restart(void);
