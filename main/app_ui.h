#pragma once

#include "app_list.h"
#include "bsp_button.h"
#include "lvgl.h"
#include <stddef.h>

#define KB_COLS      6
#define KB_ROWS      8
#define KB_N         (KB_COLS * KB_ROWS)
#define APP_UI_LIST_MAX 12

void app_ui_screen_style(lv_obj_t *obj);
lv_obj_t *app_ui_card(lv_obj_t *parent);
lv_obj_t *app_ui_title(lv_obj_t *card, const char *text);
lv_obj_t *app_ui_hint(lv_obj_t *card);
lv_obj_t *app_ui_body(lv_obj_t *card, int y);
lv_obj_t *app_ui_page_title(lv_obj_t *parent, const char *text);
lv_obj_t *app_ui_footer(lv_obj_t *parent, const char *text);
lv_obj_t *app_ui_badge(lv_obj_t *parent, int x, int y, const char *ch,
                       uint32_t color);
lv_obj_t *app_ui_row(lv_obj_t *parent, int x, int y, int w, int h);
void app_ui_select(lv_obj_t *obj, bool selected, uint32_t accent);

void app_ui_move(int *sel, int n, int delta);

// 在 parent 的 [y, y+h) 区间铺 rows_vis 个可复用行对象。页面 enter() 里调一次,
// 之后按键只调 app_ui_list_render()。parent 被 lv_obj_clean() 后需重新 bind。
void app_ui_list_bind(lv_obj_t *parent, int y, int h, int rows_vis);
void app_ui_list_render(const app_row_t *rows, const app_list_t *l);
int app_ui_list_rows(void);

const char *const *app_kb_keys(int set);
void app_kb_render(char *out, size_t n, const char *heading, const char *value,
                   int sel, int set);
void app_kb_show(lv_obj_t *parent, const char *value, int sel, int set,
                 int reserve_bottom);
void app_kb_hide(void);
int app_kb_click(char *buf, size_t cap, int *sel, int *set);
