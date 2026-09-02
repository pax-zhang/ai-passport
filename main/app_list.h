#pragma once

#include <stdbool.h>
#include <stdint.h>

// 列表焦点与窗口的纯逻辑层,不依赖 LVGL,可在 host 上测试。
// 渲染在 app_ui_list_render()。

typedef enum {
    APP_ROW_ACTION = 0,  // 进入子页或执行动作
    APP_ROW_TOGGLE,      // 开关
    APP_ROW_CHOICE,      // 循环取值
    APP_ROW_INFO,        // 只读信息,不可聚焦
    APP_ROW_HEADER,      // 分组标题,不可聚焦
} app_row_kind_t;

typedef struct {
    uint8_t kind;
    const char *label;  // 已过 app_str()
    const char *value;  // 右侧值,可空
    const char *badge;  // 角标,如未读数,可空
    uint32_t accent;    // 0 = 主题默认色
    bool disabled;      // 可聚焦种类也可临时禁用
    bool danger;        // 危险动作,渲染成告警色
} app_row_t;

typedef struct {
    int sel;       // 当前焦点行
    int top;       // 可见窗口首行
    int n;         // 总行数
    int rows;      // 可见行数
    bool no_wrap;  // 默认环形移动;置位后到边界停住
} app_list_t;

// rows 为 NULL 时视为每一行都可聚焦,方便纯索引列表复用窗口逻辑。
bool app_row_focusable(const app_row_t *rows, int i, int n);
int app_list_first_focusable(const app_row_t *rows, int n);

// 焦点落到第一个可聚焦行。进入页面时用。
void app_list_reset(app_list_t *l, const app_row_t *rows, int n, int rows_vis);
// 行数不变且原焦点仍可聚焦时保留焦点,否则等同 reset。返回上一层时用。
void app_list_keep(app_list_t *l, const app_row_t *rows, int n, int rows_vis);
void app_list_move(app_list_t *l, const app_row_t *rows, int delta);
// 依据 sel 重算 top,保证焦点在可见窗口内。
void app_list_reveal(app_list_t *l);
