// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include "src/display/lv_display_private.h"
#include "src/core/lv_refr.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;
static lv_display_flush_cb_t s_flush_orig;
static int s_shot_y;
static bool s_shot_stream;
static volatile bool s_shot_done;
static uint8_t s_shot_zrow[BSP_LCD_W * 2];

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    // 中文回退字体 cmap 多,标题/列表排版比纯英文吃栈。5120 在中文主页会溢出黑屏。
    pc.task_stack = 12288;
    pc.task_max_sleep_ms = 10000;
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM(总共约 150KB)。
        // 20 行单缓冲 ≈ 9.6KB;若改成 40 行双缓冲(≈37.5KB)会把 I2S 等外设的
        // DMA 描述符挤到 NO_MEM。刷新略慢但稳。
        .buffer_size   = (uint32_t)BSP_LCD_W * 20,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

void bsp_lvgl_flush_enable(bool on)
{
    if (!s_disp) return;
    lv_display_enable_invalidation(s_disp, on);
}

void bsp_lvgl_tick_enable(bool on)
{
    if (on) {
        lvgl_port_resume();
        lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);
    } else {
        lvgl_port_stop();
    }
}

void bsp_lvgl_pause(void) { bsp_lvgl_flush_enable(false); }
void bsp_lvgl_resume(void) { bsp_lvgl_flush_enable(true); }

static void usb_write(const void *data, size_t len)
{
    const uint8_t *p = data;
    int spins = 0;
    while (len) {
        if (!usb_serial_jtag_ll_txfifo_writable()) {
            if (++spins > 64) {
                vTaskDelay(1);
                spins = 0;
            } else {
                esp_rom_delay_us(50);
            }
            continue;
        }
        spins = 0;
        int n = usb_serial_jtag_ll_write_txfifo(p, len);
        usb_serial_jtag_ll_txfifo_flush();
        if (n <= 0) continue;
        p += (size_t)n;
        len -= (size_t)n;
    }
    usb_serial_jtag_ll_txfifo_flush();
}

static void shot_pad_to(int y)
{
    while (s_shot_y < y && s_shot_y < BSP_LCD_H) {
        usb_write(s_shot_zrow, sizeof(s_shot_zrow));
        s_shot_y++;
    }
}

static void shot_copy_area(const lv_area_t *area, const uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    if (!s_shot_stream) return;
    if (area->x1 != 0 || w != BSP_LCD_W) return;
    shot_pad_to((int)area->y1);
    if (area->y1 != s_shot_y) return;
    usb_write(px_map, (size_t)w * (size_t)h * 2);
    s_shot_y = area->y2 + 1;
}

static void shot_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (area && px_map) shot_copy_area(area, px_map);
    if (s_flush_orig) s_flush_orig(disp, area, px_map);
}

static void screenshot_refresh(void)
{
    lv_obj_t *scr = lv_display_get_screen_active(s_disp);
    if (scr) lv_obj_invalidate(scr);
    lv_refr_now(s_disp);
}

static void screenshot_on_lvgl(void *arg)
{
    (void)arg;
    size_t need = (size_t)BSP_LCD_W * (size_t)BSP_LCD_H * 2;
    char hdr[64];
    int n = snprintf(hdr, sizeof(hdr), "FAP_SCREENSHOT_V1 %d %d RGB565LE %u\n",
                     BSP_LCD_W, BSP_LCD_H, (unsigned)need);
    if (n > 0) usb_write(hdr, (size_t)n);
    s_shot_y = 0;
    s_shot_stream = true;
    screenshot_refresh();
    shot_pad_to(BSP_LCD_H);
    s_shot_stream = false;
    usb_serial_jtag_ll_txfifo_flush();
    s_shot_done = true;
}

static void screenshot_dump(void)
{
    if (!s_disp || !s_flush_orig || s_shot_stream) return;
    s_shot_done = false;
    if (!bsp_lvgl_lock(1000)) return;
    lv_result_t r = lv_async_call(screenshot_on_lvgl, NULL);
    bsp_lvgl_unlock();
    if (r != LV_RESULT_OK) return;
    for (int i = 0; i < 800 && !s_shot_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void screenshot_task(void *arg)
{
    (void)arg;
    char line[24];
    int n = 0;
    uint8_t c;
    for (;;) {
        if (!usb_serial_jtag_ll_rxfifo_data_available()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (usb_serial_jtag_ll_read_rxfifo(&c, 1) <= 0) continue;
        if (c == '\n' || c == '\r') {
            line[n] = 0;
            if (n && strcmp(line, "FAP_SCREENSHOT_V1") == 0) screenshot_dump();
            n = 0;
            continue;
        }
        if (n < (int)sizeof(line) - 1) line[n++] = (char)c;
        else n = 0;
    }
}

void bsp_screenshot_start(void)
{
    static bool started;
    if (started || !s_disp) return;
    s_flush_orig = s_disp->flush_cb;
    if (s_flush_orig) s_disp->flush_cb = shot_flush;
    started = true;
    xTaskCreate(screenshot_task, "fap_shot", 3072, NULL, 5, NULL);
}
