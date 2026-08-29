// components/bsp/include/bsp_display.h
// ST7789P3 240x320 显示:SPI 面板初始化 + 厂商专属寄存器 + LEDC 背光调光。
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 初始化 SPI 总线、面板、厂商寄存器、背光 LEDC。成功后屏幕已上电但内容未定。
esp_err_t bsp_display_init(void);

// 取底层面板句柄。想直接 esp_lcd_panel_draw_bitmap 画,或接 LVGL 以外的 GUI 时用。
// 未初始化返回 NULL。
esp_lcd_panel_handle_t bsp_display_panel(void);

// 取底层 panel io 句柄(LVGL 接入需要)。未初始化返回 NULL。
esp_lcd_panel_io_handle_t bsp_display_io(void);

// 背光亮度 0..100(%)。LEDC PWM,0=全灭。
void bsp_display_backlight(uint8_t percent);

// 面板休眠:DISPOFF + SLPIN。息屏时调用;GRAM 通常保留,唤醒后不必全屏重绘。
void bsp_display_sleep(bool sleep);

// ---------------------------------------------------------------------------
// LVGL 接入(可选层)。必须先 bsp_display_init() 成功后再调。
// 不想用 LVGL 的开发者可忽略本段,直接用 bsp_display_panel() 自己画。
// ---------------------------------------------------------------------------
// 前向声明:LVGL 里 lv_display_t 是 `typedef struct _lv_display_t lv_display_t;`,
// 故此处用 struct 形式即可,避免本头文件强行 include lvgl.h。
struct _lv_display_t;

// 启动 LVGL 与其渲染任务,返回 lv_display_t*。失败返回 NULL。
struct _lv_display_t *bsp_lvgl_init(void);

// LVGL 非线程安全:在【非 LVGL 任务】里操作任何 lv_* 对象前后必须加解锁。
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);

// 关掉 flush,避免往已睡眠的面板刷 SPI。
void bsp_lvgl_flush_enable(bool on);

// 停/开 LVGL tick。息屏后关掉 5ms 定时器,否则进不了浅睡。
void bsp_lvgl_tick_enable(bool on);

// 暂停/恢复往 LCD 刷帧。LVGL 定时器仍跑(shell/通知),只是不 SPI 传输。
void bsp_lvgl_pause(void);
void bsp_lvgl_resume(void);

void bsp_lvgl_screenshot_emit(void (*emit)(const uint8_t *p, size_t n));
