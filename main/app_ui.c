#include "app_ui.h"

#include "app.h"
#include "app_i18n.h"
#include "ui_pixel.h"

#include <stdio.h>
#include <string.h>

static const char *const KB_LOWER[KB_N] = {
    "1", "2", "3", "4", "5", "6",
    "7", "8", "9", "0", ".", "@",
    "a", "b", "c", "d", "e", "f",
    "g", "h", "i", "j", "k", "l",
    "m", "n", "o", "p", "q", "r",
    "s", "t", "u", "v", "w", "x",
    "y", "z", "_", "-", "#", "/",
    "SPC", "DEL", "Aa", "BK", "GO", "-",
};
static const char *const KB_UPPER[KB_N] = {
    "1", "2", "3", "4", "5", "6",
    "7", "8", "9", "0", ".", "@",
    "A", "B", "C", "D", "E", "F",
    "G", "H", "I", "J", "K", "L",
    "M", "N", "O", "P", "Q", "R",
    "S", "T", "U", "V", "W", "X",
    "Y", "Z", "_", "-", "#", "/",
    "SPC", "DEL", "Aa", "BK", "GO", "-",
};
static const char *const KB_SYM[KB_N] = {
    "!", "?", ":", ";", "+", "=",
    "*", "&", "%", "$", ",", "~",
    "(", ")", "[", "]", "{", "}",
    "<", ">", "'", "\"", "\\", "|",
    "^", "`", "_", "#", "/", "-",
    "@", ".", "0", "1", "2", "3",
    "4", "5", "6", "7", "8", "9",
    "SPC", "DEL", "Aa", "BK", "GO", "-",
};

void app_ui_screen_style(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(ui_style_bg()), 0);
}

lv_obj_t *app_ui_card(lv_obj_t *parent)
{
    int h = (int)lv_obj_get_height(parent);
    if (h < 80) h = APP_BODY_H;
    int w = (int)lv_obj_get_width(parent);
    if (w < 80) w = APP_CONTENT_W;
    lv_obj_t *c = ui_pixel_panel_create(parent, 0, 0, w, h, UI_PANEL);
    return c;
}

lv_obj_t *app_ui_page_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *t = lv_label_create(parent);
    lv_obj_set_style_text_font(t, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_TEXT), 0);
    lv_label_set_text(t, text ? text : "");
    lv_obj_set_pos(t, 4, 2);
    return t;
}

lv_obj_t *app_ui_footer(lv_obj_t *parent, const char *text)
{
    lv_obj_t *h = lv_label_create(parent);
    lv_obj_set_style_text_font(h, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(h, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_width(h, APP_CONTENT_W);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(h, LV_LABEL_LONG_CLIP);
    lv_label_set_text(h, text ? text : "");
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -2);
    return h;
}

lv_obj_t *app_ui_badge(lv_obj_t *parent, int x, int y, const char *ch,
                       uint32_t color)
{
    lv_obj_t *b = lv_obj_create(parent);
    ui_pixel_strip_theme(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 22, 22);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_t *lab = lv_label_create(b);
    lv_obj_set_style_text_font(lab, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lab, ch ? ch : "");
    lv_obj_center(lab);
    return b;
}

lv_obj_t *app_ui_row(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    ui_pixel_strip_theme(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, UI_RADIUS, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_CARD), 0);
    return o;
}

void app_ui_select(lv_obj_t *obj, bool selected, uint32_t accent)
{
    ui_pixel_select(obj, selected, accent);
}

lv_obj_t *app_ui_title(lv_obj_t *card, const char *text)
{
    lv_obj_t *t = lv_label_create(card);
    lv_obj_set_style_text_font(t, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_TEXT), 0);
    lv_label_set_text(t, text);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    return t;
}

lv_obj_t *app_ui_hint(lv_obj_t *card)
{
    lv_obj_t *h = lv_label_create(card);
    lv_obj_set_style_text_font(h, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(h, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_width(h, APP_TEXT_W);
    lv_label_set_long_mode(h, LV_LABEL_LONG_CLIP);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, 24);
    return h;
}

lv_obj_t *app_ui_body(lv_obj_t *card, int y)
{
    lv_obj_t *b = lv_label_create(card);
    lv_obj_set_style_text_font(b, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(UI_TEXT), 0);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, APP_TEXT_W);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, 0, y);
    return b;
}

void app_ui_move(int *sel, int n, int delta)
{
    if (!sel || n < 1) return;
    int v = *sel + delta;
    v %= n;
    if (v < 0) v += n;
    *sel = v;
}

static lv_obj_t *s_lparent;
static lv_obj_t *s_lrow[APP_UI_LIST_MAX];
static lv_obj_t *s_llab[APP_UI_LIST_MAX];
static lv_obj_t *s_lval[APP_UI_LIST_MAX];
static lv_obj_t *s_lbdg[APP_UI_LIST_MAX];
static lv_obj_t *s_ltrack, *s_lthumb;
static int s_lrows;
static int s_ltrack_y, s_ltrack_h;

#define LIST_GAP  6
#define LIST_RAIL 4
#define LIST_PAD  10
#define LIST_VAL_W 72

static lv_obj_t *row_label(lv_obj_t *row, uint32_t color, int w)
{
    lv_obj_t *o = lv_label_create(row);
    lv_obj_set_style_text_font(o, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(o, w);
    return o;
}

static void lab_text(lv_obj_t *o, const char *text)
{
    if (!o) return;
    if (!text) text = "";
    const char *old = lv_label_get_text(o);
    if (!old || strcmp(old, text) != 0) lv_label_set_text(o, text);
}

static void lab_color(lv_obj_t *o, uint32_t color)
{
    if (!o) return;
    lv_color_t want = lv_color_hex(color);
    if (lv_color_eq(lv_obj_get_style_text_color(o, 0), want)) return;
    lv_obj_set_style_text_color(o, want, 0);
}

static void obj_hidden(lv_obj_t *o, bool hide)
{
    if (!o) return;
    bool h = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (hide && !h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!hide && h) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *rail_bar(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    ui_pixel_strip_theme(o);
    lv_obj_set_pos(o, APP_CONTENT_W - LIST_RAIL, s_ltrack_y);
    lv_obj_set_size(o, 3, s_ltrack_h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

static void row_paint(lv_obj_t *row, bool selected, uint32_t accent)
{
    (void)accent;
    lv_color_t want = lv_color_hex(selected ? UI_FILL : UI_CARD);
    if (lv_color_eq(lv_obj_get_style_bg_color(row, 0), want)) return;
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, want, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, UI_RADIUS_SM, 0);
    lv_obj_set_style_clip_corner(row, false, 0);
}

void app_ui_list_bind(lv_obj_t *parent, int y, int h, int rows_vis)
{
    memset(s_lrow, 0, sizeof(s_lrow));
    memset(s_llab, 0, sizeof(s_llab));
    memset(s_lval, 0, sizeof(s_lval));
    memset(s_lbdg, 0, sizeof(s_lbdg));
    s_lparent = NULL;
    s_ltrack = NULL;
    s_lthumb = NULL;
    s_lrows = 0;
    if (!parent || rows_vis < 1) return;
    if (rows_vis > APP_UI_LIST_MAX) rows_vis = APP_UI_LIST_MAX;

    int rh = (h - LIST_GAP * (rows_vis - 1)) / rows_vis;
    if (rh < 28) rh = 28;
    int w = APP_CONTENT_W - LIST_RAIL - 1;
    for (int i = 0; i < rows_vis; i++) {
        lv_obj_t *r = lv_obj_create(parent);
        ui_pixel_strip_theme(r);
        lv_obj_set_pos(r, 0, y + i * (rh + LIST_GAP));
        lv_obj_set_size(r, w, rh);
        row_paint(r, false, UI_CYAN);
        s_lrow[i] = r;
        int lab_w = w - LIST_VAL_W - LIST_PAD * 3;
        s_llab[i] = row_label(r, UI_TEXT, lab_w);
        lv_obj_align(s_llab[i], LV_ALIGN_LEFT_MID, LIST_PAD, 0);
        s_lval[i] = row_label(r, UI_MUTE, LIST_VAL_W);
        lv_obj_set_style_text_align(s_lval[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_lval[i], LV_ALIGN_RIGHT_MID, -LIST_PAD, 0);
    }
    s_lparent = parent;
    s_lrows = rows_vis;
    s_ltrack_y = y;
    s_ltrack_h = rows_vis * rh + (rows_vis - 1) * LIST_GAP;
}

int app_ui_list_rows(void)
{
    return s_lrows;
}

static void render_row(int slot, const app_row_t *r, bool selected)
{
    lv_obj_t *row = s_lrow[slot];
    bool head = r->kind == APP_ROW_HEADER;
    uint32_t accent = r->accent ? r->accent : (r->danger ? UI_ROSE : UI_CYAN);

    if (head) {
        lv_color_t bg = lv_color_hex(ui_style_bg());
        if (!lv_color_eq(lv_obj_get_style_bg_color(row, 0), bg)) {
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(row, bg, 0);
            lv_obj_set_style_border_width(row, 0, 0);
        }
    } else {
        row_paint(row, selected && !r->disabled, accent);
    }

    uint32_t fg = UI_TEXT;
    if (head) fg = UI_MUTE;
    else if (r->disabled) fg = UI_MUTE;
    else if (r->danger) fg = UI_ROSE;
    lab_color(s_llab[slot], fg);
    lab_text(s_llab[slot], r->label ? r->label : "");

    if (r->value && r->value[0]) {
        lab_text(s_lval[slot], r->value);
        lab_color(s_lval[slot], selected ? UI_TEXT : UI_MUTE);
        obj_hidden(s_lval[slot], false);
    } else {
        obj_hidden(s_lval[slot], true);
    }

    if (r->badge && r->badge[0]) {
        if (!s_lbdg[slot]) {
            s_lbdg[slot] = row_label(row, UI_CYAN, 40);
            lv_obj_align(s_lbdg[slot], LV_ALIGN_LEFT_MID, LIST_PAD + 80, 0);
        }
        lab_text(s_lbdg[slot], r->badge);
        lab_color(s_lbdg[slot], accent);
        obj_hidden(s_lbdg[slot], false);
    } else if (s_lbdg[slot]) {
        obj_hidden(s_lbdg[slot], true);
    }
}

void app_ui_list_render(const app_row_t *rows, const app_list_t *l)
{
    if (!rows || !l) return;
    for (int slot = 0; slot < s_lrows; slot++) {
        lv_obj_t *row = s_lrow[slot];
        if (!row) continue;
        int i = l->top + slot;
        if (i < 0 || i >= l->n) {
            obj_hidden(row, true);
            continue;
        }
        obj_hidden(row, false);
        render_row(slot, &rows[i], i == l->sel);
    }

    if (l->n <= s_lrows) {
        obj_hidden(s_ltrack, true);
        obj_hidden(s_lthumb, true);
        return;
    }
    if (!s_lparent) return;
    if (!s_ltrack) s_ltrack = rail_bar(s_lparent, UI_GRID);
    if (!s_lthumb) s_lthumb = rail_bar(s_lparent, UI_CYAN);
    obj_hidden(s_ltrack, false);
    obj_hidden(s_lthumb, false);
    int th = s_ltrack_h * s_lrows / l->n;
    if (th < 6) th = 6;
    int span = s_ltrack_h - th;
    int ty = l->n > s_lrows ? span * l->top / (l->n - s_lrows) : 0;
    lv_obj_set_height(s_lthumb, th);
    lv_obj_set_y(s_lthumb, s_ltrack_y + ty);
}

const char *const *app_kb_keys(int set)
{
    if (set == 1) return KB_UPPER;
    if (set == 2) return KB_SYM;
    return KB_LOWER;
}

#define KB_GAP   2
#define KB_VAL_H 18

static lv_obj_t *s_kb;
static char s_kb_val[80];
static int s_kb_sel, s_kb_set;

static bool kb_geom(lv_area_t *box, int *ox, int *gy, int *cw, int *ch)
{
    if (!s_kb || !box || !ox || !gy || !cw || !ch) return false;
    lv_obj_get_coords(s_kb, box);
    int w = (int)lv_area_get_width(box);
    int h = (int)lv_area_get_height(box);
    if (w < 8 || h < 8) return false;
    *gy = box->y1 + KB_VAL_H + KB_GAP;
    int gh = box->y2 - *gy + 1;
    if (gh < KB_ROWS) return false;
    *cw = (w - KB_GAP * (KB_COLS - 1)) / KB_COLS;
    *ch = (gh - KB_GAP * (KB_ROWS - 1)) / KB_ROWS;
    if (*cw < 8 || *ch < 12) return false;
    *ox = box->x1 + (w - (*cw * KB_COLS + KB_GAP * (KB_COLS - 1))) / 2;
    return true;
}

static void kb_invalidate_cell(int i)
{
    lv_area_t box;
    int ox, gy, cw, ch;
    if (i < 0 || i >= KB_N || !kb_geom(&box, &ox, &gy, &cw, &ch)) return;
    int c = i % KB_COLS, r = i / KB_COLS;
    lv_area_t a = {
        ox + c * (cw + KB_GAP),
        gy + r * (ch + KB_GAP),
        0, 0,
    };
    a.x2 = a.x1 + cw - 1;
    a.y2 = a.y1 + ch - 1;
    lv_obj_invalidate_area(s_kb, &a);
}

static void kb_invalidate_val(void)
{
    lv_area_t box;
    if (!s_kb) return;
    lv_obj_get_coords(s_kb, &box);
    lv_area_t va = { box.x1, box.y1, box.x2, box.y1 + KB_VAL_H - 1 };
    lv_obj_invalidate_area(s_kb, &va);
}

static void kb_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    int ox, gy, cw, ch;
    if (!kb_geom(&box, &ox, &gy, &cw, &ch)) return;

    const lv_font_t *font = ui_pixel_font_14();
    const char *val = s_kb_val[0] ? s_kb_val : app_str(APP_STR_EMPTY);
    lv_draw_label_dsc_t vd;
    lv_draw_label_dsc_init(&vd);
    vd.text = val;
    vd.text_local = 1;
    vd.font = font;
    vd.color = lv_color_hex(UI_TEXT);
    vd.opa = LV_OPA_COVER;
    vd.align = LV_TEXT_ALIGN_LEFT;
    lv_area_t va = { box.x1, box.y1, box.x2, box.y1 + KB_VAL_H - 1 };
    lv_draw_label(layer, &vd, &va);

    int fh = (int)font->line_height;
    const char *const *keys = app_kb_keys(s_kb_set);

    for (int i = 0; i < KB_N; i++) {
        int c = i % KB_COLS, r = i / KB_COLS;
        int x = ox + c * (cw + KB_GAP);
        int y = gy + r * (ch + KB_GAP);
        bool sel = (i == s_kb_sel);
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.bg_color = lv_color_hex(sel ? UI_FILL : UI_CARD);
        rd.bg_opa = LV_OPA_COVER;
        rd.border_width = 0;
        rd.border_color = lv_color_hex(UI_CYAN);
        rd.radius = UI_RADIUS_SM;
        rd.outline_width = 0;
        rd.shadow_width = 0;
        lv_area_t ka = { x, y, x + cw - 1, y + ch - 1 };
        lv_draw_rect(layer, &rd, &ka);

        int ty = y + (ch - fh) / 2;
        if (ty < y) ty = y;
        lv_draw_label_dsc_t ld;
        lv_draw_label_dsc_init(&ld);
        ld.text = keys[i];
        ld.text_local = 1;
        ld.font = font;
        ld.color = lv_color_hex(sel ? UI_CYAN : UI_TEXT);
        ld.opa = LV_OPA_COVER;
        ld.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t ta = { x, ty, x + cw - 1, y + ch - 1 };
        lv_draw_label(layer, &ld, &ta);
    }
}

void app_kb_show(lv_obj_t *parent, const char *value, int sel, int set,
                 int reserve_bottom)
{
    if (!parent) return;
    if (s_kb && !lv_obj_is_valid(s_kb)) s_kb = NULL;
    if (s_kb && lv_obj_get_parent(s_kb) != parent) {
        lv_obj_delete(s_kb);
        s_kb = NULL;
    }
    bool created = false;
    if (!s_kb) {
        s_kb = lv_obj_create(parent);
        ui_pixel_strip_theme(s_kb);
        lv_obj_set_style_bg_opa(s_kb, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(s_kb, kb_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
        created = true;
    }
    bool was_hidden = lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    char val[80];
    ui_pixel_utf8_copy(val, sizeof(val), value ? value : "");
    int old_sel = s_kb_sel;
    int old_set = s_kb_set;
    bool same_val = strcmp(s_kb_val, val) == 0;
    bool ready = !created && !was_hidden;

    if (created || was_hidden) {
        lv_obj_update_layout(parent);
        int y = 18;
        int ph = (int)lv_obj_get_content_height(parent);
        if (ph < 80) ph = APP_BODY_H;
        int h = ph - y - reserve_bottom;
        if (h < 120) h = 120;
        lv_obj_set_pos(s_kb, 0, y);
        lv_obj_set_size(s_kb, APP_CONTENT_W, h);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }

    if (ready && same_val && old_set == set && old_sel == sel) return;

    ui_pixel_utf8_copy(s_kb_val, sizeof(s_kb_val), val);
    s_kb_sel = sel;
    s_kb_set = set;

    if (ready && old_set == set) {
        if (!same_val) kb_invalidate_val();
        if (old_sel != sel) {
            kb_invalidate_cell(old_sel);
            kb_invalidate_cell(sel);
        }
        return;
    }
    lv_obj_invalidate(s_kb);
}

void app_kb_hide(void)
{
    if (s_kb && lv_obj_is_valid(s_kb)) lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    else s_kb = NULL;
}

#define KB_CELL 6

static void kb_cell(char *dst, size_t n, const char *k, bool sel)
{
    char buf[8];
    if (sel) snprintf(buf, sizeof(buf), "[%s]", k);
    else snprintf(buf, sizeof(buf), "%s", k);
    int len = (int)strlen(buf);
    int pad = KB_CELL - len;
    if (pad < 0) pad = 0;
    int left = pad / 2;
    int i = 0;
    while (left-- > 0 && i + 1 < (int)n) dst[i++] = ' ';
    int copy = len;
    if (copy > (int)n - 1 - i) copy = (int)n - 1 - i;
    if (copy > 0) {
        memcpy(dst + i, buf, (size_t)copy);
        i += copy;
    }
    while (i < KB_CELL && i + 1 < (int)n) dst[i++] = ' ';
    dst[i] = 0;
}

void app_kb_render(char *out, size_t n, const char *heading, const char *value,
                   int sel, int set)
{
    if (!out || n == 0) return;
    const char *const *keys = app_kb_keys(set);
    int off = snprintf(out, n, "%s\n%s\n", heading ? heading : "",
                       value && value[0] ? value : app_str(APP_STR_EMPTY));
    if (off < 0 || (size_t)off >= n) return;
    for (int r = 0; r < KB_ROWS; r++) {
        for (int c = 0; c < KB_COLS; c++) {
            int i = r * KB_COLS + c;
            char cell[12];
            kb_cell(cell, sizeof(cell), keys[i], i == sel);
            int w = snprintf(out + off, n - (size_t)off, "%s", cell);
            if (w < 0) return;
            off += w;
            if ((size_t)off >= n) return;
        }
        int w = snprintf(out + off, n - (size_t)off, "\n");
        if (w < 0) return;
        off += w;
        if ((size_t)off >= n) return;
    }
}

int app_kb_click(char *buf, size_t cap, int *sel, int *set)
{
    if (!buf || cap == 0 || !sel || !set) return 0;
    const char *k = app_kb_keys(*set)[*sel];
    if (strcmp(k, "DEL") == 0) {
        size_t len = strlen(buf);
        if (len) {
            size_t i = len - 1;
            while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80) i--;
            buf[i] = 0;
        }
        return 1;
    }
    if (strcmp(k, "Aa") == 0) {
        *set = (*set + 1) % 3;
        return 1;
    }
    if (strcmp(k, "BK") == 0) return 3;
    if (strcmp(k, "GO") == 0) return 2;
    if (strcmp(k, "QR") == 0) return 4;
    if (strcmp(k, "SPC") == 0) k = " ";
    size_t len = strlen(buf);
    size_t kn = strlen(k);
    if (len + kn >= cap) return 1;
    memcpy(buf + len, k, kn + 1);
    return 1;
}
