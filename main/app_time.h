#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define APP_CITY_N 73

#define APP_REG_ASIA 0
#define APP_REG_EU   1
#define APP_REG_AM   2
#define APP_REG_OC   3
#define APP_REG_AF   4

typedef struct {
    const char *zh;
    const char *en;
    const char *abbr;
    int16_t off;
    uint8_t region;
} app_city_t;

void app_time_init(void);
void app_time_apply_tz(void);
void app_time_set_utc(int64_t epoch, int16_t off_min, const char *tz_name);
void app_time_local(struct tm *out);
void app_time_at_off(int16_t off_min, struct tm *out);
bool app_time_valid(void);

int app_city_count(void);
const app_city_t *app_city(int id);
const char *app_city_code(int id);
void app_time_hm(const struct tm *t, char *out, size_t n);
void app_time_hms(const struct tm *t, char *out, size_t n);
void app_time_date(const struct tm *t, char *out, size_t n);
int app_city_day(int city_id);
void app_city_off_text(int16_t off, char *out, size_t n);
