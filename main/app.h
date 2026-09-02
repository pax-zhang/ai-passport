#pragma once

#include "app_list.h"
#include "bsp_button.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>

#define APP_SCREEN_W    240
#define APP_SCREEN_H    320
#define APP_CORNER_R    5
#define APP_MASK_H      5
#define APP_SAFE_TOP    ((APP_MASK_H + 1) / 2)
#define APP_SAFE_BOTTOM (APP_MASK_H / 2)
#define APP_SAFE_INSET  APP_CORNER_R
#define APP_VIEW_X      APP_SAFE_INSET
#define APP_VIEW_Y      APP_SAFE_TOP
#define APP_VIEW_W      (APP_SCREEN_W - APP_SAFE_INSET * 2)
#define APP_VIEW_H      (APP_SCREEN_H - APP_SAFE_TOP - APP_SAFE_BOTTOM)
#define APP_CONTENT_W   APP_VIEW_W
#define APP_TEXT_W      (APP_CONTENT_W - 12)
#define APP_HEADER_H    18
#define APP_BODY_Y      (APP_VIEW_Y + APP_HEADER_H)
#define APP_BODY_H      (APP_VIEW_H - APP_HEADER_H)

// 全局按键约定:UP/DOWN 短按移动焦点(跳过标题与禁用项),UP/DOWN 长按滚动或
// 页面自定义动作,OK 短按确认/进入,OK 长按返回上一层(首页则锁屏)。
// 列表内不放返回项;进入页面焦点落在第一个可操作行,返回时恢复上次焦点。
typedef void (*app_enter_fn)(lv_obj_t *parent);
typedef void (*app_exit_fn)(void);
typedef void (*app_key_fn)(bsp_btn_t btn, bsp_btn_ev_t ev);
// 返回 true = 页面自己退了一层子视图,不要弹页栈。
typedef bool (*app_back_fn)(void);

#define APP_PAGE_BLE (1u << 0)  // 该页需要 BLE 栈常驻

// 覆盖层按 prio 降序抢占按键。prio 高于 APP_MODAL_WAKE_GATE 的在息屏唤醒的
// "吞掉首击" 之前分发,用于必须立刻响应的配对确认。
#define APP_MODAL_WAKE_GATE 90
#define APP_MODAL_MAX       6

typedef struct {
    bool (*visible)(void);
    bool (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);  // true = 已消费
    uint8_t prio;
} app_modal_t;

void app_shell_start(void);
void app_shell_on_key(bsp_btn_t btn, bsp_btn_ev_t ev);
lv_obj_t *app_shell_screen(void);
void app_shell_open(app_enter_fn enter, app_exit_fn exit, app_key_fn key);
void app_shell_open_ex(app_enter_fn enter, app_exit_fn exit, app_key_fn key,
                       uint8_t traits);
void app_shell_back(void);
void app_shell_reload(void);
void app_shell_wake(void);
bool app_shell_asleep(void);
bool app_shell_locked(void);
void app_shell_header_sync(void);
void app_shell_retheme(void);
void app_shell_page_obscure(bool hide);
void app_shell_page_vacate(void);
void app_shell_register_modal(const app_modal_t *m);
// 有子视图的页面在 enter() 里登记,长按 OK 时先给它一次机会逐层退。
void app_shell_set_back(app_back_fn fn);
// 当前页栈层的列表状态。返回上一层后焦点由此恢复。
app_list_t *app_shell_list(void);

void app_lock_init(lv_obj_t *screen);
void app_lock_show(void);
void app_lock_hide(void);
bool app_lock_visible(void);
void app_lock_tick(void);
void app_lock_retheme(void);

void app_home_enter(lv_obj_t *p);
void app_home_exit(void);
void app_home_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_settings_enter(lv_obj_t *p);
void app_settings_exit(void);
void app_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool app_settings_open_now(void);

void app_ancs_enter(lv_obj_t *p);
void app_ancs_exit(void);
void app_ancs_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void app_ancs_resume(void);

void app_hw_enter(lv_obj_t *p);
void app_hw_exit(void);
void app_hw_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_logs_start(void);
void app_logs_enter(lv_obj_t *p);
void app_logs_exit(void);
void app_logs_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_wx_start(void);
void app_wx_pause(bool on);
void app_wx_stop(void);
// 有有效预报时写一行锁屏天气。
bool app_wx_lock_line(char *out, size_t n);
bool app_wx_brief(char *out, size_t n);
bool app_wx_lock_card(char *city, size_t cn, char *temp, size_t tn,
                      char *sub, size_t sn);
// 有有效预报时返回 WMO 天气码,否则 -1。
int app_wx_wmo(void);
// 在 40x40 透明容器里画像素天气图标。wmo<0 时清空。
void app_wx_draw_icon(lv_obj_t *parent, int wmo);
void app_wx_enter(lv_obj_t *p);
void app_wx_exit(void);
void app_wx_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_totp_enter(lv_obj_t *p);
void app_totp_exit(void);
void app_totp_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_rc_enter(lv_obj_t *p);
void app_rc_exit(void);
void app_rc_key(bsp_btn_t btn, bsp_btn_ev_t ev);
