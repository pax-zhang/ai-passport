#pragma once

#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

#define UI_BG         0x0E100F
#define UI_CARD       0x1B201C
#define UI_LINE       0x343C36
#define UI_TEXT       0xF0E8D4
#define UI_MUTE       0x8A8676
#define UI_CYAN       0xC9A35A
#define UI_AMBER      0xD4A04A
#define UI_BLUE       0x7E9A96
#define UI_MINT       0x7FAE7A
#define UI_VIOLET     0xA8926E
#define UI_ROSE       0xC45B48
#define UI_GRID       0x232826
#define UI_PANEL      0x0E100F
#define UI_FILL       0x2A332C
#define UI_ON_ACCENT  0x0E100F
#define UI_RADIUS     10
#define UI_RADIUS_SM  6

#define UI_THEME_N  5
#define UI_ST_ANIME 0
#define UI_ST_GEEK  1
#define UI_ST_INK   2
#define UI_ST_MINI  3
#define UI_ST_POP   4

int ui_theme_id(void);
int ui_theme_count(void);
void ui_theme_set(int id);
const char *ui_theme_name(int id);
uint32_t ui_style_bg(void);
uint32_t ui_style_card(void);
uint32_t ui_style_text(void);
uint32_t ui_style_mute(void);
uint32_t ui_style_line(void);
uint32_t ui_style_accent(void);
uint32_t ui_style_fill(void);
uint32_t ui_style_on(void);
uint32_t ui_style_urgent(void);

#define UI_SKY        UI_BG
#define UI_SKY_DARK   UI_MUTE
#define UI_INK        UI_TEXT
#define UI_PAPER      UI_CARD
#define UI_GRASS      0x82BE2D
#define UI_GRASS_DARK 0x55951D
#define UI_YELLOW     UI_CYAN
#define UI_ORANGE     UI_AMBER
#define UI_RED        0xFF6B8A
#define UI_MUTED      UI_LINE

void ui_pixel_theme_init(void);
void ui_pixel_strip_theme(lv_obj_t *obj);
void ui_pixel_glass(lv_obj_t *obj);
void ui_pixel_wallpaper_attach(lv_obj_t *scr);
void ui_pixel_hud_decor(lv_obj_t *parent);

void ui_pixel_card_style(lv_obj_t *obj, uint32_t bg, uint32_t border);
lv_obj_t *ui_pixel_screen_create(const char *title);
lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color);
lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color);
lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y);
void ui_pixel_mascot_jump(lv_obj_t *mascot);
void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled);
void ui_pixel_select(lv_obj_t *panel, bool selected, uint32_t accent);

// Montserrat + CJK/假名/全角回退。显示初始化后、创建界面前调用。
void ui_pixel_fonts_init(void);
const lv_font_t *ui_pixel_font_14(void);
const lv_font_t *ui_pixel_font_20(void);
const lv_font_t *ui_pixel_font_cjk(void);

/* 锁屏时钟:Montserrat 20 近邻放大 2 倍。 */
void ui_pixel_draw_clock4x(lv_layer_t *layer, const char *txt, const lv_area_t *box,
                           uint32_t color);

// 按完整 UTF-8 码点拷贝,避免中文 SSID 被截断成非法序列。
void ui_pixel_utf8_copy(char *dst, size_t dst_n, const char *src);
