#include "app.h"
#include "app_i18n.h"
#include "app_ui.h"
#include "bsp_ble.h"
#include "ui_pixel.h"

#include <stdio.h>
#include <string.h>

#define MODE_N 2
#define HUD_BG       UI_BG
#define HUD_PANEL    UI_CARD
#define HUD_LINE     UI_LINE
#define HUD_CYAN     UI_CYAN
#define HUD_VIOLET   UI_VIOLET
#define HUD_MUTE     UI_MUTE

static const app_str_id_t MODE_STR[MODE_N] = {
    APP_STR_RC_CAM, APP_STR_RC_MUSIC,
};
static const char *const GLYPH[MODE_N] = { "\xE2\x97\x8F", "\xE2\x96\xB6" };
static const uint32_t ACCENT = HUD_VIOLET;

static lv_obj_t *s_rows[MODE_N], *s_subs[MODE_N], *s_hint, *s_body;
static lv_timer_t *s_vol_tm;
static int s_mode, s_vol_dir;
static uint32_t s_vol_last;

// 两张大卡片,焦点存页栈里,返回时回到原来那张。
static int rc_sel(void)
{
    const app_list_t *l = app_shell_list();
    return l ? l->sel : 0;
}

static bool linked(void)
{
    return bsp_ble_enabled() && bsp_ble_hid_ready();
}

static void paint_list(void)
{
    if (s_hint) {
        lv_label_set_text(s_hint, linked() ? app_str(APP_STR_HINT_SEL)
                                           : app_str(APP_STR_HINT_RC_NEED));
    }
    int sel = rc_sel();
    for (int i = 0; i < MODE_N; i++) {
        if (!s_rows[i]) continue;
        app_ui_select(s_rows[i], sel == i, ACCENT);
        if (!s_subs[i]) continue;
        lv_obj_set_style_text_color(s_subs[i],
                                    lv_color_hex(sel == i ? HUD_CYAN : HUD_MUTE), 0);
        char buf[80];
        if (i == 0) {
            snprintf(buf, sizeof(buf), app_str(APP_STR_RC_OKKEY),
                     app_str(APP_STR_RC_SHUTTER));
        } else if (i == 1) {
            snprintf(buf, sizeof(buf), "%s · %s",
                     app_str(APP_STR_RC_PREV), app_str(APP_STR_RC_PLAY));
        }
        lv_label_set_text(s_subs[i], buf);
    }
}

static void paint_mode(void)
{
    if (s_hint) {
        lv_label_set_text(s_hint, linked() ? app_str(APP_STR_RC_HINT)
                                           : app_str(APP_STR_RC_NEED));
    }
}

static void vol_tick(lv_timer_t *t)
{
    (void)t;
    if (s_vol_dir == 0 || !linked()) return;
    if (s_vol_last && lv_tick_elaps(s_vol_last) < 180) return;
    s_vol_last = lv_tick_get();
    bsp_ble_hid_tap(s_vol_dir > 0 ? BSP_BLE_HID_VOL_UP : BSP_BLE_HID_VOL_DOWN);
}

static lv_obj_t *control_box(lv_obj_t *p, int x, int y, int w, int h,
                             const char *key, const char *action, bool strong)
{
    lv_obj_t *box = app_ui_row(p, x, y, w, h);
    app_ui_select(box, strong, ACCENT);

    lv_obj_t *a = lv_label_create(box);
    lv_obj_set_style_text_font(a, (strong && h >= 80) ? ui_pixel_font_20()
                                                     : ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(a, lv_color_hex(strong ? HUD_CYAN : UI_TEXT), 0);
    lv_label_set_text(a, action);

    lv_obj_t *k = lv_label_create(box);
    lv_obj_set_style_text_font(k, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(k, lv_color_hex(HUD_VIOLET), 0);
    lv_label_set_text(k, key);

    if (h >= 80) {
        lv_obj_align(a, LV_ALIGN_CENTER, 0, -12);
        lv_obj_align(k, LV_ALIGN_BOTTOM_MID, 0, -12);
    } else if (w >= 140) {
        lv_obj_set_width(a, w - 72);
        lv_label_set_long_mode(a, LV_LABEL_LONG_CLIP);
        lv_obj_align(a, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_align(k, LV_ALIGN_RIGHT_MID, -8, 0);
    } else {
        lv_obj_set_width(a, w - 8);
        lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(a, LV_LABEL_LONG_CLIP);
        lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 3);
        lv_obj_set_width(k, w - 8);
        lv_obj_set_style_text_align(k, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(k, LV_LABEL_LONG_CLIP);
        lv_obj_align(k, LV_ALIGN_BOTTOM_MID, 0, -3);
    }
    return box;
}

static void mode_enter(lv_obj_t *p)
{
    app_ui_screen_style(p);
    lv_obj_set_style_bg_color(p, lv_color_hex(HUD_BG), 0);
    app_ui_page_title(p, app_str(MODE_STR[s_mode]));

    if (s_mode == 0) {
        control_box(p, 20, 72, 200, 132, app_str(APP_STR_RC_K_OK),
                    app_str(APP_STR_RC_SHUTTER), true);
    } else {
        control_box(p, 20, 58, 200, 28, app_str(APP_STR_RC_K_UP),
                    app_str(APP_STR_RC_PREV), false);
        control_box(p, 20, 92, 200, 40, app_str(APP_STR_RC_K_OK),
                    app_str(APP_STR_RC_PLAY), true);
        control_box(p, 20, 138, 200, 28, app_str(APP_STR_RC_K_DOWN),
                    app_str(APP_STR_RC_NEXT), false);
        control_box(p, 20, 174, 96, 42, app_str(APP_STR_RC_K_HOLD_UP),
                    app_str(APP_STR_RC_VOL_UP), false);
        control_box(p, 124, 174, 96, 42, app_str(APP_STR_RC_K_HOLD_DOWN),
                    app_str(APP_STR_RC_VOL_DOWN), false);
    }
    s_body = p;
    s_hint = app_ui_footer(p, app_str(APP_STR_RC_HINT));
    s_vol_dir = 0;
    s_vol_last = 0;
    s_vol_tm = lv_timer_create(vol_tick, 180, NULL);
    paint_mode();
}

static void mode_exit(void)
{
    s_vol_dir = 0;
    if (s_vol_tm) {
        lv_timer_delete(s_vol_tm);
        s_vol_tm = NULL;
    }
    s_hint = s_body = NULL;
}

static void mode_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!linked()) {
        s_vol_dir = 0;
        paint_mode();
        return;
    }
    if (s_mode == 1 && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        if (ev == BSP_BTN_LONG) {
            s_vol_dir = (btn == BSP_BTN_UP) ? 1 : -1;
            s_vol_last = 0;
            vol_tick(NULL);
            return;
        }
        if (ev == BSP_BTN_RELEASE) {
            s_vol_dir = 0;
            return;
        }
    }
    if (ev != BSP_BTN_CLICK) return;
    if (s_mode == 0) {
        if (btn == BSP_BTN_OK) bsp_ble_hid_tap(BSP_BLE_HID_VOL_UP);
        return;
    }
    if (btn == BSP_BTN_UP) bsp_ble_hid_tap(BSP_BLE_HID_PREV);
    else if (btn == BSP_BTN_DOWN) bsp_ble_hid_tap(BSP_BLE_HID_NEXT);
    else if (btn == BSP_BTN_OK) bsp_ble_hid_tap(BSP_BLE_HID_PLAY);
}

void app_rc_enter(lv_obj_t *p)
{
    app_list_keep(app_shell_list(), NULL, MODE_N, MODE_N);
    app_ui_screen_style(p);
    lv_obj_set_style_bg_color(p, lv_color_hex(HUD_BG), 0);
    app_ui_page_title(p, app_str(APP_STR_HOME_REMOTE));

    for (int i = 0; i < MODE_N; i++) {
        s_rows[i] = app_ui_row(p, 0, 40 + i * 52, APP_CONTENT_W, 46);
        app_ui_badge(s_rows[i], 10, 9, GLYPH[i], i == 0 ? HUD_CYAN : HUD_VIOLET);
        lv_obj_t *lab = lv_label_create(s_rows[i]);
        lv_obj_set_style_text_font(lab, ui_pixel_font_14(), 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(UI_TEXT), 0);
        lv_label_set_text(lab, app_str(MODE_STR[i]));
        lv_obj_set_pos(lab, 40, 8);
        s_subs[i] = lv_label_create(s_rows[i]);
        lv_obj_set_style_text_font(s_subs[i], ui_pixel_font_14(), 0);
        lv_obj_set_style_text_color(s_subs[i], lv_color_hex(HUD_MUTE), 0);
        lv_obj_set_width(s_subs[i], 180);
        lv_label_set_long_mode(s_subs[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(s_subs[i], 40, 26);
    }
    s_hint = app_ui_footer(p, app_str(APP_STR_HINT_RC_NEED));
    paint_list();
}

void app_rc_exit(void)
{
    s_hint = s_body = NULL;
    for (int i = 0; i < MODE_N; i++) s_rows[i] = s_subs[i] = NULL;
}

void app_rc_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        app_list_move(app_shell_list(), NULL, btn == BSP_BTN_UP ? -1 : 1);
        paint_list();
        return;
    }
    if (btn != BSP_BTN_OK) return;
    if (!linked()) return;
    s_mode = rc_sel();
    app_shell_open(mode_enter, mode_exit, mode_key);
}
