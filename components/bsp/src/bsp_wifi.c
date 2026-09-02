#include "bsp_wifi.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "bsp_wifi";

#define NVS_NS    "bsp_wifi"
#define NVS_SSID  "ssid"
#define NVS_PASS  "pass"
#define NVS_EN    "sta_on"
#define NVS_AUTO  "auto"
#define AUTH_FAIL_LIMIT 3

static bool s_inited;
static volatile bsp_wifi_state_t s_state;
static volatile bool s_scan_active;
static volatile bool s_scan_cancel;
static volatile bool s_sta_up;
static volatile bool s_want_connect;
static bool s_enabled = false;
static bool s_auto = true;
static bool s_started;
static bool s_ps = true;
static int s_ps_hold;
static bool s_radio_held;
static bool s_resume_connect;
static bool s_ap_on;
static char s_ap_ssid[BSP_WIFI_AP_SSID_MAX + 1];
static char s_ssid[BSP_WIFI_SSID_MAX + 1];
static char s_pass[BSP_WIFI_PASS_MAX + 1];
static SemaphoreHandle_t s_mu;
static SemaphoreHandle_t s_scan_mu;
static int s_auth_fails;

static void lock(void) {
    if (s_mu) xSemaphoreTake(s_mu, portMAX_DELAY);
}

static void unlock(void) {
    if (s_mu) xSemaphoreGive(s_mu);
}

static bool should_retry(uint8_t r) {
    if (r == WIFI_REASON_NO_AP_FOUND) return false;
    // 认证失败停手,避免用错密码把路由器锁住;信号丢失则继续自动连。
    if (r == WIFI_REASON_AUTH_FAIL
        || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
        || r == WIFI_REASON_HANDSHAKE_TIMEOUT
        || r == WIFI_REASON_802_1X_AUTH_FAILED) {
        return false;
    }
    return true;
}

static bool load_creds(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = sizeof(s_ssid), pl = sizeof(s_pass);
    bool ok = nvs_get_str(h, NVS_SSID, s_ssid, &sl) == ESP_OK && s_ssid[0];
    if (ok) {
        if (nvs_get_str(h, NVS_PASS, s_pass, &pl) != ESP_OK) s_pass[0] = 0;
    }
    nvs_close(h);
    return ok;
}

static void save_creds(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "打开 NVS 失败,凭据未保存");
        return;
    }
    char ssid[BSP_WIFI_SSID_MAX + 1] = { 0 };
    char pass[BSP_WIFI_PASS_MAX + 1] = { 0 };
    size_t sn = sizeof(ssid), pn = sizeof(pass);
    bool same = nvs_get_str(h, NVS_SSID, ssid, &sn) == ESP_OK &&
                nvs_get_str(h, NVS_PASS, pass, &pn) == ESP_OK &&
                strcmp(ssid, s_ssid) == 0 && strcmp(pass, s_pass) == 0;
    esp_err_t e = ESP_OK;
    if (!same) {
        e = nvs_set_str(h, NVS_SSID, s_ssid);
        if (e == ESP_OK) e = nvs_set_str(h, NVS_PASS, s_pass);
        if (e == ESP_OK) e = nvs_commit(h);
    }
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "保存凭据失败: %s", esp_err_to_name(e));
    else if (!same) ESP_LOGI(TAG, "已保存 SSID '%s'", s_ssid);
}

static void erase_creds(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t e = nvs_erase_key(h, NVS_SSID);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    esp_err_t pe = nvs_erase_key(h, NVS_PASS);
    if (pe == ESP_ERR_NVS_NOT_FOUND) pe = ESP_OK;
    if (e == ESP_OK) e = pe;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "删除凭据失败: %s", esp_err_to_name(e));
}

static void load_flags(void) {
    nvs_handle_t h;
    s_enabled = false;
    s_auto = true;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, NVS_EN, &v) == ESP_OK) s_enabled = v != 0;
    if (nvs_get_u8(h, NVS_AUTO, &v) == ESP_OK) s_auto = v != 0;
    nvs_close(h);
}

static void save_flags(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t e = nvs_set_u8(h, NVS_EN, s_enabled ? 1 : 0);
    if (e == ESP_OK) e = nvs_set_u8(h, NVS_AUTO, s_auto ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "保存标志失败: %s", esp_err_to_name(e));
}

static void apply_ps(void) {
    if (!s_started) return;
    bool none = !s_ps || s_ps_hold > 0 || s_ap_on;
    esp_err_t e = esp_wifi_set_ps(none ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    if (e != ESP_OK) ESP_LOGW(TAG, "set_ps 失败: %s", esp_err_to_name(e));
}

static void apply_mode(void)
{
    wifi_mode_t mode = WIFI_MODE_STA;
    if (s_ap_on) mode = s_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    esp_err_t e = esp_wifi_set_mode(mode);
    if (e != ESP_OK) ESP_LOGW(TAG, "set_mode 失败: %s", esp_err_to_name(e));
}

static esp_err_t apply_sta_config(void) {
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, s_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (e != ESP_OK) ESP_LOGE(TAG, "set_config 失败: %s", esp_err_to_name(e));
    return e;
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_scan_active) return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_sta_up = true;
        if (s_want_connect) {
            s_state = BSP_WIFI_CONNECTING;
            esp_wifi_connect();
        }
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        s_sta_up = false;
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        uint8_t reason = ev ? ev->reason : 0;
        ESP_LOGW(TAG, "断开 reason=%u ssid='%s'", reason, s_ssid);
        if (!s_enabled || !s_want_connect) {
            if (s_state != BSP_WIFI_CONNECTING) s_state = BSP_WIFI_IDLE;
            return;
        }
        bool was_up = (s_state == BSP_WIFI_CONNECTED);
        if (was_up && !s_auto) {
            s_want_connect = false;
            s_state = BSP_WIFI_IDLE;
            return;
        }
        if (!should_retry(reason)) {
            if (++s_auth_fails >= AUTH_FAIL_LIMIT) {
                ESP_LOGE(TAG, "认证失败次数过多,停止自动重连");
                s_want_connect = false;
                s_state = BSP_WIFI_FAILED;
                return;
            }
        }
        s_state = BSP_WIFI_CONNECTING;
        esp_wifi_connect();
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "已连接 SSID='%s' ip=" IPSTR,
                 s_ssid, IP2STR(&ev->ip_info.ip));
        s_auth_fails = 0;
        s_state = BSP_WIFI_CONNECTED;
        lock();
        save_creds();
        unlock();
    }
}

esp_err_t bsp_wifi_init(void) {
    if (s_inited) return ESP_OK;

    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
        if (!s_mu) return ESP_ERR_NO_MEM;
    }
    if (!s_scan_mu) {
        s_scan_mu = xSemaphoreCreateMutex();
        if (!s_scan_mu) return ESP_ERR_NO_MEM;
    }

    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    if (!esp_netif_create_default_wifi_sta()) {
        ESP_LOGE(TAG, "创建 STA netif 失败");
        return ESP_FAIL;
    }
    if (!esp_netif_create_default_wifi_ap()) {
        ESP_LOGE(TAG, "创建 AP netif 失败");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init 失败: %s", esp_err_to_name(e));
        return e;
    }

    uint8_t mac[6] = { 0 };
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Passport-%02X%02X", mac[4], mac[5]);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);

    lock();
    load_flags();
    bool saved = load_creds();
    unlock();
    if (s_enabled && s_auto && saved) {
        ESP_LOGI(TAG, "发现已存 SSID '%s',将自动连接", s_ssid);
        s_want_connect = true;
        s_state = BSP_WIFI_CONNECTING;
        apply_sta_config();
    } else {
        s_state = BSP_WIFI_IDLE;
    }

    if (s_enabled) {
        e = esp_wifi_start();
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start 失败: %s", esp_err_to_name(e));
            return e;
        }
        s_started = true;
        apply_ps();
    }

    s_inited = true;
    ESP_LOGI(TAG, "WiFi STA 就绪,free heap=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    return ESP_OK;
}

bsp_wifi_state_t bsp_wifi_state(void) { return s_state; }

const char *bsp_wifi_ssid(void) { return s_ssid; }

esp_err_t bsp_wifi_ip(char *buf, size_t n) {
    if (!buf || n < 8) return ESP_ERR_INVALID_ARG;
    strcpy(buf, "0.0.0.0");
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return ESP_OK;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return ESP_OK;
    snprintf(buf, n, IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}

static void copy_ssid(char *dst, const uint8_t *src) {
    memcpy(dst, src, BSP_WIFI_SSID_MAX);
    dst[BSP_WIFI_SSID_MAX] = 0;
    // 去掉尾部填充的 0 已经靠上面;若 SSID 含不可打印字符仍原样交给 UI。
}

int bsp_wifi_scan(bsp_wifi_ap_t *out, int max) {
    if (!s_inited || !s_enabled || !out || max <= 0) return -1;
    if (!s_scan_mu || xSemaphoreTake(s_scan_mu, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGW(TAG, "扫描忙");
        return -1;
    }
    int count = -1;
    if (max > BSP_WIFI_SCAN_MAX) max = BSP_WIFI_SCAN_MAX;

    if (!s_started) {
        esp_err_t st = esp_wifi_start();
        if (st != ESP_OK && st != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "扫描前 start 失败: %s", esp_err_to_name(st));
            goto done;
        }
        s_started = true;
        apply_ps();
    }
    for (int i = 0; i < 30 && !s_sta_up; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!s_sta_up) {
        ESP_LOGW(TAG, "扫描时 STA 未就绪");
        goto done;
    }

    /* 连接过程中 scan_start 会失败;已连接时后台同步扫描。 */
    bool restore = s_want_connect;
    bool was_up = (s_state == BSP_WIFI_CONNECTED);
    bool kicked = false;
    s_scan_cancel = false;
    s_scan_active = true;
    if (s_state == BSP_WIFI_CONNECTING) {
        s_want_connect = false;
        esp_wifi_disconnect();
        kicked = true;
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    wifi_scan_config_t cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 0,
        .scan_time.active.max = 120,
    };

    esp_wifi_scan_stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "开始扫描…");
    esp_err_t e = esp_wifi_scan_start(&cfg, true);
    if (e == ESP_ERR_WIFI_STATE) {
        esp_wifi_disconnect();
        kicked = true;
        vTaskDelay(pdMS_TO_TICKS(300));
        e = esp_wifi_scan_start(&cfg, true);
    }
    if (e != ESP_OK || s_scan_cancel) {
        ESP_LOGW(TAG, "scan 失败: %s", esp_err_to_name(e));
        s_want_connect = restore;
        if (restore && s_ssid[0] && s_enabled && (kicked || !was_up)) {
            s_state = BSP_WIFI_CONNECTING;
            apply_sta_config();
            esp_wifi_connect();
        }
        goto done;
    }

    static wifi_ap_record_t raw[BSP_WIFI_SCAN_MAX];
    uint16_t got = sizeof(raw) / sizeof(raw[0]);
    e = esp_wifi_scan_get_ap_records(&got, raw);
    if (e != ESP_OK) {
        s_want_connect = restore;
        if (restore && s_ssid[0] && s_enabled && (kicked || !was_up)) {
            s_state = BSP_WIFI_CONNECTING;
            apply_sta_config();
            esp_wifi_connect();
        }
        goto done;
    }

    count = 0;
    for (uint16_t i = 0; i < got; i++) {
        if (!raw[i].ssid[0]) continue;
        char ssid[BSP_WIFI_SSID_MAX + 1];
        copy_ssid(ssid, raw[i].ssid);
        int dup = -1;
        for (int j = 0; j < count; j++) {
            if (strcmp(out[j].ssid, ssid) == 0) { dup = j; break; }
        }
        if (dup >= 0) {
            if (raw[i].rssi > out[dup].rssi) {
                out[dup].rssi = raw[i].rssi;
                out[dup].open = (raw[i].authmode == WIFI_AUTH_OPEN);
            }
            continue;
        }
        if (count >= max) {
            int weakest = 0;
            for (int j = 1; j < count; j++) {
                if (out[j].rssi < out[weakest].rssi) weakest = j;
            }
            if (raw[i].rssi <= out[weakest].rssi) continue;
            strlcpy(out[weakest].ssid, ssid, sizeof(out[weakest].ssid));
            out[weakest].rssi = raw[i].rssi;
            out[weakest].open = (raw[i].authmode == WIFI_AUTH_OPEN);
            continue;
        }
        strlcpy(out[count].ssid, ssid, sizeof(out[count].ssid));
        out[count].rssi = raw[i].rssi;
        out[count].open = (raw[i].authmode == WIFI_AUTH_OPEN);
        count++;
    }

    for (int i = 1; i < count; i++) {
        bsp_wifi_ap_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < key.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    s_want_connect = restore;
    if (restore && s_enabled && s_ssid[0] && (kicked || !was_up)) {
        s_state = BSP_WIFI_CONNECTING;
        apply_sta_config();
        esp_wifi_connect();
    }

    ESP_LOGI(TAG, "扫描到 %d 个网络", count);
done:
    s_scan_active = false;
    s_scan_cancel = false;
    xSemaphoreGive(s_scan_mu);
    return count;
}

void bsp_wifi_scan_cancel(void)
{
    if (!s_scan_active) return;
    s_scan_cancel = true;
    esp_wifi_scan_stop();
}

esp_err_t bsp_wifi_connect(const char *ssid, const char *password) {
    if (!s_inited || !s_enabled || !ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    if (!password) password = "";
    if (strlen(ssid) > BSP_WIFI_SSID_MAX) return ESP_ERR_INVALID_ARG;
    if (strlen(password) > BSP_WIFI_PASS_MAX) return ESP_ERR_INVALID_ARG;

    lock();
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, password, sizeof(s_pass));
    s_auth_fails = 0;
    s_want_connect = false;
    s_state = BSP_WIFI_CONNECTING;
    unlock();

    if (!s_started) {
        esp_err_t st = esp_wifi_start();
        if (st != ESP_OK && st != ESP_ERR_INVALID_STATE) {
            s_state = BSP_WIFI_FAILED;
            return st;
        }
        s_started = true;
        apply_ps();
    }
    for (int i = 0; i < 30 && !s_sta_up; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!s_sta_up) {
        s_state = BSP_WIFI_FAILED;
        return ESP_ERR_INVALID_STATE;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    lock();
    esp_err_t e = apply_sta_config();
    if (e == ESP_OK) {
        s_want_connect = true;
        s_state = BSP_WIFI_CONNECTING;
    }
    unlock();
    if (e != ESP_OK) {
        s_state = BSP_WIFI_FAILED;
        return e;
    }

    ESP_LOGI(TAG, "开始连接 SSID='%s'", s_ssid);
    e = esp_wifi_connect();
    if (e != ESP_OK && e != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "connect 失败: %s", esp_err_to_name(e));
        s_want_connect = false;
        s_state = BSP_WIFI_FAILED;
        return e;
    }
    return ESP_OK;
}

esp_err_t bsp_wifi_forget(void) {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    lock();
    s_want_connect = false;
    s_ssid[0] = 0;
    s_pass[0] = 0;
    s_state = BSP_WIFI_IDLE;
    erase_creds();
    unlock();
    esp_wifi_disconnect();
    ESP_LOGI(TAG, "已忘记网络");
    return ESP_OK;
}

bool bsp_wifi_has_saved(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = BSP_WIFI_SSID_MAX + 1;
    char tmp[BSP_WIFI_SSID_MAX + 1];
    bool ok = nvs_get_str(h, NVS_SSID, tmp, &sl) == ESP_OK && tmp[0];
    nvs_close(h);
    return ok;
}

bool bsp_wifi_saved_pass(const char *ssid, char *pass, size_t n) {
    if (!ssid || !pass || n == 0) return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    char saved[BSP_WIFI_SSID_MAX + 1];
    size_t sl = sizeof(saved);
    bool ok = nvs_get_str(h, NVS_SSID, saved, &sl) == ESP_OK && strcmp(saved, ssid) == 0;
    if (ok) {
        size_t pl = n;
        ok = nvs_get_str(h, NVS_PASS, pass, &pl) == ESP_OK;
    }
    nvs_close(h);
    if (!ok) pass[0] = 0;
    return ok;
}

bool bsp_wifi_enabled(void) {
    return s_enabled;
}

esp_err_t bsp_wifi_set_enabled(bool on) {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (s_enabled == on) return ESP_OK;

    lock();
    s_enabled = on;
    save_flags();
    unlock();

    if (on) {
        if (s_auto && s_ssid[0]) {
            s_want_connect = true;
            s_auth_fails = 0;
            s_state = BSP_WIFI_CONNECTING;
            apply_sta_config();
        }
        if (s_radio_held) {
            s_resume_connect = s_want_connect;
            ESP_LOGI(TAG, "WiFi 已开启(射频暂停中,按键唤醒后上电)");
            return ESP_OK;
        }
        apply_mode();
        esp_err_t e = esp_wifi_start();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "wifi start: %s", esp_err_to_name(e));
            return e;
        }
        s_started = true;
        apply_ps();
        if (s_want_connect) esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi 已开启");
        return ESP_OK;
    }

    s_want_connect = false;
    s_state = BSP_WIFI_IDLE;
    esp_wifi_disconnect();
    if (s_ap_on) {
        apply_mode();
        ESP_LOGI(TAG, "STA 已关,热点仍开");
        return ESP_OK;
    }
    esp_wifi_stop();
    s_started = false;
    ESP_LOGI(TAG, "WiFi 已关闭");
    return ESP_OK;
}

bool bsp_wifi_auto_connect(void) {
    return s_auto;
}

esp_err_t bsp_wifi_set_auto_connect(bool on) {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (s_auto == on) return ESP_OK;
    lock();
    s_auto = on;
    save_flags();
    unlock();
    if (on && s_enabled && s_started && s_ssid[0] && s_state != BSP_WIFI_CONNECTED) {
        s_want_connect = true;
        s_auth_fails = 0;
        s_state = BSP_WIFI_CONNECTING;
        apply_sta_config();
        esp_wifi_connect();
    }
    return ESP_OK;
}

bool bsp_wifi_power_save(void) {
    return s_ps;
}

esp_err_t bsp_wifi_set_power_save(bool on) {
    s_ps = on;
    apply_ps();
    return ESP_OK;
}

void bsp_wifi_ps_hold(void)
{
    lock();
    s_ps_hold++;
    unlock();
    apply_ps();
}

void bsp_wifi_ps_release(void)
{
    lock();
    if (s_ps_hold > 0) s_ps_hold--;
    unlock();
    apply_ps();
}

esp_err_t bsp_wifi_radio_suspend(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (s_ap_on) return ESP_OK;
    if (s_radio_held) return ESP_OK;
    s_radio_held = true;
    s_resume_connect = s_want_connect;
    if (!s_started) return ESP_OK;
    s_want_connect = false;
    s_state = BSP_WIFI_IDLE;
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_started = false;
    s_sta_up = false;
    ESP_LOGI(TAG, "WiFi 射频暂停(息屏)");
    return ESP_OK;
}

esp_err_t bsp_wifi_radio_resume(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!s_radio_held) return ESP_OK;
    s_radio_held = false;
    if (!s_enabled && !s_ap_on) return ESP_OK;
    if (s_resume_connect && s_ssid[0]) {
        s_want_connect = true;
        s_auth_fails = 0;
        s_state = BSP_WIFI_CONNECTING;
        apply_sta_config();
    }
    apply_mode();
    esp_err_t e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "wifi resume start: %s", esp_err_to_name(e));
        return e;
    }
    s_started = true;
    apply_ps();
    if (s_want_connect) esp_wifi_connect();
    ESP_LOGI(TAG, "WiFi 射频恢复");
    return ESP_OK;
}

esp_err_t bsp_wifi_ap_start(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (s_ap_on && s_started) {
        s_radio_held = false;
        return ESP_OK;
    }
    bool was_held = s_radio_held;
    s_radio_held = false;

    if (!s_ap_ssid[0]) {
        uint8_t mac[6] = { 0 };
        if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
        }
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Passport-%02X%02X", mac[4], mac[5]);
    }

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.beacon_interval = 200;

    s_ap_on = true;
    apply_mode();
    esp_err_t e;
    if (!s_started) {
        e = esp_wifi_start();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
            s_ap_on = false;
            s_radio_held = was_held;
            apply_mode();
            ESP_LOGE(TAG, "AP start: %s largest=%u", esp_err_to_name(e),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            return e;
        }
        s_started = true;
    }
    e = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (e != ESP_OK) {
        s_ap_on = false;
        s_radio_held = was_held;
        apply_mode();
        ESP_LOGE(TAG, "AP set_config: %s", esp_err_to_name(e));
        return e;
    }
    apply_ps();
    ESP_LOGI(TAG, "热点 %s", s_ap_ssid);
    return ESP_OK;
}

esp_err_t bsp_wifi_ap_stop(void)
{
    if (!s_inited || !s_ap_on) return ESP_OK;
    s_ap_on = false;
    apply_mode();
    apply_ps();
    ESP_LOGI(TAG, "热点已关");
    return ESP_OK;
}

bool bsp_wifi_ap_active(void)
{
    return s_ap_on;
}

const char *bsp_wifi_ap_ssid(void)
{
    return s_ap_ssid;
}

esp_err_t bsp_wifi_ap_ip(char *buf, size_t n)
{
    if (!buf || n < 8) return ESP_ERR_INVALID_ARG;
    strcpy(buf, "192.168.4.1");
    if (!s_ap_on) return ESP_OK;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!netif) return ESP_OK;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return ESP_OK;
    if (ip.ip.addr) snprintf(buf, n, IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}
