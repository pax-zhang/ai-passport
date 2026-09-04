#pragma once

#include "bsp_button.h"
#include "lvgl.h"

void app_dial_enter(lv_obj_t *parent);
void app_dial_exit(void);
void app_dial_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void app_dial_tick(void);
void app_dial_request_reload(void);
void app_dial_set_face(int id);
