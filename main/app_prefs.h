#pragma once

#include <stdint.h>

#define APP_FACE_N      19
#define APP_FACE_CUSTOM 18
#define APP_FACE_OLD    10
#define APP_FACE_PREV   14
#define APP_COMP_MAX    8

#define APP_COMP_NONE   0
#define APP_COMP_TIME   1
#define APP_COMP_DATE   2
#define APP_COMP_WORLD  3
#define APP_COMP_ANALOG 4
#define APP_COMP_NEON   5
#define APP_COMP_XL     6

#define APP_ST_PILL     0x0001u
#define APP_ST_NUMS     0x0002u
#define APP_ST_GMT      0x0004u
#define APP_ST_HIDE     0x0008u
#define APP_ST_NO_SEC   0x0010u
#define APP_ST_NO_MIN   0x0020u
#define APP_ST_NO_HOUR  0x0040u
#define APP_ST_NO_TICK  0x0080u
#define APP_ST_NO_RING  0x0100u
#define APP_ST_NO_CAP   0x0200u
#define APP_ST_ABBR     0x0400u
#define APP_ST_WRAP     0x0800u
#define APP_ST_HMS      0x1000u
#define APP_ST_DFMT_SHIFT 14
#define APP_ST_DFMT_MASK  0xC000u

#define APP_ANA_CLASSIC 0
#define APP_ANA_CAL     1
#define APP_ANA_INK     2
#define APP_ANA_GMT     3
#define APP_ANA_HERMES  4
#define APP_ANA_XMAS    5
#define APP_ANA_ASTRO   6
#define APP_ANA_COLOR   7
#define APP_ANA_MAX     7

#define APP_DFMT_FULL  0
#define APP_DFMT_WD    1
#define APP_DFMT_ISO   2
#define APP_DFMT_SOL   3

#define APP_FONT_AUTO  0
#define APP_FONT_CJK   1
#define APP_FONT_LAT14 2
#define APP_FONT_LAT20 3

#define APP_CITY_MAX 4
#define APP_CITY_OFF 255
#define APP_ELEM_N   3
#define APP_TZ_NAME  40

#define APP_ELEM_TIME  0
#define APP_ELEM_DATE  1
#define APP_ELEM_WORLD 2

#define APP_FACE_CLASSIC    0
#define APP_FACE_NEON       1
#define APP_FACE_WORLD      2
#define APP_FACE_INK        3
#define APP_FACE_SPLIT      4
#define APP_FACE_MODULAR    5
#define APP_FACE_CALIFORNIA 6
#define APP_FACE_XLARGE     7
#define APP_FACE_INFOGRAPH  8
#define APP_FACE_GMT        9
#define APP_FACE_HERMES     10
#define APP_FACE_TERM       11
#define APP_FACE_XMAS       12
#define APP_FACE_ASTRO      13
#define APP_FACE_NUMERAL    14
#define APP_FACE_ROUND      15
#define APP_FACE_TUBE       16
#define APP_FACE_BANDS      17

typedef struct {
    uint8_t type;
    uint8_t city;
    uint8_t scale;
    uint8_t font;
    uint8_t weight;
    uint8_t shadow;
    uint8_t bg_opa;
    uint8_t radius;
    uint16_t style;
    int16_t x;
    int16_t y;
    uint32_t fg;
    uint32_t acc;
} app_comp_t;

typedef struct {
    uint8_t n;
    uint8_t has_bg;
    uint8_t pad[2];
    uint32_t canvas;
    app_comp_t comp[APP_COMP_MAX];
} app_custom_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t scale;
} app_elem_t;

typedef struct {
    uint8_t city[APP_CITY_MAX];
    app_elem_t elem[APP_ELEM_N];
} app_face_t;

typedef struct {
    uint8_t brightness;
    uint16_t sleep_sec;
    uint8_t face;
    int16_t tz_off;
    char tz_name[APP_TZ_NAME];
    app_face_t faces[APP_FACE_CUSTOM];
    app_custom_t custom;
} app_prefs_t;

void app_prefs_load(void);
void app_prefs_save(void);
void app_prefs_flush(void);
void app_prefs_tick(void);
app_prefs_t *app_prefs(void);
void app_prefs_apply_display(void);
void app_prefs_face_defaults(app_face_t *f, int style);
void app_prefs_custom_defaults(app_custom_t *c);
app_custom_t *app_prefs_custom(void);
