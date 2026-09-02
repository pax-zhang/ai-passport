#include "app.h"
#include "app_i18n.h"
#include "app_meow_set.h"
#include "app_ota.h"
#include "app_prefs.h"
#include "app_ui.h"
#include "bsp_ble.h"
#include "bsp_wifi.h"
#include "ui_pixel.h"

#include <stdio.h>
#include <string.h>

#define N 10
#define VISIBLE_N 6

static lv_obj_t *s_title, *s_hint;
static lv_timer_t *s_lang_timer;
static bool s_open;
static app_row_t s_rows[N];
static char s_meta[N][40];

static const app_str_id_t SET_STR[N] = {
    APP_STR_LANGUAGE, APP_STR_WIFI, APP_STR_BLUETOOTH,
    APP_STR_DATETIME, APP_STR_SCREEN, APP_STR_LOCK, APP_STR_SOUND, APP_STR_UPDATE,
    APP_STR_HARDWARE, APP_STR_LOG
};

static const meow_set_id_t SET_MAP[7] = {
    MEOW_SET_WIFI, MEOW_SET_BLE, MEOW_SET_CLOCK,
    MEOW_SET_SCREEN, MEOW_SET_LOCK, MEOW_SET_SOUND, MEOW_SET_OTA
};

static void meta_text(int i, char *out, size_t n)
{
    if (i == 0) snprintf(out, n, "%s", app_lang_name(app_lang()));
    else if (i == 1) snprintf(out, n, "%s", app_str_onoff(bsp_wifi_enabled()));
    else if (i == 2) snprintf(out, n, "%s", app_str_onoff(bsp_ble_enabled()));
    else if (i == 3) snprintf(out, n, "%s", app_str(APP_STR_NTP));
    else if (i == 4) snprintf(out, n, "%s", app_str(APP_STR_SET_SCR_META));
    else if (i == 5) snprintf(out, n, "%s", app_str(APP_STR_SET_LOCK_META));
    else if (i == 6) snprintf(out, n, "%s", app_str(APP_STR_SET_SND_META));
    else if (i == 7) snprintf(out, n, "v%s", app_ota_cur_ver());
    else if (i == 8) snprintf(out, n, "ESP32-C3");
    else snprintf(out, n, "%s", app_str(APP_STR_SET_LOG_META));
}

static void build_rows(void)
{
    memset(s_rows, 0, sizeof(s_rows));
    for (int i = 0; i < N; i++) {
        meta_text(i, s_meta[i], sizeof(s_meta[i]));
        s_rows[i].kind = i == 0 ? APP_ROW_CHOICE : APP_ROW_ACTION;
        s_rows[i].label = app_str(SET_STR[i]);
        s_rows[i].value = s_meta[i];
    }
}

static void paint(void)
{
    if (app_meow_set_open_now()) return;
    if (s_title && !lv_obj_is_valid(s_title)) s_title = NULL;
    if (s_hint && !lv_obj_is_valid(s_hint)) s_hint = NULL;
    if (s_title) lv_label_set_text(s_title, app_str(APP_STR_SETTINGS));
    const app_list_t *l = app_shell_list();
    int sel = l ? l->sel : 0;
    if (s_hint) {
        lv_label_set_text(s_hint, sel == 0 ? app_str(APP_STR_HINT_CYCLE)
                                           : app_str(APP_STR_HINT_OPEN));
    }
    build_rows();
    app_ui_list_render(s_rows, app_shell_list());
}

static void lang_apply(lv_timer_t *t)
{
    (void)t;
    s_lang_timer = NULL;
    app_prefs_save_lang();
    if (!s_open || app_meow_set_open_now()) return;
    paint();
}

void app_settings_enter(lv_obj_t *p)
{
    s_open = true;
    app_ui_screen_style(p);
    s_title = app_ui_page_title(p, app_str(APP_STR_SETTINGS));
    s_hint = app_ui_footer(p, app_str(APP_STR_HINT_OPEN));
    build_rows();
    app_list_keep(app_shell_list(), s_rows, N, VISIBLE_N);
    app_ui_list_bind(p, 28, APP_BODY_H - 50, VISIBLE_N);
    paint();
}

void app_settings_exit(void)
{
    app_meow_set_close();
    if (s_lang_timer) {
        lv_timer_delete(s_lang_timer);
        s_lang_timer = NULL;
        app_prefs_save_lang();
    }
    s_title = s_hint = NULL;
    s_open = false;
    if (bsp_ble_enabled() && !bsp_ble_stack_up() && !app_ota_busy())
        bsp_ble_resume();
}

bool app_settings_open_now(void)
{
    return s_open;
}

void app_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (app_meow_set_open_now()) return;
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        app_list_move(app_shell_list(), s_rows, btn == BSP_BTN_UP ? -1 : 1);
        paint();
        return;
    }
    if (btn != BSP_BTN_OK) return;
    const app_list_t *l = app_shell_list();
    int sel = l ? l->sel : 0;
    if (sel == 0) {
        app_prefs_t *p = app_prefs();
        p->lang = (uint8_t)((p->lang + 1) % APP_LANG_N);
        app_lang_set((app_lang_t)p->lang);
        if (s_lang_timer) {
            lv_timer_reset(s_lang_timer);
        } else {
            s_lang_timer = lv_timer_create(lang_apply, 20, NULL);
            lv_timer_set_repeat_count(s_lang_timer, 1);
        }
        paint();
        return;
    }
    if (sel >= 1 && sel <= 7) {
        app_meow_set_open(app_shell_screen(), SET_MAP[sel - 1]);
        return;
    }
    if (sel == 8) app_shell_open(app_hw_enter, app_hw_exit, app_hw_key);
    else if (sel == 9) app_shell_open(app_logs_enter, app_logs_exit, app_logs_key);
}
