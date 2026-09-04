#include "app_prefs.h"

#include "app_time.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "app_prefs";
static const char *NS = "dial";

static app_prefs_t s_p;
static app_prefs_t s_saved;
static bool s_dirty;
static int64_t s_dirty_us;

static void clamp_elem(app_elem_t *e)
{
    if (e->scale < 50) e->scale = 50;
    if (e->scale > 200) e->scale = 200;
    if (e->x < 8) e->x = 8;
    if (e->x > 232) e->x = 232;
    if (e->y < 8) e->y = 8;
    if (e->y > 312) e->y = 312;
}

void app_prefs_face_defaults(app_face_t *f, int style)
{
    memset(f, 0, sizeof(*f));
    switch (style) {
    case APP_FACE_CLASSIC:
        f->city[0] = 8;
        f->city[1] = 12;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 122, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 240, 85 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 296, 95 };
        break;
    case APP_FACE_NEON:
        f->city[0] = 0;
        f->city[1] = 9;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 110, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 188, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 268, 100 };
        break;
    case APP_FACE_WORLD:
        f->city[0] = 12;
        f->city[1] = 8;
        f->city[2] = 0;
        f->city[3] = 16;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 58, 22, 80 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 182, 22, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 176, 100 };
        break;
    case APP_FACE_INK:
        f->city[0] = 1;
        f->city[1] = 45;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 132, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 178, 168, 100 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 292, 95 };
        break;
    case APP_FACE_SPLIT:
        f->city[0] = 14;
        f->city[1] = 55;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 86, 110 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 152, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 236, 112 };
        break;
    case APP_FACE_MODULAR:
        f->city[0] = 12;
        f->city[1] = 8;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 104, 120 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 56, 26, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 244, 100 };
        break;
    case APP_FACE_CALIFORNIA:
        f->city[0] = 55;
        f->city[1] = 0;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 150, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 268, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 150, 100 };
        break;
    case APP_FACE_XLARGE:
        f->city[0] = 0;
        f->city[1] = 8;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 148, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 52, 22, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 300, 85 };
        break;
    case APP_FACE_INFOGRAPH:
        f->city[0] = 12;
        f->city[1] = 8;
        f->city[2] = 0;
        f->city[3] = 16;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 152, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 220, 70 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 160, 100 };
        break;
    case APP_FACE_GMT:
        f->city[0] = 12;
        f->city[1] = 8;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 146, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 26, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 296, 90 };
        break;
    case APP_FACE_HERMES:
        f->city[0] = 12;
        f->city[1] = 0;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 152, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 254, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 298, 90 };
        break;
    case APP_FACE_TERM:
        f->city[0] = 12;
        f->city[1] = 8;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 96, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 160, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 248, 100 };
        break;
    case APP_FACE_XMAS:
        f->city[0] = 12;
        f->city[1] = 8;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 128, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 244, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 296, 92 };
        break;
    case APP_FACE_ASTRO:
        f->city[0] = 8;
        f->city[1] = 12;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 132, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 244, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 296, 92 };
        break;
    case APP_FACE_NUMERAL:
        f->city[0] = 8;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 113, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 197, 100 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 243, 100 };
        break;
    case APP_FACE_ROUND:
        f->city[0] = 12;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 140, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 210, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 255, 100 };
        break;
    case APP_FACE_TUBE:
        f->city[0] = 0;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 88, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 212, 100 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 251, 100 };
        break;
    case APP_FACE_BANDS:
        f->city[0] = 9;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 75, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 170, 100 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 232, 100 };
        break;
    default:
        f->city[0] = 12;
        f->city[1] = 8;
        f->elem[APP_ELEM_TIME] = (app_elem_t){ 120, 146, 100 };
        f->elem[APP_ELEM_DATE] = (app_elem_t){ 120, 26, 80 };
        f->elem[APP_ELEM_WORLD] = (app_elem_t){ 120, 296, 90 };
        break;
    }
}

static void set_defaults(void)
{
    memset(&s_p, 0, sizeof(s_p));
    s_p.brightness = 50;
    s_p.sleep_sec = 30;
    s_p.face = 0;
    s_p.tz_off = 480;
    strlcpy(s_p.tz_name, "Asia/Shanghai", sizeof(s_p.tz_name));
    for (int i = 0; i < APP_FACE_CUSTOM; i++) {
        app_prefs_face_defaults(&s_p.faces[i], i);
    }
    app_prefs_custom_defaults(&s_p.custom);
}

void app_prefs_custom_defaults(app_custom_t *c)
{
    memset(c, 0, sizeof(*c));
    c->n = 4;
    c->canvas = 0x000000;
    c->comp[0] = (app_comp_t){
        .type = APP_COMP_ANALOG, .scale = 100, .x = 120, .y = 122,
        .fg = 0xF5F5F7, .acc = 0xFF453A
    };
    c->comp[1] = (app_comp_t){
        .type = APP_COMP_DATE, .scale = 85, .bg_opa = 255, .radius = 8,
        .x = 120, .y = 240, .fg = 0xF5F5F7, .acc = 0x1C1C1E
    };
    c->comp[2] = (app_comp_t){
        .type = APP_COMP_WORLD, .city = 8, .scale = 90, .bg_opa = 255, .radius = 8,
        .x = 64, .y = 296, .fg = 0xF5F5F7, .acc = 0x1C1C1E
    };
    c->comp[3] = (app_comp_t){
        .type = APP_COMP_WORLD, .city = 12, .scale = 90, .bg_opa = 255, .radius = 8,
        .x = 176, .y = 296, .fg = 0xF5F5F7, .acc = 0x1C1C1E
    };
}

void app_prefs_load(void)
{
    set_defaults();
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        s_saved = s_p;
        return;
    }
    uint8_t u8;
    uint16_t u16;
    int16_t i16;
    if (nvs_get_u8(h, "bl", &u8) == ESP_OK && u8 >= 10 && u8 <= 100) {
        s_p.brightness = u8;
    }
    if (nvs_get_u16(h, "sleep", &u16) == ESP_OK) s_p.sleep_sec = u16;
    if (nvs_get_u8(h, "face", &u8) == ESP_OK && u8 < APP_FACE_N) s_p.face = u8;
    if (nvs_get_i16(h, "tzoff", &i16) == ESP_OK && i16 >= -720 && i16 <= 840) {
        s_p.tz_off = i16;
    }
    size_t n = sizeof(s_p.tz_name);
    nvs_get_str(h, "tzn", s_p.tz_name, &n);
    n = sizeof(s_p.faces);
    bool ok = nvs_get_blob(h, "face7", s_p.faces, &n) == ESP_OK && n == sizeof(s_p.faces);
    if (!ok) {
        n = sizeof(app_face_t) * APP_FACE_PREV;
        bool old6 = nvs_get_blob(h, "face6", s_p.faces, &n) == ESP_OK &&
                    n == sizeof(app_face_t) * APP_FACE_PREV;
        if (old6 && s_p.face == APP_FACE_PREV) s_p.face = APP_FACE_CUSTOM;
        bool old = old6;
        if (!old) {
            n = sizeof(app_face_t) * APP_FACE_PREV;
            old = nvs_get_blob(h, "face5", s_p.faces, &n) == ESP_OK &&
                  n == sizeof(app_face_t) * APP_FACE_PREV;
        }
        if (!old) {
            n = sizeof(app_face_t) * APP_FACE_PREV;
            old = nvs_get_blob(h, "face4", s_p.faces, &n) == ESP_OK &&
                  n == sizeof(app_face_t) * APP_FACE_PREV;
        }
        if (!old) {
            n = sizeof(app_face_t) * APP_FACE_PREV;
            old = nvs_get_blob(h, "face3", s_p.faces, &n) == ESP_OK &&
                  n == sizeof(app_face_t) * APP_FACE_PREV;
        }
        if (!old) {
            n = sizeof(app_face_t) * APP_FACE_OLD;
            old = nvs_get_blob(h, "face2", s_p.faces, &n) == ESP_OK &&
                  n == sizeof(app_face_t) * APP_FACE_OLD;
        }
        if (old && !old6) {
            for (int i = 0; i < APP_FACE_CUSTOM; i++) {
                app_face_t tmp;
                uint8_t city[APP_CITY_MAX];
                memcpy(city, s_p.faces[i].city, sizeof(city));
                app_prefs_face_defaults(&tmp, i);
                s_p.faces[i] = tmp;
                memcpy(s_p.faces[i].city, city, sizeof(city));
            }
        }
        ok = old;
    }
    if (ok) {
        for (int i = 0; i < APP_FACE_CUSTOM; i++) {
            for (int k = 0; k < APP_CITY_MAX; k++) {
                if (s_p.faces[i].city[k] >= APP_CITY_N &&
                    s_p.faces[i].city[k] != APP_CITY_OFF) {
                    s_p.faces[i].city[k] = 0;
                }
            }
            for (int k = 0; k < APP_ELEM_N; k++) {
                clamp_elem(&s_p.faces[i].elem[k]);
            }
        }
    }
    n = sizeof(s_p.custom);
    if (nvs_get_blob(h, "cust", &s_p.custom, &n) != ESP_OK || n != sizeof(s_p.custom)) {
        app_prefs_custom_defaults(&s_p.custom);
    }
    if (s_p.custom.n > APP_COMP_MAX) s_p.custom.n = APP_COMP_MAX;
    for (int i = 0; i < APP_COMP_MAX; i++) {
        app_comp_t *c = &s_p.custom.comp[i];
        if (c->type > APP_COMP_XL) c->type = APP_COMP_NONE;
        if (c->scale < 50) c->scale = 50;
        if (c->scale > 200) c->scale = 200;
        if (c->type == APP_COMP_ANALOG) {
            if (c->font > APP_ANA_MAX) c->font = APP_ANA_CLASSIC;
        } else if (c->font > APP_FONT_LAT20) {
            c->font = APP_FONT_AUTO;
        }
        if (c->weight > 1) c->weight = 1;
        if (c->shadow > 3) c->shadow = 3;
        if (c->radius > 20) c->radius = 20;
        if (c->x < 8) c->x = 8;
        if (c->x > 232) c->x = 232;
        if (c->y < 8) c->y = 8;
        if (c->y > 312) c->y = 312;
        if (c->city >= APP_CITY_N && c->city != APP_CITY_OFF) c->city = 0;
    }
    nvs_close(h);
    s_saved = s_p;
}

static bool prefs_write(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = ESP_OK;
    bool ch = false;
#define SAVE(field, key, fn) do { \
    if (s_p.field != s_saved.field) { \
        ch = true; \
        if (e == ESP_OK) e = fn(h, key, s_p.field); \
    } \
} while (0)
    SAVE(brightness, "bl", nvs_set_u8);
    SAVE(sleep_sec, "sleep", nvs_set_u16);
    SAVE(face, "face", nvs_set_u8);
    SAVE(tz_off, "tzoff", nvs_set_i16);
#undef SAVE
    if (strcmp(s_p.tz_name, s_saved.tz_name) != 0) {
        ch = true;
        if (e == ESP_OK) e = nvs_set_str(h, "tzn", s_p.tz_name);
    }
    if (memcmp(s_p.faces, s_saved.faces, sizeof(s_p.faces)) != 0) {
        ch = true;
        if (e == ESP_OK) {
            e = nvs_set_blob(h, "face7", s_p.faces, sizeof(s_p.faces));
        }
    }
    if (memcmp(&s_p.custom, &s_saved.custom, sizeof(s_p.custom)) != 0) {
        ch = true;
        if (e == ESP_OK) {
            e = nvs_set_blob(h, "cust", &s_p.custom, sizeof(s_p.custom));
        }
    }
    if (ch && e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "save %s", esp_err_to_name(e));
    return e == ESP_OK;
}

void app_prefs_flush(void)
{
    if (!s_dirty) return;
    if (prefs_write()) {
        s_saved = s_p;
        s_dirty = false;
        s_dirty_us = 0;
    }
}

void app_prefs_save(void)
{
    s_dirty = true;
    s_dirty_us = esp_timer_get_time();
}

void app_prefs_tick(void)
{
    if (s_dirty && esp_timer_get_time() - s_dirty_us >= 1000000) {
        app_prefs_flush();
    }
}

app_prefs_t *app_prefs(void)
{
    return &s_p;
}

app_custom_t *app_prefs_custom(void)
{
    return &s_p.custom;
}

void app_prefs_apply_display(void)
{
    bsp_display_backlight(s_p.brightness);
}
