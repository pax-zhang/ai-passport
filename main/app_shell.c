#include "app.h"

#include "app_dial.h"
#include "app_prefs.h"
#include "app_web.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pm.h"
#include "ui_pixel.h"

#include "esp_timer.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_main;
static lv_timer_t *s_timer;
static volatile bool s_asleep;
static bool s_wake_skip;
static uint32_t s_idle_ms;

static void sleep_now(void)
{
    if (s_asleep) return;
    app_prefs_flush();
    s_asleep = true;
    bsp_display_backlight(0);
    bsp_lvgl_flush_enable(false);
    bsp_display_sleep(true);
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
    bsp_display_sleep(false);
    bsp_lvgl_flush_enable(true);
    if (s_scr) lv_obj_invalidate(s_scr);
    lv_refr_now(NULL);
    app_prefs_apply_display();
}

bool app_shell_asleep(void)
{
    return s_asleep;
}

lv_obj_t *app_shell_screen(void)
{
    return s_scr;
}

void app_shell_reload(void)
{
    app_dial_request_reload();
}

static void tick(lv_timer_t *t)
{
    (void)t;
    app_prefs_tick();
    app_web_poll();
    app_dial_tick();
    if (app_web_keep_awake()) {
        s_idle_ms = 0;
        return;
    }
    uint16_t lim = app_prefs()->sleep_sec;
    if (lim == 0 || s_asleep) return;
    s_idle_ms += 250;
    if (s_idle_ms >= (uint32_t)lim * 1000) sleep_now();
}

static void on_gpio_wake(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    s_wake_skip = true;
    app_shell_wake();
    bsp_lvgl_unlock();
}

void app_shell_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_asleep) {
        if (ev == BSP_BTN_PRESS || ev == BSP_BTN_CLICK) {
            app_shell_wake();
            s_wake_skip = true;
        }
        return;
    }
    s_idle_ms = 0;
    if (s_wake_skip) {
        if (ev == BSP_BTN_CLICK) s_wake_skip = false;
        return;
    }
    if (app_web_key(btn, ev)) return;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        app_web_open();
        return;
    }
    app_dial_key(btn, ev);
}

void app_shell_start(void)
{
    ui_theme_set(UI_ST_GEEK);
    ui_pixel_theme_init();

    s_scr = lv_obj_create(NULL);
    ui_pixel_strip_theme(s_scr);
    lv_obj_set_size(s_scr, 240, 320);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0B1A2E), 0);
    lv_obj_set_style_text_font(s_scr, ui_pixel_font_14(), 0);

    s_main = lv_obj_create(s_scr);
    ui_pixel_strip_theme(s_main);
    lv_obj_set_pos(s_main, 0, 0);
    lv_obj_set_size(s_main, 240, 320);

    app_dial_enter(s_main);
    app_web_init(s_scr);
    s_asleep = false;
    s_idle_ms = 0;
    bsp_button_set_wake_cb(on_gpio_wake);
    s_timer = lv_timer_create(tick, 250, NULL);
    lv_screen_load(s_scr);
    lv_obj_invalidate(s_scr);
    lv_refr_now(NULL);
}
