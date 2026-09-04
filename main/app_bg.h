#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_BG_W    240
#define APP_BG_H    320
#define APP_BG_SIZE (APP_BG_W * APP_BG_H * 2)

void app_bg_init(void);
bool app_bg_ok(void);
bool app_bg_anim(void);
int app_bg_nframes(void);
uint32_t app_bg_delay_ms(int frame);
const lv_image_dsc_t *app_bg_dsc(void);
void app_bg_draw(lv_layer_t *layer, const lv_area_t *coords, int frame);
const uint8_t *app_bg_blob(void);
size_t app_bg_blob_len(void);
size_t app_bg_max(void);
bool app_bg_begin(void);
bool app_bg_feed(const void *p, size_t n);
bool app_bg_finish(void);
void app_bg_clear(void);
