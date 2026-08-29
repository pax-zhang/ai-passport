#pragma once

#include "bsp_button.h"
#include "lvgl.h"
#include <stddef.h>

#define KB_COLS      6
#define KB_ROWS      8
#define KB_N         (KB_COLS * KB_ROWS)
#define KB_ID_COLS   4
#define KB_ID_ROWS   4
#define KB_ID_N      (KB_ID_COLS * KB_ID_ROWS)

lv_obj_t *app_ui_card(lv_obj_t *parent);
lv_obj_t *app_ui_title(lv_obj_t *card, const char *text);
lv_obj_t *app_ui_hint(lv_obj_t *card);
lv_obj_t *app_ui_body(lv_obj_t *card, int y);

void app_ui_move(int *sel, int n, int delta);

const char *const *app_kb_keys(int set);
const char *const *app_kb_id_keys(void);
void app_kb_render(char *out, size_t n, const char *heading, const char *value,
                   int sel, int set);
void app_kb_id_move(int *sel, int delta);
// returns 1=char applied, 2=GO, 3=BK, 4=QR, 0=other (Aa/DEL/SPC handled inside)
int app_kb_click(char *buf, size_t cap, int *sel, int *set);
int app_kb_id_click(char *buf, size_t cap, int sel);
