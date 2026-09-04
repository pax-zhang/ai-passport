#include "app_dial.h"

#include "app_bg.h"
#include "app_prefs.h"
#include "app_time.h"
#include "bsp_battery.h"
#include "ui_pixel.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FACE_W 240
#define FACE_H 320

#define ANA_TICKS  1u
#define ANA_NUMS   2u
#define ANA_COLOR  4u
#define ANA_GMT    8u
#define ANA_XMAS   16u
#define ANA_ASTRO  32u
#define ANA_HERMES  64u
#define ANA_NO_SEC  128u
#define ANA_NO_MIN  256u
#define ANA_NO_HOUR 512u
#define ANA_NO_RING 1024u
#define ANA_NO_CAP  2048u
#define ANA_INK     4096u
#define ANA_ROUND   8192u

static lv_obj_t *s_root;
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_world;
static lv_obj_t *s_clk;
static lv_obj_t *s_sec;
static lv_obj_t *s_bat;
static lv_obj_t *s_colon;
static lv_obj_t *s_world_lab[APP_CITY_MAX];
static lv_obj_t *s_comp_lab[APP_COMP_MAX];
static uint8_t s_comp_kind[APP_COMP_MAX];
static lv_obj_t *s_bgimg;
static lv_timer_t *s_bg_tm;
static lv_timer_t *s_hand_tm;
static int s_bg_fr;
static uint32_t s_draw_fg;
static uint32_t s_draw_acc;
static uint32_t s_draw_mute;
static unsigned s_ana_flags;
static int s_style = -1;
static int s_last_sec = -1;
static int s_last_min = -1;
static volatile bool s_reload;
static int s_digit_mode;
static int s_date_cx;
static int s_date_cy;
static int s_date_mw;
static int s_date_mh;
static char s_time_txt[16];
static char s_date_txt[32];

static const uint32_t FACE_BG[APP_FACE_N] = {
    0x000000, 0x121510, 0x071018, 0xF3E6C8, 0x0A0A0C,
    0x000000, 0x1A120C, 0x000000, 0x1C1C1E, 0x070B10,
    0xF4E4C8, 0x050805, 0x141008, 0x05070C,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
};
static const uint32_t FACE_FG[APP_FACE_N] = {
    0xF5F5F7, 0xC6F24A, 0xE8EEF4, 0x2A1810, 0xF2F2F2,
    0xF5F5F7, 0xF3E6D0, 0xF5F5F7, 0xF5F5F7, 0xE8EEF4,
    0xF4E4C8, 0x33FF66, 0xF3E6C8, 0xF2F2F2,
    0xF5F5F7, 0xF5F5F7, 0xF5F5F7, 0xF5F5F7, 0xF5F5F7,
};
static const uint32_t FACE_MUTE[APP_FACE_N] = {
    0x636366, 0x5A6B40, 0x6B8494, 0x8A7060, 0x8E8E93,
    0x8E8E93, 0xA89080, 0x3A3A3C, 0x8E8E93, 0x4A5A66,
    0x8A9BB8, 0x1F7A38, 0x8A7060, 0x6B7380,
    0x8E8E93, 0x8E8E93, 0x8E8E93, 0x8E8E93, 0x8E8E93,
};
static const uint32_t FACE_ACCENT[APP_FACE_N] = {
    0xFF453A, 0x3DDCFF, 0x5AC8FA, 0xC41E3A, 0xFFD60A,
    0x0A84FF, 0xE8D5A3, 0xFF9F0A, 0x64D2FF, 0xFF9F0A,
    0xFF6A00, 0x33FF66, 0xC41E3A, 0xFC3D21,
    0x0A84FF, 0xFF9F0A, 0x0A84FF, 0x0A84FF, 0xFF453A,
};

static const uint8_t SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

static void analog_build(const app_elem_t *te, int base, unsigned flags);
static void digit_mask(lv_layer_t *layer, int x, int y, int dw, int dh, uint8_t m,
                       uint32_t c);
static void digit_lcd(lv_layer_t *layer, int x, int y, int dw, int dh, int n,
                      uint32_t on, uint32_t off);

static int isin(int deg)
{
    static const int16_t T[91] = {
        0, 3, 6, 8, 11, 14, 17, 19, 22, 25, 28, 31, 33, 36, 39, 41,
        44, 47, 49, 52, 55, 57, 60, 63, 65, 68, 70, 73, 75, 78, 80, 82,
        85, 87, 89, 92, 94, 96, 99, 101, 103, 105, 107, 109, 111, 113, 115, 117,
        119, 121, 123, 124, 126, 128, 129, 131, 133, 134, 136, 137, 139, 140, 141, 143,
        144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 155, 156, 157, 157,
        158, 158, 158, 159, 159, 159, 160, 160, 160, 160, 160,
    };
    deg %= 360;
    if (deg < 0) deg += 360;
    int q = deg / 90;
    int r = deg % 90;
    int s = T[q == 1 || q == 3 ? 90 - r : r];
    if (q >= 2) s = -s;
    return s;
}

static int icos(int deg)
{
    return isin(deg + 90);
}

static void place(lv_obj_t *obj, const app_elem_t *e, int w, int h)
{
    if (!obj || !e) return;
    lv_obj_set_pos(obj, e->x - w / 2, e->y - h / 2);
    lv_obj_set_size(obj, w, h);
}

static void fit_lab(lv_obj_t *obj, int cx, int cy, int min_w, int min_h)
{
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_update_layout(obj);
    int w = (int)lv_obj_get_width(obj);
    int h = (int)lv_obj_get_height(obj);
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, cx - w / 2, cy - h / 2);
}

static const lv_font_t *comp_font(const app_comp_t *c)
{
    uint8_t f = c->font;
    if (f == APP_FONT_AUTO) {
        f = (c->type == APP_COMP_TIME) ? APP_FONT_LAT20 : APP_FONT_CJK;
    }
    if (f == APP_FONT_LAT14) return ui_pixel_font_14();
    if (f == APP_FONT_LAT20) return ui_pixel_font_20();
    return ui_pixel_font_cjk();
}

static void text_fx(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_BEGIN) return;
    lv_obj_t *obj = lv_event_get_target(e);
    const char *txt = lv_label_get_text(obj);
    if (!txt || !txt[0]) return;
    uintptr_t u = (uintptr_t)lv_event_get_user_data(e);
    unsigned sh = (unsigned)(u & 3u);
    unsigned bold = (unsigned)((u >> 2) & 1u);
    if (!sh && !bold) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_content_coords(obj, &box);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = lv_obj_get_style_text_font(obj, 0);
    d.align = LV_TEXT_ALIGN_CENTER;
    d.text = txt;
    if (sh) {
        d.color = lv_color_hex(0x000000);
        d.opa = (lv_opa_t)(sh == 1 ? LV_OPA_40 : (sh == 2 ? LV_OPA_70 : LV_OPA_COVER));
        d.ofs_x = sh >= 2 ? 2 : 1;
        d.ofs_y = sh >= 2 ? 2 : 1;
        lv_draw_label(layer, &d, &box);
    }
    if (bold) {
        d.color = lv_obj_get_style_text_color(obj, 0);
        d.opa = LV_OPA_COVER;
        d.ofs_x = 1;
        d.ofs_y = 0;
        lv_draw_label(layer, &d, &box);
    }
}

static lv_obj_t *lab(lv_obj_t *p, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *o = lv_label_create(p);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(o, "");
    return o;
}

static int scaled(int base, uint8_t scale)
{
    int v = base * (int)scale / 100;
    if (v < 8) v = 8;
    return v;
}

static int box_h(const lv_font_t *font, int lines, int pad_v, int min_h)
{
    int lh = font ? (int)lv_font_get_line_height(font) : 16;
    if (lines < 1) lines = 1;
    int h = lh * lines + pad_v * 2 + 2;
    if (h < min_h) h = min_h;
    return h;
}

static bool analog_style(int st)
{
    return st == APP_FACE_CLASSIC || st == APP_FACE_CALIFORNIA ||
           st == APP_FACE_INFOGRAPH || st == APP_FACE_GMT ||
           st == APP_FACE_HERMES || st == APP_FACE_XMAS ||
           st == APP_FACE_ASTRO || st == APP_FACE_INK ||
           st == APP_FACE_ROUND;
}

static bool pack_style(int st)
{
    return st == APP_FACE_NUMERAL || st == APP_FACE_TUBE ||
           st == APP_FACE_BANDS;
}

static void draw_seg(lv_layer_t *layer, int x0, int y0, int x1, int y1,
                     int w, uint32_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(c);
    d.width = (int32_t)w;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = x0;
    d.p1.y = y0;
    d.p2.x = x1;
    d.p2.y = y1;
    lv_draw_line(layer, &d);
}

static void analog_hand(lv_layer_t *layer, int cx, int cy, int r, int deg,
                        int len_pct, int tail_pct, int w, uint32_t col)
{
    int len = r * len_pct / 100;
    int tail = r * tail_pct / 100;
    draw_seg(layer,
             cx - icos(deg) * tail / 160,
             cy + isin(deg) * tail / 160,
             cx + icos(deg) * len / 160,
             cy - isin(deg) * len / 160, w, col);
}

static void draw_ring(lv_layer_t *layer, int cx, int cy, int r, int bw, uint32_t c)
{
    lv_draw_rect_dsc_t ring;
    lv_draw_rect_dsc_init(&ring);
    ring.bg_opa = LV_OPA_TRANSP;
    ring.border_width = bw;
    ring.border_color = lv_color_hex(c);
    ring.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = { cx - r, cy - r, cx + r - 1, cy + r - 1 };
    lv_draw_rect(layer, &ring, &a);
}

static void draw_dot(lv_layer_t *layer, int x, int y, int rad, uint32_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(c);
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = { x - rad, y - rad, x + rad, y + rad };
    lv_draw_rect(layer, &d, &a);
}

static void bg_tm_del(void)
{
    if (s_bg_tm) {
        lv_timer_delete(s_bg_tm);
        s_bg_tm = NULL;
    }
    s_bg_fr = 0;
}

static void hand_tm_del(void)
{
    if (s_hand_tm) {
        lv_timer_delete(s_hand_tm);
        s_hand_tm = NULL;
    }
}

static void hand_tick(lv_timer_t *t)
{
    (void)t;
    if (s_clk) lv_obj_invalidate(s_clk);
}

static void bg_anim_draw(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    app_bg_draw(lv_event_get_layer(e), &box, s_bg_fr);
}

static void bg_anim_tick(lv_timer_t *t)
{
    (void)t;
    int n = app_bg_nframes();
    if (n < 2 || !s_bgimg) return;
    s_bg_fr++;
    if (s_bg_fr >= n) s_bg_fr = 0;
    lv_timer_set_period(s_bg_tm, app_bg_delay_ms(s_bg_fr));
    lv_obj_invalidate(s_bgimg);
}

static void hermes_bg(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    if (s_style != APP_FACE_HERMES) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    lv_draw_triangle_dsc_t td;
    lv_draw_triangle_dsc_init(&td);
    td.color = lv_color_hex(0xFF6A00);
    td.p[0].x = box.x1;
    td.p[0].y = box.y1;
    td.p[1].x = box.x1 + FACE_W + 10;
    td.p[1].y = box.y1;
    td.p[2].x = box.x1;
    td.p[2].y = box.y1 + FACE_W + 10;
    lv_draw_triangle(layer, &td);
}

static void analog_nums(lv_layer_t *layer, int cx, int cy, int r, uint32_t col)
{
    static const char *const NUM[4] = { "12", "3", "6", "9" };
    static const int ND[4] = { 90, 0, 270, 180 };
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = ui_pixel_font_20();
    ld.color = lv_color_hex(col);
    ld.align = LV_TEXT_ALIGN_CENTER;
    for (int i = 0; i < 4; i++) {
        int nx = cx + icos(ND[i]) * (r - 22) / 160;
        int ny = cy - isin(ND[i]) * (r - 22) / 160;
        lv_area_t ta = { nx - 14, ny - 12, nx + 13, ny + 11 };
        ld.text = NUM[i];
        ld.text_local = 1;
        lv_draw_label(layer, &ld, &ta);
    }
}

static void analog_draw(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    int w = lv_area_get_width(&box);
    int cx = box.x1 + w / 2;
    int cy = box.y1 + w / 2;
    int r = w / 2 - 4;
    if (r < 20) r = 20;
    uint32_t fg = s_draw_fg;
    uint32_t acc = s_draw_acc;
    uint32_t mute = s_draw_mute;
    unsigned flags = s_ana_flags;
    static const uint32_t COL[12] = {
        0xFF453A, 0xFF9F0A, 0xFFD60A, 0x30D158,
        0x64D2FF, 0x0A84FF, 0x5E5CE6, 0xBF5AF2,
        0xFF2D55, 0xFF9F0A, 0x30D158, 0x64D2FF,
    };

    if (flags & ANA_ASTRO) {
        static const int8_t ST[][2] = {
            { -38, -22 }, { 28, -40 }, { -12, 36 }, { 40, 8 }, { -44, 14 },
            { 16, 42 }, { -24, -48 }, { 8, -18 }, { 36, -12 }, { -8, 22 },
            { 22, 28 }, { -32, 4 }, { 4, 48 }, { -18, -8 }, { 48, -28 },
        };
        for (unsigned i = 0; i < sizeof(ST) / sizeof(ST[0]); i++) {
            draw_dot(layer, cx + ST[i][0], cy + ST[i][1], i % 4 == 0 ? 2 : 1, 0xE8EEF4);
        }
        draw_ring(layer, cx, cy, r - 2, 1, acc);
        for (int i = 0; i < 12; i++) {
            int deg = 90 - i * 30;
            draw_dot(layer,
                     cx + icos(deg) * (r - 8) / 160,
                     cy - isin(deg) * (r - 8) / 160, 2, fg);
        }
    }
    if (flags & ANA_XMAS) {
        static const int8_t SN[][2] = {
            { -30, -28 }, { 22, -36 }, { -8, 32 }, { 34, 16 }, { -40, 6 },
            { 12, 40 }, { -20, -44 }, { 6, -10 }, { 28, -20 }, { -16, 18 },
        };
        for (unsigned i = 0; i < sizeof(SN) / sizeof(SN[0]); i++) {
            draw_dot(layer, cx + SN[i][0], cy + SN[i][1], 1, 0xF5F5F7);
        }
        for (int i = 0; i < 24; i++) {
            int deg = 90 - i * 15;
            uint32_t col = (i & 1) ? 0x1B7A3A : acc;
            draw_seg(layer,
                     cx + icos(deg) * (r - 14) / 160,
                     cy - isin(deg) * (r - 14) / 160,
                     cx + icos(deg) * r / 160,
                     cy - isin(deg) * r / 160, 4, col);
        }
        draw_ring(layer, cx, cy, r - 16, 1, 0xD4A017);
    }
    if (flags & ANA_HERMES) {
        analog_nums(layer, cx, cy, r, 0x0B1A32);
    }
    if (flags & ANA_GMT) {
        for (int i = 0; i < 24; i++) {
            int hr = (12 + i) % 24;
            int day = (hr >= 6 && hr < 18);
            uint32_t col = day ? 0xC41E3A : 0x1E4A8A;
            int deg0 = 90 - i * 15;
            for (int k = 0; k < 7; k++) {
                int deg = deg0 - k * 2;
                draw_seg(layer,
                         cx + icos(deg) * (r - 13) / 160,
                         cy - isin(deg) * (r - 13) / 160,
                         cx + icos(deg) * (r + 2) / 160,
                         cy - isin(deg) * (r + 2) / 160, 3, col);
            }
        }
        draw_dot(layer, cx, cy, r - 14, 0x070B10);
        draw_seg(layer, cx - 6, cy - r + 10, cx, cy - r - 1, 2, fg);
        draw_seg(layer, cx + 6, cy - r + 10, cx, cy - r - 1, 2, fg);
        draw_seg(layer, cx - 6, cy - r + 10, cx + 6, cy - r + 10, 2, fg);
        for (int i = 0; i < 12; i++) {
            int deg = 90 - i * 30;
            draw_seg(layer,
                     cx + icos(deg) * (r - 24) / 160,
                     cy - isin(deg) * (r - 24) / 160,
                     cx + icos(deg) * (r - 18) / 160,
                     cy - isin(deg) * (r - 18) / 160, 2, fg);
        }
    }
    if ((flags & ANA_NUMS) && !(flags & ANA_HERMES) && !(flags & ANA_ROUND)) {
        for (int i = 0; i < 60; i++) {
            if (i % 15 == 0) continue;
            int deg = 90 - i * 6;
            int major = (i % 5 == 0);
            draw_seg(layer,
                     cx + icos(deg) * (r - (major ? 6 : 4)) / 160,
                     cy - isin(deg) * (r - (major ? 6 : 4)) / 160,
                     cx + icos(deg) * r / 160,
                     cy - isin(deg) * r / 160, 1, mute);
            if (major) {
                draw_seg(layer,
                         cx + icos(deg) * (r - 16) / 160,
                         cy - isin(deg) * (r - 16) / 160,
                         cx + icos(deg) * (r - 8) / 160,
                         cy - isin(deg) * (r - 8) / 160, 3, fg);
            }
        }
        analog_nums(layer, cx, cy, r, fg);
    }
    if (flags & ANA_COLOR) {
        for (int i = 0; i < 12; i++) {
            int deg = 90 - i * 30;
            draw_seg(layer,
                     cx + icos(deg) * (r - 14) / 160,
                     cy - isin(deg) * (r - 14) / 160,
                     cx + icos(deg) * r / 160,
                     cy - isin(deg) * r / 160, 5, COL[i]);
        }
    }
    if (flags & ANA_ROUND) {
        draw_ring(layer, cx, cy, r, 1, mute);
        for (int i = 0; i < 12; i++) {
            if (i % 3 == 0) continue;
            int deg = 90 - i * 30;
            draw_seg(layer,
                     cx + icos(deg) * (r - 10) / 160,
                     cy - isin(deg) * (r - 10) / 160,
                     cx + icos(deg) * r / 160,
                     cy - isin(deg) * r / 160, 2, mute);
        }
        analog_nums(layer, cx, cy, r, fg);
    }
    if ((flags & ANA_TICKS) && !(flags & ANA_GMT) && !(flags & ANA_XMAS) &&
        !(flags & ANA_ASTRO) && !(flags & ANA_COLOR) && !(flags & ANA_INK) &&
        !(flags & ANA_NUMS) && !(flags & ANA_ROUND)) {
        if (!(flags & ANA_NO_RING)) draw_ring(layer, cx, cy, r + 1, 1, fg);
        for (int i = 0; i < 60; i++) {
            int deg = 90 - i * 6;
            int major = (i % 5 == 0);
            draw_seg(layer,
                     cx + icos(deg) * (r - (major ? 10 : 4)) / 160,
                     cy - isin(deg) * (r - (major ? 10 : 4)) / 160,
                     cx + icos(deg) * r / 160,
                     cy - isin(deg) * r / 160, major ? 2 : 1, major ? fg : mute);
        }
    }

    struct tm t;
    app_time_local(&t);
    int hdeg = 90 - (t.tm_hour % 12) * 30 - t.tm_min / 2;
    int mdeg = 90 - t.tm_min * 6 - t.tm_sec / 10;
    int sdeg = 90 - t.tm_sec * 6;
    uint32_t hf = fg;
    int hw = 4, hl = 54, ht = 12;
    int mw = 3, ml = 76, mt = 14;
    int sw = 2, sl = 88, st = 18;
    if (flags & ANA_INK) {
        hf = 0x2A1810;
        hw = 8;
        hl = 48;
        ht = 8;
        mw = 5;
        ml = 78;
        mt = 10;
    } else if (flags & ANA_HERMES) {
        hf = 0x0B1A32;
        hw = 5;
        hl = 50;
        mw = 3;
        ml = 72;
    } else if (flags & ANA_NUMS) {
        hw = 5;
        hl = 50;
        ht = 10;
        mw = 4;
        ml = 72;
        mt = 12;
    } else if (flags & ANA_GMT) {
        hw = 6;
        hl = 48;
        ht = 10;
        mw = 3;
        ml = 70;
        sw = 2;
        sl = 80;
        st = 16;
    } else if (flags & ANA_ASTRO) {
        hw = 2;
        hl = 50;
        mw = 2;
        ml = 76;
        sw = 1;
        sl = 90;
    } else if (flags & ANA_XMAS) {
        hf = 0xD4A017;
        hw = 4;
        mw = 3;
    } else if (flags & ANA_ROUND) {
        hw = 6;
        hl = 67;
        ht = 10;
        mw = 4;
        ml = 85;
        mt = 12;
        sw = 2;
        sl = 94;
        st = 16;
    } else if (flags & ANA_COLOR) {
        hw = 3;
        hl = 46;
        mw = 2;
        ml = 70;
        sw = 2;
        sl = 78;
    } else {
        hw = 3;
        hl = 52;
        mw = 2;
        ml = 74;
        sw = 1;
        sl = 86;
        st = 20;
    }
    if (!(flags & ANA_NO_HOUR)) {
        analog_hand(layer, cx, cy, r, hdeg, hl, ht, hw, hf);
        if (flags & ANA_GMT) {
            draw_dot(layer,
                     cx + icos(hdeg) * (r * 30 / 100) / 160,
                     cy - isin(hdeg) * (r * 30 / 100) / 160, 4, hf);
        }
    }
    if (!(flags & ANA_NO_MIN)) {
        analog_hand(layer, cx, cy, r, mdeg, ml, mt, mw, hf);
    }
    if (!(flags & ANA_NO_SEC)) {
        analog_hand(layer, cx, cy, r, sdeg, sl, st, sw,
                    (flags & ANA_GMT) ? fg : acc);
    }
    uint8_t gcity = APP_CITY_OFF;
    if (s_style != APP_FACE_CUSTOM) gcity = app_prefs()->faces[s_style].city[0];
    else {
        const app_custom_t *cu = app_prefs_custom();
        uint8_t any = APP_CITY_OFF;
        for (int i = 0; i < cu->n && i < APP_COMP_MAX; i++) {
            const app_comp_t *ac = &cu->comp[i];
            if (ac->type != APP_COMP_ANALOG) continue;
            if (any == APP_CITY_OFF) any = ac->city;
            if (ac->font == APP_ANA_GMT || (ac->style & APP_ST_GMT)) {
                gcity = ac->city;
                break;
            }
        }
        if (gcity == APP_CITY_OFF) gcity = any;
    }
    if ((flags & ANA_GMT) && gcity != APP_CITY_OFF) {
        const app_city_t *c = app_city(gcity);
        struct tm wtm;
        app_time_at_off(c->off, &wtm);
        int gdeg = 90 - (wtm.tm_hour - 12) * 15 - wtm.tm_min / 4;
        analog_hand(layer, cx, cy, r, gdeg, 90, 8, 3, acc);
    }
    if (!(flags & ANA_NO_CAP) && !(flags & ANA_INK)) {
        lv_draw_rect_dsc_t cap;
        lv_draw_rect_dsc_init(&cap);
        cap.bg_color = lv_color_hex((flags & ANA_HERMES) ? 0x0B1A32 : hf);
        cap.radius = LV_RADIUS_CIRCLE;
        int cr = (flags & ANA_ASTRO) ? 3 : 5;
        lv_area_t hub = { cx - cr, cy - cr, cx + cr - 1, cy + cr - 1 };
        lv_draw_rect(layer, &cap, &hub);
        if (!(flags & ANA_GMT)) {
            cap.bg_color = lv_color_hex(acc);
            lv_area_t ca = { cx - 2, cy - 2, cx + 1, cy + 1 };
            lv_draw_rect(layer, &cap, &ca);
        }
    }
}

static unsigned analog_comp_flags(const app_comp_t *c)
{
    unsigned fl = 0;
    switch (c->font) {
    case APP_ANA_CAL:    fl |= ANA_NUMS; break;
    case APP_ANA_INK:    fl |= ANA_INK; break;
    case APP_ANA_GMT:    fl |= ANA_GMT; break;
    case APP_ANA_HERMES: fl |= ANA_NUMS | ANA_HERMES; break;
    case APP_ANA_XMAS:   fl |= ANA_XMAS; break;
    case APP_ANA_ASTRO:  fl |= ANA_ASTRO; break;
    case APP_ANA_COLOR:  fl |= ANA_COLOR; break;
    default: break;
    }
    if (!(c->style & APP_ST_NO_TICK)) fl |= ANA_TICKS;
    if (c->style & APP_ST_NUMS) fl |= ANA_NUMS;
    if (c->style & APP_ST_GMT) fl |= ANA_GMT;
    if (c->style & APP_ST_NO_SEC) fl |= ANA_NO_SEC;
    if (c->style & APP_ST_NO_MIN) fl |= ANA_NO_MIN;
    if (c->style & APP_ST_NO_HOUR) fl |= ANA_NO_HOUR;
    if (c->style & APP_ST_NO_RING) fl |= ANA_NO_RING;
    if (c->style & APP_ST_NO_CAP) fl |= ANA_NO_CAP;
    return fl;
}

static void custom_date(char *buf, size_t n, const app_comp_t *c, const struct tm *t)
{
    unsigned fmt = (c->style & APP_ST_DFMT_MASK) >> APP_ST_DFMT_SHIFT;
    static const char *const WD[] = { "日", "一", "二", "三", "四", "五", "六" };
    int w = t->tm_wday;
    if (w < 0 || w > 6) w = 0;
    if (fmt == APP_DFMT_WD) {
        snprintf(buf, n, "周%s %d", WD[w], t->tm_mday);
    } else if (fmt == APP_DFMT_ISO) {
        snprintf(buf, n, "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1,
                 t->tm_mday);
    } else if (fmt == APP_DFMT_SOL) {
        snprintf(buf, n, "SOL %03d", t->tm_yday + 1);
    } else {
        app_time_date(t, buf, n);
    }
}

static void custom_world(char *buf, size_t n, const app_comp_t *c)
{
    const app_city_t *city = app_city(c->city);
    struct tm t;
    app_time_at_off(city->off, &t);
    int day = app_city_day(c->city);
    const char *ds = day > 0 ? " 次日" : (day < 0 ? " 昨日" : "");
    const char *name = (c->style & APP_ST_ABBR) ? city->abbr : city->zh;
    if (c->style & APP_ST_WRAP) {
        snprintf(buf, n, "%s\n%02d:%02d%s", name, t.tm_hour, t.tm_min, ds);
    } else {
        snprintf(buf, n, "%s  %02d:%02d%s", name, t.tm_hour, t.tm_min, ds);
    }
}

static void analog_hands(void)
{
    if (s_clk) lv_obj_invalidate(s_clk);
}

#define PACK_HM       1
#define PACK_HM_GHOST 2
#define PACK_SS       3
#define PACK_SS_GHOST 4

static void pack_date_txt(char *buf, size_t n, const struct tm *t, bool short_wd)
{
    static const char *const WD[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
    };
    static const char *const WD2[] = {
        "日", "一", "二", "三", "四", "五", "六",
    };
    int w = t->tm_wday;
    if (w < 0 || w > 6) w = 0;
    if (short_wd) {
        snprintf(buf, n, "%02d-%02d 星期%s", t->tm_mon + 1, t->tm_mday, WD2[w]);
    } else {
        snprintf(buf, n, "%04d-%02d-%02d %s",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, WD[w]);
    }
}

static void pack_world_txt(char *buf, size_t n, uint8_t city_id)
{
    if (city_id == APP_CITY_OFF) {
        buf[0] = 0;
        return;
    }
    const app_city_t *c = app_city(city_id);
    struct tm t;
    app_time_at_off(c->off, &t);
    snprintf(buf, n, "%s %02d:%02d", app_city_code(city_id), t.tm_hour, t.tm_min);
}

static void pack_bat_txt(char *buf, size_t n)
{
    int soc = bsp_battery_soc();
    if (soc < 0) snprintf(buf, n, "BAT --");
    else snprintf(buf, n, "BAT %d%%", soc);
}

static void pack_hm_digits(lv_layer_t *layer, int x, int y, int dw, int dh, int gap,
                           int colon, int hour, int min, uint32_t on, uint32_t off,
                           bool ghost)
{
    int n[4] = { hour / 10, hour % 10, min / 10, min % 10 };
    for (int i = 0; i < 4; i++) {
        if (ghost) digit_lcd(layer, x, y, dw, dh, n[i], on, off);
        else digit_mask(layer, x, y, dw, dh, SEG[n[i]], on);
        x += dw + gap;
        if (i == 1) x += colon;
    }
}

static void pack_lcd_draw(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    uintptr_t mode = (uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int w = lv_area_get_width(&a);
    int h = lv_area_get_height(&a);
    struct tm t;
    app_time_local(&t);
    uint32_t on = s_draw_fg;
    uint32_t off = 0x1A1A1A;
    if (mode == PACK_SS || mode == PACK_SS_GHOST) {
        int dw = (w - 6) / 2;
        int dh = h;
        int n0 = t.tm_sec / 10, n1 = t.tm_sec % 10;
        if (mode == PACK_SS_GHOST) {
            digit_lcd(layer, a.x1, a.y1, dw, dh, n0, on, off);
            digit_lcd(layer, a.x1 + dw + 6, a.y1, dw, dh, n1, on, off);
        } else {
            digit_mask(layer, a.x1, a.y1, dw, dh, SEG[n0], on);
            digit_mask(layer, a.x1 + dw + 6, a.y1, dw, dh, SEG[n1], on);
        }
        return;
    }
    int dw = h * 11 / 20;
    int gap = 6;
    int colon = h / 6;
    if (colon < 10) colon = 10;
    int total = dw * 4 + gap * 3 + colon;
    int x = a.x1 + (w - total) / 2;
    bool ghost = (mode == PACK_HM_GHOST);
    pack_hm_digits(layer, x, a.y1, dw, h, gap, colon, t.tm_hour, t.tm_min, on, off,
                   ghost);
}

static lv_obj_t *pack_lcd(int x, int y, int w, int h, uintptr_t mode)
{
    lv_obj_t *o = lv_obj_create(s_root);
    ui_pixel_strip_theme(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(o, pack_lcd_draw, LV_EVENT_DRAW_MAIN, (void *)mode);
    return o;
}

static void pack_colon(int x, int y, int w, int h, uint32_t c)
{
    s_colon = lv_obj_create(s_root);
    ui_pixel_strip_theme(s_colon);
    lv_obj_set_pos(s_colon, x, y);
    lv_obj_set_size(s_colon, w, h);
    lv_obj_set_style_bg_opa(s_colon, LV_OPA_TRANSP, 0);
    int dw = w / 3;
    if (dw < 4) dw = 4;
    int gap = h / 5;
    lv_obj_t *a = lv_obj_create(s_colon);
    ui_pixel_strip_theme(a);
    lv_obj_set_size(a, dw, dw);
    lv_obj_set_style_bg_color(a, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(a, 1, 0);
    lv_obj_align(a, LV_ALIGN_TOP_MID, 0, gap);
    lv_obj_t *b = lv_obj_create(s_colon);
    ui_pixel_strip_theme(b);
    lv_obj_set_size(b, dw, dw);
    lv_obj_set_style_bg_color(b, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 1, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -gap);
}

static void pack_colon_in(int box_x, int box_w, int y, int h, uint32_t c)
{
    int dw = h * 11 / 20;
    int gap = 6;
    int colon = h / 6;
    if (colon < 10) colon = 10;
    int total = dw * 4 + gap * 3 + colon;
    int x0 = box_x + (box_w - total) / 2;
    pack_colon(x0 + dw * 2 + gap, y, colon, h, c);
}

static void pack_meta(const struct tm *t, bool force)
{
    int mk = t->tm_hour * 60 + t->tm_min;
    if (!force && mk == s_last_min) return;
    s_last_min = mk;
    if (s_clk) lv_obj_invalidate(s_clk);
    if (s_date) {
        pack_date_txt(s_date_txt, sizeof(s_date_txt), t, s_style == APP_FACE_ROUND);
        lv_label_set_text(s_date, s_date_txt);
        fit_lab(s_date, s_date_cx, s_date_cy, s_date_mw, s_date_mh);
    }
    if (s_world) {
        uint8_t id = APP_CITY_OFF;
        if (s_style != APP_FACE_CUSTOM) id = app_prefs()->faces[s_style].city[0];
        pack_world_txt(s_time_txt, sizeof(s_time_txt), id);
        lv_label_set_text(s_world, s_time_txt);
        if (s_time_txt[0]) lv_obj_clear_flag(s_world, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_world, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_bat) {
        char buf[16];
        pack_bat_txt(buf, sizeof(buf));
        lv_label_set_text(s_bat, buf);
    }
}

static void paint_pack(const struct tm *t, bool force)
{
    if (s_colon) {
        if (t->tm_sec % 2 == 0) lv_obj_clear_flag(s_colon, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_colon, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_sec && (force || t->tm_sec != s_last_sec)) {
        if (s_digit_mode == 2) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02d", t->tm_sec);
            lv_label_set_text(s_sec, buf);
        } else {
            lv_obj_invalidate(s_sec);
        }
    }
    s_last_sec = t->tm_sec;
    pack_meta(t, force);
}

static void build_pack(void)
{
    uint32_t fg = FACE_FG[s_style];
    uint32_t mute = FACE_MUTE[s_style];
    uint32_t blue = 0x0A84FF;
    s_draw_fg = fg;
    s_draw_acc = FACE_ACCENT[s_style];
    s_draw_mute = mute;

    if (s_style == APP_FACE_NUMERAL) {
        s_digit_mode = 2;
        s_clk = pack_lcd(0, 70, FACE_W, 86, PACK_HM);
        pack_colon_in(0, FACE_W, 70, 86, fg);
        s_sec = lab(s_root, ui_pixel_font_20(), mute);
        lv_obj_set_pos(s_sec, 172, 78);
        lv_obj_set_size(s_sec, 48, 34);
        s_date = lab(s_root, ui_pixel_font_cjk(), mute);
        s_date_mh = box_h(ui_pixel_font_cjk(), 1, 0, 28);
        s_date_mw = FACE_W;
        s_date_cx = FACE_W / 2;
        s_date_cy = 185 + s_date_mh / 2;
        lv_obj_set_pos(s_date, 0, 185);
        lv_obj_set_size(s_date, FACE_W, s_date_mh);
        s_world = lab(s_root, ui_pixel_font_20(), blue);
        lv_obj_set_pos(s_world, 0, 230);
        lv_obj_set_size(s_world, FACE_W, 28);
        s_bat = lab(s_root, ui_pixel_font_20(), mute);
        lv_obj_set_pos(s_bat, 0, 275);
        lv_obj_set_size(s_bat, FACE_W, 26);
    } else if (s_style == APP_FACE_TUBE) {
        s_clk = pack_lcd(0, 45, FACE_W, 86, PACK_HM_GHOST);
        pack_colon_in(0, FACE_W, 45, 86, fg);
        s_sec = pack_lcd(80, 148, 80, 40, PACK_SS_GHOST);
        s_date = lab(s_root, ui_pixel_font_cjk(), mute);
        s_date_mh = box_h(ui_pixel_font_cjk(), 1, 0, 28);
        s_date_mw = FACE_W;
        s_date_cx = FACE_W / 2;
        s_date_cy = 200 + s_date_mh / 2;
        lv_obj_set_pos(s_date, 0, 200);
        lv_obj_set_size(s_date, FACE_W, s_date_mh);
        s_world = lab(s_root, ui_pixel_font_20(), blue);
        lv_obj_set_style_text_letter_space(s_world, 2, 0);
        lv_obj_set_pos(s_world, 0, 238);
        lv_obj_set_size(s_world, FACE_W, 26);
        s_bat = lab(s_root, ui_pixel_font_20(), mute);
        lv_obj_set_pos(s_bat, 0, 272);
        lv_obj_set_size(s_bat, FACE_W, 26);
    } else {
        s_digit_mode = 2;
        s_clk = pack_lcd(0, 32, 196, 72, PACK_HM);
        pack_colon_in(0, 196, 32, 72, fg);
        s_sec = lab(s_root, ui_pixel_font_20(), mute);
        lv_obj_set_pos(s_sec, 196, 48);
        lv_obj_set_size(s_sec, 40, 28);
        s_date = lab(s_root, ui_pixel_font_cjk(), mute);
        s_date_mh = box_h(ui_pixel_font_cjk(), 1, 0, 40);
        s_date_mw = FACE_W;
        s_date_cx = FACE_W / 2;
        s_date_cy = 150 + s_date_mh / 2;
        lv_obj_set_pos(s_date, 0, 150);
        lv_obj_set_size(s_date, FACE_W, s_date_mh);
        s_world = lab(s_root, ui_pixel_font_20(), blue);
        lv_obj_set_pos(s_world, 0, 220);
        lv_obj_set_size(s_world, FACE_W, 28);
        s_bat = lab(s_root, ui_pixel_font_20(), mute);
        lv_obj_set_pos(s_bat, 0, 255);
        lv_obj_set_size(s_bat, FACE_W, 26);
    }
}

static void build_round(void)
{
    app_elem_t te = { 120, 140, 100 };
    analog_build(&te, 224, ANA_ROUND);
    uint32_t mute = FACE_MUTE[s_style];
    uint32_t blue = 0x0A84FF;
    s_date = lab(s_root, ui_pixel_font_cjk(), mute);
    s_date_mh = box_h(ui_pixel_font_cjk(), 1, 0, 22);
    s_date_mw = 160;
    s_date_cx = 120;
    s_date_cy = 211;
    lv_obj_set_pos(s_date, s_date_cx - s_date_mw / 2, s_date_cy - s_date_mh / 2);
    lv_obj_set_size(s_date, s_date_mw, s_date_mh);
    s_world = lab(s_root, ui_pixel_font_20(), blue);
    lv_obj_set_pos(s_world, 0, 242);
    lv_obj_set_size(s_world, FACE_W, 26);
    s_bat = lab(s_root, ui_pixel_font_20(), mute);
    lv_obj_set_pos(s_bat, 0, 274);
    lv_obj_set_size(s_bat, FACE_W, 26);
    s_hand_tm = lv_timer_create(hand_tick, 200, NULL);
}

static void analog_build(const app_elem_t *te, int base, unsigned flags)
{
    int sz = scaled(base, te->scale);
    s_ana_flags = flags;
    s_clk = lv_obj_create(s_root);
    ui_pixel_strip_theme(s_clk);
    lv_obj_set_style_bg_opa(s_clk, LV_OPA_TRANSP, 0);
    if ((flags & ANA_HERMES) || (flags & ANA_ROUND)) {
        lv_obj_set_style_radius(s_clk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(s_clk, true, 0);
    }
    place(s_clk, te, sz, sz);
    lv_obj_add_event_cb(s_clk, analog_draw, LV_EVENT_DRAW_MAIN, NULL);
}

static void seg_rect(lv_layer_t *layer, int x, int y, int w, int h, uint32_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(c);
    d.radius = 1;
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(layer, &d, &a);
}

static void digit_mask(lv_layer_t *layer, int x, int y, int dw, int dh, uint8_t m,
                       uint32_t c)
{
    int t = dh / 10;
    if (t < 2) t = 2;
    int gap = 1;
    if (m & 0x01) seg_rect(layer, x + t, y, dw - 2 * t, t, c);
    if (m & 0x02) seg_rect(layer, x + dw - t, y + t + gap, t, dh / 2 - t - gap * 2, c);
    if (m & 0x04) seg_rect(layer, x + dw - t, y + dh / 2 + gap, t, dh / 2 - t - gap * 2, c);
    if (m & 0x08) seg_rect(layer, x + t, y + dh - t, dw - 2 * t, t, c);
    if (m & 0x10) seg_rect(layer, x, y + dh / 2 + gap, t, dh / 2 - t - gap * 2, c);
    if (m & 0x20) seg_rect(layer, x, y + t + gap, t, dh / 2 - t - gap * 2, c);
    if (m & 0x40) seg_rect(layer, x + t, y + dh / 2 - t / 2, dw - 2 * t, t, c);
}

static void digit_lcd(lv_layer_t *layer, int x, int y, int dw, int dh, int n,
                      uint32_t on, uint32_t off)
{
    digit_mask(layer, x, y, dw, dh, 0x7F, off);
    uint8_t m = (n >= 0 && n <= 9) ? SEG[n] : 0;
    digit_mask(layer, x, y, dw, dh, m, on);
}

static void neon_draw(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int w = lv_area_get_width(&a);
    int h = lv_area_get_height(&a);
    struct tm t;
    app_time_local(&t);
    uint32_t on = s_draw_fg;
    uint32_t acc = s_draw_acc;
    uint32_t off = (s_style == APP_FACE_XLARGE || s_digit_mode == 1 ||
                    s_style == APP_FACE_SPLIT) ? 0x1C1C1E :
                   (s_style == APP_FACE_TERM ? 0x0A1F0A :
                    (s_style == APP_FACE_NEON ? 0x1A2414 : 0x082824));
    int x0 = a.x1, y0 = a.y1, x1 = a.x2, y1 = a.y2;
    if (s_style == APP_FACE_NEON) {
        lv_draw_rect_dsc_t fr;
        lv_draw_rect_dsc_init(&fr);
        fr.bg_color = lv_color_hex(0x2A3324);
        fr.radius = 8;
        lv_draw_rect(layer, &fr, &a);
        x0 += 8;
        y0 += 8;
        x1 -= 8;
        y1 -= 8;
        fr.bg_color = lv_color_hex(0x0C140C);
        fr.radius = 4;
        lv_area_t inner = { x0, y0, x1, y1 };
        lv_draw_rect(layer, &fr, &inner);
        seg_rect(layer, x0 + 6, y0 + 5, (x1 - x0) - 12, 2, acc);
        seg_rect(layer, x0 + 6, y1 - 6, (x1 - x0) - 12, 2, acc);
        static const char *const WD3[] = {
            "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
        };
        lv_draw_label_dsc_t ld;
        lv_draw_label_dsc_init(&ld);
        ld.font = ui_pixel_font_14();
        ld.color = lv_color_hex(on);
        ld.align = LV_TEXT_ALIGN_LEFT;
        int wd = t.tm_wday;
        if (wd < 0 || wd > 6) wd = 0;
        lv_area_t ta = { x0 + 8, y0 + 10, x0 + 50, y0 + 26 };
        ld.text = WD3[wd];
        ld.text_local = 1;
        lv_draw_label(layer, &ld, &ta);
        char db[4];
        snprintf(db, sizeof(db), "%d", t.tm_mday);
        ld.align = LV_TEXT_ALIGN_RIGHT;
        lv_area_t da = { x1 - 40, y0 + 10, x1 - 8, y0 + 26 };
        ld.text = db;
        ld.text_local = 1;
        lv_draw_label(layer, &ld, &da);
        y0 += 24;
        w = x1 - x0 + 1;
        h = y1 - y0 - 4;
    }
    if (s_style == APP_FACE_TERM) {
        for (int sy = a.y1; sy < a.y2; sy += 3) {
            seg_rect(layer, a.x1, sy, w, 1, 0x031403);
        }
        seg_rect(layer, a.x1, a.y1, w, 1, on);
        seg_rect(layer, a.x1, a.y2 - 1, w, 1, on);
    }
    if (s_style == APP_FACE_XLARGE || s_digit_mode == 1) {
        int dw = w * 40 / 100;
        int dh = h * 40 / 100;
        int gx = a.x1 + (w - dw * 2 - 8) / 2;
        int gy = a.y1 + (h - dh * 2 - 18) / 2;
        digit_lcd(layer, gx, gy, dw, dh, t.tm_hour / 10, on, off);
        digit_lcd(layer, gx + dw + 8, gy, dw, dh, t.tm_hour % 10, on, off);
        digit_lcd(layer, gx, gy + dh + 8, dw, dh, t.tm_min / 10, on, off);
        digit_lcd(layer, gx + dw + 8, gy + dh + 8, dw, dh, t.tm_min % 10, on, off);
        int bw = w * 72 / 100;
        int bx = a.x1 + (w - bw) / 2;
        int by = a.y2 - 7;
        seg_rect(layer, bx, by, bw, 3, off);
        seg_rect(layer, bx, by, bw * t.tm_sec / 60, 3, acc);
        return;
    }
    int dw = w * 18 / 100;
    int dh = h * 70 / 100;
    int gap = w * 4 / 100;
    int colon = w * 6 / 100;
    int total = dw * 4 + gap * 3 + colon;
    int x = x0 + (w - total) / 2;
    int y = y0 + (h - dh - 8) / 2;
    digit_lcd(layer, x, y, dw, dh, t.tm_hour / 10, on, off);
    x += dw + gap;
    digit_lcd(layer, x, y, dw, dh, t.tm_hour % 10, on, off);
    x += dw + gap;
    uint32_t cc = (t.tm_sec % 2 == 0) ? acc : off;
    seg_rect(layer, x + colon / 3, y + dh / 3, colon / 3, colon / 3, cc);
    seg_rect(layer, x + colon / 3, y + dh * 2 / 3, colon / 3, colon / 3, cc);
    x += colon + gap;
    digit_lcd(layer, x, y, dw, dh, t.tm_min / 10, on, off);
    x += dw + gap;
    digit_lcd(layer, x, y, dw, dh, t.tm_min % 10, on, off);
    if (s_style == APP_FACE_TERM && t.tm_sec % 2 == 0) {
        seg_rect(layer, x + dw + 4, y + dh - dh / 5, dw / 4, dh / 5, on);
    }
    int bw = w * 72 / 100;
    int bx = x0 + (w - bw) / 2;
    int by = (s_style == APP_FACE_NEON ? y1 : a.y2) - 4;
    seg_rect(layer, bx, by, bw, 2, off);
    seg_rect(layer, bx, by, bw * t.tm_sec / 60, 2, acc);
}

static int city_n(void)
{
    if (s_style == APP_FACE_WORLD || s_style == APP_FACE_INFOGRAPH) return 4;
    return 2;
}

static void world_text(char *out, size_t n, int city_id, bool card)
{
    const app_city_t *c = app_city(city_id);
    struct tm t;
    app_time_at_off(c->off, &t);
    int day = app_city_day(city_id);
    const char *ds = day > 0 ? " 次日" : (day < 0 ? " 昨日" : "");
    if (s_style == APP_FACE_TERM) {
        snprintf(out, n, "$ %s %02d:%02d%s", c->abbr, t.tm_hour, t.tm_min, ds);
    } else if (card) {
        snprintf(out, n, "%s\n%02d:%02d%s", c->zh, t.tm_hour, t.tm_min, ds);
    } else {
        snprintf(out, n, "%s  %02d:%02d%s", c->zh, t.tm_hour, t.tm_min, ds);
    }
}

static void fill_world(const app_face_t *f)
{
    char buf[48];
    if (s_world_lab[0]) {
        int lim = city_n();
        bool any = false;
        for (int i = 0; i < lim; i++) {
            if (!s_world_lab[i]) continue;
            lv_obj_t *box = lv_obj_get_parent(s_world_lab[i]);
            if (f->city[i] == APP_CITY_OFF) {
                lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(box, LV_OBJ_FLAG_HIDDEN);
            world_text(buf, sizeof(buf), f->city[i], true);
            lv_label_set_text(s_world_lab[i], buf);
            any = true;
        }
        if (s_world) {
            if (any) lv_obj_clear_flag(s_world, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(s_world, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (!s_world) return;
    buf[0] = 0;
    int n = city_n();
    bool any = false;
    for (int i = 0; i < n; i++) {
        if (f->city[i] == APP_CITY_OFF) continue;
        char row[40];
        if (s_style == APP_FACE_SPLIT) {
            const app_city_t *c = app_city(f->city[i]);
            struct tm t;
            app_time_at_off(c->off, &t);
            int day = app_city_day(f->city[i]);
            const char *ds = day > 0 ? "次日" : (day < 0 ? "昨日" : c->abbr);
            snprintf(row, sizeof(row), "%s  %02d:%02d  %s", c->zh, t.tm_hour,
                     t.tm_min, ds);
        } else {
            world_text(row, sizeof(row), f->city[i], false);
        }
        if (any) strlcat(buf, "\n", sizeof(buf));
        strlcat(buf, row, sizeof(buf));
        any = true;
    }
    lv_label_set_text(s_world, buf);
    if (any) lv_obj_clear_flag(s_world, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_world, LV_OBJ_FLAG_HIDDEN);
}

static void paint_custom(void)
{
    struct tm t;
    app_time_local(&t);
    const app_custom_t *cu = app_prefs_custom();
    char buf[48];
    for (int i = 0; i < APP_COMP_MAX; i++) {
        const app_comp_t *c = &cu->comp[i];
        if (c->type == APP_COMP_ANALOG || c->type == APP_COMP_NEON ||
            c->type == APP_COMP_XL) {
            if (s_clk) lv_obj_invalidate(s_clk);
            continue;
        }
        if (!s_comp_lab[i] || c->type == APP_COMP_NONE) continue;
        if (c->type == APP_COMP_TIME) {
            if (c->style & APP_ST_HMS) app_time_hms(&t, buf, sizeof(buf));
            else app_time_hm(&t, buf, sizeof(buf));
            lv_label_set_text(s_comp_lab[i], buf);
        } else if (c->type == APP_COMP_DATE) {
            custom_date(buf, sizeof(buf), c, &t);
            lv_label_set_text(s_comp_lab[i], buf);
            uint8_t opa = c->bg_opa;
            if (!opa && (c->style & APP_ST_PILL)) opa = LV_OPA_COVER;
            int pad_h = opa ? 8 : 0;
            int pad_v = opa ? 4 : 0;
            int tw = scaled(220, c->scale) + pad_h * 2;
            int th = box_h(comp_font(c), 1, pad_v, scaled(26, c->scale));
            fit_lab(s_comp_lab[i], c->x, c->y, tw, th);
        } else if (c->type == APP_COMP_WORLD && c->city != APP_CITY_OFF) {
            custom_world(buf, sizeof(buf), c);
            lv_label_set_text(s_comp_lab[i], buf);
        }
    }
}

static void build_custom(void)
{
    const app_custom_t *cu = app_prefs_custom();
    uint32_t canvas = cu->canvas & 0xFFFFFF;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(canvas), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    s_bgimg = NULL;
    bg_tm_del();
    if (cu->has_bg && app_bg_ok()) {
        if (app_bg_anim()) {
            s_bgimg = lv_obj_create(s_root);
            ui_pixel_strip_theme(s_bgimg);
            lv_obj_remove_flag(s_bgimg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(s_bgimg, 0, 0);
            lv_obj_set_size(s_bgimg, FACE_W, FACE_H);
            lv_obj_set_style_bg_opa(s_bgimg, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(s_bgimg, 0, 0);
            lv_obj_set_style_pad_all(s_bgimg, 0, 0);
            lv_obj_add_event_cb(s_bgimg, bg_anim_draw, LV_EVENT_DRAW_MAIN, NULL);
            s_bg_fr = 0;
            if (app_bg_nframes() > 1) {
                s_bg_tm = lv_timer_create(bg_anim_tick, app_bg_delay_ms(0), NULL);
            }
        } else if (app_bg_dsc()) {
            s_bgimg = lv_image_create(s_root);
            lv_obj_remove_flag(s_bgimg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(s_bgimg, 0, 0);
            lv_obj_set_size(s_bgimg, FACE_W, FACE_H);
            lv_image_set_src(s_bgimg, app_bg_dsc());
        }
    }
    for (int i = 0; i < APP_COMP_MAX; i++) {
        const app_comp_t *c = &cu->comp[i];
        s_comp_kind[i] = c->type;
        if (c->type == APP_COMP_NONE || i >= cu->n) continue;
        if (c->style & APP_ST_HIDE) continue;
        app_elem_t e = { c->x, c->y, c->scale };
        s_draw_fg = c->fg ? c->fg : 0xF5F5F7;
        s_draw_acc = c->acc ? c->acc : 0xFF453A;
        s_draw_mute = 0x8E8E93;
        if (c->type == APP_COMP_ANALOG) {
            analog_build(&e, 184, analog_comp_flags(c));
        } else if (c->type == APP_COMP_NEON || c->type == APP_COMP_XL) {
            s_digit_mode = c->type == APP_COMP_XL ? 1 : 0;
            int w = scaled(c->type == APP_COMP_XL ? 200 : 220, c->scale);
            int h = scaled(c->type == APP_COMP_XL ? 168 : 72, c->scale);
            s_clk = lv_obj_create(s_root);
            ui_pixel_strip_theme(s_clk);
            lv_obj_set_style_bg_opa(s_clk, LV_OPA_TRANSP, 0);
            place(s_clk, &e, w, h);
            lv_obj_add_event_cb(s_clk, neon_draw, LV_EVENT_DRAW_MAIN, NULL);
        } else {
            uint32_t col = s_draw_fg;
            const lv_font_t *font = comp_font(c);
            s_comp_lab[i] = lab(s_root, font, col);
            if (c->type == APP_COMP_TIME && c->font == APP_FONT_AUTO) {
                lv_obj_set_style_text_letter_space(s_comp_lab[i], 2, 0);
            }
            if (c->type == APP_COMP_WORLD && (c->style & APP_ST_WRAP)) {
                lv_label_set_long_mode(s_comp_lab[i], LV_LABEL_LONG_WRAP);
            }
            uint8_t opa = c->bg_opa;
            if (!opa && (c->style & APP_ST_PILL)) opa = LV_OPA_COVER;
            int pad_h = opa ? 8 : 0;
            int pad_v = opa ? 4 : 0;
            int lines = (c->type == APP_COMP_WORLD && (c->style & APP_ST_WRAP)) ? 2 : 1;
            int tw = scaled(c->type == APP_COMP_WORLD ? 120 :
                            (c->type == APP_COMP_TIME && (c->style & APP_ST_HMS) ? 240 :
                             (c->type == APP_COMP_DATE ? 220 : 200)),
                            c->scale);
            int th = scaled(c->type == APP_COMP_TIME ? 36 :
                            (c->type == APP_COMP_WORLD && (c->style & APP_ST_WRAP) ? 48 : 26),
                            c->scale);
            tw += pad_h * 2;
            th = box_h(font, lines, pad_v, th);
            place(s_comp_lab[i], &e, tw, th);
            if (opa) {
                lv_obj_set_style_bg_opa(s_comp_lab[i], opa, 0);
                lv_obj_set_style_bg_color(s_comp_lab[i],
                                          lv_color_hex(c->acc ? c->acc : 0x1C1C1E), 0);
                lv_obj_set_style_radius(s_comp_lab[i], c->radius ? c->radius : 8, 0);
                lv_obj_set_style_clip_corner(s_comp_lab[i], false, 0);
                lv_obj_set_style_pad_hor(s_comp_lab[i], pad_h, 0);
                lv_obj_set_style_pad_ver(s_comp_lab[i], pad_v, 0);
            }
            if (c->shadow || c->weight) {
                uintptr_t fx = (c->shadow > 3 ? 3u : c->shadow) |
                               ((c->weight ? 1u : 0u) << 2);
                lv_obj_add_event_cb(s_comp_lab[i], text_fx, LV_EVENT_DRAW_MAIN_BEGIN,
                                    (void *)fx);
            }
        }
    }
}

static void paint_time(void)
{
    struct tm t;
    app_time_local(&t);
    if (s_style == APP_FACE_CUSTOM) {
        paint_custom();
        return;
    }
    if (analog_style(s_style)) analog_hands();
    else if (s_style == APP_FACE_NEON || s_style == APP_FACE_XLARGE ||
               s_style == APP_FACE_TERM || s_style == APP_FACE_SPLIT) {
        if (s_clk) lv_obj_invalidate(s_clk);
    } else if (s_time) {
        app_time_hm(&t, s_time_txt, sizeof(s_time_txt));
        lv_label_set_text(s_time, s_time_txt);
    }
    if (s_date) {
        if (s_style == APP_FACE_MODULAR || s_style == APP_FACE_XLARGE ||
            s_style == APP_FACE_INFOGRAPH) {
            static const char *const WD[] = {
                "周日", "周一", "周二", "周三", "周四", "周五", "周六",
            };
            int w = t.tm_wday;
            if (w < 0 || w > 6) w = 0;
            snprintf(s_date_txt, sizeof(s_date_txt), "%s %d", WD[w], t.tm_mday);
        } else if (s_style == APP_FACE_INK) {
            snprintf(s_date_txt, sizeof(s_date_txt), "%d", t.tm_mday);
        } else if (s_style == APP_FACE_TERM) {
            snprintf(s_date_txt, sizeof(s_date_txt), "%04d-%02d-%02d",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        } else if (s_style == APP_FACE_ASTRO) {
            snprintf(s_date_txt, sizeof(s_date_txt), "SOL %03d", t.tm_yday + 1);
        } else {
            app_time_date(&t, s_date_txt, sizeof(s_date_txt));
        }
        lv_label_set_text(s_date, s_date_txt);
        fit_lab(s_date, s_date_cx, s_date_cy, s_date_mw, s_date_mh);
    }
    fill_world(&app_prefs()->faces[s_style]);
}

static void dress_card(lv_obj_t *c, uint32_t bg, uint32_t acc)
{
    lv_obj_set_style_bg_color(c, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(acc), 0);
}

static uint32_t card_bg(void)
{
    if (s_style == APP_FACE_WORLD) return 0x12202C;
    if (s_style == APP_FACE_NEON) return 0x1A2414;
    if (s_style == APP_FACE_GMT) return 0x121A22;
    if (s_style == APP_FACE_INFOGRAPH) return 0x2C2C2E;
    if (s_style == APP_FACE_HERMES) return 0x163050;
    if (s_style == APP_FACE_TERM) return 0x0A140A;
    if (s_style == APP_FACE_XMAS) return 0x1A2418;
    if (s_style == APP_FACE_ASTRO) return 0x12151C;
    return 0x1C1C1E;
}

static void world_corners(const app_elem_t *we)
{
    int gw = scaled(220, we->scale);
    int gh = scaled(284, we->scale);
    s_world = lv_obj_create(s_root);
    ui_pixel_strip_theme(s_world);
    lv_obj_set_style_bg_opa(s_world, LV_OPA_TRANSP, 0);
    place(s_world, we, gw, gh);
    int cw = 100;
    int ch = 48;
    int pos[4][2] = {
        { 0, 0 }, { gw - cw, 0 }, { 0, gh - ch }, { gw - cw, gh - ch },
    };
    static const uint32_t AC[4] = { 0xFF453A, 0x0A84FF, 0x30D158, 0xFF9F0A };
    uint32_t fg = FACE_FG[s_style];
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = lv_obj_create(s_world);
        ui_pixel_strip_theme(c);
        lv_obj_set_pos(c, pos[i][0], pos[i][1]);
        lv_obj_set_size(c, cw, ch);
        dress_card(c, card_bg(), AC[i]);
        s_world_lab[i] = lab(c, ui_pixel_font_cjk(), fg);
        lv_obj_set_width(s_world_lab[i], cw - 10);
        lv_label_set_long_mode(s_world_lab[i], LV_LABEL_LONG_WRAP);
        lv_obj_center(s_world_lab[i]);
    }
}

static void world_chips(const app_elem_t *we, int n)
{
    int gw = scaled(220, we->scale);
    int gh = scaled(40, we->scale);
    s_world = lv_obj_create(s_root);
    ui_pixel_strip_theme(s_world);
    lv_obj_set_style_bg_opa(s_world, LV_OPA_TRANSP, 0);
    place(s_world, we, gw, gh);
    int gap = 6;
    int cw = (gw - (n - 1) * gap) / n;
    uint32_t fg = FACE_FG[s_style];
    uint32_t acc = FACE_ACCENT[s_style];
    for (int i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_create(s_world);
        ui_pixel_strip_theme(c);
        lv_obj_set_pos(c, i * (cw + gap), 0);
        lv_obj_set_size(c, cw, gh);
        dress_card(c, card_bg(), acc);
        s_world_lab[i] = lab(c, ui_pixel_font_cjk(), fg);
        lv_obj_set_width(s_world_lab[i], cw - 4);
        lv_label_set_long_mode(s_world_lab[i], LV_LABEL_LONG_CLIP);
        lv_obj_center(s_world_lab[i]);
    }
}

static void world_lr(const app_elem_t *we)
{
    int gw = scaled(232, we->scale);
    int gh = scaled(48, we->scale);
    s_world = lv_obj_create(s_root);
    ui_pixel_strip_theme(s_world);
    lv_obj_set_style_bg_opa(s_world, LV_OPA_TRANSP, 0);
    place(s_world, we, gw, gh);
    int cw = 48;
    uint32_t fg = FACE_FG[s_style];
    for (int i = 0; i < 2; i++) {
        lv_obj_t *c = lv_obj_create(s_world);
        ui_pixel_strip_theme(c);
        lv_obj_set_pos(c, i ? gw - cw : 0, 0);
        lv_obj_set_size(c, cw, gh);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        s_world_lab[i] = lab(c, ui_pixel_font_cjk(), fg);
        lv_obj_set_width(s_world_lab[i], cw);
        lv_label_set_long_mode(s_world_lab[i], LV_LABEL_LONG_WRAP);
        lv_obj_center(s_world_lab[i]);
    }
}

static void world_cards(const app_elem_t *we, int n, int gw, int gh)
{
    s_world = lv_obj_create(s_root);
    ui_pixel_strip_theme(s_world);
    lv_obj_set_style_bg_opa(s_world, LV_OPA_TRANSP, 0);
    place(s_world, we, gw, gh);
    int cols = n > 2 ? 2 : n;
    int rows = (n + cols - 1) / cols;
    int cw = (gw - (cols - 1) * 8) / cols;
    int ch = (gh - (rows - 1) * 8) / rows;
    uint32_t fg = FACE_FG[s_style];
    uint32_t acc = FACE_ACCENT[s_style];
    for (int i = 0; i < n; i++) {
        int col = i % cols;
        int row = i / cols;
        lv_obj_t *c = lv_obj_create(s_world);
        ui_pixel_strip_theme(c);
        lv_obj_set_pos(c, col * (cw + 8), row * (ch + 8));
        lv_obj_set_size(c, cw, ch);
        dress_card(c, card_bg(), acc);
        s_world_lab[i] = lab(c, ui_pixel_font_cjk(), fg);
        lv_obj_set_width(s_world_lab[i], cw - 8);
        lv_label_set_long_mode(s_world_lab[i], LV_LABEL_LONG_WRAP);
        lv_obj_center(s_world_lab[i]);
    }
}

static void build(void)
{
    if (!s_root) return;
    bg_tm_del();
    hand_tm_del();
    lv_obj_clean(s_root);
    s_time = NULL;
    s_date = NULL;
    s_world = NULL;
    s_clk = NULL;
    s_sec = NULL;
    s_bat = NULL;
    s_colon = NULL;
    s_bgimg = NULL;
    s_digit_mode = 0;
    s_date_cx = FACE_W / 2;
    s_date_cy = 0;
    s_date_mw = 0;
    s_date_mh = 0;
    memset(s_world_lab, 0, sizeof(s_world_lab));
    memset(s_comp_lab, 0, sizeof(s_comp_lab));
    memset(s_comp_kind, 0, sizeof(s_comp_kind));

    app_prefs_t *p = app_prefs();
    s_style = p->face;
    if (s_style >= APP_FACE_N) s_style = 0;
    s_draw_fg = FACE_FG[s_style];
    s_draw_acc = FACE_ACCENT[s_style];
    s_draw_mute = FACE_MUTE[s_style];
    if (s_style == APP_FACE_CUSTOM) {
        build_custom();
        s_last_sec = -1;
        paint_time();
        return;
    }
    const app_face_t *f = &p->faces[s_style];
    uint32_t bg = FACE_BG[s_style];

    lv_obj_set_style_bg_color(s_root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    if (pack_style(s_style)) {
        build_pack();
        s_last_sec = -1;
        s_last_min = -1;
        struct tm t;
        app_time_local(&t);
        paint_pack(&t, true);
        return;
    }
    if (s_style == APP_FACE_ROUND) {
        build_round();
        s_last_sec = -1;
        s_last_min = -1;
        struct tm t;
        app_time_local(&t);
        pack_meta(&t, true);
        return;
    }

    uint32_t fg = FACE_FG[s_style];
    uint32_t mute = FACE_MUTE[s_style];

    const app_elem_t *te = &f->elem[APP_ELEM_TIME];
    const app_elem_t *de = &f->elem[APP_ELEM_DATE];
    const app_elem_t *we = &f->elem[APP_ELEM_WORLD];

    if (s_style == APP_FACE_CLASSIC) {
        analog_build(te, 196, ANA_TICKS);
    } else if (s_style == APP_FACE_INK) {
        analog_build(te, 176, ANA_INK | ANA_NO_SEC | ANA_NO_RING | ANA_NO_CAP);
    } else if (s_style == APP_FACE_CALIFORNIA) {
        analog_build(te, 136, ANA_NUMS | ANA_NO_SEC | ANA_NO_RING);
    } else if (s_style == APP_FACE_INFOGRAPH) {
        analog_build(te, 112, ANA_COLOR | ANA_NO_RING);
    } else if (s_style == APP_FACE_GMT) {
        analog_build(te, 184, ANA_GMT | ANA_NO_RING);
    } else if (s_style == APP_FACE_HERMES) {
        analog_build(te, 168, ANA_NUMS | ANA_HERMES | ANA_NO_SEC | ANA_NO_RING);
    } else if (s_style == APP_FACE_XMAS) {
        analog_build(te, 188, ANA_XMAS | ANA_NO_RING);
    } else if (s_style == APP_FACE_ASTRO) {
        analog_build(te, 176, ANA_ASTRO | ANA_NO_RING);
    } else if (s_style == APP_FACE_NEON || s_style == APP_FACE_XLARGE ||
               s_style == APP_FACE_TERM || s_style == APP_FACE_SPLIT) {
        int w = scaled(s_style == APP_FACE_XLARGE ? 184 : 220, te->scale);
        int h = scaled(s_style == APP_FACE_XLARGE ? 168 :
                      (s_style == APP_FACE_NEON ? 112 :
                       (s_style == APP_FACE_SPLIT ? 72 : 80)), te->scale);
        if (w > FACE_W) w = FACE_W;
        if (h > FACE_H - 36) h = FACE_H - 36;
        s_clk = lv_obj_create(s_root);
        ui_pixel_strip_theme(s_clk);
        lv_obj_set_style_bg_opa(s_clk, LV_OPA_TRANSP, 0);
        place(s_clk, te, w, h);
        lv_obj_add_event_cb(s_clk, neon_draw, LV_EVENT_DRAW_MAIN, NULL);
    } else {
        s_time = lab(s_root, ui_pixel_font_20(), fg);
        lv_obj_set_style_text_letter_space(s_time, 3, 0);
        int tw = scaled(s_style == APP_FACE_WORLD ? 88 : 200, te->scale);
        int th = scaled(s_style == APP_FACE_MODULAR ? 52 :
                       (s_style == APP_FACE_WORLD ? 32 : 36), te->scale);
        th = box_h(ui_pixel_font_20(), 1, 0, th);
        place(s_time, te, tw, th);
    }

    const lv_font_t *dfont = ui_pixel_font_cjk();
    s_date = lab(s_root, dfont, mute);
    bool date_pill = s_style == APP_FACE_MODULAR || s_style == APP_FACE_XLARGE ||
                     s_style == APP_FACE_CLASSIC || s_style == APP_FACE_WORLD ||
                     s_style == APP_FACE_INK || s_style == APP_FACE_HERMES ||
                     s_style == APP_FACE_TERM || s_style == APP_FACE_XMAS ||
                     s_style == APP_FACE_ASTRO || s_style == APP_FACE_NEON ||
                     s_style == APP_FACE_INFOGRAPH || s_style == APP_FACE_GMT;
    int pad_v = date_pill ? 4 : 0;
    int dw = scaled(168, de->scale);
    int dh = scaled(28, de->scale);
    if (s_style == APP_FACE_INK) {
        dw = 32;
        dh = 32;
        pad_v = 2;
    } else if (s_style == APP_FACE_MODULAR || s_style == APP_FACE_XLARGE) {
        dw = scaled(88, de->scale);
        dh = scaled(28, de->scale);
    } else if (s_style == APP_FACE_INFOGRAPH) {
        dw = scaled(96, de->scale);
        dh = scaled(28, de->scale);
    }
    dh = box_h(dfont, 1, pad_v, dh);
    if (date_pill) dw += 16;
    s_date_cx = de->x;
    s_date_cy = de->y;
    s_date_mw = dw;
    s_date_mh = dh;
    place(s_date, de, dw, dh);
    if (date_pill) {
        uint32_t pill = s_style == APP_FACE_INK ? 0xC41E3A :
                        (s_style == APP_FACE_HERMES ? FACE_ACCENT[s_style] :
                         (s_style == APP_FACE_NEON ? 0x1A2414 : card_bg()));
        uint32_t pc = s_style == APP_FACE_HERMES ? 0x0B1A32 :
                         (s_style == APP_FACE_INK ? 0xF4E4C8 : FACE_FG[s_style]);
        lv_obj_set_style_bg_opa(s_date, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_date, lv_color_hex(pill), 0);
        lv_obj_set_style_radius(s_date, s_style == APP_FACE_INK ? 4 : 8, 0);
        lv_obj_set_style_clip_corner(s_date, false, 0);
        lv_obj_set_style_pad_hor(s_date, 8, 0);
        lv_obj_set_style_pad_ver(s_date, pad_v, 0);
        lv_obj_set_style_text_color(s_date, lv_color_hex(pc), 0);
    }

    if (s_style == APP_FACE_WORLD) {
        world_cards(we, 4, scaled(216, we->scale), scaled(220, we->scale));
    } else if (s_style == APP_FACE_INFOGRAPH) {
        world_corners(we);
    } else if (s_style == APP_FACE_MODULAR) {
        world_cards(we, 2, scaled(216, we->scale), scaled(80, we->scale));
    } else if (s_style == APP_FACE_CALIFORNIA) {
        world_lr(we);
    } else if (s_style == APP_FACE_CLASSIC || s_style == APP_FACE_GMT ||
               s_style == APP_FACE_XLARGE || s_style == APP_FACE_HERMES ||
               s_style == APP_FACE_XMAS || s_style == APP_FACE_ASTRO ||
               s_style == APP_FACE_NEON) {
        world_chips(we, 2);
    } else {
        s_world = lab(s_root, ui_pixel_font_cjk(),
                      s_style == APP_FACE_NEON ? mute : fg);
        int wh = scaled(s_style == APP_FACE_SPLIT ? 88 : 48, we->scale);
        place(s_world, we, scaled(220, we->scale), wh);
        lv_label_set_long_mode(s_world, LV_LABEL_LONG_WRAP);
        if (s_style == APP_FACE_SPLIT) {
            lv_obj_set_style_text_font(s_world, ui_pixel_font_20(), 0);
        }
    }

    s_last_sec = -1;
    paint_time();
}

void app_dial_enter(lv_obj_t *parent)
{
    s_root = parent;
    ui_pixel_strip_theme(parent);
    lv_obj_set_pos(parent, 0, 0);
    lv_obj_set_size(parent, FACE_W, FACE_H);
    lv_obj_remove_event_cb(parent, hermes_bg);
    lv_obj_add_event_cb(parent, hermes_bg, LV_EVENT_DRAW_MAIN, NULL);
    build();
}

void app_dial_exit(void)
{
    bg_tm_del();
    hand_tm_del();
    s_root = NULL;
    s_time = NULL;
    s_date = NULL;
    s_world = NULL;
    s_clk = NULL;
    s_sec = NULL;
    s_bat = NULL;
    s_colon = NULL;
    s_bgimg = NULL;
    memset(s_world_lab, 0, sizeof(s_world_lab));
    memset(s_comp_lab, 0, sizeof(s_comp_lab));
    s_style = -1;
}

void app_dial_set_face(int id)
{
    if (id < 0) id = APP_FACE_N - 1;
    if (id >= APP_FACE_N) id = 0;
    app_prefs()->face = (uint8_t)id;
    app_prefs_save();
    build();
}

void app_dial_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) app_dial_set_face((int)app_prefs()->face - 1);
    else if (btn == BSP_BTN_DOWN) app_dial_set_face((int)app_prefs()->face + 1);
}

void app_dial_request_reload(void)
{
    s_reload = true;
}

void app_dial_tick(void)
{
    if (s_reload) {
        s_reload = false;
        build();
        return;
    }
    if (!s_root) return;
    struct tm t;
    app_time_local(&t);
    if (pack_style(s_style)) {
        if (t.tm_sec != s_last_sec) paint_pack(&t, false);
        return;
    }
    if (s_style == APP_FACE_ROUND) {
        pack_meta(&t, false);
        return;
    }
    if (t.tm_sec == s_last_sec) return;
    s_last_sec = t.tm_sec;
    paint_time();
}
