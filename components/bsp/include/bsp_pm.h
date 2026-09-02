// 自动 Light Sleep + DFS。息屏时打开;Deep Sleep 会掉射频,不用。
#pragma once

#include "esp_err.h"
#include <stdbool.h>

// 配 DFS。默认不进浅睡,由 bsp_pm_set_sleeping() 在息屏时打开。幂等。
esp_err_t bsp_pm_init(void);

// true:允许 tickless Light Sleep(CPU 40MHz 下限)。false:只保留 DFS,方便 UI。
void bsp_pm_set_sleeping(bool on);

// 亮屏时:true 把 DFS 下限留在 80MHz(按键/小游戏);false 再降到 40MHz(菜单静止)。
// 息屏时由 bsp_pm_set_sleeping() 决定,本开关暂不生效。BLE 在跑时始终 80MHz。
void bsp_pm_set_perf(bool on);

// BLE 启停后重配 DFS/浅睡,避免息屏期间掉到 40MHz 或误开 light sleep。
void bsp_pm_touch(void);
