#pragma once

#include "bsp_button.h"
#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    MEOW_SET_WIFI = 0,
    MEOW_SET_BLE,
    MEOW_SET_CLOCK,
    MEOW_SET_BED,
    MEOW_SET_SCREEN,
    MEOW_SET_LOCK,
    MEOW_SET_SOUND,
    MEOW_SET_OTA
} meow_set_id_t;

void app_meow_set_init(void);
void app_meow_set_open(lv_obj_t *lcd, meow_set_id_t id);
void app_meow_set_close(void);
void app_meow_set_suspend(bool hide);
bool app_meow_set_open_now(void);
bool app_meow_set_ble_now(void);
bool app_meow_set_busy(void);
void app_meow_set_on_key(bsp_btn_t btn, bsp_btn_ev_t ev);
