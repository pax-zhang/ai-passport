#include "app.h"
#include "app_i18n.h"
#include "app_ui.h"
#include "ui_pixel.h"

#define HOME_N 5
#define TILE_H 40
#define TILE_Y 22
#define GAP    6

static lv_obj_t *s_cards[HOME_N];
static int s_painted_sel = -1;

static int home_sel(void)
{
    const app_list_t *l = app_shell_list();
    return l ? l->sel : 0;
}

static const uint32_t ACCENT[HOME_N] = {
    UI_BLUE, UI_CYAN, UI_MINT, UI_VIOLET, UI_MUTE
};

static void paint(void)
{
    int sel = home_sel();
    if (sel == s_painted_sel) return;
    if (s_painted_sel >= 0 && s_painted_sel < HOME_N && s_cards[s_painted_sel])
        app_ui_select(s_cards[s_painted_sel], false, ACCENT[s_painted_sel]);
    if (sel >= 0 && sel < HOME_N && s_cards[sel])
        app_ui_select(s_cards[sel], true, ACCENT[sel]);
    s_painted_sel = sel;
}

void app_home_enter(lv_obj_t *p)
{
    app_list_keep(app_shell_list(), NULL, HOME_N, HOME_N);
    app_ui_screen_style(p);
    const char *names[] = {
        app_str(APP_STR_HOME_ALERTS),
        app_str(APP_STR_HOME_WEATHER),
        app_str(APP_STR_HOME_CODES),
        app_str(APP_STR_HOME_REMOTE),
        app_str(APP_STR_HOME_SETTINGS),
    };

    lv_obj_t *title = lv_label_create(p);
    lv_obj_set_style_text_font(title, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_MUTE), 0);
    lv_label_set_text(title, app_str(APP_STR_HOME_APPS));
    lv_obj_set_pos(title, 4, 2);

    s_painted_sel = -1;
    for (int i = 0; i < HOME_N; i++) {
        int y = TILE_Y + i * (TILE_H + GAP);
        s_cards[i] = app_ui_row(p, 0, y, APP_CONTENT_W, TILE_H);

        lv_obj_t *lab = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(lab, ui_pixel_font_cjk(), 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(UI_TEXT), 0);
        lv_label_set_text(lab, names[i]);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 10, 0);
    }

    app_ui_footer(p, app_str(APP_STR_HINT_SEL));
    paint();
}

void app_home_exit(void)
{
    s_painted_sel = -1;
    for (int i = 0; i < HOME_N; i++) s_cards[i] = NULL;
}

void app_home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        app_list_move(app_shell_list(), NULL, btn == BSP_BTN_UP ? -1 : 1);
        paint();
        return;
    }
    if (btn != BSP_BTN_OK) return;
    switch (home_sel()) {
    case 0:
        app_shell_open_ex(app_ancs_enter, app_ancs_exit, app_ancs_key, APP_PAGE_BLE);
        break;
    case 1: app_shell_open(app_wx_enter, app_wx_exit, app_wx_key); break;
    case 2: app_shell_open(app_totp_enter, app_totp_exit, app_totp_key); break;
    case 3: app_shell_open(app_rc_enter, app_rc_exit, app_rc_key); break;
    default:
        app_shell_open(app_settings_enter, app_settings_exit, app_settings_key);
        break;
    }
}
