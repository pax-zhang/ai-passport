#pragma once

#include "bsp_button.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>

void app_farm_web_open(char *buf, size_t cap, const char *field,
                       void (*refresh)(void));
void app_farm_web_close(void);
bool app_farm_web_shown(void);
bool app_farm_web_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void app_farm_web_poll(void);
void app_farm_web_draw(lv_layer_t *layer, int bx, int by);
