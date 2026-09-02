#include "app.h"

#include "app_i18n.h"
#include "app_notif.h"
#include "app_prefs.h"
#include "app_ui.h"
#include "app_web.h"
#include "ui_pixel.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_box, *s_seal, *s_count, *s_rule, *s_hint;
static bool s_shown;

static void set_text(lv_obj_t *o, const char *text)
{
    if (!o || !text) return;
    const char *old = lv_label_get_text(o);
    if (!old || strcmp(old, text) != 0) lv_label_set_text(o, text);
}

static void paint(void)
{
    if (!s_shown) return;
    int n = app_notif_unread();
    char line[32];
    if (n > 0) snprintf(line, sizeof(line), app_str(APP_STR_HOME_N_MSG), n);
    else snprintf(line, sizeof(line), "%s", app_str(APP_STR_HOME_NO_MSG));
    int st = ui_theme_id();
    if (st == UI_ST_GEEK) {
        snprintf(line, sizeof(line), n > 0 ? "UNREAD %d" : "UNREAD 0", n);
    } else if (st == UI_ST_ANIME && n > 0) {
        snprintf(line, sizeof(line), "* %d", n);
    }
    set_text(s_count, line);
    if (st == UI_ST_GEEK) set_text(s_hint, "// OK");
    else set_text(s_hint, app_str(APP_STR_LOCK_HINT));
    if (s_box) ui_pixel_glass(s_box);
    if (s_count) {
        const lv_font_t *font = ui_pixel_font_cjk();
        if (st == UI_ST_GEEK) font = ui_pixel_font_14();
        uint32_t cc = ui_style_text();
        if (n <= 0 && st != UI_ST_POP) cc = ui_style_mute();
        lv_obj_set_style_text_font(s_count, font, 0);
        lv_obj_set_style_text_color(s_count, lv_color_hex(cc), 0);
        int oy = -8;
        if (st == UI_ST_ANIME) oy = 4;
        lv_obj_align(s_count, LV_ALIGN_CENTER, 0, oy);
    }
    if (s_hint) {
        uint32_t hc = (st == UI_ST_POP) ? ui_style_text() : ui_style_mute();
        lv_obj_set_style_text_color(s_hint, lv_color_hex(hc), 0);
    }
    if (s_seal) lv_obj_add_flag(s_seal, LV_OBJ_FLAG_HIDDEN);
    if (s_rule) {
        if (st == UI_ST_INK) {
            lv_obj_set_size(s_rule, 64, 1);
            lv_obj_set_style_bg_color(s_rule, lv_color_hex(ui_style_line()), 0);
            lv_obj_align(s_rule, LV_ALIGN_CENTER, 0, 20);
            lv_obj_remove_flag(s_rule, LV_OBJ_FLAG_HIDDEN);
        } else if (st == UI_ST_GEEK) {
            lv_obj_set_size(s_rule, APP_TEXT_W, 1);
            lv_obj_set_style_bg_color(s_rule, lv_color_hex(ui_style_line()), 0);
            lv_obj_align(s_rule, LV_ALIGN_CENTER, 0, 18);
            lv_obj_remove_flag(s_rule, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rule, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static bool lock_modal_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {
        app_web_qr_open();
        return true;
    }
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_LONG) {
        int id = (ui_theme_id() + 1) % ui_theme_count();
        ui_theme_set(id);
        app_prefs()->theme = (uint8_t)id;
        app_prefs_save();
        app_shell_retheme();
        return true;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        app_lock_hide();
        app_shell_header_sync();
    }
    return true;
}

void app_lock_init(lv_obj_t *screen)
{
    static const app_modal_t modal = {
        .visible = app_lock_visible,
        .key = lock_modal_key,
        .prio = 50,
    };
    app_shell_register_modal(&modal);

    s_box = lv_obj_create(screen);
    ui_pixel_strip_theme(s_box);
    lv_obj_set_pos(s_box, 0, 0);
    lv_obj_set_size(s_box, APP_SCREEN_W, APP_SCREEN_H);
    ui_pixel_glass(s_box);

    s_seal = lv_obj_create(s_box);
    ui_pixel_strip_theme(s_seal);
    lv_obj_set_size(s_seal, 10, 10);
    lv_obj_set_style_radius(s_seal, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_seal, lv_color_hex(UI_CYAN), 0);
    lv_obj_align(s_seal, LV_ALIGN_CENTER, 0, -40);

    s_count = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_count, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_count, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_align(s_count, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_count, APP_TEXT_W);
    lv_label_set_long_mode(s_count, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_count, LV_ALIGN_CENTER, 0, -8);

    s_rule = lv_obj_create(s_box);
    ui_pixel_strip_theme(s_rule);
    lv_obj_set_size(s_rule, 40, 2);
    lv_obj_set_style_bg_color(s_rule, lv_color_hex(UI_CYAN), 0);
    lv_obj_align(s_rule, LV_ALIGN_CENTER, 0, 18);

    s_hint = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_hint, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint, APP_TEXT_W);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    s_shown = false;
}

void app_lock_show(void)
{
    if (!s_box) return;
    s_shown = true;
    app_shell_page_obscure(true);
    paint();
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_box);
}

void app_lock_hide(void)
{
    if (!s_box) return;
    s_shown = false;
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    app_shell_page_obscure(false);
}

bool app_lock_visible(void)
{
    return s_shown;
}

void app_lock_tick(void)
{
    if (s_shown) paint();
}

void app_lock_retheme(void)
{
    if (s_shown) paint();
}
