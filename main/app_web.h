#pragma once

#include "bsp_button.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>

#define APP_WEB_HTTP_PORT 80

void app_web_init(lv_obj_t *screen);
void app_web_poll(void);
bool app_web_visible(void);
bool app_web_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void app_web_open(void);
void app_web_close(void);
bool app_web_keep_awake(void);
bool app_web_url(char *buf, size_t n);
