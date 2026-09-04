#include "app.h"
#include "app_bg.h"
#include "app_prefs.h"
#include "app_time.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "bsp_pm.h"
#include "bsp_wifi.h"
#include "ui_pixel.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;
    app_shell_on_key(btn, ev);
    bsp_lvgl_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy AI Passport Dial");

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS %s", esp_err_to_name(e));
    } else if (e != ESP_OK) {
        ESP_LOGE(TAG, "NVS init %s", esp_err_to_name(e));
    }

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display MOSI=%d SCLK=%d CS=%d DC=%d BL=%d",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    ui_pixel_fonts_init();
    bsp_screenshot_start();
    bsp_display_backlight(50);

    bsp_button_init(on_key, NULL);
    bsp_battery_init();
    if (bsp_wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "wifi");
    }

    app_prefs_load();
    app_bg_init();
    app_prefs_custom()->has_bg = app_bg_ok() ? 1 : 0;
    app_prefs_apply_display();
    app_time_init();
    bsp_pm_init();

    if (bsp_lvgl_lock(1000)) {
        app_shell_start();
        bsp_lvgl_unlock();
    }
}
