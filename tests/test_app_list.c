#include "app_list.h"

#include <assert.h>
#include <stddef.h>

static const app_row_t ROWS[] = {
    { .kind = APP_ROW_HEADER, .label = "group" },
    { .kind = APP_ROW_INFO, .label = "info" },
    { .kind = APP_ROW_ACTION, .label = "a" },
    { .kind = APP_ROW_TOGGLE, .label = "b", .disabled = true },
    { .kind = APP_ROW_CHOICE, .label = "c" },
};
#define ROW_N ((int)(sizeof(ROWS) / sizeof(ROWS[0])))

static void test_focusable(void)
{
    assert(!app_row_focusable(ROWS, 0, ROW_N));
    assert(!app_row_focusable(ROWS, 1, ROW_N));
    assert(app_row_focusable(ROWS, 2, ROW_N));
    assert(!app_row_focusable(ROWS, 3, ROW_N));
    assert(app_row_focusable(ROWS, 4, ROW_N));
    assert(!app_row_focusable(ROWS, -1, ROW_N));
    assert(!app_row_focusable(ROWS, ROW_N, ROW_N));
    // rows 为空时视为全部可聚焦,给自绘页面用
    assert(app_row_focusable(NULL, 0, 3));

    assert(app_list_first_focusable(ROWS, ROW_N) == 2);
    assert(app_list_first_focusable(ROWS, 2) == -1);
    assert(app_list_first_focusable(NULL, 4) == 0);
}

static void test_initial_focus(void)
{
    app_list_t l = { 0 };
    app_list_reset(&l, ROWS, ROW_N, 4);
    assert(l.sel == 2);
    assert(l.top == 0);

    // 全不可聚焦时退到 0,避免 sel 变成 -1
    static const app_row_t only_info[] = {
        { .kind = APP_ROW_HEADER, .label = "h" },
        { .kind = APP_ROW_INFO, .label = "i" },
    };
    app_list_reset(&l, only_info, 2, 4);
    assert(l.sel == 0);
}

static void test_move(void)
{
    app_list_t l = { 0 };
    app_list_reset(&l, ROWS, ROW_N, ROW_N);
    assert(l.sel == 2);
    app_list_move(&l, ROWS, 1);
    assert(l.sel == 4);  // 跳过 disabled 的 3
    app_list_move(&l, ROWS, 1);
    assert(l.sel == 2);  // 环形回到首个可聚焦项
    app_list_move(&l, ROWS, -1);
    assert(l.sel == 4);

    l.no_wrap = true;
    app_list_move(&l, ROWS, 1);
    assert(l.sel == 4);  // 到边界停住
    app_list_move(&l, ROWS, -1);
    assert(l.sel == 2);
    app_list_move(&l, ROWS, -1);
    assert(l.sel == 2);

    // 无行模型时按普通环形列表走
    app_list_t p = { 0 };
    app_list_reset(&p, NULL, 3, 3);
    assert(p.sel == 0);
    app_list_move(&p, NULL, -1);
    assert(p.sel == 2);
    app_list_move(&p, NULL, 2);
    assert(p.sel == 1);
    app_list_move(&p, NULL, 0);
    assert(p.sel == 1);
}

static void test_reveal(void)
{
    app_list_t l = { .sel = 0, .n = 10, .rows = 4 };
    app_list_reveal(&l);
    assert(l.top == 0);

    l.sel = 3;
    app_list_reveal(&l);
    assert(l.top == 0);

    l.sel = 4;
    app_list_reveal(&l);
    assert(l.top == 1);

    l.sel = 9;
    app_list_reveal(&l);
    assert(l.top == 6);

    l.sel = 2;
    app_list_reveal(&l);
    assert(l.top == 2);

    // 总行数不足一屏时窗口固定在 0
    app_list_t small = { .sel = 1, .top = 3, .n = 2, .rows = 5 };
    app_list_reveal(&small);
    assert(small.top == 0);

    // rows 非法时兜底成 1
    app_list_t bad = { .sel = 2, .n = 5, .rows = 0 };
    app_list_reveal(&bad);
    assert(bad.rows == 1 && bad.top == 2);
}

static void test_keep(void)
{
    app_list_t l = { 0 };
    app_list_reset(&l, ROWS, ROW_N, 3);
    app_list_move(&l, ROWS, 1);
    assert(l.sel == 4);

    // 行数不变则保留焦点,这是返回上一层后恢复光标的依据
    app_list_keep(&l, ROWS, ROW_N, 3);
    assert(l.sel == 4);

    // 行数变了就重新定位到第一个可操作行
    app_list_keep(&l, ROWS, 3, 3);
    assert(l.sel == 2);

    // 原焦点变得不可聚焦时也重新定位
    static app_row_t mut[ROW_N];
    for (int i = 0; i < ROW_N; i++) mut[i] = ROWS[i];
    app_list_reset(&l, mut, ROW_N, 3);
    app_list_move(&l, mut, 1);
    assert(l.sel == 4);
    mut[4].disabled = true;
    app_list_keep(&l, mut, ROW_N, 3);
    assert(l.sel == 2);
}

static void test_null_safe(void)
{
    app_list_reset(NULL, ROWS, ROW_N, 3);
    app_list_keep(NULL, ROWS, ROW_N, 3);
    app_list_move(NULL, ROWS, 1);
    app_list_reveal(NULL);
}

int main(void)
{
    test_focusable();
    test_initial_focus();
    test_move();
    test_reveal();
    test_keep();
    test_null_safe();
    return 0;
}
