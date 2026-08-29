#include "app_ui.h"

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
    "SPC", "DEL", "Aa", "QR", "GO", "BK",
};
static const char *const KB_UPPER[KB_N] = {
    "1", "2", "3", "4", "5", "6",
    "7", "8", "9", "0", ".", "@",
    "A", "B", "C", "D", "E", "F",
    "G", "H", "I", "J", "K", "L",
    "M", "N", "O", "P", "Q", "R",
    "S", "T", "U", "V", "W", "X",
    "Y", "Z", "_", "-", "#", "/",
    "SPC", "DEL", "Aa", "QR", "GO", "BK",
};
static const char *const KB_SYM[KB_N] = {
    "!", "?", ":", ";", "+", "=",
    "*", "&", "%", "$", ",", "~",
    "(", ")", "[", "]", "{", "}",
    "<", ">", "'", "\"", "\\", "|",
    "^", "`", ",", ".", ";", ":",
    "+", "-", "_", "#", "@", "/",
    "!", "?", "*", "&", "%", "$",
    "SPC", "DEL", "Aa", "QR", "GO", "BK",
};

static const char *const KB_ID[KB_ID_N] = {
    "1", "2", "3", "4",
    "5", "6", "7", "8",
    "9", "0", "DEL", "GO",
    "",  "BK", "",  "",
};

static bool kb_id_valid(int i)
{
    return i >= 0 && i < KB_ID_N && KB_ID[i][0] != 0;
}

lv_obj_t *app_ui_card(lv_obj_t *parent)
{
    int h = (int)lv_obj_get_height(parent);
    if (h < 80) h = 288;
    return ui_pixel_panel_create(parent, 8, 6, 224, h - 14, UI_PAPER);
}

lv_obj_t *app_ui_title(lv_obj_t *card, const char *text)
{
    lv_obj_t *t = lv_label_create(card);
    lv_obj_set_style_text_font(t, ui_pixel_font_20(), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_INK), 0);
    lv_label_set_text(t, text);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    return t;
}

lv_obj_t *app_ui_hint(lv_obj_t *card)
{
    lv_obj_t *h = lv_label_create(card);
    lv_obj_set_style_text_font(h, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(h, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_set_width(h, 200);
    lv_label_set_long_mode(h, LV_LABEL_LONG_CLIP);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, 24);
    return h;
}

lv_obj_t *app_ui_body(lv_obj_t *card, int y)
{
    lv_obj_t *b = lv_label_create(card);
    lv_obj_set_style_text_font(b, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(UI_INK), 0);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 200);
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

const char *const *app_kb_keys(int set)
{
    if (set == 1) return KB_UPPER;
    if (set == 2) return KB_SYM;
    return KB_LOWER;
}

const char *const *app_kb_id_keys(void)
{
    return KB_ID;
}

void app_kb_id_move(int *sel, int delta)
{
    int n, dir, steps, s, t;

    if (!sel || delta == 0) return;
    n = *sel;
    dir = delta > 0 ? 1 : -1;
    steps = delta > 0 ? delta : -delta;
    for (s = 0; s < steps; s++) {
        for (t = 0; t < KB_ID_N; t++) {
            n += dir;
            n %= KB_ID_N;
            if (n < 0) n += KB_ID_N;
            if (kb_id_valid(n)) break;
        }
    }
    *sel = n;
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
            if (i == sel) snprintf(cell, sizeof(cell), "[%s]", keys[i]);
            else snprintf(cell, sizeof(cell), " %s ", keys[i]);
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

int app_kb_id_click(char *buf, size_t cap, int sel)
{
    const char *k;
    size_t len;

    if (!buf || cap == 0 || !kb_id_valid(sel)) return 0;
    k = KB_ID[sel];
    if (strcmp(k, "DEL") == 0) {
        len = strlen(buf);
        if (len) buf[len - 1] = 0;
        return 1;
    }
    if (strcmp(k, "BK") == 0) return 3;
    if (strcmp(k, "GO") == 0) return 2;
    if (k[0] < '0' || k[0] > '9' || k[1] != 0) return 0;
    len = strlen(buf);
    if (len >= 6 || len + 1 >= cap) return 1;
    buf[len] = k[0];
    buf[len + 1] = 0;
    return 1;
}
