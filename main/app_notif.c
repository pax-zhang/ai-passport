#include "app_notif.h"

#include "app.h"
#include "app_i18n.h"
#include "app_logic.h"
#include "app_notif_rule.h"
#include "app_prefs.h"
#include "app_tone.h"
#include "app_web.h"
#include "ble_filter.h"
#include "bsp_ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "ui_pixel.h"

#include <stdio.h>
#include <string.h>

#define STORE_NS       "app_notif"
#define STORE_KEY      "hist"
#define STORE_FLUSH_US (60 * 1000000LL)

static const char *TAG = "app_notif";

static lv_obj_t *s_dim, *s_box, *s_back, *s_pager, *s_title, *s_meta, *s_code, *s_scroll, *s_body;
static lv_obj_t *s_hint, *s_act_row[APP_NOTIF_ACT_MAX], *s_act_lab[APP_NOTIF_ACT_MAX];
static app_notif_q_t s_q;
static int s_card_i;
static app_notif_store_t s_store;
static bool s_store_dirty;
static int64_t s_store_dirty_us;
static bool s_shown;
static bool s_pairing;
static int s_left_ms;
static int s_hint_sec = -1;
static uint32_t s_seen_key;
static app_notif_act_t s_acts[APP_NOTIF_ACT_MAX];
static int s_act_n;
static int s_act_sel;
static uint8_t s_cat;
static bool s_urgent;
static bool s_box_tall;
static bool s_code_badge;
static bool s_in_call;
static bool s_reading;
static bool s_can_scroll;
static char s_call_name[64];
static char s_call_num[32];
static lv_timer_t *s_vol_tm;
static int s_vol_dir;
static uint32_t s_vol_last;

static void store_touch(void)
{
    s_store_dirty = true;
    if (!s_store_dirty_us) s_store_dirty_us = esp_timer_get_time();
}

static void store_load(void)
{
    app_notif_store_init(&s_store);
    nvs_handle_t h;
    if (nvs_open(STORE_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = 0;
    if (nvs_get_blob(h, STORE_KEY, NULL, &n) == ESP_OK && n > 0) {
        uint8_t *buf = malloc(n);
        if (buf && nvs_get_blob(h, STORE_KEY, buf, &n) == ESP_OK) {
            if (!app_notif_store_deserialize(&s_store, buf, n)) {
                ESP_LOGW(TAG, "history blob rejected, starting empty");
                app_notif_store_init(&s_store);
            }
        }
        free(buf);
    }
    nvs_close(h);
}

// 通知来得比设置频繁,写盘节流到 60 秒,息屏时由 app_notif_store_flush() 兜底。
void app_notif_store_flush(void)
{
    if (!s_store_dirty) return;
    size_t n = app_notif_store_blob_size(&s_store);
    uint8_t *buf = malloc(n);
    if (!buf) return;
    size_t used = app_notif_store_serialize(&s_store, buf, n);
    nvs_handle_t h;
    esp_err_t e = nvs_open(STORE_NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, STORE_KEY, buf, used);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    free(buf);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "save history: %s", esp_err_to_name(e));
        return;
    }
    s_store_dirty = false;
    s_store_dirty_us = 0;
}

static void raise(void)
{
    if (s_dim) lv_obj_move_foreground(s_dim);
    if (s_back) lv_obj_move_foreground(s_back);
    if (s_box) lv_obj_move_foreground(s_box);
    if (s_hint) {
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_hint);
    }
}

static void clamp_card(void)
{
    int n = app_notif_q_count(&s_q);
    if (s_card_i >= n) s_card_i = n > 0 ? n - 1 : 0;
    if (s_card_i < 0) s_card_i = 0;
}

static const app_notif_item_t *card_item(void)
{
    clamp_card();
    return app_notif_q_at(&s_q, s_card_i);
}

static void paint_pager(void)
{
    int n = app_notif_q_count(&s_q);
    if (s_pager) {
        if (n <= 1) {
            lv_obj_add_flag(s_pager, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text_fmt(s_pager, app_str(APP_STR_NOTIF_CARDS),
                                  s_card_i + 1, n);
            lv_obj_remove_flag(s_pager, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_back) {
        if (n <= 1) {
            lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static const char *act_text(const app_notif_act_t *a)
{
    if (a->label[0]) return a->label;
    if (a->kind == APP_NOTIF_ACT_POS) {
        return app_str(s_cat == APP_CAT_INCOMING ? APP_STR_ACT_ANSWER
                                                 : APP_STR_ACT_REPLY);
    }
    if (a->kind == APP_NOTIF_ACT_NEG) {
        return app_str(s_cat == APP_CAT_INCOMING ? APP_STR_ACT_DECLINE
                                                 : APP_STR_ACT_CLEAR);
    }
    return app_str(APP_STR_ACT_CLOSE);
}

static void load_acts(const app_notif_item_t *it)
{
    s_cat = it->category;
    s_act_n = app_notif_acts(it, s_acts, APP_NOTIF_ACT_MAX);
    s_act_sel = app_notif_act_default(s_acts, s_act_n, it->category);
    if (s_act_sel < 0 || s_act_sel >= s_act_n) s_act_sel = 0;
}

static int act_kind_index(uint8_t kind)
{
    for (int i = 0; i < s_act_n; i++) {
        if (s_acts[i].kind == kind) return i;
    }
    return -1;
}

static bool phone_char(char c)
{
    return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == ' ' ||
           c == '(' || c == ')' || c == '.';
}

static bool extract_phone(const char *s, char *out, size_t n)
{
    if (!s || !out || n < 2) return false;
    out[0] = 0;
    const char *best = NULL;
    int best_d = 0, best_len = 0;
    const char *p = s;
    while (*p) {
        if (phone_char(*p)) {
            int d = 0, len = 0;
            while (p[len] && phone_char(p[len])) {
                if (p[len] >= '0' && p[len] <= '9') d++;
                len++;
            }
            if (d >= 7 && d > best_d) {
                best = p;
                best_d = d;
                best_len = len;
            }
            p += len ? len : 1;
        } else {
            p++;
        }
    }
    if (!best) return false;
    while (best_len > 0 && best[0] == ' ') {
        best++;
        best_len--;
    }
    while (best_len > 0 && best[best_len - 1] == ' ') best_len--;
    if ((size_t)best_len + 1 > n) best_len = (int)n - 1;
    memcpy(out, best, (size_t)best_len);
    out[best_len] = 0;
    return out[0] != 0;
}

static bool is_phone_text(const char *s)
{
    char tmp[32];
    return extract_phone(s, tmp, sizeof(tmp)) && s && strcmp(s, tmp) == 0;
}

static void fill_call_fields(const app_notif_item_t *it)
{
    char title[64], sub[64], msg[160];
    s_call_name[0] = 0;
    s_call_num[0] = 0;
    if (!it) return;
    ui_pixel_utf8_copy(title, sizeof(title), it->title);
    ui_pixel_utf8_copy(sub, sizeof(sub), it->subtitle);
    ui_pixel_utf8_copy(msg, sizeof(msg), it->message);
    extract_phone(title, s_call_num, sizeof(s_call_num));
    if (!s_call_num[0]) extract_phone(sub, s_call_num, sizeof(s_call_num));
    if (!s_call_num[0]) extract_phone(msg, s_call_num, sizeof(s_call_num));
    if (title[0] && !is_phone_text(title)) {
        strlcpy(s_call_name, title, sizeof(s_call_name));
    } else if (sub[0] && !is_phone_text(sub)) {
        strlcpy(s_call_name, sub, sizeof(s_call_name));
    }
    if (!s_call_name[0]) {
        if (s_call_num[0]) strlcpy(s_call_name, s_call_num, sizeof(s_call_name));
        else if (title[0]) strlcpy(s_call_name, title, sizeof(s_call_name));
        else if (it->app_name[0]) {
            ui_pixel_utf8_copy(s_call_name, sizeof(s_call_name), it->app_name);
        }
    }
}

static bool call_ringing(void)
{
    return !s_pairing && !s_in_call && s_cat == APP_CAT_INCOMING;
}

static bool call_screen(void)
{
    return s_in_call || call_ringing();
}

static void hide_act_rows(void)
{
    for (int i = 0; i < APP_NOTIF_ACT_MAX; i++) {
        if (s_act_row[i]) lv_obj_add_flag(s_act_row[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void paint_act_rows(int w)
{
    for (int i = 0; i < APP_NOTIF_ACT_MAX; i++) {
        if (!s_act_row[i] || !s_act_lab[i]) continue;
        if (s_pairing || i >= s_act_n) {
            lv_obj_add_flag(s_act_row[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        bool on = i == s_act_sel;
        uint32_t fg = ui_style_text();
        if (s_acts[i].kind == APP_NOTIF_ACT_POS) fg = UI_MINT;
        else if (s_acts[i].kind == APP_NOTIF_ACT_NEG) fg = ui_style_urgent();
        else if (s_urgent) fg = ui_style_urgent();
        else fg = ui_style_accent();
        lv_obj_remove_flag(s_act_row[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_act_row[i], w);
        lv_obj_set_style_bg_color(s_act_row[i],
                                  lv_color_hex(on ? ui_style_fill() : ui_style_card()), 0);
        lv_obj_set_style_border_width(s_act_row[i], 1, 0);
        lv_obj_set_style_border_color(s_act_row[i], lv_color_hex(on ? fg : ui_style_line()), 0);
        lv_label_set_text(s_act_lab[i], act_text(&s_acts[i]));
        lv_obj_set_style_text_color(s_act_lab[i], lv_color_hex(fg), 0);
        lv_obj_center(s_act_lab[i]);
    }
}

static const char *sel_label(void)
{
    if (s_act_n > 0 && s_act_sel < s_act_n) return act_text(&s_acts[s_act_sel]);
    return app_str(APP_STR_ACT_CLOSE);
}

static void paint_hint(void)
{
    if (!s_hint || s_pairing) return;
    if (s_in_call) {
        lv_label_set_text(s_hint, app_str(APP_STR_CALL_VOL));
        s_hint_sec = 0;
        return;
    }
    if (s_reading) {
        lv_label_set_text(s_hint, app_str(s_can_scroll ? APP_STR_NOTIF_READ
                                                       : APP_STR_NOTIF_CLOSE));
        s_hint_sec = 0;
        return;
    }
    if (call_ringing()) {
        const char *ok = sel_label();
        const char *up = app_str(APP_STR_ACT_DECLINE);
        int ni = act_kind_index(APP_NOTIF_ACT_NEG);
        if (ni >= 0) up = act_text(&s_acts[ni]);
        lv_label_set_text_fmt(s_hint, app_str(APP_STR_CALL_KEYS), ok, up);
        s_hint_sec = 0;
        return;
    }
    if (s_left_ms <= 0) {
        lv_label_set_text(s_hint, app_str(APP_STR_NOTIF_KEYS));
        s_hint_sec = 0;
        return;
    }
    int sec = (s_left_ms + 999) / 1000;
    if (sec < 1) sec = 1;
    if (sec == s_hint_sec) return;
    s_hint_sec = sec;
    lv_label_set_text_fmt(s_hint, app_str(APP_STR_NOTIF_KEYS_AUTO), sec);
}

static bool notif_takeover(void)
{
    return s_urgent && s_box_tall && !s_pairing && !s_in_call;
}

static void style_popup_box(void)
{
    int st = ui_theme_id();
    bool take = notif_takeover();
    int rad = 10;
    if (st == UI_ST_ANIME) rad = 12;
    else if (st == UI_ST_GEEK || st == UI_ST_POP) rad = 0;
    else if (st == UI_ST_MINI) rad = 3;
    else if (st == UI_ST_INK) rad = 2;
    uint32_t bg = ui_style_card();
    uint32_t bd = ui_style_accent();
    if (st == UI_ST_POP) {
        bg = 0xFFE600;
        bd = 0x111111;
    }
    if (take) {
        if (st == UI_ST_GEEK) {
            bg = ui_style_accent();
            bd = ui_style_bg();
        } else if (st == UI_ST_MINI) {
            bg = ui_style_accent();
            bd = ui_style_accent();
        } else if (st == UI_ST_ANIME) {
            bg = ui_style_fill();
            bd = ui_style_accent();
        } else if (st == UI_ST_POP) {
            bg = 0x00E8FF;
            bd = 0x111111;
        } else if (st == UI_ST_INK) {
            bg = ui_style_card();
            bd = ui_style_urgent();
        } else {
            bg = ui_style_card();
            bd = ui_style_urgent();
        }
    }
    if (s_box) {
        lv_obj_set_style_radius(s_box, rad, 0);
        lv_obj_set_style_bg_color(s_box, lv_color_hex(bg), 0);
        lv_obj_set_style_border_color(s_box, lv_color_hex(bd), 0);
        lv_obj_set_style_border_side(s_box,
            (st == UI_ST_INK && take) ? LV_BORDER_SIDE_LEFT : LV_BORDER_SIDE_FULL, 0);
    }
    if (s_dim) {
        lv_obj_set_style_bg_color(s_dim, lv_color_hex(ui_style_bg()), 0);
        lv_obj_set_style_bg_opa(s_dim, take ? LV_OPA_40 : LV_OPA_50, 0);
    }
}

static void set_box_full(bool full)
{
    if (!s_box) return;
    s_box_tall = full;
    if (full) {
        int inset = notif_takeover() ? 8 : 0;
        lv_obj_set_pos(s_box, inset, inset);
        lv_obj_set_size(s_box, APP_SCREEN_W - inset * 2, APP_SCREEN_H - inset * 2);
        lv_obj_set_style_pad_all(s_box, 12, 0);
        int st = ui_theme_id();
        int bw = 0;
        if (notif_takeover()) {
            if (st == UI_ST_ANIME) bw = 2;
            else if (st == UI_ST_GEEK || st == UI_ST_MINI) bw = 1;
            else if (st == UI_ST_POP) bw = 4;
            else if (st == UI_ST_INK) bw = 3;
        }
        lv_obj_set_style_border_width(s_box, bw, 0);
    } else {
        lv_obj_set_pos(s_box, APP_VIEW_X + 3, APP_VIEW_Y + 5);
        lv_obj_set_size(s_box, APP_VIEW_W - 6, APP_VIEW_H - 10);
        lv_obj_set_style_pad_all(s_box, 11, 0);
        lv_obj_set_style_border_width(s_box, 2, 0);
    }
    style_popup_box();
}

static void set_box_compact(void)
{
    if (!s_box) return;
    s_box_tall = false;
    int st = ui_theme_id();
    int inset = 10;
    lv_obj_set_pos(s_box, inset, inset);
    lv_obj_set_size(s_box, APP_SCREEN_W - inset * 2, 120);
    lv_obj_set_style_pad_all(s_box, st == UI_ST_ANIME ? 12 : 10, 0);
    int bw = 2;
    if (st == UI_ST_INK) bw = 0;
    else if (st == UI_ST_MINI || st == UI_ST_GEEK) bw = 1;
    else if (st == UI_ST_POP) bw = 2;
    else if (st == UI_ST_ANIME) bw = 1;
    lv_obj_set_style_border_width(s_box, bw, 0);
    style_popup_box();
}

static void layout_acts(int inner_w, int inner_h, int hint_h, int btn_h, int y_min)
{
    int nbtn = s_act_n > APP_NOTIF_ACT_MAX ? APP_NOTIF_ACT_MAX : s_act_n;
    int gap = 6;
    int stack = nbtn > 0 ? nbtn * btn_h + (nbtn - 1) * gap : 0;
    int by = inner_h - hint_h - 8 - stack;
    if (by < y_min) by = y_min;
    paint_act_rows(inner_w);
    for (int i = 0; i < APP_NOTIF_ACT_MAX; i++) {
        if (!s_act_row[i] || lv_obj_has_flag(s_act_row[i], LV_OBJ_FLAG_HIDDEN))
            continue;
        lv_obj_set_pos(s_act_row[i], 0, by);
        lv_obj_set_size(s_act_row[i], inner_w, btn_h);
        if (s_act_lab[i]) lv_obj_center(s_act_lab[i]);
        by += btn_h + gap;
    }
}

static void layout_body(void)
{
    if (!s_box || !s_scroll || !s_hint) return;
    lv_obj_update_layout(s_box);
    int inner_h = (int)lv_obj_get_content_height(s_box);
    int inner_w = (int)lv_obj_get_content_width(s_box);
    if (inner_h < 80) inner_h = 248;
    if (inner_w < 80) inner_w = 196;

    if (s_title) lv_obj_set_width(s_title, inner_w);
    if (s_meta) lv_obj_set_width(s_meta, inner_w);
    if (s_hint) {
        lv_obj_set_width(s_hint, APP_TEXT_W);
        lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
        lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    bool call = call_screen();
    int hint_h = 0;
    if (s_box_tall) hint_h = (call || s_act_n > 1) ? 52 : 26;
    int btn_h = call ? 48 : 36;
    int nbtn = 0;
    if (call_ringing()) {
        nbtn = s_act_n > APP_NOTIF_ACT_MAX ? APP_NOTIF_ACT_MAX : s_act_n;
    }
    int gap = 6;
    int stack = nbtn > 0 ? nbtn * btn_h + (nbtn - 1) * gap : 0;
    int bottom = hint_h + (stack ? 8 + stack : 4);

    if (call) {
        lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
        if (s_title) {
            lv_obj_set_style_text_font(s_title, ui_pixel_font_20(), 0);
            lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
        }
        if (s_meta) {
            lv_obj_set_style_text_font(s_meta, ui_pixel_font_20(), 0);
            lv_obj_set_style_text_align(s_meta, LV_TEXT_ALIGN_CENTER, 0);
        }
        int y = 8;
        if (s_code && !lv_obj_has_flag(s_code, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_set_style_text_font(s_code, ui_pixel_font_14(), 0);
            lv_obj_align(s_code, LV_ALIGN_TOP_MID, 0, y);
            lv_obj_update_layout(s_code);
            y += (int)lv_obj_get_height(s_code) + 16;
        }
        lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_update_layout(s_title);
        y += (int)lv_obj_get_height(s_title) + 10;
        if (s_meta && !lv_obj_has_flag(s_meta, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_align(s_meta, LV_ALIGN_TOP_MID, 0, y);
            lv_obj_update_layout(s_meta);
            y += (int)lv_obj_get_height(s_meta) + 8;
        }
        if (s_in_call) hide_act_rows();
        else layout_acts(inner_w, inner_h, hint_h, btn_h, y + 8);
        return;
    }

    if (s_title) lv_obj_set_style_text_font(s_title, ui_pixel_font_cjk(), 0);
    if (s_meta) lv_obj_set_style_text_font(s_meta, ui_pixel_font_cjk(), 0);
    if (s_code) lv_obj_set_style_text_font(s_code, ui_pixel_font_20(), 0);

    lv_obj_remove_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(s_meta, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_meta, LV_TEXT_ALIGN_LEFT, 0);

    int y = 0;
    if (s_pager && !lv_obj_has_flag(s_pager, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_width(s_pager, inner_w);
        lv_obj_align(s_pager, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_update_layout(s_pager);
        y = (int)lv_obj_get_height(s_pager) + 6;
    }
    bool badge = s_code_badge && s_code && !lv_obj_has_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    if (badge) {
        lv_obj_set_pos(s_code, 0, y);
        lv_obj_update_layout(s_code);
        y += (int)lv_obj_get_height(s_code) + 8;
    }
    if (s_title) {
        lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, y);
        lv_obj_update_layout(s_title);
        y += (int)lv_obj_get_height(s_title);
    }
    if (s_meta && !lv_obj_has_flag(s_meta, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_pos(s_meta, 0, y + 4);
        lv_obj_update_layout(s_meta);
        y += 4 + (int)lv_obj_get_height(s_meta) + 6;
    } else {
        y += 8;
    }
    if (s_code && !lv_obj_has_flag(s_code, LV_OBJ_FLAG_HIDDEN) && !badge) {
        lv_obj_set_pos(s_code, 0, y);
        lv_obj_update_layout(s_code);
        y += (int)lv_obj_get_height(s_code) + 8;
    }

    int h = inner_h - y - bottom;
    if (h < 36) h = 36;
    lv_obj_set_pos(s_scroll, 0, y);
    lv_obj_set_size(s_scroll, inner_w, h);
    if (s_body) lv_obj_set_width(s_body, inner_w);
    lv_obj_update_layout(s_scroll);
    s_can_scroll = lv_obj_get_scroll_bottom(s_scroll) > 0 ||
                   lv_obj_get_scroll_top(s_scroll) > 0;
    lv_obj_set_scrollbar_mode(s_scroll, s_can_scroll ? LV_SCROLLBAR_MODE_AUTO
                                                     : LV_SCROLLBAR_MODE_OFF);
    if (!s_reading) lv_obj_scroll_to_y(s_scroll, 0, LV_ANIM_OFF);
    if (s_pairing || nbtn < 1) hide_act_rows();
    else layout_acts(inner_w, inner_h, hint_h, btn_h, y + 8);
}

static void scroll_body(int dir)
{
    if (!s_scroll || s_pairing || !s_can_scroll) return;
    lv_obj_scroll_by(s_scroll, 0, -dir * 24, LV_ANIM_OFF);
    if (s_left_ms > 0) {
        int sec = app_prefs()->auto_hide;
        if (sec > 0) {
            s_left_ms = sec * 1000;
            s_hint_sec = -1;
            paint_hint();
        }
    }
}

static void hide(void)
{
    s_in_call = false;
    s_reading = false;
    s_can_scroll = false;
    s_vol_dir = 0;
    if (s_dim) lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    if (s_box) lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    if (s_hint) lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    s_shown = false;
    s_pairing = false;
    s_code_badge = false;
    s_left_ms = 0;
    s_hint_sec = -1;
    hide_act_rows();
    if (s_pager) lv_obj_add_flag(s_pager, LV_OBJ_FLAG_HIDDEN);
    if (s_back) lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
}

static void paint_call_face(bool talking)
{
    if (s_pager) lv_obj_add_flag(s_pager, LV_OBJ_FLAG_HIDDEN);
    if (s_back) lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_title, lv_color_hex(talking ? ui_style_text() : ui_style_urgent()), 0);
    lv_obj_set_style_text_color(s_meta, lv_color_hex(ui_style_text()), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(talking ? ui_style_mute() : ui_style_urgent()), 0);
    lv_label_set_text(s_title, s_call_name[0] ? s_call_name : app_str(APP_STR_ALERT));
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    bool same = s_call_num[0] && strcmp(s_call_name, s_call_num) == 0;
    if (s_call_num[0] && !same) {
        lv_label_set_text(s_meta, s_call_num);
        lv_label_set_long_mode(s_meta, LV_LABEL_LONG_WRAP);
        lv_obj_remove_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_code) {
        uint32_t chip = talking ? UI_MINT : ui_style_urgent();
        lv_label_set_text(s_code, app_str(talking ? APP_STR_CALL_ON : APP_STR_CAT_CALL));
        lv_obj_set_style_text_color(s_code, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_color(s_code, lv_color_hex(chip), 0);
        lv_obj_set_style_border_color(s_code, lv_color_hex(chip), 0);
        lv_obj_remove_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_body, " ");
    s_left_ms = 0;
    s_hint_sec = -1;
    paint_hint();
    layout_body();
    if (s_dim) lv_obj_remove_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    raise();
}

static void paint_in_call(void)
{
    s_pairing = false;
    s_shown = true;
    s_in_call = true;
    s_act_n = 0;
    set_box_full(true);
    style_popup_box();
    hide_act_rows();
    paint_call_face(true);
}

static void vol_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_in_call || s_vol_dir == 0) return;
    if (s_vol_last && lv_tick_elaps(s_vol_last) < 180) return;
    s_vol_last = lv_tick_get();
    bsp_ble_hid_tap(s_vol_dir > 0 ? BSP_BLE_HID_VOL_UP : BSP_BLE_HID_VOL_DOWN);
}

static void paint_pairing(uint32_t key)
{
    if (s_pager) lv_obj_add_flag(s_pager, LV_OBJ_FLAG_HIDDEN);
    if (s_back) lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
    s_pairing = true;
    s_shown = true;
    s_left_ms = 0;
    set_box_full(false);
    hide_act_rows();
    style_popup_box();
    lv_obj_set_style_text_color(s_title, lv_color_hex(ui_style_text()), 0);
    lv_obj_set_style_text_color(s_meta, lv_color_hex(ui_style_mute()), 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(ui_style_text()), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(ui_style_mute()), 0);
    lv_label_set_text(s_title, app_str(APP_STR_PAIRING));
    lv_label_set_text(s_meta, app_str(APP_STR_PAIR_CONFIRM));
    lv_obj_remove_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_code, "%06lu", (unsigned long)key);
    lv_obj_remove_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_body, " ");
    lv_label_set_text(s_hint, app_str(bsp_ble_pair_needs_confirm()
                                         ? APP_STR_PAIR_ACTIONS
                                         : APP_STR_PAIR_CONFIRM));
    layout_body();
    if (s_dim) lv_obj_remove_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    raise();
}

static void style_urgent_badge(void)
{
    int st = ui_theme_id();
    uint32_t urg = ui_style_urgent();
    uint32_t bg = urg;
    uint32_t fg = 0xFFFFFF;
    int rad = 4;
    if (st == UI_ST_GEEK) {
        bg = ui_style_bg();
        fg = urg;
        rad = 0;
    } else if (st == UI_ST_MINI) {
        bg = 0xFFFFFF;
        fg = 0x09090B;
        rad = 2;
    } else if (st == UI_ST_INK) {
        fg = 0xF5E6C8;
        rad = 2;
    } else if (st == UI_ST_ANIME) {
        rad = 8;
    } else if (st == UI_ST_POP) {
        bg = 0xFF2D95;
        fg = 0xFFFFFF;
        rad = 0;
    }
    const lv_font_t *font = (st == UI_ST_GEEK || st == UI_ST_MINI)
                                ? ui_pixel_font_14() : ui_pixel_font_cjk();
    lv_obj_set_style_text_font(s_code, font, 0);
    lv_obj_set_style_text_color(s_code, lv_color_hex(fg), 0);
    lv_obj_set_style_bg_color(s_code, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(s_code, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(s_code, rad, 0);
}

static const char *urgent_badge_text(void)
{
    int st = ui_theme_id();
    if (st == UI_ST_GEEK) return "[ ALERT ]";
    if (st == UI_ST_MINI) return "URGENT";
    return app_str(APP_STR_ALERT_URGENT);
}

static void paint_notif(const app_notif_item_t *it)
{
    s_pairing = false;
    s_shown = true;
    s_in_call = false;
    s_reading = false;
    s_code_badge = false;
    load_acts(it);
    bool incoming = it->category == APP_CAT_INCOMING;
    s_urgent = it->alert >= APP_ALERT_URGENT;
    bool high = s_urgent;
    if (incoming) {
        set_box_full(true);
        fill_call_fields(it);
        paint_call_face(false);
        return;
    }
    if (high) set_box_full(true);
    else if (s_act_n > 1) set_box_full(false);
    else set_box_compact();

    uint32_t title_c = ui_style_text();
    uint32_t meta_c = ui_style_mute();
    uint32_t body_c = ui_style_text();
    uint32_t hint_c = ui_style_mute();
    if (high) {
        int st = ui_theme_id();
        if (st == UI_ST_GEEK) {
            title_c = meta_c = body_c = hint_c = ui_style_on();
        } else if (st == UI_ST_MINI) {
            title_c = body_c = ui_style_on();
            meta_c = hint_c = ui_style_mute();
        } else if (st == UI_ST_POP) {
            title_c = meta_c = body_c = hint_c = 0x111111;
        } else {
            title_c = hint_c = ui_style_urgent();
            meta_c = ui_style_mute();
            body_c = ui_style_text();
        }
    }
    lv_obj_set_style_text_color(s_title, lv_color_hex(title_c), 0);
    lv_obj_set_style_text_color(s_meta, lv_color_hex(meta_c), 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(body_c), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(hint_c), 0);

    char title[64];
    char app_name[40];
    char subtitle[64];
    char msg[160];
    char date[16];
    ui_pixel_utf8_copy(title, sizeof(title), it->title);
    ui_pixel_utf8_copy(app_name, sizeof(app_name), it->app_name);
    ui_pixel_utf8_copy(subtitle, sizeof(subtitle), it->subtitle);
    ui_pixel_utf8_copy(msg, sizeof(msg), it->message);
    bool has_date = app_ancs_date_text(it->date, date, sizeof(date));

    bool title_from_app = !title[0] && app_name[0];
    if (title_from_app) ui_pixel_utf8_copy(title, sizeof(title), app_name);
    lv_label_set_text(s_title, title[0] ? title : app_str(APP_STR_ALERT));
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);

    bool show_sub = app_notif_show_subtitle(title, subtitle);
    if (show_sub && msg[0]) {
        char body[230];
        snprintf(body, sizeof(body), "%s\n%s", subtitle, msg);
        lv_label_set_text(s_body, body);
    } else if (show_sub) {
        lv_label_set_text(s_body, subtitle);
    } else {
        lv_label_set_text(s_body, msg[0] ? msg : " ");
    }

    char meta[80];
    meta[0] = 0;
    const char *meta_app = title_from_app ? "" : app_name;
    if (meta_app[0] && has_date) snprintf(meta, sizeof(meta), "%s  %s", meta_app, date);
    else if (meta_app[0]) snprintf(meta, sizeof(meta), "%s", meta_app);
    else if (has_date) snprintf(meta, sizeof(meta), "%s", date);
    if (meta[0]) {
        lv_label_set_text(s_meta, meta);
        lv_obj_remove_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    }

    char otp[12];
    ble_filter_pick_code(it->title, it->subtitle, it->message, otp, sizeof(otp));
    if (s_code) {
        if (otp[0]) {
            uint32_t chip = high ? ui_style_urgent() : ui_style_accent();
            s_code_badge = false;
            lv_label_set_text(s_code, otp);
            lv_obj_set_style_text_font(s_code, ui_pixel_font_20(), 0);
            uint32_t otp_fg = UI_ON_ACCENT;
            if (high) {
                int st = ui_theme_id();
                if (st == UI_ST_GEEK) {
                    otp_fg = ui_style_on();
                } else if (st == UI_ST_MINI) {
                    chip = 0xFFFFFF;
                    otp_fg = 0x09090B;
                } else if (st == UI_ST_POP) {
                    otp_fg = 0x111111;
                } else {
                    otp_fg = 0xFFFFFF;
                }
            }
            lv_obj_set_style_text_color(s_code, lv_color_hex(otp_fg), 0);
            lv_obj_set_style_bg_color(s_code, lv_color_hex(chip), 0);
            lv_obj_set_style_border_color(s_code, lv_color_hex(chip), 0);
            lv_obj_set_style_radius(s_code, UI_RADIUS_SM, 0);
            lv_obj_remove_flag(s_code, LV_OBJ_FLAG_HIDDEN);
        } else if (high) {
            s_code_badge = true;
            lv_label_set_text(s_code, urgent_badge_text());
            style_urgent_badge();
            lv_obj_remove_flag(s_code, LV_OBJ_FLAG_HIDDEN);
        } else {
            s_code_badge = false;
            lv_obj_add_flag(s_code, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (high) {
        s_left_ms = 0;
    } else {
        int sec = app_prefs()->auto_hide;
        s_left_ms = sec > 0 ? sec * 1000 : 0;
    }
    s_hint_sec = -1;
    paint_hint();
    paint_pager();
    layout_body();
    if (s_dim) lv_obj_remove_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    raise();
}

static void show_front(void)
{
    const app_notif_item_t *it = card_item();
    if (!it) {
        hide();
        return;
    }
    paint_notif(it);
}

static void dismiss(void)
{
    if (s_pairing) return;
    const app_notif_item_t *it = card_item();
    if (it) app_notif_q_drop_uid(&s_q, it->uid);
    else app_notif_q_pop(&s_q);
    clamp_card();
    show_front();
}

static void run_act(void)
{
    if (s_pairing) return;
    const app_notif_item_t *it = card_item();
    uint8_t kind = APP_NOTIF_ACT_CLOSE;
    if (it && s_act_n > 0 && s_act_sel < s_act_n) {
        kind = s_acts[s_act_sel].kind;
        if (kind == APP_NOTIF_ACT_POS || kind == APP_NOTIF_ACT_NEG) {
            bsp_ble_notif_action(it->conn, it->uid,
                                 kind == APP_NOTIF_ACT_POS ? BSP_BLE_ACT_POS
                                                           : BSP_BLE_ACT_NEG);
            if (app_notif_store_mark_read_uid(&s_store, it->uid)) store_touch();
        }
    }
        if (kind == APP_NOTIF_ACT_POS && call_ringing()) {
        fill_call_fields(it);
        if (it) app_notif_q_drop_uid(&s_q, it->uid);
        paint_in_call();
        return;
    }
    dismiss();
}

static void bump_hide(void)
{
    if (s_left_ms > 0) {
        int sec = app_prefs()->auto_hide;
        if (sec > 0) s_left_ms = sec * 1000;
    }
    s_hint_sec = -1;
    paint_hint();
    int w = s_box ? (int)lv_obj_get_content_width(s_box) : APP_CONTENT_W;
    paint_act_rows(w);
}

static bool pair_visible(void)
{
    return s_shown && s_pairing;
}

static bool plain_visible(void)
{
    return s_shown && !s_pairing;
}

void app_notif_init(lv_obj_t *screen)
{
    // 配对确认排在唤醒吞键之前,否则息屏时的首击会被吃掉。
    static const app_modal_t pair_modal = {
        .visible = pair_visible,
        .key = app_notif_key,
        .prio = 100,
    };
    static const app_modal_t notif_modal = {
        .visible = plain_visible,
        .key = app_notif_key,
        .prio = 80,
    };
    app_shell_register_modal(&pair_modal);
    app_shell_register_modal(&notif_modal);

    app_notif_q_init(&s_q);
    store_load();
    s_back = lv_obj_create(screen);
    ui_pixel_strip_theme(s_back);
    lv_obj_set_pos(s_back, APP_VIEW_X + 10, APP_VIEW_Y + 14);
    lv_obj_set_size(s_back, APP_VIEW_W - 8, APP_VIEW_H - 16);
    lv_obj_set_style_border_width(s_back, 2, 0);
    lv_obj_set_style_border_color(s_back, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_radius(s_back, UI_RADIUS, 0);
    lv_obj_set_style_bg_opa(s_back, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_back, lv_color_hex(UI_FILL), 0);
    lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);

    s_dim = lv_obj_create(screen);
    ui_pixel_strip_theme(s_dim);
    lv_obj_set_pos(s_dim, 0, 0);
    lv_obj_set_size(s_dim, APP_SCREEN_W, APP_SCREEN_H);
        lv_obj_set_style_bg_color(s_dim, lv_color_hex(ui_style_bg()), 0);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_50, 0);

    s_box = lv_obj_create(screen);
    ui_pixel_strip_theme(s_box);
    lv_obj_set_pos(s_box, APP_VIEW_X + 3, APP_VIEW_Y + 5);
    lv_obj_set_size(s_box, APP_VIEW_W - 6, APP_VIEW_H - 10);
    lv_obj_set_style_border_width(s_box, 1, 0);
    lv_obj_set_style_border_color(s_box, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_border_opa(s_box, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(s_box, 0, 0);
    lv_obj_set_style_radius(s_box, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(s_box, 14, 0);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_box, lv_color_hex(UI_CARD), 0);

    s_pager = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_pager, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_pager, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_text_align(s_pager, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_pager, 194);
    lv_label_set_long_mode(s_pager, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_pager, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(s_pager, LV_OBJ_FLAG_HIDDEN);

    s_title = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_title, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_width(s_title, 194);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_meta = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_meta, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_meta, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_width(s_meta, 194);
    lv_label_set_long_mode(s_meta, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_meta, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_add_flag(s_meta, LV_OBJ_FLAG_HIDDEN);

    s_code = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_code, ui_pixel_font_20(), 0);
    lv_obj_set_style_text_color(s_code, lv_color_hex(UI_ON_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_code, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_code, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_pad_hor(s_code, 8, 0);
    lv_obj_set_style_pad_ver(s_code, 4, 0);
    lv_obj_set_style_radius(s_code, UI_RADIUS_SM, 0);
    lv_obj_set_style_border_width(s_code, 0, 0);
    lv_obj_set_style_border_color(s_code, lv_color_hex(UI_CYAN), 0);
    lv_obj_add_flag(s_code, LV_OBJ_FLAG_HIDDEN);

    s_scroll = lv_obj_create(s_box);
    ui_pixel_strip_theme(s_scroll);
    lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(s_scroll, 0, 40);
    lv_obj_set_size(s_scroll, 194, 180);

    s_body = lv_label_create(s_scroll);
    lv_obj_set_style_text_font(s_body, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_width(s_body, 194);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);

    for (int i = 0; i < APP_NOTIF_ACT_MAX; i++) {
        s_act_row[i] = lv_obj_create(s_box);
        ui_pixel_strip_theme(s_act_row[i]);
        lv_obj_set_size(s_act_row[i], 194, 48);
        lv_obj_set_style_radius(s_act_row[i], UI_RADIUS_SM, 0);
        lv_obj_set_style_border_width(s_act_row[i], 0, 0);
        lv_obj_set_style_bg_opa(s_act_row[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_act_row[i], lv_color_hex(UI_CARD), 0);
        s_act_lab[i] = lv_label_create(s_act_row[i]);
        lv_obj_set_style_text_font(s_act_lab[i], ui_pixel_font_cjk(), 0);
        lv_obj_set_style_text_align(s_act_lab[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_act_lab[i]);
        lv_obj_add_flag(s_act_row[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_hint = lv_label_create(screen);
    lv_obj_set_style_text_font(s_hint, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_width(s_hint, 194);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    hide();
    s_vol_tm = lv_timer_create(vol_tick, 180, NULL);
}

void app_notif_poll(void)
{
    bsp_ble_state_t st = bsp_ble_state();
    uint32_t key = bsp_ble_passkey();
    if (st == BSP_BLE_PAIRING && key) {
        if (!s_pairing || key != s_seen_key) paint_pairing(key);
        s_seen_key = key;
        raise();
        app_shell_wake();
        return;
    }
    if (s_pairing) {
        hide();
        show_front();
    }
    s_seen_key = 0;

    bsp_ble_notif_t n;
    while (bsp_ble_take_notif(&n)) {
        const app_prefs_t *p = app_prefs();
        app_notif_ctx_t ctx = {
            .app_id = n.app_id,
            .app_name = n.app_name,
            .title = n.title,
            .subtitle = n.subtitle,
            .message = n.message,
            .category = n.category,
        };
        app_alert_t alert =
            n.category == APP_CAT_INCOMING
                ? APP_ALERT_URGENT
                : app_notif_decide(&ctx, p->kw, p->kw_n, false,
                                   (app_alert_t)p->notif_def);

        app_notif_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        ui_pixel_utf8_copy(rec.app_id, sizeof(rec.app_id), n.app_id);
        ui_pixel_utf8_copy(rec.app_name, sizeof(rec.app_name), n.app_name);
        ui_pixel_utf8_copy(rec.title, sizeof(rec.title), n.title);
        ui_pixel_utf8_copy(rec.subtitle, sizeof(rec.subtitle), n.subtitle);
        ui_pixel_utf8_copy(rec.message, sizeof(rec.message), n.message);
        ui_pixel_utf8_copy(rec.date, sizeof(rec.date), n.date);
        rec.uid = n.uid;
        rec.category = n.category;
        rec.alert = (uint8_t)alert;
        rec.unread = 1;
        if (alert == APP_ALERT_DROP) continue;
        app_notif_store_push(&s_store, &rec);
        store_touch();

        if (alert == APP_ALERT_SILENT) continue;

        app_notif_item_t it;
        memset(&it, 0, sizeof(it));
        ui_pixel_utf8_copy(it.title, sizeof(it.title), n.title);
        ui_pixel_utf8_copy(it.subtitle, sizeof(it.subtitle), n.subtitle);
        ui_pixel_utf8_copy(it.message, sizeof(it.message), n.message);
        ui_pixel_utf8_copy(it.app_name, sizeof(it.app_name), n.app_name);
        ui_pixel_utf8_copy(it.date, sizeof(it.date), n.date);
        ui_pixel_utf8_copy(it.pos_label, sizeof(it.pos_label), n.pos_label);
        ui_pixel_utf8_copy(it.neg_label, sizeof(it.neg_label), n.neg_label);
        it.uid = n.uid;
        it.conn = n.conn;
        it.flags = n.flags;
        it.category = n.category;
        it.alert = (uint8_t)alert;
        // 队列里已有同一条时原地更新,不重复排队也不重复响铃。
        if (app_notif_q_update(&s_q, &it)) {
            const app_notif_item_t *cur = card_item();
            if (s_shown && !s_pairing && cur && cur->uid == n.uid) show_front();
            continue;
        }
        app_notif_q_push(&s_q, &it);
        s_card_i = app_notif_q_count(&s_q) - 1;
        if (s_shown && !s_pairing && !s_in_call) show_front();
        app_tone_play(alert == APP_ALERT_URGENT ? (int)p->tone_alert
                                                : (int)p->tone_msg);
    }

    uint32_t gone;
    while (bsp_ble_take_removed(&gone)) {
        const app_notif_item_t *cur = card_item();
        bool was_cur = s_shown && !s_pairing && !s_in_call && cur &&
                       cur->uid == gone;
        app_notif_q_drop_uid(&s_q, gone);
        if (app_notif_store_remove_uid(&s_store, gone)) store_touch();
        if (was_cur) {
            clamp_card();
            show_front();
        } else if (s_shown && !s_pairing) {
            paint_pager();
            layout_body();
        }
    }

    if (!s_shown && app_notif_q_front(&s_q)) show_front();
    if (s_shown && app_shell_asleep()) app_shell_wake();
    if (s_store_dirty && esp_timer_get_time() - s_store_dirty_us >= STORE_FLUSH_US) {
        app_notif_store_flush();
    }
}

bool app_notif_visible(void)
{
    return s_shown;
}

bool app_notif_pairing(void)
{
    return s_pairing;
}

bool app_notif_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_shown) return false;
    if (s_pairing) {
        if (!bsp_ble_pair_needs_confirm() || ev != BSP_BTN_CLICK) return true;
        if (btn == BSP_BTN_OK) {
            bsp_ble_pair_reply(true);
            hide();
        } else if (btn == BSP_BTN_DOWN) {
            bsp_ble_pair_reply(false);
            hide();
        }
        return true;
    }
    if (s_in_call) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            int dir = (btn == BSP_BTN_UP) ? 1 : -1;
            if (ev == BSP_BTN_CLICK) {
                s_vol_dir = 0;
                bsp_ble_hid_tap(dir > 0 ? BSP_BLE_HID_VOL_UP
                                        : BSP_BLE_HID_VOL_DOWN);
            } else if (ev == BSP_BTN_LONG) {
                s_vol_dir = dir;
                s_vol_last = 0;
                vol_tick(NULL);
            } else if (ev == BSP_BTN_RELEASE) {
                s_vol_dir = 0;
            }
            return true;
        }
        if (btn == BSP_BTN_OK &&
            (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
            hide();
            if (ev == BSP_BTN_CLICK) show_front();
        }
        return true;
    }
    if (call_ringing()) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
            int i = act_kind_index(APP_NOTIF_ACT_NEG);
            if (i >= 0) {
                s_act_sel = i;
                run_act();
            }
            return true;
        }
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            if (ev == BSP_BTN_CLICK && s_act_n > 1) {
                int dir = btn == BSP_BTN_DOWN ? 1 : -1;
                s_act_sel = (s_act_sel + dir + s_act_n) % s_act_n;
                bump_hide();
            }
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            dismiss();
            return true;
        }
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) run_act();
        return true;
    }
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        int dir = btn == BSP_BTN_DOWN ? 1 : -1;
        if (s_reading && (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
            scroll_body(dir);
            return true;
        }
        if (!s_reading && ev == BSP_BTN_CLICK && app_notif_q_count(&s_q) > 1) {
            s_card_i += dir;
            clamp_card();
            show_front();
        }
        return true;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        dismiss();
        return true;
    }
    if (ev != BSP_BTN_CLICK) return true;
    if (btn == BSP_BTN_OK) {
        dismiss();
        if (app_lock_visible()) {
            app_lock_hide();
            app_shell_header_sync();
        }
        return true;
    }
    return true;
}

void app_notif_tick(uint32_t ms)
{
    if (!s_shown || s_pairing || s_in_call) return;
    if (s_reading || s_left_ms <= 0) return;
    s_left_ms -= (int)ms;
    if (s_left_ms <= 0) {
        dismiss();
        return;
    }
    paint_hint();
}

const app_notif_store_t *app_notif_hist(void)
{
    return &s_store;
}

int app_notif_unread(void)
{
    return app_notif_store_unread(&s_store);
}

bool app_notif_hist_remove(int newest_i)
{
    if (!app_notif_store_remove(&s_store, newest_i)) return false;
    store_touch();
    return true;
}

void app_notif_hist_clear(void)
{
    app_notif_store_clear(&s_store);
    store_touch();
}

void app_notif_mark_read(int newest_i)
{
    if (app_notif_store_mark_read(&s_store, newest_i)) store_touch();
}

void app_notif_mark_unread(int newest_i)
{
    if (app_notif_store_mark_unread(&s_store, newest_i)) store_touch();
}

void app_notif_mark_all_read(void)
{
    if (app_notif_store_unread(&s_store) == 0) return;
    app_notif_store_mark_all_read(&s_store);
    store_touch();
}
