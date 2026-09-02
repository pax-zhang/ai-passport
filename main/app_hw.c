#include "app.h"
#include "app_i18n.h"
#include "app_ui.h"
#include "bsp_battery.h"
#include "bsp_ble.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "ui_pixel.h"

#include "driver/temperature_sensor.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#define HW_MAX    28
#define HW_WIN    11
#define HW_COLS   36

static lv_obj_t *s_title, *s_hint, *s_body;
static lv_timer_t *s_timer;
static int s_n;
static char s_line[HW_MAX][HW_COLS];
static bool s_codec;
static bool s_gauge;
static temperature_sensor_handle_t s_ts;

static void add(const char *k, const char *v)
{
    if (s_n >= HW_MAX) return;
    snprintf(s_line[s_n], sizeof(s_line[0]), "%s  %s", k, v);
    s_n++;
}

static void fmt_mb(char *o, size_t n, uint32_t bytes)
{
    unsigned mb = bytes / (1024u * 1024u);
    unsigned frac = (unsigned)(((uint64_t)(bytes % (1024u * 1024u)) * 100u) /
                               (1024u * 1024u));
    if (frac) snprintf(o, n, "%u.%02u MB", mb, frac);
    else snprintf(o, n, "%u MB", mb);
}

static void fmt_kb(char *o, size_t n, size_t bytes)
{
    snprintf(o, n, "%u kB", (unsigned)((bytes + 512) / 1024));
}

static void fmt_mac(char *o, size_t n, const uint8_t *m)
{
    snprintf(o, n, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void fmt_up(char *o, size_t n, int64_t us)
{
    unsigned sec = (unsigned)(us / 1000000LL);
    unsigned d = sec / 86400;
    unsigned h = (sec / 3600) % 24;
    unsigned m = (sec / 60) % 60;
    unsigned s = sec % 60;
    if (d) snprintf(o, n, "%ud %uh %um", d, h, m);
    else if (h) snprintf(o, n, "%uh %um", h, m);
    else snprintf(o, n, "%um %us", m, s);
}

static const char *rst_name(esp_reset_reason_t r)
{
    bool zh = app_lang() == APP_LANG_ZH;
    switch (r) {
    case ESP_RST_POWERON:   return zh ? "上电" : "power";
    case ESP_RST_EXT:       return zh ? "外部" : "external";
    case ESP_RST_SW:        return zh ? "软件" : "software";
    case ESP_RST_PANIC:     return zh ? "异常" : "panic";
    case ESP_RST_INT_WDT:   return zh ? "中断狗" : "int WDT";
    case ESP_RST_TASK_WDT:  return zh ? "任务狗" : "task WDT";
    case ESP_RST_WDT:       return zh ? "看门狗" : "WDT";
    case ESP_RST_DEEPSLEEP: return zh ? "深睡" : "deepsleep";
    case ESP_RST_BROWNOUT:  return zh ? "欠压" : "brownout";
    case ESP_RST_SDIO:      return zh ? "SDIO" : "SDIO";
    case ESP_RST_USB:       return zh ? "USB" : "USB";
    case ESP_RST_JTAG:      return zh ? "JTAG" : "JTAG";
    default:                return zh ? "未知" : "unknown";
    }
}

static const char *chip_name(esp_chip_model_t m)
{
    switch (m) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C2: return "ESP32-C2";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32H2: return "ESP32-H2";
    default:           return CONFIG_IDF_TARGET;
    }
}

static void collect(void)
{
    s_n = 0;
    char v[HW_COLS];

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    snprintf(v, sizeof(v), "%s r%u %dc", chip_name(chip.model),
             (unsigned)chip.revision, (int)chip.cores);
    add(app_str(APP_STR_HW_CHIP), v);

    snprintf(v, sizeof(v), "%d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    add(app_str(APP_STR_HW_CPU), v);

    uint32_t flash = 0;
    if (esp_flash_get_size(NULL, &flash) == ESP_OK) fmt_mb(v, sizeof(v), flash);
    else snprintf(v, sizeof(v), "%s", app_str(APP_STR_HW_MISS));
    add(app_str(APP_STR_HW_FLASH), v);

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (part) fmt_mb(v, sizeof(v), part->size);
    else snprintf(v, sizeof(v), "%s", app_str(APP_STR_HW_MISS));
    add(app_str(APP_STR_HW_PART), v);

    fmt_kb(v, sizeof(v), heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    add(app_str(APP_STR_HW_RAM), v);
    fmt_kb(v, sizeof(v), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    add(app_str(APP_STR_HW_HEAP), v);
    fmt_kb(v, sizeof(v), esp_get_minimum_free_heap_size());
    add(app_str(APP_STR_HW_MIN), v);
    fmt_kb(v, sizeof(v),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    add(app_str(APP_STR_HW_BLOCK), v);
    add(app_str(APP_STR_HW_PSRAM),
        (chip.features & CHIP_FEATURE_EMB_PSRAM) ? app_str(APP_STR_HW_OK)
                                             : app_str(APP_STR_HW_NONE));

    lv_mem_monitor_t lv;
    lv_mem_monitor(&lv);
    snprintf(v, sizeof(v), "%u/%u kB",
             (unsigned)((lv.free_size + 512) / 1024),
             (unsigned)((lv.total_size + 512) / 1024));
    add(app_str(APP_STR_HW_LVGL), v);
    snprintf(v, sizeof(v), "%u B",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    add("LV stack", v);

    const esp_app_desc_t *app = esp_app_get_description();
    if (app && app->version[0]) {
        snprintf(v, sizeof(v), "%s", app->version);
        add(app_str(APP_STR_HW_APP), v);
        if (app->idf_ver[0]) add(app_str(APP_STR_HW_IDF), app->idf_ver);
    }

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        fmt_mac(v, sizeof(v), mac);
        add(app_str(APP_STR_HW_MAC), v);
    }
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
        fmt_mac(v, sizeof(v), mac);
        add(app_str(APP_STR_HW_BTMAC), v);
    }
    add(app_str(APP_STR_HW_NAME), bsp_ble_name());

    snprintf(v, sizeof(v), "%dx%d", BSP_LCD_W, BSP_LCD_H);
    add(app_str(APP_STR_HW_LCD), v);

    int soc = bsp_battery_soc();
    int mv = bsp_battery_mv();
    if (soc < 0 && mv < 0) {
        add(app_str(APP_STR_HW_BATT), app_str(APP_STR_HW_MISS));
    } else {
        snprintf(v, sizeof(v), "%d%%  %dmV", soc < 0 ? 0 : soc, mv < 0 ? 0 : mv);
        add(app_str(APP_STR_HW_BATT), v);
    }
    add(app_str(APP_STR_HW_CODEC),
        s_codec ? app_str(APP_STR_HW_OK) : app_str(APP_STR_HW_MISS));
    add(app_str(APP_STR_HW_GAUGE),
        s_gauge ? app_str(APP_STR_HW_OK) : app_str(APP_STR_HW_MISS));

    if (s_ts) {
        float c = 0;
        if (temperature_sensor_get_celsius(s_ts, &c) == ESP_OK) {
            snprintf(v, sizeof(v), "%.1f C", c);
            add(app_str(APP_STR_HW_TEMP), v);
        }
    }

    fmt_up(v, sizeof(v), esp_timer_get_time());
    add(app_str(APP_STR_HW_UP), v);
    add(app_str(APP_STR_HW_RESET), rst_name(esp_reset_reason()));
}

static void paint(void)
{
    if (s_title) lv_label_set_text(s_title, app_str(APP_STR_HARDWARE));
    if (s_hint) lv_label_set_text(s_hint, app_str(APP_STR_HW_HINT));
    if (!s_body) return;

    collect();
    if (s_n < 1) s_n = 1;
    app_list_t *l = app_shell_list();
    app_list_keep(l, NULL, s_n, HW_WIN);
    int s_top = 0;
    if (l) {
        l->no_wrap = true;
        s_top = l->top;
    }

    char buf[HW_WIN * (HW_COLS + 1) + 8];
    size_t o = 0;
    buf[0] = 0;
    int end = s_top + HW_WIN;
    if (end > s_n) end = s_n;
    for (int i = s_top; i < end; i++) {
        int w = snprintf(buf + o, sizeof(buf) - o, "%s\n", s_line[i]);
        if (w < 0) break;
        o += (size_t)w;
        if (o >= sizeof(buf)) break;
    }
    lv_label_set_text(s_body, buf);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    paint();
}

static void probe_i2c(void)
{
    s_codec = false;
    s_gauge = false;
    bsp_i2c_scan();
    i2c_master_bus_handle_t bus = bsp_i2c_bus();
    if (!bus) return;
    s_codec = i2c_master_probe(bus, BSP_I2C_ES8311_ADDR, 50) == ESP_OK;
    s_gauge = i2c_master_probe(bus, BSP_I2C_CW2017_ADDR, 50) == ESP_OK;
}

static void temp_start(void)
{
    s_ts = NULL;
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_ts) != ESP_OK) {
        s_ts = NULL;
        return;
    }
    if (temperature_sensor_enable(s_ts) != ESP_OK) {
        temperature_sensor_uninstall(s_ts);
        s_ts = NULL;
    }
}

static void temp_stop(void)
{
    if (!s_ts) return;
    temperature_sensor_disable(s_ts);
    temperature_sensor_uninstall(s_ts);
    s_ts = NULL;
}

void app_hw_enter(lv_obj_t *p)
{
    probe_i2c();
    temp_start();
    lv_obj_t *card = app_ui_card(p);
    s_title = app_ui_title(card, app_str(APP_STR_HARDWARE));
    s_hint = app_ui_hint(card);
    s_body = app_ui_body(card, 44);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(UI_TEXT), 0);
    s_timer = lv_timer_create(tick, 1000, NULL);
    paint();
}

void app_hw_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    temp_stop();
    s_title = s_hint = s_body = NULL;
}

void app_hw_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn != BSP_BTN_UP && btn != BSP_BTN_DOWN) return;
    app_list_move(app_shell_list(), NULL, btn == BSP_BTN_UP ? -1 : 1);
    paint();
}
