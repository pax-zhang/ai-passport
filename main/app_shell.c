#include "app.h"

#include "app_notif.h"
#include "app_prefs.h"
#include "app_time.h"
#include "app_ui.h"
#include "app_web.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_ble.h"
#include "bsp_display.h"
#include "bsp_pm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_pixel.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STACK_MAX 8
#define RESOURCE_LOG_MS 60000

typedef struct {
    app_enter_fn enter;
    app_exit_fn exit;
    app_key_fn key;
    app_back_fn back;
    uint8_t traits;
    app_list_t list;
} page_t;

static lv_obj_t *s_scr, *s_hdr, *s_main, *s_batt, *s_link_bar;
static lv_timer_t *s_timer, *s_home_lock_timer;
static esp_timer_handle_t s_wake_timer;
static page_t s_stack[STACK_MAX];
static int s_sp = -1;
static app_modal_t s_modal[APP_MODAL_MAX];
static int s_modal_n;
static volatile bool s_asleep;
static volatile bool s_ble_wake_pending;
static bool s_lock_skip;
static uint32_t s_idle_ms;
static uint32_t s_resource_ms;
static uint32_t s_ble_idle_ms;
static char s_header_batt[24];
static int8_t s_header_link = -1;
static const char *TAG = "app_shell";

void app_shell_register_modal(const app_modal_t *m)
{
    if (!m || !m->visible || !m->key || s_modal_n >= APP_MODAL_MAX) return;
    int i = s_modal_n++;
    while (i > 0 && s_modal[i - 1].prio < m->prio) {
        s_modal[i] = s_modal[i - 1];
        i--;
    }
    s_modal[i] = *m;
}

// 只分发 prio 落在 (lo, hi] 区间的覆盖层。s_modal 已按 prio 降序排好。
static bool modal_dispatch(bsp_btn_t btn, bsp_btn_ev_t ev, int lo, int hi)
{
    for (int i = 0; i < s_modal_n; i++) {
        int p = s_modal[i].prio;
        if (p > hi || p <= lo) continue;
        if (!s_modal[i].visible()) continue;
        if (s_modal[i].key(btn, ev)) return true;
    }
    return false;
}

app_list_t *app_shell_list(void)
{
    if (s_sp < 0) return NULL;
    return &s_stack[s_sp].list;
}

void app_shell_set_back(app_back_fn fn)
{
    if (s_sp < 0) return;
    s_stack[s_sp].back = fn;
}

static void resource_log(const char *at)
{
    lv_mem_monitor_t lv;
    lv_mem_monitor(&lv);
    size_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "%s page=%d heap=%u min=%u block=%u lv=%u/%u stack=%u",
             at ? at : "resource", s_sp,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)esp_get_minimum_free_heap_size(), (unsigned)block,
             (unsigned)lv.free_size, (unsigned)lv.total_size,
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    if (block < 12 * 1024) ESP_LOGW(TAG, "low contiguous heap: %u", (unsigned)block);
}

static void header_refresh(void)
{
    char batt[24];

    int soc = bsp_battery_soc();
    bool link = bsp_ble_enabled() &&
                (bsp_ble_state() == BSP_BLE_ANCS ||
                 bsp_ble_state() == BSP_BLE_CONNECTED);
    if (soc >= 0) snprintf(batt, sizeof(batt), "%d%%", soc);
    else batt[0] = 0;
    if (strcmp(batt, s_header_batt) != 0) {
        strlcpy(s_header_batt, batt, sizeof(s_header_batt));
        lv_label_set_text(s_batt, batt);
    }
    if (s_link_bar && (int8_t)link != s_header_link) {
        s_header_link = (int8_t)link;
        lv_obj_set_style_bg_color(s_link_bar,
                                  lv_color_hex(link ? ui_style_accent() : ui_style_line()), 0);
        lv_obj_set_style_bg_opa(s_link_bar, LV_OPA_COVER, 0);
    }
}

static void sleep_now(void)
{
    if (s_asleep) return;

    const app_prefs_t *p = app_prefs();
    app_prefs_flush();
    app_notif_store_flush();
    app_shell_page_obscure(true);
    if (p->lock_on || app_lock_visible()) {
        app_lock_show();
        if (p->lock_stay) {
            lv_refr_now(NULL);
            s_idle_ms = 0;
            return;
        }
    }

    s_asleep = true;
    bsp_display_backlight(0);
    bsp_lvgl_flush_enable(false);
    bsp_display_sleep(true);
    bsp_audio_standby();
    bsp_button_sleep_gpio(true);
    bsp_lvgl_tick_enable(false);
    bsp_pm_set_sleeping(true);
}

void app_shell_wake(void)
{
    s_idle_ms = 0;
    if (!s_asleep) return;
    s_asleep = false;
    bsp_pm_set_sleeping(false);
    bsp_button_sleep_gpio(false);
    bsp_lvgl_tick_enable(true);
    if (app_prefs()->lock_on || app_lock_visible()) {
        if (!app_lock_visible()) app_lock_show();
        app_lock_tick();
    } else {
        app_lock_hide();
    }
    bsp_display_sleep(false);
    bsp_lvgl_flush_enable(true);
    lv_obj_invalidate(s_scr);
    lv_refr_now(NULL);
    app_prefs_apply_display();
    if (s_timer) lv_timer_set_period(s_timer, 250);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (s_ble_wake_pending) {
        s_ble_wake_pending = false;
        s_ble_idle_ms = 0;
        app_shell_wake();
    }
    app_time_tick();
    app_prefs_tick();
    app_web_poll();
    app_notif_poll();
    app_notif_tick(s_asleep ? 1000 : 250);
    if (!s_asleep && !app_lock_visible() && !app_notif_visible() &&
        !app_web_keep_awake() && bsp_ble_enabled() && !bsp_ble_synced()) {
        s_ble_idle_ms += 250;
        if (s_ble_idle_ms >= 5000) {
            s_ble_idle_ms = 0;
            bsp_ble_resume();
        }
    } else {
        s_ble_idle_ms = 0;
    }
    s_resource_ms += s_asleep ? 1000 : 250;
    if (s_resource_ms >= RESOURCE_LOG_MS) {
        s_resource_ms = 0;
        resource_log("periodic");
    }
    if (app_lock_visible() && !s_asleep) app_lock_tick();
    if (!s_asleep && !app_lock_visible()) header_refresh();

    if (app_notif_visible() || app_web_keep_awake()) {
        s_idle_ms = 0;
        return;
    }
    if (app_lock_visible() && app_prefs()->lock_stay) {
        s_idle_ms = 0;
        return;
    }
    uint16_t lim = app_prefs()->sleep_sec;
    if (lim == 0) return;
    s_idle_ms += s_asleep ? 1000 : 250;
    if (s_idle_ms >= (uint32_t)lim * 1000) sleep_now();
}

static void show_page(void)
{
    if (s_sp < 0) return;
    resource_log("page-leave");
    lv_obj_clean(s_main);
    app_ui_list_bind(NULL, 0, 0, 0);  // 行对象已随 clean 释放,丢掉缓存指针
    app_ui_screen_style(s_main);
    ui_pixel_hud_decor(s_main);
    s_stack[s_sp].enter(s_main);
    resource_log("page-enter");
}

void app_shell_open_ex(app_enter_fn enter, app_exit_fn exit, app_key_fn key,
                       uint8_t traits)
{
    if (s_sp + 1 >= STACK_MAX) return;
    if (s_sp >= 0 && s_stack[s_sp].exit) s_stack[s_sp].exit();
    s_sp++;
    memset(&s_stack[s_sp], 0, sizeof(s_stack[s_sp]));
    s_stack[s_sp].enter = enter;
    s_stack[s_sp].exit = exit;
    s_stack[s_sp].key = key;
    s_stack[s_sp].traits = traits;
    show_page();
}

void app_shell_open(app_enter_fn enter, app_exit_fn exit, app_key_fn key)
{
    app_shell_open_ex(enter, exit, key, 0);
}

void app_shell_back(void)
{
    if (s_sp <= 0) return;
    if (s_stack[s_sp].exit) s_stack[s_sp].exit();
    s_sp--;
    show_page();
}

void app_shell_reload(void)
{
    if (s_sp < 0) return;
    if (s_stack[s_sp].exit) s_stack[s_sp].exit();
    show_page();
}

bool app_shell_asleep(void)
{
    return s_asleep;
}

lv_obj_t *app_shell_screen(void)
{
    return s_scr;
}

bool app_shell_locked(void)
{
    return app_lock_visible();
}

void app_shell_retheme(void)
{
    if (s_scr) {
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(ui_style_bg()), 0);
        lv_obj_set_style_text_color(s_scr, lv_color_hex(ui_style_text()), 0);
    }
    if (s_hdr) ui_pixel_glass(s_hdr);
    if (s_main) ui_pixel_glass(s_main);
    if (s_batt) lv_obj_set_style_text_color(s_batt, lv_color_hex(ui_style_mute()), 0);
    s_header_link = -1;
    app_shell_header_sync();
    app_lock_retheme();
}

void app_shell_header_sync(void)
{
    header_refresh();
}

void app_shell_page_obscure(bool hide)
{
    if (s_main) {
        if (hide) lv_obj_add_flag(s_main, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_main, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_hdr) {
        if (hide) lv_obj_add_flag(s_hdr, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_hdr, LV_OBJ_FLAG_HIDDEN);
    }
}

void app_shell_page_vacate(void)
{
    if (!s_main) return;
    lv_obj_clean(s_main);
    app_ui_list_bind(NULL, 0, 0, 0);
    lv_obj_add_flag(s_main, LV_OBJ_FLAG_HIDDEN);
}

static void lock_now(void)
{
    if (app_lock_visible()) return;
    app_lock_show();
}

static void home_lock_cancel(void)
{
    if (s_home_lock_timer) {
        lv_timer_delete(s_home_lock_timer);
        s_home_lock_timer = NULL;
    }
}

static void home_lock_timeout(lv_timer_t *t)
{
    (void)t;
    s_home_lock_timer = NULL;
    if (s_sp <= 0 && !app_lock_visible() && !s_asleep &&
        !app_notif_visible() && !app_web_keep_awake()) {
        lock_now();
    }
}

static bool on_home(void)
{
    return s_sp <= 0;
}

static void on_gpio_wake(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    s_lock_skip = true;
    app_shell_wake();
    bsp_lvgl_unlock();
}

static void wake_timer_cb(void *arg)
{
    (void)arg;
    s_ble_wake_pending = true;
    bsp_lvgl_tick_enable(true);
}

static void on_ble_activity(void)
{
    if (!app_shell_asleep() || !s_wake_timer) return;
    esp_timer_stop(s_wake_timer);
    esp_timer_start_once(s_wake_timer, 1);
}

void app_shell_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    s_ble_idle_ms = 0;
    if (s_asleep) {
        home_lock_cancel();
        if (ev == BSP_BTN_PRESS || ev == BSP_BTN_CLICK) {
            app_shell_wake();
            s_lock_skip = true;
        }
        return;
    }
    s_idle_ms = 0;
    s_resource_ms = 0;

    bool overlay = app_lock_visible() || app_notif_visible() ||
                   app_web_keep_awake();
    if (btn == BSP_BTN_OK && ev == BSP_BTN_PRESS && on_home() && !overlay) {
        home_lock_cancel();
        s_home_lock_timer = lv_timer_create(home_lock_timeout, 800, NULL);
        lv_timer_set_repeat_count(s_home_lock_timer, 1);
    } else if (ev == BSP_BTN_RELEASE || btn != BSP_BTN_OK) {
        home_lock_cancel();
    }

    if (modal_dispatch(btn, ev, APP_MODAL_WAKE_GATE, 255)) return;

    if (s_lock_skip) {
        if (ev == BSP_BTN_CLICK) s_lock_skip = false;
        return;
    }

    if (modal_dispatch(btn, ev, 0, APP_MODAL_WAKE_GATE)) return;

    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        if (on_home()) lock_now();
        else if (!(s_sp >= 0 && s_stack[s_sp].back && s_stack[s_sp].back()))
            app_shell_back();
        return;
    }
    if (s_sp >= 0 && s_stack[s_sp].key) s_stack[s_sp].key(btn, ev);
}

static void shell_paint_bg(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_pad_row(o, 0, 0);
    lv_obj_set_style_pad_column(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(ui_style_bg()), 0);
}

void app_shell_start(void)
{
    ui_theme_set((int)app_prefs()->theme);
    ui_pixel_theme_init();

    s_scr = lv_obj_create(NULL);
    shell_paint_bg(s_scr);
    lv_obj_set_style_text_font(s_scr, ui_pixel_font_cjk(), 0);
    ui_pixel_wallpaper_attach(s_scr);

    s_hdr = lv_obj_create(s_scr);
    shell_paint_bg(s_hdr);
    lv_obj_set_pos(s_hdr, APP_VIEW_X, APP_VIEW_Y);
    lv_obj_set_size(s_hdr, APP_VIEW_W, APP_HEADER_H);
    lv_obj_set_style_bg_color(s_hdr, lv_color_hex(ui_style_bg()), 0);
    ui_pixel_glass(s_hdr);

    s_link_bar = lv_obj_create(s_hdr);
    shell_paint_bg(s_link_bar);
    lv_obj_set_size(s_link_bar, 7, 7);
    lv_obj_set_style_radius(s_link_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_link_bar, lv_color_hex(UI_GRID), 0);
    lv_obj_set_style_bg_opa(s_link_bar, LV_OPA_COVER, 0);
    lv_obj_align(s_link_bar, LV_ALIGN_RIGHT_MID, -40, 0);

    s_batt = lv_label_create(s_hdr);
    lv_obj_set_style_text_font(s_batt, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(ui_style_mute()), 0);
    lv_obj_set_style_bg_opa(s_batt, LV_OPA_TRANSP, 0);
    lv_obj_align(s_batt, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_label_set_text(s_batt, "");

    s_main = lv_obj_create(s_scr);
    shell_paint_bg(s_main);
    app_ui_screen_style(s_main);
    ui_pixel_glass(s_main);
    lv_obj_set_pos(s_main, APP_VIEW_X, APP_BODY_Y);
    lv_obj_set_size(s_main, APP_VIEW_W, APP_BODY_H);
    lv_obj_set_style_min_width(s_main, APP_VIEW_W, 0);
    lv_obj_set_style_min_height(s_main, APP_BODY_H, 0);

    s_modal_n = 0;
    app_lock_init(s_scr);
    app_notif_init(s_scr);
    app_web_init(s_scr);
    s_sp = -1;
    s_asleep = false;
    s_idle_ms = 0;
    s_ble_idle_ms = 0;
    s_header_batt[0] = 0;
    s_header_link = -1;
    bsp_button_set_wake_cb(on_gpio_wake);
    bsp_ble_set_activity_cb(on_ble_activity);
    const esp_timer_create_args_t wake_args = {
        .callback = wake_timer_cb,
        .name = "shell_wake",
    };
    if (!s_wake_timer) esp_timer_create(&wake_args, &s_wake_timer);
    app_shell_open_ex(app_ancs_enter, app_ancs_exit, app_ancs_key, APP_PAGE_BLE);
    header_refresh();
    s_timer = lv_timer_create(tick, 250, NULL);
    lv_obj_update_layout(s_scr);
    lv_screen_load(s_scr);
    lv_obj_invalidate(s_scr);
    lv_refr_now(NULL);
    app_web_boot_setup();
}
