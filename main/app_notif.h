#pragma once

#include "app_logic.h"
#include "app_notif_store.h"
#include "bsp_button.h"
#include "lvgl.h"
#include <stdbool.h>

void app_notif_init(lv_obj_t *screen);
void app_notif_poll(void);
bool app_notif_visible(void);
bool app_notif_pairing(void);
bool app_notif_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void app_notif_tick(uint32_t ms);

const app_notif_store_t *app_notif_hist(void);
int app_notif_unread(void);
bool app_notif_hist_remove(int newest_i);
void app_notif_hist_clear(void);
void app_notif_mark_read(int newest_i);
void app_notif_mark_unread(int newest_i);
void app_notif_mark_all_read(void);
// 息屏前调用,把节流中的历史落盘。
void app_notif_store_flush(void);
