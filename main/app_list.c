#include "app_list.h"

bool app_row_focusable(const app_row_t *rows, int i, int n)
{
    if (i < 0 || i >= n) return false;
    if (!rows) return true;
    const app_row_t *r = &rows[i];
    if (r->disabled) return false;
    return r->kind == APP_ROW_ACTION || r->kind == APP_ROW_TOGGLE ||
           r->kind == APP_ROW_CHOICE;
}

int app_list_first_focusable(const app_row_t *rows, int n)
{
    for (int i = 0; i < n; i++) {
        if (app_row_focusable(rows, i, n)) return i;
    }
    return -1;
}

void app_list_reveal(app_list_t *l)
{
    if (!l) return;
    if (l->rows < 1) l->rows = 1;
    if (l->n <= l->rows) {
        l->top = 0;
        return;
    }
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel > l->top + l->rows - 1) l->top = l->sel - l->rows + 1;
    if (l->top > l->n - l->rows) l->top = l->n - l->rows;
    if (l->top < 0) l->top = 0;
}

static void apply(app_list_t *l, const app_row_t *rows, int n, int rows_vis,
                  int sel)
{
    l->n = n < 0 ? 0 : n;
    l->rows = rows_vis < 1 ? 1 : rows_vis;
    if (sel < 0 || sel >= l->n || !app_row_focusable(rows, sel, l->n)) {
        int first = app_list_first_focusable(rows, l->n);
        sel = first < 0 ? 0 : first;
    }
    l->sel = sel;
    app_list_reveal(l);
}

void app_list_reset(app_list_t *l, const app_row_t *rows, int n, int rows_vis)
{
    if (!l) return;
    l->top = 0;
    apply(l, rows, n, rows_vis, -1);
}

void app_list_keep(app_list_t *l, const app_row_t *rows, int n, int rows_vis)
{
    if (!l) return;
    int sel = l->n == n ? l->sel : -1;
    apply(l, rows, n, rows_vis, sel);
}

void app_list_move(app_list_t *l, const app_row_t *rows, int delta)
{
    if (!l || l->n < 1 || delta == 0) return;
    int dir = delta > 0 ? 1 : -1;
    int steps = delta > 0 ? delta : -delta;
    for (int s = 0; s < steps; s++) {
        int next = l->sel;
        for (int k = 1; k <= l->n; k++) {
            int i = l->sel + dir * k;
            if (l->no_wrap) {
                if (i < 0 || i >= l->n) break;
            } else {
                i %= l->n;
                if (i < 0) i += l->n;
            }
            if (app_row_focusable(rows, i, l->n)) {
                next = i;
                break;
            }
        }
        if (next == l->sel) break;
        l->sel = next;
    }
    app_list_reveal(l);
}
