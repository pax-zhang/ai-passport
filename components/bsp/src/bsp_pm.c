#include "bsp_pm.h"
#include "bsp_ble.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"

static const char *TAG = "bsp_pm";
static bool s_inited;
static bool s_sleeping;
static bool s_perf;

#if CONFIG_PM_ENABLE
static void apply(void)
{
    esp_pm_config_t cfg = {
        .max_freq_mhz = 160,
        .min_freq_mhz = (s_sleeping || !s_perf) ? 40 : 80,
        .light_sleep_enable = s_sleeping && !bsp_ble_stack_up(),
    };
    esp_err_t e = esp_pm_configure(&cfg);
    if (e != ESP_OK && cfg.min_freq_mhz == 40) {
        cfg.min_freq_mhz = 80;
        e = esp_pm_configure(&cfg);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "pm_configure 失败: %s (sleep=%d)", esp_err_to_name(e), (int)s_sleeping);
        return;
    }
    ESP_LOGI(TAG, "pm min=%dMHz light_sleep=%d", cfg.min_freq_mhz, (int)cfg.light_sleep_enable);
}
#endif

esp_err_t bsp_pm_init(void)
{
    if (s_inited) return ESP_OK;
    s_inited = true;
    s_sleeping = false;
    s_perf = true;
#if CONFIG_PM_ENABLE
    apply();
#else
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE 未开,息屏不会进浅睡");
#endif
    return ESP_OK;
}

void bsp_pm_set_sleeping(bool on)
{
    if (!s_inited) bsp_pm_init();
    if (s_sleeping == on) return;
    s_sleeping = on;
#if CONFIG_PM_ENABLE
    apply();
#endif
}

void bsp_pm_set_perf(bool on)
{
    if (!s_inited) bsp_pm_init();
    if (s_perf == on) return;
    s_perf = on;
    if (s_sleeping) return;
#if CONFIG_PM_ENABLE
    apply();
#endif
}
