#pragma once

#include "bsp_button.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_SCREEN_W 240
#define APP_SCREEN_H 320

void app_shell_start(void);
void app_shell_on_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void app_shell_wake(void);
bool app_shell_asleep(void);
lv_obj_t *app_shell_screen(void);
void app_shell_reload(void);
