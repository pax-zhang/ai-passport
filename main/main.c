#include "app.h"
#include "app_prefs.h"
#include "app_time.h"
#include "app_tone.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_ble.h"
#include "bsp_wifi.h"
#include "bsp_pm.h"
#include "bsp_pins.h"
#include "ui_pixel.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static bool s_ok[5];

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;
    app_shell_on_key(btn, ev);
    bsp_lvgl_unlock();
}

void app_main(void)
{
    app_logs_start();
    ESP_LOGI(TAG, "FoloToy AI Passport");

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS 不可用，保留数据并使用默认配置: %s",
                 esp_err_to_name(e));
    } else if (e != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(e));
    }

    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    ui_pixel_fonts_init();
    bsp_screenshot_start();
    bsp_display_backlight(50);

    s_ok[0] = true;
    s_ok[1] = (bsp_button_init(on_key, NULL) == ESP_OK);
    s_ok[2] = (bsp_audio_init() == ESP_OK);
    s_ok[3] = (bsp_battery_init() == ESP_OK);
    if (bsp_wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 初始化失败");
    }

    app_prefs_load();
    s_ok[4] = (bsp_ble_init() == ESP_OK);

    app_prefs_apply_audio();
    app_prefs_apply_display();
    app_time_init();
    app_tone_start();
    bsp_pm_init();

    if (bsp_lvgl_lock(1000)) {
        app_shell_start();
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪:Display=%d Button=%d Audio=%d Battery=%d BLE=%d theme=%d %s",
             s_ok[0], s_ok[1], s_ok[2], s_ok[3], s_ok[4],
             ui_theme_id(), ui_theme_name(ui_theme_id()));
}
