#include "app_fap_shot.h"

#include "bsp_display.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int quiet_vprintf(const char *fmt, va_list args)
{
    (void)fmt;
    (void)args;
    return 0;
}

static void emit_stdout(const uint8_t *p, size_t n)
{
    if (p && n) fwrite(p, 1, n, stdout);
}

static void send_shot(void)
{
    vprintf_like_t prev;
    unsigned bytes = (unsigned)BSP_LCD_W * (unsigned)BSP_LCD_H * 2u;

    if (!bsp_lvgl_lock(2000)) return;
    prev = esp_log_set_vprintf(quiet_vprintf);
    fprintf(stdout, "FAP_SCREENSHOT_V1 %d %d RGB565LE %u\n",
            BSP_LCD_W, BSP_LCD_H, bytes);
    fflush(stdout);
    bsp_lvgl_screenshot_emit(emit_stdout);
    fflush(stdout);
    esp_log_set_vprintf(prev);
    bsp_lvgl_unlock();
}

static void shot_task(void *arg)
{
    char line[48];

    (void)arg;
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (strncmp(line, "FAP_SCREENSHOT_V1", 17) == 0) send_shot();
    }
}

void app_fap_shot_start(void)
{
    xTaskCreate(shot_task, "fap_shot", 8192, NULL, 2, NULL);
}
