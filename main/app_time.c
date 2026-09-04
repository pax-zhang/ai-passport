#include "app_time.h"

#include "app_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static const app_city_t CITIES[APP_CITY_N] = {
    { "东京", "Tokyo", "JST", 540, APP_REG_ASIA },
    { "北京", "Beijing", "CST", 480, APP_REG_ASIA },
    { "香港", "Hong Kong", "HKT", 480, APP_REG_ASIA },
    { "新加坡", "Singapore", "SGT", 480, APP_REG_ASIA },
    { "首尔", "Seoul", "KST", 540, APP_REG_ASIA },
    { "德里", "Delhi", "IST", 330, APP_REG_ASIA },
    { "迪拜", "Dubai", "GST", 240, APP_REG_ASIA },
    { "莫斯科", "Moscow", "MSK", 180, APP_REG_EU },
    { "伦敦", "London", "GMT", 0, APP_REG_EU },
    { "巴黎", "Paris", "CET", 60, APP_REG_EU },
    { "柏林", "Berlin", "CET", 60, APP_REG_EU },
    { "开罗", "Cairo", "EET", 120, APP_REG_AF },
    { "纽约", "New York", "EST", -300, APP_REG_AM },
    { "芝加哥", "Chicago", "CST", -360, APP_REG_AM },
    { "洛杉矶", "Los Angeles", "PST", -480, APP_REG_AM },
    { "圣保罗", "Sao Paulo", "BRT", -180, APP_REG_AM },
    { "悉尼", "Sydney", "AEST", 600, APP_REG_OC },
    { "奥克兰", "Auckland", "NZST", 720, APP_REG_OC },
    { "台北", "Taipei", "CST", 480, APP_REG_ASIA },
    { "曼谷", "Bangkok", "ICT", 420, APP_REG_ASIA },
    { "雅加达", "Jakarta", "WIB", 420, APP_REG_ASIA },
    { "马尼拉", "Manila", "PHT", 480, APP_REG_ASIA },
    { "吉隆坡", "Kuala Lumpur", "MYT", 480, APP_REG_ASIA },
    { "河内", "Hanoi", "ICT", 420, APP_REG_ASIA },
    { "孟买", "Mumbai", "IST", 330, APP_REG_ASIA },
    { "卡拉奇", "Karachi", "PKT", 300, APP_REG_ASIA },
    { "利雅得", "Riyadh", "AST", 180, APP_REG_ASIA },
    { "伊斯坦布尔", "Istanbul", "TRT", 180, APP_REG_EU },
    { "罗马", "Rome", "CET", 60, APP_REG_EU },
    { "马德里", "Madrid", "CET", 60, APP_REG_EU },
    { "阿姆斯特丹", "Amsterdam", "CET", 60, APP_REG_EU },
    { "苏黎世", "Zurich", "CET", 60, APP_REG_EU },
    { "雅典", "Athens", "EET", 120, APP_REG_EU },
    { "约翰内斯堡", "Johannesburg", "SAST", 120, APP_REG_AF },
    { "拉各斯", "Lagos", "WAT", 60, APP_REG_AF },
    { "内罗毕", "Nairobi", "EAT", 180, APP_REG_AF },
    { "多伦多", "Toronto", "EST", -300, APP_REG_AM },
    { "温哥华", "Vancouver", "PST", -480, APP_REG_AM },
    { "丹佛", "Denver", "MST", -420, APP_REG_AM },
    { "墨西哥城", "Mexico City", "CST", -360, APP_REG_AM },
    { "布宜诺斯艾利斯", "Buenos Aires", "ART", -180, APP_REG_AM },
    { "檀香山", "Honolulu", "HST", -600, APP_REG_AM },
    { "墨尔本", "Melbourne", "AEST", 600, APP_REG_OC },
    { "珀斯", "Perth", "AWST", 480, APP_REG_OC },
    { "上海", "Shanghai", "CST", 480, APP_REG_ASIA },
    { "大阪", "Osaka", "JST", 540, APP_REG_ASIA },
    { "胡志明", "Ho Chi Minh", "ICT", 420, APP_REG_ASIA },
    { "德黑兰", "Tehran", "IRST", 210, APP_REG_ASIA },
    { "特拉维夫", "Tel Aviv", "IST", 120, APP_REG_ASIA },
    { "维也纳", "Vienna", "CET", 60, APP_REG_EU },
    { "斯德哥尔摩", "Stockholm", "CET", 60, APP_REG_EU },
    { "赫尔辛基", "Helsinki", "EET", 120, APP_REG_EU },
    { "华沙", "Warsaw", "CET", 60, APP_REG_EU },
    { "里斯本", "Lisbon", "WET", 0, APP_REG_EU },
    { "都柏林", "Dublin", "GMT", 0, APP_REG_EU },
    { "旧金山", "San Francisco", "PST", -480, APP_REG_AM },
    { "西雅图", "Seattle", "PST", -480, APP_REG_AM },
    { "迈阿密", "Miami", "EST", -300, APP_REG_AM },
    { "波士顿", "Boston", "EST", -300, APP_REG_AM },
    { "华盛顿", "Washington", "EST", -300, APP_REG_AM },
    { "圣地亚哥", "Santiago", "CLT", -240, APP_REG_AM },
    { "利马", "Lima", "PET", -300, APP_REG_AM },
    { "波哥大", "Bogota", "COT", -300, APP_REG_AM },
    { "里约", "Rio", "BRT", -180, APP_REG_AM },
    { "布里斯班", "Brisbane", "AEST", 600, APP_REG_OC },
    { "阿德莱德", "Adelaide", "ACDT", 570, APP_REG_OC },
    { "雷克雅未克", "Reykjavik", "GMT", 0, APP_REG_EU },
    { "达卡", "Dhaka", "BST", 360, APP_REG_ASIA },
    { "仰光", "Yangon", "MMT", 390, APP_REG_ASIA },
    { "金边", "Phnom Penh", "ICT", 420, APP_REG_ASIA },
    { "卡萨布兰卡", "Casablanca", "WEST", 60, APP_REG_AF },
    { "安克雷奇", "Anchorage", "AKST", -540, APP_REG_AM },
    { "苏瓦", "Suva", "FJT", 720, APP_REG_OC },
};

_Static_assert(sizeof(CITIES) / sizeof(CITIES[0]) == APP_CITY_N, "city n");

static bool s_valid;

static void posix_tz(int16_t off, char *buf, size_t n)
{
    int sign = off >= 0 ? 1 : -1;
    int abs = off >= 0 ? off : -off;
    int h = abs / 60;
    int m = abs % 60;
    if (m) {
        snprintf(buf, n, "UTC%c%d:%02d", sign > 0 ? '-' : '+', h, m);
    } else {
        snprintf(buf, n, "UTC%c%d", sign > 0 ? '-' : '+', h);
    }
}

void app_time_apply_tz(void)
{
    char tz[20];
    posix_tz(app_prefs()->tz_off, tz, sizeof(tz));
    setenv("TZ", tz, 1);
    tzset();
}

void app_time_init(void)
{
    s_valid = false;
    app_time_apply_tz();
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    s_valid = (t.tm_year + 1900) >= 2024;
}

void app_time_set_utc(int64_t epoch, int16_t off_min, const char *tz_name)
{
    if (epoch < 1704067200) return;
    if (off_min < -720) off_min = -720;
    if (off_min > 840) off_min = 840;
    app_prefs_t *p = app_prefs();
    p->tz_off = off_min;
    if (tz_name && tz_name[0]) {
        strlcpy(p->tz_name, tz_name, sizeof(p->tz_name));
    }
    app_prefs_save();
    app_time_apply_tz();
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_valid = true;
}

void app_time_at_off(int16_t off_min, struct tm *out)
{
    if (!out) return;
    time_t utc = time(NULL);
    time_t local = utc + (time_t)off_min * 60;
    gmtime_r(&local, out);
}

void app_time_local(struct tm *out)
{
    app_time_at_off(app_prefs()->tz_off, out);
}

bool app_time_valid(void)
{
    if (s_valid) return true;
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    s_valid = (t.tm_year + 1900) >= 2024;
    return s_valid;
}

int app_city_count(void)
{
    return APP_CITY_N;
}

const app_city_t *app_city(int id)
{
    if (id < 0 || id >= APP_CITY_N) id = 0;
    return &CITIES[id];
}

const char *app_city_code(int id)
{
    static const char *const CODE[APP_CITY_N] = {
        "TYO", "BJS", "HKG", "SIN", "SEL", "DEL", "DXB", "MOW",
        "LON", "PAR", "BER", "CAI", "NYC", "CHI", "LAX", "SAO",
        "SYD", "AKL", "TPE", "BKK", "JKT", "MNL", "KUL", "HAN",
        "BOM", "KHI", "RUH", "IST", "ROM", "MAD", "AMS", "ZRH",
        "ATH", "JNB", "LOS", "NBO", "YTO", "YVR", "DEN", "MEX",
        "BUE", "HNL", "MEL", "PER", "SHA", "OSA", "SGN", "THR",
        "TLV", "VIE", "STO", "HEL", "WAW", "LIS", "DUB", "SFO",
        "SEA", "MIA", "BOS", "WAS", "SCL", "LIM", "BOG", "RIO",
        "BNE", "ADL", "REK", "DAC", "RGN", "PNH", "CAS", "ANC",
        "SUV",
    };
    if (id < 0 || id >= APP_CITY_N) id = 0;
    _Static_assert(sizeof(CODE) / sizeof(CODE[0]) == APP_CITY_N, "city code n");
    return CODE[id];
}

void app_time_hm(const struct tm *t, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!t) {
        out[0] = 0;
        return;
    }
    snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
}

void app_time_hms(const struct tm *t, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!t) {
        out[0] = 0;
        return;
    }
    snprintf(out, n, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
}

void app_time_date(const struct tm *t, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!t) {
        out[0] = 0;
        return;
    }
    static const char *const WD[] = {
        "日", "一", "二", "三", "四", "五", "六",
    };
    int w = t->tm_wday;
    if (w < 0 || w > 6) w = 0;
    snprintf(out, n, "%04d.%02d.%02d 周%s",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, WD[w]);
}

int app_city_day(int city_id)
{
    struct tm loc, t;
    app_time_local(&loc);
    app_time_at_off(app_city(city_id)->off, &t);
    int a = loc.tm_year * 400 + loc.tm_yday;
    int b = t.tm_year * 400 + t.tm_yday;
    if (b > a) return 1;
    if (b < a) return -1;
    return 0;
}

void app_city_off_text(int16_t off, char *out, size_t n)
{
    if (!out || n == 0) return;
    int sign = off >= 0 ? 1 : -1;
    int abs = off >= 0 ? off : -off;
    int h = abs / 60;
    int m = abs % 60;
    if (m) snprintf(out, n, "UTC%c%d:%02d", sign > 0 ? '+' : '-', h, m);
    else snprintf(out, n, "UTC%c%d", sign > 0 ? '+' : '-', h);
}
