// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include "src/display/lv_display_private.h"

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;
static lv_display_flush_cb_t s_flush;
static void (*s_shot_emit)(const uint8_t *p, size_t n);

static void shot_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (s_shot_emit && area && px_map) {
        uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
        uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
        s_shot_emit(px_map, (size_t)w * (size_t)h * 2u);
    }
    if (s_flush) s_flush(disp, area, px_map);
}

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    // 中文回退字体 cmap 多,标题/列表排版比纯英文吃栈。5120 在中文主页会溢出黑屏。
    pc.task_stack = 8192;
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
    s_flush = s_disp->flush_cb;
    s_disp->flush_cb = shot_flush;

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
}

void bsp_lvgl_screenshot_emit(void (*emit)(const uint8_t *p, size_t n))
{
    if (!s_disp || !emit) return;
    s_shot_emit = emit;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_disp);
    s_shot_emit = NULL;
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
