#include "ui_pixel.h"

#include "app_i18n.h"

#include <string.h>

extern const uint8_t bg_anime_bin_start[] asm("_binary_bg_anime_bin_start");
extern const uint8_t bg_geek_bin_start[] asm("_binary_bg_geek_bin_start");
extern const uint8_t bg_ink_bin_start[] asm("_binary_bg_ink_bin_start");
extern const uint8_t bg_pop_bin_start[] asm("_binary_bg_pop_bin_start");

static int s_theme_id = UI_ST_GEEK;
static const app_str_id_t THEME_NAME[UI_THEME_N] = {
    APP_STR_THEME_INK, APP_STR_THEME_PLASMA, APP_STR_THEME_PAPER,
    APP_STR_THEME_ABYSS, APP_STR_THEME_EMBER,
};

typedef struct {
    uint32_t bg, card, text, mute, line, accent, fill, on;
} style_pal_t;

static const style_pal_t PALS[UI_THEME_N] = {
    { 0xFFF4F8, 0xFFFFFF, 0x3A2048, 0xA87890, 0xFFB0C8, 0xFF6B9A, 0xFFE0EC, 0xFFFFFF },
    { 0x0A0C0A, 0x101410, 0xB8FF9A, 0x5A7A50, 0x3A5A32, 0x7CFF4A, 0x1A2A14, 0x0A0C0A },
    { 0xF3E6C8, 0xFBF1D8, 0x2A1810, 0x8A7060, 0xC4A070, 0xC41E3A, 0xE8D4A8, 0xFBF1D8 },
    { 0xFFFFFF, 0xFFFFFF, 0x09090B, 0x71717A, 0xE4E4E7, 0x09090B, 0xF4F4F5, 0xFFFFFF },
    { 0xFFE600, 0xFFE600, 0x111111, 0x111111, 0x111111, 0xFF2D95, 0x00E8FF, 0xFFFFFF },
};

static int pal_id(void)
{
    return (s_theme_id >= 0 && s_theme_id < UI_THEME_N) ? s_theme_id : UI_ST_GEEK;
}

uint32_t ui_style_bg(void) { return PALS[pal_id()].bg; }
uint32_t ui_style_card(void) { return PALS[pal_id()].card; }
uint32_t ui_style_text(void) { return PALS[pal_id()].text; }
uint32_t ui_style_mute(void) { return PALS[pal_id()].mute; }
uint32_t ui_style_line(void) { return PALS[pal_id()].line; }
uint32_t ui_style_accent(void) { return PALS[pal_id()].accent; }
uint32_t ui_style_fill(void) { return PALS[pal_id()].fill; }
uint32_t ui_style_on(void) { return PALS[pal_id()].on; }

uint32_t ui_style_urgent(void)
{
    switch (pal_id()) {
    case UI_ST_ANIME:
        return 0xFF6B9A;
    case UI_ST_INK:
        return 0xC41E3A;
    case UI_ST_MINI:
        return 0x09090B;
    case UI_ST_POP:
        return 0xFF2D95;
    default:
        return 0x7CFF4A;
    }
}

int ui_theme_id(void)
{
    return s_theme_id;
}

int ui_theme_count(void)
{
    return UI_THEME_N;
}

void ui_theme_set(int id)
{
    if (id < 0 || id >= UI_THEME_N) id = UI_ST_GEEK;
    s_theme_id = id;
    ui_pixel_theme_init();
}

const char *ui_theme_name(int id)
{
    if (id < 0 || id >= UI_THEME_N) id = UI_ST_GEEK;
    return app_str(THEME_NAME[id]);
}

LV_FONT_DECLARE(lv_font_cjk_12);

static lv_font_t s_font_14;
static lv_font_t s_font_20;
static lv_font_t s_cjk_title;
static lv_font_t s_cjk_body;
static bool s_fonts_ready;

/* 点阵放大共用暂存。LVGL 单线程绘制,CJK 2x 与时钟 4x 不会重入。 */
static uint8_t s_scale_src[64 * 24];

/* 12px 点阵放大一倍给 20px 标题用。必须返回 lv_draw_buf_t*,不能返回像素指针,
 * 否则 LVGL9 会把像素当 draw_buf 解引用 → 黑屏。内层 bitmap 也必须走
 * lv_font_cjk_12(fmt_txt),不能再指向 s_cjk_title,否则递归。 */
static bool cjk_body_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                         uint32_t letter, uint32_t next)
{
    (void)font;
    if (letter > 0x10FFFF || !dsc || !lv_font_cjk_12.get_glyph_dsc ||
        !lv_font_cjk_12.get_glyph_dsc(&lv_font_cjk_12, dsc, letter, next)) {
        if (dsc) {
            dsc->box_w = 0;
            dsc->box_h = 0;
        }
        return false;
    }
    if (dsc->box_w > 24 || dsc->box_h > 24) {
        dsc->box_w = 0;
        dsc->box_h = 0;
        return false;
    }
    return true;
}

static bool cjk_title_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                          uint32_t letter, uint32_t next)
{
    (void)font;
    if (letter > 0x10FFFF || !dsc || !lv_font_cjk_12.get_glyph_dsc ||
        !lv_font_cjk_12.get_glyph_dsc(&lv_font_cjk_12, dsc, letter, next) ||
        dsc->box_w > 24 || dsc->box_h > 24) {
        if (dsc) {
            dsc->box_w = 0;
            dsc->box_h = 0;
        }
        return false;
    }
    dsc->adv_w = (uint16_t)(dsc->adv_w * 2);
    dsc->box_w = (uint16_t)(dsc->box_w * 2);
    dsc->box_h = (uint16_t)(dsc->box_h * 2);
    dsc->ofs_x = (int16_t)(dsc->ofs_x * 2);
    dsc->ofs_y = (int16_t)(dsc->ofs_y * 2);
    dsc->stride = 0;
    dsc->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc->resolved_font = &s_cjk_title;
    return true;
}

static const void *cjk_title_bitmap(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf)
{
    if (!g_dsc || !draw_buf || !draw_buf->data || !g_dsc->gid.index) return NULL;
    if (g_dsc->box_w < 2 || g_dsc->box_h < 2) return NULL;

    uint16_t sw = (uint16_t)(g_dsc->box_w / 2);
    uint16_t sh = (uint16_t)(g_dsc->box_h / 2);
    uint32_t stride_in = lv_draw_buf_width_to_stride(sw, LV_COLOR_FORMAT_A8);
    if (stride_in == 0 || (uint32_t)sh * stride_in > sizeof(s_scale_src)) return NULL;

    lv_draw_buf_t src_buf;
    if (lv_draw_buf_init(&src_buf, sw, sh, LV_COLOR_FORMAT_A8,
                         stride_in, s_scale_src, sizeof(s_scale_src)) != LV_RESULT_OK) {
        return NULL;
    }

    lv_font_glyph_dsc_t src = *g_dsc;
    src.resolved_font = &lv_font_cjk_12;
    src.req_raw_bitmap = 0;
    src.box_w = sw;
    src.box_h = sh;
    src.stride = 0;
    src.format = LV_FONT_GLYPH_FORMAT_A1;
    if (!lv_font_cjk_12.get_glyph_bitmap(&src, &src_buf)) return NULL;

    uint32_t stride_out = draw_buf->header.stride;
    if (stride_out == 0) stride_out = g_dsc->box_w;
    if (draw_buf->data_size &&
        (uint32_t)g_dsc->box_h * stride_out > draw_buf->data_size) {
        return NULL;
    }

    const uint8_t *in = src_buf.data;
    uint8_t *out = draw_buf->data;
    for (uint16_t y = 0; y < g_dsc->box_h; y++) {
        const uint8_t *row = in + (y / 2) * stride_in;
        uint8_t *dst = out + y * stride_out;
        for (uint16_t x = 0; x < g_dsc->box_w; x++) dst[x] = row[x / 2];
    }
    return draw_buf;
}

#define CLOCK_SCALE 2

static int clock4x_advance(uint32_t letter, uint32_t next)
{
    lv_font_glyph_dsc_t dsc;
    if (!lv_font_montserrat_20.get_glyph_dsc(&lv_font_montserrat_20, &dsc, letter, next)) {
        return 8 * CLOCK_SCALE;
    }
    return (int)dsc.adv_w * CLOCK_SCALE;
}

void ui_pixel_draw_clock4x(lv_layer_t *layer, const char *txt, const lv_area_t *box,
                           uint32_t color)
{
    if (!layer || !txt || !txt[0] || !box) return;

    int text_w = 0;
    for (const char *p = txt; *p; p++) {
        text_w += clock4x_advance((uint8_t)*p, (uint8_t)p[1]);
    }
    int box_w = (int)lv_area_get_width(box);
    int pen = box->x1;
    if (box_w > text_w) pen += (box_w - text_w) / 2;
    const int y = box->y1;
    const int line_h = lv_font_montserrat_20.line_height;
    const int base = lv_font_montserrat_20.base_line;

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(color);
    rd.radius = 0;
    rd.border_width = 0;
    rd.outline_width = 0;
    rd.shadow_width = 0;

    for (const char *p = txt; *p; p++) {
        uint32_t letter = (uint8_t)*p;
        uint32_t next = (uint8_t)p[1];
        lv_font_glyph_dsc_t dsc;
        if (!lv_font_montserrat_20.get_glyph_dsc(&lv_font_montserrat_20, &dsc,
                                                 letter, next) ||
            !dsc.gid.index) {
            pen += clock4x_advance(letter, next);
            continue;
        }

        uint16_t sw = dsc.box_w;
        uint16_t sh = dsc.box_h;
        uint32_t stride = lv_draw_buf_width_to_stride(sw, LV_COLOR_FORMAT_A8);
        if (stride == 0 || (uint32_t)sh * stride > sizeof(s_scale_src)) {
            pen += (int)dsc.adv_w * CLOCK_SCALE;
            continue;
        }

        lv_draw_buf_t src_buf;
        if (lv_draw_buf_init(&src_buf, sw, sh, LV_COLOR_FORMAT_A8,
                             stride, s_scale_src, sizeof(s_scale_src)) != LV_RESULT_OK) {
            pen += (int)dsc.adv_w * CLOCK_SCALE;
            continue;
        }
        dsc.resolved_font = &lv_font_montserrat_20;
        dsc.req_raw_bitmap = 0;
        dsc.stride = 0;
        dsc.format = LV_FONT_GLYPH_FORMAT_A8;
        if (!lv_font_montserrat_20.get_glyph_bitmap(&dsc, &src_buf)) {
            pen += (int)dsc.adv_w * CLOCK_SCALE;
            continue;
        }

        int gx = pen + dsc.ofs_x * CLOCK_SCALE;
        int gy = y + (line_h - base - dsc.box_h - dsc.ofs_y) * CLOCK_SCALE;
        const uint8_t *in = src_buf.data;
        for (uint16_t sy = 0; sy < sh; sy++) {
            const uint8_t *row = in + sy * stride;
            int run = -1;
            uint8_t run_a = 0;
            for (int sx = 0; sx <= (int)sw; sx++) {
                uint8_t a = (sx < (int)sw) ? row[sx] : 0;
                if (a < 24) a = 0;
                if (a && run < 0) {
                    run = sx;
                    run_a = a;
                } else if (run >= 0 &&
                           (a == 0 || a + 48 < run_a || run_a + 48 < a)) {
                    rd.bg_opa = run_a;
                    lv_area_t ar = {
                        .x1 = gx + run * CLOCK_SCALE,
                        .y1 = gy + sy * CLOCK_SCALE,
                        .x2 = gx + sx * CLOCK_SCALE - 1,
                        .y2 = gy + (sy + 1) * CLOCK_SCALE - 1,
                    };
                    lv_draw_rect(layer, &rd, &ar);
                    run = a ? sx : -1;
                    run_a = a;
                }
            }
        }
        pen += (int)dsc.adv_w * CLOCK_SCALE;
    }
}

void ui_pixel_fonts_init(void)
{
    if (s_fonts_ready) return;
    s_font_14 = lv_font_montserrat_14;
    s_cjk_body = lv_font_cjk_12;
    s_cjk_body.get_glyph_dsc = cjk_body_dsc;
    s_cjk_body.fallback = NULL;
    s_font_14.fallback = &s_cjk_body;

    s_cjk_title = lv_font_cjk_12;
    s_cjk_title.get_glyph_dsc = cjk_title_dsc;
    s_cjk_title.get_glyph_bitmap = cjk_title_bitmap;
    s_cjk_title.release_glyph = NULL;
    s_cjk_title.line_height = 28;
    s_cjk_title.base_line = 6;
    s_cjk_title.static_bitmap = 0;
    s_cjk_title.fallback = NULL;

    s_font_20 = lv_font_montserrat_20;
    s_font_20.line_height = 28;
    s_font_20.fallback = &s_cjk_title;
    s_fonts_ready = true;
}

const lv_font_t *ui_pixel_font_14(void)
{
    return s_fonts_ready ? &s_font_14 : &lv_font_montserrat_14;
}

const lv_font_t *ui_pixel_font_20(void)
{
    return s_fonts_ready ? &s_font_20 : &lv_font_montserrat_20;
}

const lv_font_t *ui_pixel_font_cjk(void)
{
    return s_fonts_ready ? &s_cjk_body : &lv_font_cjk_12;
}

void ui_pixel_utf8_copy(char *dst, size_t dst_n, const char *src)
{
    if (!dst || dst_n == 0) return;
    dst[0] = 0;
    if (!src) return;

    size_t o = 0;
    for (size_t i = 0; src[i] && o + 1 < dst_n; ) {
        unsigned char c = (unsigned char)src[i];
        int w = 1;
        if ((c & 0x80) == 0) w = 1;
        else if ((c & 0xE0) == 0xC0) w = 2;
        else if ((c & 0xF0) == 0xE0) w = 3;
        else if ((c & 0xF8) == 0xF0) w = 4;
        if (o + (size_t)w >= dst_n) break;
        memcpy(dst + o, src + i, (size_t)w);
        o += (size_t)w;
        i += (size_t)w;
    }
    dst[o] = 0;
}

static lv_obj_t *s_wall, *s_wash;
static lv_image_dsc_t s_dsc_anime, s_dsc_geek, s_dsc_ink, s_dsc_pop;
static bool s_dsc_ready;

static void dsc_init(void)
{
    if (s_dsc_ready) return;
    s_dsc_anime.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc_anime.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc_anime.header.w = 240;
    s_dsc_anime.header.h = 320;
    s_dsc_anime.header.stride = 480;
    s_dsc_anime.data_size = 240 * 320 * 2;
    s_dsc_anime.data = bg_anime_bin_start;
    s_dsc_geek = s_dsc_anime;
    s_dsc_geek.data = bg_geek_bin_start;
    s_dsc_ink = s_dsc_anime;
    s_dsc_ink.data = bg_ink_bin_start;
    s_dsc_pop = s_dsc_anime;
    s_dsc_pop.data = bg_pop_bin_start;
    s_dsc_ready = true;
}

static const lv_image_dsc_t *wall_dsc(void)
{
    dsc_init();
    switch (pal_id()) {
    case UI_ST_ANIME:
        return &s_dsc_anime;
    case UI_ST_GEEK:
        return &s_dsc_geek;
    case UI_ST_INK:
        return &s_dsc_ink;
    case UI_ST_POP:
        return &s_dsc_pop;
    default:
        return NULL;
    }
}

static lv_opa_t wash_opa(void)
{
    switch (pal_id()) {
    case UI_ST_ANIME:
        return LV_OPA_40;
    case UI_ST_GEEK:
        return LV_OPA_50;
    case UI_ST_INK:
        return LV_OPA_40;
    default:
        return LV_OPA_TRANSP;
    }
}

static void wallpaper_paint(void)
{
    const lv_image_dsc_t *d = wall_dsc();
    lv_opa_t wash = wash_opa();
    if (s_wall) {
        if (d) {
            lv_image_set_src(s_wall, d);
            lv_obj_remove_flag(s_wall, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wall, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_wash) {
        lv_obj_set_style_bg_color(s_wash, lv_color_hex(ui_style_bg()), 0);
        lv_obj_set_style_bg_opa(s_wash, wash, 0);
        if (wash == LV_OPA_TRANSP) lv_obj_add_flag(s_wash, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_wash, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_pixel_glass(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

void ui_pixel_wallpaper_attach(lv_obj_t *scr)
{
    if (!scr) return;
    dsc_init();
    if (!s_wall) {
        s_wall = lv_image_create(scr);
        lv_obj_remove_flag(s_wall, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_wall, 0, 0);
        lv_obj_set_size(s_wall, 240, 320);
    }
    if (!s_wash) {
        s_wash = lv_obj_create(scr);
        ui_pixel_strip_theme(s_wash);
        lv_obj_remove_flag(s_wash, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_wash, 0, 0);
        lv_obj_set_size(s_wash, 240, 320);
    }
    wallpaper_paint();
}

void ui_pixel_theme_init(void)
{
    lv_display_t *d = lv_display_get_default();
    if (!d) return;
    lv_display_set_theme(d, NULL);

    lv_obj_t *scr = lv_screen_active();
    if (scr) {
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(scr, lv_color_hex(ui_style_bg()), 0);
    }
    lv_obj_t *bot = lv_display_get_layer_bottom(d);
    if (bot) {
        lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bot, lv_color_hex(ui_style_bg()), 0);
    }
    lv_obj_t *top = lv_display_get_layer_top(d);
    if (top) {
        lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    }
    wallpaper_paint();
}

void ui_pixel_strip_theme(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_clip_corner(obj, false, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_row(obj, 0, 0);
    lv_obj_set_style_pad_column(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(ui_style_bg()), 0);
}

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    ui_pixel_strip_theme(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

void ui_pixel_hud_decor(lv_obj_t *parent)
{
    (void)parent;
}

lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void ui_pixel_card_style(lv_obj_t *obj, uint32_t bg, uint32_t border)
{
    if (!obj) return;
    lv_obj_set_style_radius(obj, UI_RADIUS, 0);
    lv_obj_set_style_clip_corner(obj, false, 0);
    lv_obj_set_style_border_width(obj, border ? 1 : 0, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border ? border : UI_LINE), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
}

lv_obj_t *ui_pixel_screen_create(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_strip_theme(scr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_text_font(scr, ui_pixel_font_14(), 0);
    ui_pixel_hud_decor(scr);

    lv_obj_t *heading = ui_pixel_label(scr, title, ui_pixel_font_20(), UI_TEXT);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 16, 12);
    return scr;
}

lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color)
{
    lv_obj_t *panel = block(parent, x, y, w, h, color);
    ui_pixel_card_style(panel, color, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    return panel;
}

lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *m = lv_obj_create(parent);
    ui_pixel_strip_theme(m);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_size(m, 38, 48);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m, lv_color_hex(UI_BG), 0);

    /* 原创“小电视机器人”：天线、发光屏幕脸、橙色围巾与履带脚。 */
    block(m, 18, 0, 3, 6, UI_INK);
    block(m, 16, 0, 7, 3, UI_ORANGE);
    block(m, 3, 6, 32, 24, UI_INK);
    block(m, 0, 12, 5, 10, 0x7557D9);
    block(m, 33, 12, 5, 10, 0x7557D9);
    block(m, 7, 10, 24, 16, 0xB9F3FF);
    block(m, 11, 14, 4, 6, 0x294B7A);
    block(m, 23, 14, 4, 6, 0x294B7A);
    block(m, 16, 22, 7, 2, 0x7557D9);
    block(m, 10, 29, 18, 4, UI_ORANGE);
    block(m, 8, 33, 22, 11, 0x7557D9);
    block(m, 3, 35, 5, 7, 0xB9F3FF);
    block(m, 30, 35, 5, 7, 0xB9F3FF);
    block(m, 8, 44, 9, 4, UI_INK);
    block(m, 21, 44, 9, 4, UI_INK);
    return m;
}

static void jump_y(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

void ui_pixel_mascot_jump(lv_obj_t *mascot)
{
    if (!mascot) return;
    int y = lv_obj_get_y(mascot);
    lv_anim_delete(mascot, jump_y);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, mascot);
    lv_anim_set_exec_cb(&anim, jump_y);
    lv_anim_set_values(&anim, y, y - 5);
    lv_anim_set_duration(&anim, 110);
    lv_anim_set_playback_duration(&anim, 140);
    lv_anim_set_path_cb(&anim, lv_anim_path_step);
    lv_anim_start(&anim);
}

void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled)
{
    ui_pixel_select(panel, selected && enabled, UI_CYAN);
    if (!enabled) {
        lv_obj_set_style_bg_color(panel, lv_color_hex(UI_GRID), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    }
}

void ui_pixel_select(lv_obj_t *panel, bool selected, uint32_t accent)
{
    if (!panel) return;
    (void)accent;
    lv_color_t want = lv_color_hex(selected ? UI_FILL : UI_CARD);
    if (lv_color_eq(lv_obj_get_style_bg_color(panel, 0), want)) return;
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_outline_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, UI_RADIUS, 0);
    lv_obj_set_style_bg_color(panel, want, 0);
}
