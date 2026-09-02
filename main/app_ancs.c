#include "app.h"
#include "app_i18n.h"
#include "app_logic.h"
#include "app_notif.h"
#include "app_notif_rule.h"
#include "app_prefs.h"
#include "app_ui.h"
#include "app_web.h"
#include "ble_filter.h"
#include "ui_pixel.h"

#include <stdio.h>
#include <string.h>

#define RECENT_N APP_NOTIF_STORE_N
#define HUD_BG       ui_style_bg()
#define HUD_PANEL    ui_style_card()
#define HUD_PANEL_HI ui_style_fill()
#define HUD_LINE     ui_style_line()
#define HUD_CYAN     ui_style_accent()
#define HUD_VIOLET   ui_style_accent()
#define HUD_MUTE     ui_style_mute()

static const app_str_id_t F_LBL[APP_RULE_F_N] = {
    APP_STR_RULE_F_ANY, APP_STR_RULE_F_TITLE, APP_STR_RULE_F_SUB,
    APP_STR_RULE_F_MSG, APP_STR_RULE_F_APP, APP_STR_RULE_F_NAME,
    APP_STR_RULE_F_CAT,
};
static const app_str_id_t OP_LBL[APP_RULE_OP_N] = {
    APP_STR_RULE_OP_HAS, APP_STR_RULE_OP_NOT, APP_STR_RULE_OP_HEAD,
    APP_STR_RULE_OP_TAIL, APP_STR_RULE_OP_MATCH, APP_STR_RULE_OP_EMPTY,
};
static const app_str_id_t J_LBL[APP_RULE_J_N] = {
    APP_STR_RULE_J_AND, APP_STR_RULE_J_OR,
    APP_STR_RULE_J_ANDNOT, APP_STR_RULE_J_ORNOT,
};

#define CARD_X       2
#define CARD_W       (APP_CONTENT_W - 4)
#define LIST_Y       24
#define LIST_H       (APP_BODY_H - 50)
#define GAP          6
#define BOX_PAD      12
#define BOX_BORDER   1
#define BOX_CHROME   (BOX_PAD * 2 + BOX_BORDER * 2)
#define TITLE_H      18
#define META_H       14
#define CODE_H       26
#define ROW_GAP      3
#define BODY_H       36
#define PREVIEW_H    16
#define COLLAPSED_H      (BOX_CHROME + TITLE_H + ROW_GAP + META_H + ROW_GAP + PREVIEW_H)
#define COLLAPSED_OTP_H  (BOX_CHROME + TITLE_H + ROW_GAP + META_H + ROW_GAP + CODE_H)
#define EXPANDED_H       (BOX_CHROME + TITLE_H + ROW_GAP + META_H + ROW_GAP + BODY_H)
#define EXPANDED_OTP_H   (BOX_CHROME + TITLE_H + ROW_GAP + META_H + ROW_GAP + CODE_H + ROW_GAP + BODY_H)
#define SETTINGS_H   40
#define VIS_MAX      4
#define ACT_N        3

typedef enum {
    VIEW_RECENT = 0,
    VIEW_SET,
    VIEW_EDIT,
    VIEW_COND,
    VIEW_KB,
    VIEW_CONFIRM
} view_t;

typedef struct {
    lv_obj_t *box;
    lv_obj_t *rail;
    lv_obj_t *rule;
    lv_obj_t *title;
    lv_obj_t *meta;
    lv_obj_t *code;
    lv_obj_t *body;
    lv_obj_t *dot;
} vis_t;

static lv_obj_t *s_page, *s_title, *s_hint, *s_body;
static lv_obj_t *s_recent, *s_form, *s_list;
static lv_obj_t *s_rtitle, *s_rhint, *s_ftitle, *s_fhint, *s_fbody;
static lv_obj_t *s_act_box[ACT_N], *s_act_lab[ACT_N];
static lv_obj_t *s_menu_rows[6];
static vis_t s_vis[VIS_MAX];
static view_t s_view;
static int s_sel, s_kb_sel, s_kb_set, s_kb_kind;
static int s_rule_i, s_field, s_op, s_join;
static int s_focus, s_csel, s_del_i;
static bool s_del_rule;
static bool s_reading;
static bool s_acts;
static bool s_eat_click;
static int s_act_sel;
static uint8_t s_edit_prio;
static char s_custom[APP_KW_LEN + 1];
static char s_name[APP_KW_NAME_LEN + 1];
static char s_expr[APP_KW_LEN + 1];
static int s_hold_btn = -1;
static int s_hold_ms;
static lv_timer_t *s_hold_timer;
/* LVGL 任务栈只有 5KB,设置/键盘页共用这块静态缓冲,不要改回栈上大数组。 */
static char s_paint[900];

static const uint8_t HIDE_OPTS[] = { 0, 5, 10, 20 };

static int hide_idx(uint8_t v)
{
    for (int i = 0; i < 4; i++) if (HIDE_OPTS[i] == v) return i;
    return 2;
}

static int recent_n(void)
{
    int n = app_notif_store_count(app_notif_hist());
    if (n > RECENT_N) n = RECENT_N;
    return n;
}

static int set_n(void)
{
    return app_prefs()->kw_n + 3;
}

// 名称 / 条件 / 提醒方式 / 保存
static int edit_n(void)
{
    return s_rule_i >= 0 ? 5 : 4;
}

static app_str_id_t tier_label(uint8_t alert)
{
    if (alert >= APP_ALERT_DROP) return APP_STR_ALERT_DROP;
    if (alert >= APP_ALERT_URGENT) return APP_STR_ALERT_URGENT;
    if (alert == APP_ALERT_POPUP) return APP_STR_ALERT_POPUP;
    return APP_STR_ALERT_SILENT;
}

static int cond_n(void)
{
    return s_expr[0] ? 5 : 4;
}

static int list_n(void)
{
    if (s_view == VIEW_RECENT) {
        int n = recent_n();
        return n > 0 ? n : 1;
    }
    if (s_view == VIEW_SET) return set_n();
    if (s_view == VIEW_EDIT) return edit_n();
    if (s_view == VIEW_COND) return cond_n();
    if (s_view == VIEW_CONFIRM) return 2;
    return 1;
}

static void pretty(const char *e, char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = 0;
    if (!e || !e[0]) return;
    size_t o = 0;
    for (const char *p = e; *p && o + 8 < n; ) {
        int w = 0;
        if (p[0] == '&' && p[1] == '!') {
            w = snprintf(out + o, n - o, " %s ", app_str(APP_STR_RULE_J_ANDNOT));
            p += 2;
        } else if (p[0] == '|' && p[1] == '!') {
            w = snprintf(out + o, n - o, " %s ", app_str(APP_STR_RULE_J_ORNOT));
            p += 2;
        } else if (*p == '&') {
            w = snprintf(out + o, n - o, " %s ", app_str(APP_STR_RULE_J_AND));
            p++;
        } else if (*p == '|') {
            w = snprintf(out + o, n - o, " %s ", app_str(APP_STR_RULE_J_OR));
            p++;
        } else {
            static const char *const k[] = {
                "any", "title", "sub", "msg", "app", "name", "cat",
            };
            int neg = 0;
            if (*p == '!') {
                neg = 1;
                p++;
            }
            const char *col = strchr(p, ':');
            const char *amp = strchr(p, '&');
            const char *bar = strchr(p, '|');
            const char *stop = p + strlen(p);
            if (amp && amp < stop) stop = amp;
            if (bar && bar < stop) stop = bar;
            int fi = APP_RULE_F_ANY;
            int op = APP_RULE_OP_HAS;
            const char *val = p;
            if (col && col < stop) {
                size_t fl = (size_t)(col - p);
                for (int i = 0; i < APP_RULE_F_N; i++) {
                    if (strlen(k[i]) == fl && !strncmp(p, k[i], fl)) {
                        fi = i;
                        break;
                    }
                }
                val = col + 1;
                size_t vl = (size_t)(stop - val);
                if (vl == 0) op = APP_RULE_OP_EMPTY;
                else if (vl >= 2 && val[0] == '*' && val[vl - 1] != '*') {
                    op = APP_RULE_OP_TAIL;
                    val++;
                } else if (vl >= 1 && val[vl - 1] == '*' && val[0] != '*') {
                    op = APP_RULE_OP_HEAD;
                }
            }
            if (neg && op != APP_RULE_OP_EMPTY) op = APP_RULE_OP_NOT;
            if (op == APP_RULE_OP_EMPTY) {
                w = snprintf(out + o, n - o, "%s %s",
                             app_str(F_LBL[fi]), app_str(OP_LBL[op]));
            } else {
                char vbuf[24];
                size_t vl = (size_t)(stop - val);
                if (op == APP_RULE_OP_HEAD && vl > 0) vl--;
                if (vl >= sizeof(vbuf)) vl = sizeof(vbuf) - 1;
                memcpy(vbuf, val, vl);
                vbuf[vl] = 0;
                w = snprintf(out + o, n - o, "%s %s %s",
                             app_str(F_LBL[fi]), app_str(OP_LBL[op]), vbuf);
            }
            p = stop;
        }
        if (w < 0) break;
        o += (size_t)w;
        if (o >= n) {
            out[n - 1] = 0;
            break;
        }
    }
}

static void save_rule(void)
{
    app_prefs_t *p = app_prefs();
    if (!s_expr[0]) return;
    if (!s_name[0]) {
        strlcpy(s_name, app_str(APP_STR_RULE_ALL), sizeof(s_name));
    }
    int i = s_rule_i;
    if (i < 0) {
        if (p->kw_n >= APP_KW_MAX) return;
        i = p->kw_n;
        p->kw_n++;
        memset(&p->kw[i], 0, sizeof(p->kw[0]));
    }
    if (i < 0 || i >= p->kw_n) return;
    strlcpy(p->kw[i].name, s_name, sizeof(p->kw[0].name));
    ui_pixel_utf8_copy(p->kw[i].text, sizeof(p->kw[0].text), s_expr);
    p->kw[i].prio = s_edit_prio;
    app_prefs_save();
    s_rule_i = i;
}

static void del_rule(int k)
{
    app_prefs_t *p = app_prefs();
    if (k < 0 || k >= p->kw_n) return;
    for (int i = k; i < p->kw_n - 1; i++) p->kw[i] = p->kw[i + 1];
    p->kw_n--;
    memset(&p->kw[p->kw_n], 0, sizeof(p->kw[0]));
    app_prefs_save();
}

static void open_edit(int i)
{
    app_prefs_t *p = app_prefs();
    s_rule_i = i;
    if (i >= 0 && i < p->kw_n) {
        strlcpy(s_name, p->kw[i].name, sizeof(s_name));
        strlcpy(s_expr, p->kw[i].text, sizeof(s_expr));
        s_edit_prio = p->kw[i].prio;
        if (!s_name[0]) strlcpy(s_name, app_str(APP_STR_RULE_ALL), sizeof(s_name));
    } else {
        s_rule_i = -1;
        strlcpy(s_name, app_str(APP_STR_RULE_ALL), sizeof(s_name));
        s_expr[0] = 0;
        s_edit_prio = APP_ALERT_POPUP;
    }
    s_view = VIEW_EDIT;
    s_sel = 0;
}

static void open_cond(void)
{
    s_field = APP_RULE_F_ANY;
    s_op = APP_RULE_OP_HAS;
    s_join = APP_RULE_J_AND;
    s_custom[0] = 0;
    s_view = VIEW_COND;
    s_sel = 0;
}

static void open_kb(int kind)
{
    s_kb_kind = kind;
    s_kb_sel = 0;
    s_kb_set = 0;
    s_view = VIEW_KB;
}

static void cond_commit(void)
{
    char term[APP_KW_LEN + 1];
    app_rule_term(term, sizeof(term), s_field, s_op, s_custom);
    if (!term[0]) return;
    bool first = !s_expr[0];
    app_rule_append(s_expr, sizeof(s_expr), s_join, term, first);
    s_view = VIEW_EDIT;
    s_sel = 1;
}

static void show_only(lv_obj_t *keep)
{
    if (s_recent) {
        if (keep == s_recent) lv_obj_remove_flag(s_recent, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_recent, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_form) {
        if (keep == s_form) lv_obj_remove_flag(s_form, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_form, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ensure_form_chrome(void)
{
    if (s_form || !s_page) return;
    s_form = app_ui_card(s_page);
    if (!s_form) return;
    lv_obj_set_style_bg_color(s_form, lv_color_hex(HUD_BG), 0);
    lv_obj_set_style_border_color(s_form, lv_color_hex(HUD_LINE), 0);
    lv_obj_set_style_radius(s_form, UI_RADIUS, 0);
    lv_obj_add_flag(s_form, LV_OBJ_FLAG_HIDDEN);
    s_ftitle = app_ui_title(s_form, app_str(APP_STR_SETTINGS));
    s_fhint = app_ui_hint(s_form);
    s_fbody = app_ui_body(s_form, 44);
    if (s_ftitle) lv_obj_set_style_text_color(s_ftitle, lv_color_hex(UI_TEXT), 0);
    if (s_fhint) lv_obj_set_style_text_color(s_fhint, lv_color_hex(HUD_MUTE), 0);
}

static void ensure_recent_chrome(void)
{
    if (s_recent || !s_page) return;

    s_recent = lv_obj_create(s_page);
    if (!s_recent) return;
    ui_pixel_strip_theme(s_recent);
    lv_obj_set_pos(s_recent, 0, 0);
    lv_obj_set_size(s_recent, APP_VIEW_W, APP_VIEW_H);
    lv_obj_set_style_bg_opa(s_recent, LV_OPA_TRANSP, 0);

    s_rtitle = lv_label_create(s_recent);
    if (s_rtitle) {
        lv_obj_set_style_text_font(s_rtitle, ui_pixel_font_cjk(), 0);
        lv_obj_set_style_text_color(s_rtitle, lv_color_hex(HUD_CYAN), 0);
        lv_obj_set_pos(s_rtitle, 4, 2);
    }

    s_rhint = lv_label_create(s_recent);
    if (s_rhint) {
        lv_obj_set_style_text_font(s_rhint, ui_pixel_font_cjk(), 0);
        lv_obj_set_style_text_color(s_rhint, lv_color_hex(HUD_MUTE), 0);
        lv_obj_set_width(s_rhint, CARD_W);
        lv_obj_set_style_text_align(s_rhint, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_rhint, LV_LABEL_LONG_CLIP);
        lv_obj_align(s_rhint, LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    s_list = lv_obj_create(s_recent);
    if (!s_list) return;
    ui_pixel_strip_theme(s_list);
    lv_obj_set_pos(s_list, 0, LIST_Y);
    lv_obj_set_size(s_list, APP_VIEW_W, LIST_H);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_border_color(s_list, lv_color_hex(HUD_LINE), 0);
    lv_obj_set_style_radius(s_list, 0, 0);
}

static bool item_high(const app_notif_rec_t *it)
{
    return it && it->alert >= APP_ALERT_URGENT;
}

static int row_h(int i, int rec)
{
    (void)i;
    (void)rec;
    return 76;
}

static lv_obj_t *make_box(lv_obj_t *p, int y, int h, uint32_t bg, uint32_t border)
{
    if (!p) return NULL;
    lv_obj_t *o = lv_obj_create(p);
    if (!o) return NULL;
    ui_pixel_strip_theme(o);
    lv_obj_set_pos(o, CARD_X, y);
    lv_obj_set_size(o, CARD_W, h);
    lv_obj_set_style_border_width(o, BOX_BORDER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_radius(o, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(o, BOX_PAD, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    return o;
}

static lv_obj_t *make_lab(lv_obj_t *p, const lv_font_t *font, uint32_t color,
                          int w, lv_label_long_mode_t mode)
{
    if (!p) return NULL;
    lv_obj_t *l = lv_label_create(p);
    if (!l) return NULL;
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, mode);
    return l;
}

/* 字段排布与弹出通知一致:大标题、应用+时间、副标题/正文。 */
static void fill_notif(const app_notif_rec_t *it, bool open, char *title, size_t tn,
                       char *meta, size_t mn, char *body, size_t bn)
{
    (void)open;
    title[0] = 0;
    meta[0] = 0;
    body[0] = 0;
    if (!it) {
        ui_pixel_utf8_copy(title, tn, app_str(APP_STR_LOG_UNKNOWN));
        return;
    }

    char app_name[40];
    char date[16];
    ui_pixel_utf8_copy(title, tn, it->title);
    ui_pixel_utf8_copy(app_name, sizeof(app_name), it->app_name[0] ? it->app_name : "");
    if (!app_name[0]) app_notif_rec_label(it, app_name, sizeof(app_name));
    const char *sub = it->subtitle;
    const char *msg = it->message;
    bool has_date = app_ancs_date_text(it->date, date, sizeof(date));
    bool title_from_app = !title[0] && app_name[0];
    if (title_from_app) ui_pixel_utf8_copy(title, tn, app_name);
    if (!title[0]) ui_pixel_utf8_copy(title, tn, app_str(APP_STR_ALERT));

    const char *meta_app = title_from_app ? "" : app_name;
    if (meta_app[0] && has_date) snprintf(meta, mn, "%s  %s", meta_app, date);
    else if (meta_app[0]) snprintf(meta, mn, "%s", meta_app);
    else if (has_date) snprintf(meta, mn, "%s", date);

    bool show_sub = app_notif_show_subtitle(title, sub);
    if (show_sub && msg[0]) snprintf(body, bn, "%s\n%s", sub, msg);
    else if (show_sub) snprintf(body, bn, "%s", sub);
    else if (msg[0]) snprintf(body, bn, "%s", msg);
}

static char s_ntitle[64];
static char s_nmeta[80];
static char s_nbody[230];

static vis_t *vis_get(int slot)
{
    if (slot < 0 || slot >= VIS_MAX || !s_list) return NULL;
    vis_t *v = &s_vis[slot];
    if (v->box) return v;
    v->box = make_box(s_list, 0, COLLAPSED_H, HUD_PANEL, HUD_LINE);
    if (!v->box) return NULL;
    lv_obj_add_flag(v->box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(v->box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v->box, LV_SCROLLBAR_MODE_AUTO);
    int iw = CARD_W - BOX_PAD * 2 - 12;
    v->rail = lv_obj_create(v->box);
    if (v->rail) {
        ui_pixel_strip_theme(v->rail);
        lv_obj_set_pos(v->rail, -BOX_PAD, -BOX_PAD);
        lv_obj_set_size(v->rail, 3, COLLAPSED_H);
        lv_obj_set_style_bg_color(v->rail, lv_color_hex(UI_CYAN), 0);
        lv_obj_add_flag(v->rail, LV_OBJ_FLAG_HIDDEN);
    }
    v->title = make_lab(v->box, ui_pixel_font_cjk(), UI_TEXT, iw, LV_LABEL_LONG_WRAP);
    v->meta = make_lab(v->box, ui_pixel_font_14(), HUD_MUTE, iw, LV_LABEL_LONG_CLIP);
    v->code = make_lab(v->box, ui_pixel_font_14(), UI_TEXT, LV_SIZE_CONTENT, LV_LABEL_LONG_CLIP);
    v->body = make_lab(v->box, ui_pixel_font_cjk(), UI_MUTE, iw, LV_LABEL_LONG_WRAP);
    v->rule = lv_obj_create(v->box);
    if (v->rule) {
        ui_pixel_strip_theme(v->rule);
        lv_obj_set_size(v->rule, 40, 1);
        lv_obj_set_style_bg_color(v->rule, lv_color_hex(UI_LINE), 0);
        lv_obj_add_flag(v->rule, LV_OBJ_FLAG_HIDDEN);
    }
    v->dot = lv_obj_create(v->box);
    if (v->dot) {
        ui_pixel_strip_theme(v->dot);
        lv_obj_set_size(v->dot, 7, 7);
        lv_obj_set_style_radius(v->dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(v->dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(v->dot, lv_color_hex(UI_CYAN), 0);
        lv_obj_set_style_border_width(v->dot, 0, 0);
        lv_obj_align(v->dot, LV_ALIGN_TOP_RIGHT, 0, 4);
        lv_obj_add_flag(v->dot, LV_OBJ_FLAG_HIDDEN);
    }
    if (v->title) {
        lv_obj_set_pos(v->title, 0, 0);
        lv_obj_set_height(v->title, LV_SIZE_CONTENT);
    }
    if (v->meta) {
        lv_obj_set_pos(v->meta, 0, TITLE_H + ROW_GAP);
        lv_obj_set_height(v->meta, META_H);
    }
    if (v->code) {
        lv_obj_set_style_bg_opa(v->code, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(v->code, lv_color_hex(UI_CYAN), 0);
        lv_obj_set_style_pad_hor(v->code, 8, 0);
        lv_obj_set_style_pad_ver(v->code, 3, 0);
        lv_obj_set_style_radius(v->code, UI_RADIUS_SM, 0);
        lv_obj_set_style_border_width(v->code, 0, 0);
        lv_obj_set_pos(v->code, 0, TITLE_H + ROW_GAP + META_H + ROW_GAP);
        lv_obj_add_flag(v->code, LV_OBJ_FLAG_HIDDEN);
    }
    if (v->body) {
        lv_obj_set_pos(v->body, 0, TITLE_H + ROW_GAP + META_H + ROW_GAP);
        lv_obj_add_flag(v->body, LV_OBJ_FLAG_HIDDEN);
    }
    return v;
}

static void vis_hide_from(int slot)
{
    for (int i = slot; i < VIS_MAX; i++) {
        if (s_vis[i].box) lv_obj_add_flag(s_vis[i].box, LV_OBJ_FLAG_HIDDEN);
    }
}

static void style_geom(int *x, int *w, int *pad, int *rad, int *bw)
{
    switch (ui_theme_id()) {
    case UI_ST_ANIME:
        *x = 8; *w = APP_CONTENT_W - 16; *pad = 8; *rad = 12; *bw = 1;
        break;
    case UI_ST_GEEK:
        *x = 8; *w = APP_CONTENT_W - 16; *pad = 8; *rad = 0; *bw = 1;
        break;
    case UI_ST_INK:
        *x = 8; *w = APP_CONTENT_W - 16; *pad = 8; *rad = 2; *bw = 0;
        break;
    case UI_ST_POP:
        *x = 8; *w = APP_CONTENT_W - 16; *pad = 8; *rad = 0; *bw = 2;
        break;
    default:
        *x = 8; *w = APP_CONTENT_W - 16; *pad = 8; *rad = 3; *bw = 1;
        break;
    }
}

static void fill_vis(int slot, int y, int h, const app_notif_rec_t *it, bool sel,
                     int idx)
{
    vis_t *v = vis_get(slot);
    if (!v || !v->box) return;

    int st = ui_theme_id();
    int cx, cw, pad, rad, bw;
    style_geom(&cx, &cw, &pad, &rad, &bw);

    bool high = item_high(it);
    bool unread = it && it->unread;
    uint32_t accent = high ? ui_style_urgent() : ui_style_accent();
    uint32_t bg = ui_style_card();
    uint32_t fg = ui_style_text();
    uint32_t mute = ui_style_mute();
    uint32_t bd = sel ? accent : ui_style_line();
    if (st == UI_ST_MINI) {
        bg = sel ? ui_style_fill() : ui_style_bg();
        bd = sel ? ui_style_accent() : ui_style_line();
    } else if (st == UI_ST_INK) {
        bg = sel ? ui_style_fill() : ui_style_card();
    } else if (st == UI_ST_POP) {
        static const uint32_t pop_bg[3] = { 0xFFE600, 0x00E8FF, 0x7B2CFF };
        int pi = idx % 3;
        if (pi < 0) pi = 0;
        bg = pop_bg[pi];
        fg = (pi == 2) ? 0xFFFFFF : 0x111111;
        mute = fg;
        bd = 0x111111;
    } else if (sel) {
        bg = ui_style_fill();
    }
    if (high && st != UI_ST_POP) fg = ui_style_urgent();
    int iw = cw - pad * 2;

    lv_obj_remove_flag(v->box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(v->box, cx, y);
    lv_obj_set_size(v->box, cw, h);
    lv_obj_set_style_pad_all(v->box, pad, 0);
    lv_obj_set_style_bg_color(v->box, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(v->box, rad, 0);
    lv_obj_set_style_border_width(v->box, (st == UI_ST_ANIME && sel) ? 2 : bw, 0);
    lv_obj_set_style_border_color(v->box, lv_color_hex(bd), 0);

    fill_notif(it, sel, s_ntitle, sizeof(s_ntitle),
               s_nmeta, sizeof(s_nmeta), s_nbody, sizeof(s_nbody));

    char head[80];
    if (st == UI_ST_GEEK) {
        snprintf(head, sizeof(head), "%s %s", (unread || sel) ? "*" : ">", s_ntitle);
    } else if (st == UI_ST_ANIME) {
        snprintf(head, sizeof(head), "「%s」", s_ntitle);
    } else {
        snprintf(head, sizeof(head), "%s", s_ntitle);
    }

    char otp[12];
    ble_filter_pick_code(it ? it->title : NULL, it ? it->subtitle : NULL,
                         it ? it->message : NULL, otp, sizeof(otp));
    bool has_otp = otp[0] != 0;

    if (v->rail) {
        if (st == UI_ST_INK && sel) {
            lv_obj_set_pos(v->rail, -pad, -pad);
            lv_obj_set_size(v->rail, 3, h);
            lv_obj_set_style_radius(v->rail, 0, 0);
            lv_obj_set_style_bg_color(v->rail, lv_color_hex(ui_style_accent()), 0);
            lv_obj_remove_flag(v->rail, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(v->rail, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (v->dot) lv_obj_add_flag(v->dot, LV_OBJ_FLAG_HIDDEN);

    int cy = 0;
    if (v->rule) lv_obj_add_flag(v->rule, LV_OBJ_FLAG_HIDDEN);

    if (v->title) {
        lv_label_set_long_mode(v->title, s_reading ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
        lv_obj_set_width(v->title, iw);
        lv_label_set_text(v->title, head);
        lv_obj_set_style_text_color(v->title, lv_color_hex(fg), 0);
        lv_obj_set_style_text_font(v->title, ui_pixel_font_cjk(), 0);
        lv_obj_set_pos(v->title, 0, cy);
        if (s_reading) {
            lv_obj_set_height(v->title, LV_SIZE_CONTENT);
            lv_obj_update_layout(v->title);
            cy += (int)lv_obj_get_height(v->title) + ROW_GAP;
        } else {
            lv_obj_set_height(v->title, TITLE_H);
            cy += TITLE_H + ROW_GAP;
        }
    }

    if (v->meta) {
        lv_obj_set_width(v->meta, iw);
        lv_label_set_text(v->meta, s_nmeta[0] ? s_nmeta : " ");
        lv_obj_set_style_text_color(v->meta, lv_color_hex(mute), 0);
        lv_obj_set_pos(v->meta, 0, cy);
        lv_obj_set_height(v->meta, META_H);
        lv_obj_remove_flag(v->meta, LV_OBJ_FLAG_HIDDEN);
        cy += META_H + ROW_GAP;
    }

    if (v->code) {
        if (has_otp) {
            uint32_t chip = high ? ui_style_urgent() : ui_style_accent();
            lv_label_set_text(v->code, otp);
            lv_obj_set_style_text_color(v->code, lv_color_hex(ui_style_on()), 0);
            lv_obj_set_style_bg_opa(v->code, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(v->code, lv_color_hex(chip), 0);
            lv_obj_set_style_radius(v->code, st == UI_ST_ANIME ? 12 : 0, 0);
            lv_obj_set_pos(v->code, 0, cy);
            lv_obj_remove_flag(v->code, LV_OBJ_FLAG_HIDDEN);
            cy += CODE_H + ROW_GAP;
        } else {
            lv_obj_add_flag(v->code, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (v->body) {
        bool show_body = s_nbody[0] && (!has_otp || sel);
        if (show_body) {
            lv_label_set_long_mode(v->body, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(v->body, iw);
            lv_label_set_text(v->body, s_nbody);
            lv_obj_set_style_text_color(v->body, lv_color_hex(sel ? fg : mute), 0);
            lv_obj_set_pos(v->body, 0, cy);
            if (s_reading) {
                lv_obj_set_height(v->body, LV_SIZE_CONTENT);
            } else {
                int body_h = h - pad * 2 - bw * 2 - cy;
                if (body_h < META_H) body_h = META_H;
                lv_obj_set_height(v->body, body_h);
            }
            lv_obj_remove_flag(v->body, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(v->body, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static int style_act_h(void)
{
    int st = ui_theme_id();
    if (st == UI_ST_INK || st == UI_ST_MINI) return 28;
    if (st == UI_ST_ANIME) return 36;
    return SETTINGS_H;
}

static int style_act_gap(void)
{
    return ui_theme_id() == UI_ST_MINI ? 2 : GAP;
}

static void fill_act(int k, int y, bool sel, const char *text, bool dim)
{
    if (k < 0 || k >= ACT_N) return;
    int cx, cw, pad, rad, bw;
    style_geom(&cx, &cw, &pad, &rad, &bw);
    int ah = style_act_h();
    int st = ui_theme_id();
    if (!s_act_box[k]) {
        s_act_box[k] = make_box(s_list, y, ah, HUD_PANEL, HUD_LINE);
        if (!s_act_box[k]) return;
        s_act_lab[k] = make_lab(s_act_box[k], ui_pixel_font_cjk(), UI_TEXT, cw - 24,
                                LV_LABEL_LONG_CLIP);
        if (s_act_lab[k]) lv_obj_align(s_act_lab[k], LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_remove_flag(s_act_box[k], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_act_box[k], cx, y);
    lv_obj_set_size(s_act_box[k], cw, ah);
    uint32_t accent = (k == 2) ? ui_style_urgent() : ui_style_accent();
    char lab[48];
    if (st == UI_ST_GEEK) snprintf(lab, sizeof(lab), "[ %s ]", text);
    else if (st == UI_ST_INK) snprintf(lab, sizeof(lab), sel ? "【 %s 】" : "%s", text);
    else if (st == UI_ST_ANIME) snprintf(lab, sizeof(lab), "%s %s", sel ? "*" : " ", text);
    else snprintf(lab, sizeof(lab), "%s", text);

    if (st == UI_ST_INK) {
        lv_obj_set_style_bg_color(s_act_box[k],
                                  lv_color_hex(sel ? ui_style_accent() : ui_style_bg()), 0);
        lv_obj_set_style_radius(s_act_box[k], 0, 0);
        lv_obj_set_style_border_width(s_act_box[k], sel ? 0 : 1, 0);
        lv_obj_set_style_border_color(s_act_box[k], lv_color_hex(ui_style_line()), 0);
    } else if (st == UI_ST_MINI) {
        lv_obj_set_style_bg_color(s_act_box[k],
                                  lv_color_hex(sel ? ui_style_text() : ui_style_bg()), 0);
        lv_obj_set_style_radius(s_act_box[k], 0, 0);
        lv_obj_set_style_border_width(s_act_box[k], 1, 0);
        lv_obj_set_style_border_color(s_act_box[k], lv_color_hex(ui_style_text()), 0);
    } else if (st == UI_ST_GEEK) {
        lv_obj_set_style_bg_color(s_act_box[k], lv_color_hex(sel ? accent : ui_style_card()), 0);
        lv_obj_set_style_radius(s_act_box[k], 0, 0);
        lv_obj_set_style_border_width(s_act_box[k], 1, 0);
        lv_obj_set_style_border_color(s_act_box[k], lv_color_hex(accent), 0);
    } else if (st == UI_ST_POP) {
        lv_obj_set_style_bg_color(s_act_box[k],
                                  lv_color_hex(sel ? 0xFFE600 : 0x00E8FF), 0);
        lv_obj_set_style_radius(s_act_box[k], 0, 0);
        lv_obj_set_style_border_width(s_act_box[k], 2, 0);
        lv_obj_set_style_border_color(s_act_box[k], lv_color_hex(0x111111), 0);
    } else {
        lv_obj_set_style_bg_color(s_act_box[k], lv_color_hex(sel ? accent : ui_style_card()), 0);
        lv_obj_set_style_radius(s_act_box[k], 12, 0);
        lv_obj_set_style_border_width(s_act_box[k], 2, 0);
        lv_obj_set_style_border_color(s_act_box[k], lv_color_hex(accent), 0);
    }
    if (s_act_lab[k]) {
        lv_label_set_text(s_act_lab[k], lab);
        lv_obj_set_width(s_act_lab[k], cw - 24);
        if (st == UI_ST_GEEK || st == UI_ST_ANIME) lv_obj_align(s_act_lab[k], LV_ALIGN_CENTER, 0, 0);
        else lv_obj_align(s_act_lab[k], LV_ALIGN_LEFT_MID, 0, 0);
        uint32_t tc = ui_style_text();
        if (dim) tc = ui_style_mute();
        else if (st == UI_ST_GEEK && sel) tc = ui_style_on();
        else if (st == UI_ST_ANIME && sel) tc = ui_style_on();
        else if (st == UI_ST_MINI && sel) tc = ui_style_on();
        else if (st == UI_ST_INK && sel) tc = ui_style_on();
        else if (st == UI_ST_POP) tc = 0x111111;
        else if (sel) tc = accent;
        lv_obj_set_style_text_color(s_act_lab[k], lv_color_hex(tc), 0);
    }
}

static void acts_hide(void)
{
    for (int i = 0; i < ACT_N; i++) {
        if (s_act_box[i]) lv_obj_add_flag(s_act_box[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void paint_recent(void)
{
    ensure_recent_chrome();
    show_only(s_recent);
    s_title = s_rtitle;
    s_hint = s_rhint;
    s_body = NULL;
    int rec = recent_n();
    if (s_sel >= rec) s_sel = rec ? rec - 1 : 0;
    if (s_sel < 0) s_sel = 0;
    s_focus = s_sel;

    if (s_recent) lv_obj_set_style_bg_opa(s_recent, LV_OPA_TRANSP, 0);
    if (s_list) lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    int st = ui_theme_id();
    if (s_rtitle) {
        uint32_t tc = (st == UI_ST_POP) ? ui_style_bg() : ui_style_accent();
        lv_obj_set_style_text_color(s_rtitle, lv_color_hex(tc), 0);
    }
    if (s_rhint) {
        uint32_t hc = (st == UI_ST_POP) ? ui_style_text() : ui_style_mute();
        lv_obj_set_style_text_color(s_rhint, lv_color_hex(hc), 0);
    }
    if (s_title) {
        if (rec <= 0) {
            lv_label_set_text(s_title, ui_theme_name(st));
        } else if (st == UI_ST_GEEK) {
            lv_label_set_text_fmt(s_title, "[%d/%d]", s_sel + 1, rec);
        } else if (st == UI_ST_INK) {
            lv_label_set_text_fmt(s_title, "· %d / %d ·", s_sel + 1, rec);
        } else if (st == UI_ST_ANIME || st == UI_ST_POP) {
            lv_label_set_text_fmt(s_title, "* %d/%d", s_sel + 1, rec);
        } else {
            lv_label_set_text_fmt(s_title, "%d / %d", s_sel + 1, rec);
        }
    }
    if (s_hint) {
        lv_label_set_text(s_hint, rec == 0 ? app_str(APP_STR_HINT_SETUP)
                          : s_reading ? app_str(s_acts ? APP_STR_HINT_DETAIL
                                                       : APP_STR_HINT_CARD_ACT)
                                      : app_str(APP_STR_HINT_OPEN_CARD));
    }

    if (rec == 0) {
        acts_hide();
        vis_hide_from(0);
        return;
    }

    if (s_reading) {
        int sy = s_vis[0].box ? (int)lv_obj_get_scroll_y(s_vis[0].box) : 0;
        int card_h = LIST_H;
        if (s_acts) {
            int ah = style_act_h();
            int ag = style_act_gap();
            int act_h = ACT_N * ah + (ACT_N - 1) * ag;
            card_h = LIST_H - act_h - (ag ? ag : 0);
            if (card_h < 72) card_h = 72;
        }
        fill_vis(0, 0, card_h, app_notif_store_at(app_notif_hist(), s_sel), true, s_sel);
        if (s_vis[0].box) {
            lv_obj_add_flag(s_vis[0].box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scroll_dir(s_vis[0].box, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(s_vis[0].box, LV_SCROLLBAR_MODE_AUTO);
            lv_obj_scroll_to_y(s_vis[0].box, sy, LV_ANIM_OFF);
        }
        if (s_acts) {
            int ah = style_act_h();
            int ag = style_act_gap();
            int y = card_h + (ag ? ag : 0);
            fill_act(0, y, s_act_sel == 0, app_str(APP_STR_ACT_BACK_LIST), false);
            fill_act(1, y + ah + ag, s_act_sel == 1, app_str(APP_STR_KEEP_UNREAD), false);
            fill_act(2, y + (ah + ag) * 2, s_act_sel == 2, app_str(APP_STR_TOTP_DELETE), false);
        } else {
            acts_hide();
        }
        vis_hide_from(1);
        return;
    }

    acts_hide();
    int rh = row_h(s_sel, rec);
    int start = s_sel;
    int end = s_sel;
    int used = rh;
    while (end + 1 < rec) {
        int next = used + GAP + rh;
        if (next > LIST_H) break;
        end++;
        used = next;
    }
    while (start > 0) {
        int next = used + GAP + rh;
        if (next > LIST_H) break;
        start--;
        used = next;
    }
    int slot = 0;
    int y = 0;
    for (int i = start; i <= end && slot < VIS_MAX; i++) {
        fill_vis(slot, y, rh, app_notif_store_at(app_notif_hist(), i), i == s_sel, i);
        if (s_vis[slot].box) {
            lv_obj_remove_flag(s_vis[slot].box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(s_vis[slot].box, LV_SCROLLBAR_MODE_OFF);
            lv_obj_scroll_to_y(s_vis[slot].box, 0, LV_ANIM_OFF);
        }
        y += rh + GAP;
        slot++;
    }
    vis_hide_from(slot);
}

static void menu_rows_hide(void)
{
    for (int i = 0; i < 6; i++) {
        if (s_menu_rows[i]) lv_obj_add_flag(s_menu_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void menu_row(int slot, const char *label, const char *meta, bool selected)
{
    if (slot < 0 || slot >= 6 || !s_form) return;
    if (!s_menu_rows[slot]) {
        s_menu_rows[slot] = lv_label_create(s_form);
        lv_obj_set_style_text_font(s_menu_rows[slot], ui_pixel_font_14(), 0);
        lv_obj_set_pos(s_menu_rows[slot], 7, 46 + slot * 29);
        lv_obj_set_width(s_menu_rows[slot], 194);
        lv_label_set_long_mode(s_menu_rows[slot], LV_LABEL_LONG_CLIP);
    }
    lv_obj_remove_flag(s_menu_rows[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_menu_rows[slot],
                                lv_color_hex(selected ? HUD_CYAN : UI_TEXT), 0);
    char line[160];
    snprintf(line, sizeof(line), "%s%s  %s", selected ? ">" : " ", label, meta);
    lv_label_set_text(s_menu_rows[slot], line);
}

static void paint_menu(int n, int window)
{
    if (s_sel >= n) s_sel = n ? n - 1 : 0;
    if (s_sel < 0) s_sel = 0;
    int start = s_sel - window / 2;
    if (start < 0) start = 0;
    if (start + window > n) start = n > window ? n - window : 0;
    menu_rows_hide();
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    int slot = 0;
    for (int i = start; i < n && i < start + window; i++, slot++) {
        char label[64] = "";
        char meta[96] = "";
        if (s_view == VIEW_SET) {
            app_prefs_t *p = app_prefs();
            if (i < p->kw_n) {
                char pre[48];
                pretty(p->kw[i].text, pre, sizeof(pre));
                snprintf(label, sizeof(label), "%s",
                         p->kw[i].name[0] ? p->kw[i].name : app_str(APP_STR_RULE_ALL));
                snprintf(meta, sizeof(meta), "%s · %s",
                         app_str(tier_label(p->kw[i].prio)),
                         pre[0] ? pre : p->kw[i].text);
            } else if (i == p->kw_n) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_NEW));
            } else if (i == p->kw_n + 1) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_DEFAULT));
                snprintf(meta, sizeof(meta), "%s", app_str(tier_label(p->notif_def)));
            } else {
                uint8_t h = p->auto_hide;
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_AUTOHIDE));
                if (h == 0) snprintf(meta, sizeof(meta), "%s", app_str(APP_STR_AUTOHIDE_OFF));
                else snprintf(meta, sizeof(meta), "%ds", (int)h);
            }
        } else if (s_view == VIEW_EDIT) {
            if (i == 0) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_NAME));
                snprintf(meta, sizeof(meta), "%s",
                         s_name[0] ? s_name : app_str(APP_STR_EMPTY));
            } else if (i == 1) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_WHEN));
                pretty(s_expr, meta, sizeof(meta));
                if (!meta[0]) snprintf(meta, sizeof(meta), "%s", app_str(APP_STR_EMPTY));
            } else if (i == 2) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_TIER));
                snprintf(meta, sizeof(meta), "%s", app_str(tier_label(s_edit_prio)));
            } else if (i == 3) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_SAVE));
                snprintf(meta, sizeof(meta), "GO");
            } else {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_TOTP_DELETE));
            }
        } else {
            if (i == 0) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_FIELD));
                snprintf(meta, sizeof(meta), "%s", app_str(F_LBL[s_field]));
            } else if (i == 1) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_OP));
                snprintf(meta, sizeof(meta), "%s", app_str(OP_LBL[s_op]));
            } else if (i == 2) {
                const char *v = app_str(APP_STR_EMPTY);
                if (s_op == APP_RULE_OP_EMPTY) v = app_str(APP_STR_RULE_OP_EMPTY);
                else if (s_custom[0]) v = s_custom;
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_VAL));
                snprintf(meta, sizeof(meta), "%s", v);
            } else if (s_expr[0] && i == 3) {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_JOIN));
                snprintf(meta, sizeof(meta), "%s", app_str(J_LBL[s_join]));
            } else {
                snprintf(label, sizeof(label), "%s", app_str(APP_STR_RULE_ADD));
                snprintf(meta, sizeof(meta), "GO");
            }
        }
        menu_row(slot, label, meta, i == s_sel);
    }
}

static void paint(void)
{
    if (!s_page) return;

    if (s_view == VIEW_RECENT) {
        paint_recent();
        app_kb_hide();
        return;
    }

    ensure_form_chrome();
    show_only(s_form);
    s_title = s_ftitle;
    s_hint = s_fhint;
    s_body = s_fbody;
    if (!s_body) return;

    if (s_view == VIEW_KB) {
        menu_rows_hide();
        if (s_body) lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        if (s_hint) lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        if (s_title) {
            lv_label_set_text(s_title, app_str(s_kb_kind ? APP_STR_RULE_VAL
                                                        : APP_STR_RULE_NAME));
        }
        char *buf = s_kb_kind ? s_custom : s_name;
        app_kb_show(s_form, buf, s_kb_sel, s_kb_set, 4);
        return;
    }

    app_kb_hide();
    if (s_hint) lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    if (s_title) lv_label_set_text(s_title, app_str(APP_STR_RULE));
    if (s_view == VIEW_SET) {
        if (s_hint) lv_label_set_text(s_hint, app_str(APP_STR_RULE_HINT));
        paint_menu(set_n(), 6);
        return;
    }
    if (s_view == VIEW_EDIT) {
        if (s_hint) lv_label_set_text(s_hint, app_str(APP_STR_RULE_EDIT_HINT));
        paint_menu(edit_n(), 6);
        return;
    }
    if (s_view == VIEW_COND) {
        if (s_hint) lv_label_set_text(s_hint, app_str(APP_STR_RULE_COND_HINT));
        paint_menu(cond_n(), 6);
        return;
    }
    if (s_view == VIEW_CONFIRM) {
        menu_rows_hide();
        if (s_title) lv_label_set_text(s_title, app_str(APP_STR_TOTP_DELETE));
        if (s_hint) lv_label_set_text(s_hint, app_str(APP_STR_OK_CHOOSE));
        if (s_body) {
            lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
            snprintf(s_paint, sizeof(s_paint), "%s %s\n%s %s\n",
                     s_csel == 0 ? ">" : " ", app_str(APP_STR_TOTP_CANCEL),
                     s_csel == 1 ? ">" : " ", app_str(APP_STR_TOTP_DELETE));
            lv_label_set_text(s_body, s_paint);
        }
        return;
    }
}

static void move_kb(int delta)
{
    app_ui_move(&s_kb_sel, KB_N, delta);
}

static void hold_tick(lv_timer_t *t)
{
    (void)t;
    if (s_view != VIEW_KB || s_hold_btn < 0) return;
    s_hold_ms += 120;
    if (s_hold_ms < 280) return;
    int dir = (s_hold_btn == BSP_BTN_UP) ? -1 : 1;
    int step = (s_hold_ms >= 800) ? KB_COLS : 1;
    move_kb(dir * step);
    paint();
}

// 长按 OK 逐层退子视图,退到最外层再交给页栈。
static bool ancs_back(void)
{
    switch (s_view) {
    case VIEW_KB:
        s_view = s_kb_kind ? VIEW_COND : VIEW_EDIT;
        s_sel = s_kb_kind ? 2 : 0;
        break;
    case VIEW_COND:
        s_view = VIEW_EDIT;
        s_sel = 1;
        break;
    case VIEW_EDIT:
        s_view = VIEW_SET;
        s_sel = s_rule_i >= 0 ? s_rule_i : 0;
        break;
    case VIEW_SET:
        s_view = VIEW_RECENT;
        s_sel = recent_n() + ACT_N - 1;
        break;
    case VIEW_CONFIRM:
        s_view = s_del_rule ? VIEW_EDIT : VIEW_RECENT;
        s_sel = s_del_rule ? edit_n() - 1 : s_del_i;
        break;
    default:
        if (s_acts) {
            s_acts = false;
            break;
        }
        if (s_reading) {
            s_reading = false;
            break;
        }
        return false;
    }
    paint();
    return true;
}

void app_ancs_enter(lv_obj_t *p)
{
    s_page = p;
    app_shell_set_back(ancs_back);
    ui_pixel_glass(p);
    s_view = VIEW_RECENT;
    s_sel = 0;
    s_kb_sel = 0;
    s_kb_set = 0;
    s_kb_kind = 0;
    s_rule_i = -1;
    s_focus = 0;
    s_csel = 0;
    s_del_i = 0;
    s_del_rule = false;
    s_reading = false;
    s_acts = false;
    s_act_sel = 0;
    s_custom[0] = 0;
    s_name[0] = 0;
    s_expr[0] = 0;
    s_hold_btn = -1;
    s_title = s_hint = s_body = s_list = NULL;
    s_recent = s_form = s_rtitle = s_rhint = NULL;
    s_ftitle = s_fhint = s_fbody = NULL;
    memset(s_act_box, 0, sizeof(s_act_box));
    memset(s_act_lab, 0, sizeof(s_act_lab));
    memset(s_menu_rows, 0, sizeof(s_menu_rows));
    memset(s_vis, 0, sizeof(s_vis));
    s_hold_timer = lv_timer_create(hold_tick, 120, NULL);
    if (recent_n() > 0) app_notif_mark_read(s_sel);
    paint();
}

void app_ancs_exit(void)
{
    app_prefs_flush();
    s_hold_btn = -1;
    if (s_hold_timer) { lv_timer_delete(s_hold_timer); s_hold_timer = NULL; }
    app_kb_hide();
    s_page = s_title = s_hint = s_body = s_list = NULL;
    s_recent = s_form = s_rtitle = s_rhint = NULL;
    s_ftitle = s_fhint = s_fbody = NULL;
    memset(s_act_box, 0, sizeof(s_act_box));
    memset(s_act_lab, 0, sizeof(s_act_lab));
    memset(s_menu_rows, 0, sizeof(s_menu_rows));
    memset(s_vis, 0, sizeof(s_vis));
    s_reading = false;
    s_acts = false;
}

void app_ancs_resume(void)
{
    if (!s_page) return;
    s_view = VIEW_RECENT;
    s_reading = false;
    s_acts = false;
    s_act_sel = 0;
    s_sel = 0;
    int rec = recent_n();
    if (rec > 0) app_notif_mark_read(s_sel);
    paint();
}

void app_ancs_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_eat_click && (ev == BSP_BTN_CLICK || ev == BSP_BTN_RELEASE)) {
        s_eat_click = false;
        return;
    }
    if (s_view == VIEW_KB && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        if (ev == BSP_BTN_PRESS) {
            s_hold_btn = (int)btn;
            s_hold_ms = 0;
            move_kb(btn == BSP_BTN_UP ? -1 : 1);
            paint();
        } else if (ev == BSP_BTN_RELEASE && s_hold_btn == (int)btn) {
            s_hold_btn = -1;
            s_hold_ms = 0;
        }
        return;
    }
    if (s_view == VIEW_RECENT && !s_reading &&
        btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {
        app_web_qr_open();
        s_eat_click = true;
        return;
    }
    if (s_view == VIEW_RECENT && !s_reading &&
        btn == BSP_BTN_DOWN && ev == BSP_BTN_LONG) {
        int id = (ui_theme_id() + 1) % ui_theme_count();
        ui_theme_set(id);
        app_prefs()->theme = (uint8_t)id;
        app_prefs_save();
        app_shell_retheme();
        paint();
        s_eat_click = true;
        return;
    }
    if (ev != BSP_BTN_CLICK) return;

    if (s_view == VIEW_KB) {
        if (btn != BSP_BTN_OK) return;
        char *buf = s_kb_kind ? s_custom : s_name;
        size_t cap = s_kb_kind ? sizeof(s_custom) : sizeof(s_name);
        int r = app_kb_click(buf, cap, &s_kb_sel, &s_kb_set);
        if (r == 2 || r == 3) {
            s_view = s_kb_kind ? VIEW_COND : VIEW_EDIT;
            s_sel = s_kb_kind ? 2 : 0;
        }
        paint();
        return;
    }

    if (s_view == VIEW_CONFIRM) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            s_csel ^= 1;
            paint();
            return;
        }
        if (btn != BSP_BTN_OK) return;
        if (s_csel == 1) {
            if (s_del_rule) {
                del_rule(s_del_i);
                s_view = VIEW_SET;
                s_sel = 0;
            } else if (app_notif_hist_remove(s_del_i)) {
                s_view = VIEW_RECENT;
                int rec = recent_n();
                s_sel = rec > 0 ? rec - 1 : 0;
            } else {
                s_view = VIEW_RECENT;
            }
        } else {
            s_view = s_del_rule ? VIEW_EDIT : VIEW_RECENT;
            s_sel = s_del_rule ? edit_n() - 1 : s_del_i;
        }
        paint();
        return;
    }

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (s_view == VIEW_RECENT) {
            int rec = recent_n();
            int dir = (btn == BSP_BTN_DOWN) ? 1 : -1;
            if (s_reading) {
                lv_obj_t *box = s_vis[0].box;
                bool more = box && (dir > 0 ? lv_obj_get_scroll_bottom(box) > 0
                                            : lv_obj_get_scroll_top(box) > 0);
                if (more) {
                    lv_obj_scroll_by(box, 0, -dir * 28, LV_ANIM_OFF);
                    return;
                }
                if (!s_acts) return;
                s_act_sel += dir;
                if (s_act_sel < 0) s_act_sel = ACT_N - 1;
                if (s_act_sel >= ACT_N) s_act_sel = 0;
                paint();
                return;
            }
            if (rec > 0) {
                s_sel += dir;
                if (s_sel < 0) s_sel = rec - 1;
                if (s_sel >= rec) s_sel = 0;
                app_notif_mark_read(s_sel);
                paint();
            }
            return;
        }
        app_ui_move(&s_sel, list_n(), btn == BSP_BTN_UP ? -1 : 1);
        paint();
        return;
    }
    if (btn != BSP_BTN_OK) return;

    if (s_view == VIEW_RECENT) {
        if (recent_n() == 0) {
            app_web_qr_open();
            return;
        }
        if (s_reading) {
            if (!s_acts) {
                s_acts = true;
                s_act_sel = 0;
                paint();
                return;
            }
            if (s_act_sel == 1) {
                app_notif_mark_unread(s_sel);
                s_reading = false;
                s_acts = false;
            } else if (s_act_sel == 2) {
                app_notif_hist_remove(s_sel);
                s_reading = false;
                s_acts = false;
                if (s_sel >= recent_n()) s_sel = recent_n() ? recent_n() - 1 : 0;
            } else {
                s_reading = false;
                s_acts = false;
            }
            paint();
            return;
        }
        s_reading = true;
        s_acts = false;
        s_act_sel = 0;
        app_notif_mark_read(s_sel);
        paint();
        return;
    }

    if (s_view == VIEW_SET) {
        app_prefs_t *p = app_prefs();
        if (s_sel < p->kw_n) {
            open_edit(s_sel);
        } else if (s_sel == p->kw_n) {
            if (p->kw_n < APP_KW_MAX) open_edit(-1);
        } else if (s_sel == p->kw_n + 1) {
            p->notif_def = (uint8_t)((p->notif_def + 1) % (APP_ALERT_DROP + 1));
            app_prefs_save();
        } else {
            int i = (hide_idx(p->auto_hide) + 1) % 4;
            p->auto_hide = HIDE_OPTS[i];
            app_prefs_save();
        }
        paint();
        return;
    }

    if (s_view == VIEW_EDIT) {
        if (s_sel == 0) {
            open_kb(0);
        } else if (s_sel == 1) {
            s_expr[0] = 0;
            open_cond();
        } else if (s_sel == 2) {
            s_edit_prio = (uint8_t)((s_edit_prio + 1) % (APP_ALERT_DROP + 1));
        } else if (s_sel == 3) {
            if (s_expr[0]) {
                save_rule();
                s_view = VIEW_SET;
                s_sel = s_rule_i >= 0 ? s_rule_i : 0;
            }
        } else if (s_sel == 4 && s_rule_i >= 0) {
            s_del_i = s_rule_i;
            s_del_rule = true;
            s_csel = 0;
            s_view = VIEW_CONFIRM;
        }
        paint();
        return;
    }

    if (s_view == VIEW_COND) {
        int go = s_expr[0] ? 4 : 3;
        if (s_sel == 0) {
            s_field = (s_field + 1) % APP_RULE_F_N;
        } else if (s_sel == 1) {
            s_op = (s_op + 1) % APP_RULE_OP_N;
        } else if (s_sel == 2) {
            if (s_op != APP_RULE_OP_EMPTY) open_kb(1);
        } else if (s_expr[0] && s_sel == 3) {
            s_join = (s_join + 1) % APP_RULE_J_N;
        } else if (s_sel == go) {
            cond_commit();
        }
        paint();
        return;
    }
}
