#pragma once

#include "bsp_button.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>

#define APP_WEB_TEXT_MAX 512

void app_web_init(lv_obj_t *screen);
void app_web_listen(void);
void app_web_boot_setup(void);
void app_web_poll(void);
bool app_web_visible(void);
bool app_web_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_web_set_target(const char *name, char *buf, size_t cap,
                        void (*refresh)(void));
void app_web_clear_target(void);
void app_web_set_rules(void (*refresh)(void));
void app_web_clear_rules(void);
void app_web_set_totp(void (*refresh)(void));
void app_web_clear_totp(void);

#define APP_WEB_HTTP_PORT 80
#define APP_WEB_MINI_QR   72
#define APP_WEB_MINI_GAP  4
#define APP_WEB_MINI_URL  16
#define APP_WEB_MINI_H    (APP_WEB_MINI_URL + APP_WEB_MINI_GAP + APP_WEB_MINI_QR)

bool app_web_url(char *buf, size_t n);
void app_web_qr_open(void);
void app_web_qr_close(void);
void app_web_suspend_for_ota(bool on);
bool app_web_qr_visible(void);
bool app_web_keep_awake(void);
bool app_web_httpd_up(void);
bool app_web_qr_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool app_web_qr_prepare(void);
void app_web_qr_paint(lv_layer_t *layer, const lv_area_t *box);
void app_web_mini_qr_bind(lv_obj_t *parent, lv_obj_t **qr, lv_obj_t **url);
void app_web_mini_qr_show(lv_obj_t *qr, lv_obj_t *url, bool on);
