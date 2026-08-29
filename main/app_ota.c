#include "app_ota.h"

#include "app_ota_logic.h"
#include "bsp_battery.h"
#include "bsp_wifi.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <string.h>

static const char *TAG = "ota";

#define NVS_NS   "app"
#define NVS_SKIP "ota_skip"
#define JSON_MAX 1536
#define HTTP_BUF 1024
#define MIN_SOC  20
#define SLOT_MAX 0x3F0000u

static app_ota_state_t s_st;
static app_ota_err_t s_err;
static app_ota_manifest_t s_man;
static char s_cur[APP_OTA_VER_MAX];
static char s_skip[APP_OTA_VER_MAX];
static volatile int s_prog;
static volatile bool s_cancel;
static bool s_checked;
static TaskHandle_t s_task;
static volatile int s_job;

static void load_skip(void)
{
    nvs_handle_t h;
    size_t n = sizeof(s_skip);

    s_skip[0] = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_str(h, NVS_SKIP, s_skip, &n) != ESP_OK) s_skip[0] = 0;
    nvs_close(h);
}

static void save_skip(void)
{
    nvs_handle_t h;

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_SKIP, s_skip);
    nvs_commit(h);
    nvs_close(h);
}

static void set_fail(app_ota_err_t e)
{
    s_err = e;
    s_st = APP_OTA_FAIL;
    s_prog = 0;
}

static bool wifi_ok(void)
{
    return bsp_wifi_enabled() && bsp_wifi_state() == BSP_WIFI_CONNECTED;
}

static bool bat_ok(void)
{
    int soc = bsp_battery_soc();
    return soc < 0 || soc >= MIN_SOC;
}

static void http_cfg(esp_http_client_config_t *cfg, const char *url, int timeout_ms)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->url = url;
    cfg->timeout_ms = timeout_ms;
    cfg->buffer_size = HTTP_BUF;
    cfg->buffer_size_tx = 512;
    cfg->crt_bundle_attach = esp_crt_bundle_attach;
    cfg->keep_alive_enable = false;
}

static int fetch_json(char *out, size_t n)
{
    esp_http_client_config_t cfg;
    esp_http_client_handle_t cli;
    int got = 0, r, status;

    http_cfg(&cfg, APP_OTA_MANIFEST_URL, 15000);
    cli = esp_http_client_init(&cfg);
    if (!cli) return -1;
    esp_http_client_set_header(cli, "User-Agent", "FoloToy-AI-Passport");
    if (esp_http_client_open(cli, 0) != ESP_OK) {
        esp_http_client_cleanup(cli);
        return -1;
    }
    if (esp_http_client_fetch_headers(cli) < 0) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return -1;
    }
    status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGW(TAG, "manifest HTTP %d", status);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return -1;
    }
    while (got + 1 < (int)n) {
        r = esp_http_client_read(cli, out + got, (int)n - 1 - got);
        if (r < 0) {
            got = -1;
            break;
        }
        if (r == 0) break;
        got += r;
    }
    if (got >= 0) out[got] = 0;
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    return got;
}

static void do_check(void)
{
    char json[JSON_MAX];
    int n;

    s_st = APP_OTA_CHECKING;
    s_err = APP_OTA_E_NONE;
    if (!wifi_ok()) {
        set_fail(APP_OTA_E_WIFI);
        return;
    }
    bsp_wifi_ps_hold();
    n = fetch_json(json, sizeof(json));
    bsp_wifi_ps_release();
    if (n <= 0) {
        set_fail(APP_OTA_E_NET);
        return;
    }
    if (!app_ota_parse_manifest(json, &s_man) ||
        !app_ota_channel_ok(s_man.channel, APP_OTA_CHANNEL)) {
        set_fail(APP_OTA_E_PARSE);
        return;
    }
    if (!app_ota_is_newer(s_cur, s_man.version)) {
        s_st = APP_OTA_LATEST;
        return;
    }
    s_st = APP_OTA_AVAILABLE;
}

static void do_apply(void)
{
    esp_http_client_config_t cfg;
    esp_http_client_handle_t cli = NULL;
    const esp_partition_t *part;
    esp_ota_handle_t ota = 0;
    mbedtls_sha256_context sha;
    uint8_t buf[HTTP_BUF];
    uint8_t digest[32];
    int status, n, got = 0;
    int64_t content = -1;
    bool began = false;
    bool hashed = false;

    s_st = APP_OTA_APPLYING;
    s_err = APP_OTA_E_NONE;
    s_prog = 0;
    s_cancel = false;

    if (!wifi_ok()) {
        set_fail(APP_OTA_E_WIFI);
        return;
    }
    if (!bat_ok()) {
        set_fail(APP_OTA_E_LOWBAT);
        return;
    }
    if (!s_man.url[0] || !app_ota_channel_ok(s_man.channel, APP_OTA_CHANNEL) ||
        !app_ota_is_newer(s_cur, s_man.version)) {
        set_fail(APP_OTA_E_PARSE);
        return;
    }

    part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        set_fail(APP_OTA_E_PARSE);
        return;
    }

    bsp_wifi_ps_hold();
    http_cfg(&cfg, s_man.url, 60000);
    cli = esp_http_client_init(&cfg);
    if (!cli) {
        bsp_wifi_ps_release();
        set_fail(APP_OTA_E_NET);
        return;
    }
    esp_http_client_set_header(cli, "User-Agent", "FoloToy-AI-Passport");
    if (esp_http_client_open(cli, 0) != ESP_OK) {
        set_fail(APP_OTA_E_NET);
        goto done;
    }
    if (esp_http_client_fetch_headers(cli) < 0) {
        set_fail(APP_OTA_E_NET);
        goto done;
    }
    status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGW(TAG, "image HTTP %d", status);
        set_fail(APP_OTA_E_NET);
        goto done;
    }
    content = esp_http_client_get_content_length(cli);
    if (content > (int64_t)SLOT_MAX || (s_man.size && content > 0 &&
                                        (uint32_t)content != s_man.size)) {
        set_fail(APP_OTA_E_PARSE);
        goto done;
    }
    if (esp_ota_begin(part, content > 0 ? (size_t)content : OTA_WITH_SEQUENTIAL_WRITES,
                      &ota) != ESP_OK) {
        set_fail(APP_OTA_E_NET);
        goto done;
    }
    began = true;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    hashed = true;

    while (1) {
        if (s_cancel) {
            set_fail(APP_OTA_E_CANCEL);
            goto done;
        }
        n = esp_http_client_read(cli, (char *)buf, sizeof(buf));
        if (n < 0) {
            set_fail(APP_OTA_E_NET);
            goto done;
        }
        if (n == 0) break;
        if ((uint32_t)(got + n) > SLOT_MAX) {
            set_fail(APP_OTA_E_PARSE);
            goto done;
        }
        if (esp_ota_write(ota, buf, (size_t)n) != ESP_OK) {
            set_fail(APP_OTA_E_NET);
            goto done;
        }
        mbedtls_sha256_update(&sha, buf, (size_t)n);
        got += n;
        if (content > 0) s_prog = (int)((int64_t)got * 100 / content);
        else if (s_man.size) s_prog = got * 100 / (int)s_man.size;
        else s_prog = 0;
        if (s_prog > 99) s_prog = 99;
    }

    if (s_man.size && (uint32_t)got != s_man.size) {
        set_fail(APP_OTA_E_HASH);
        goto done;
    }
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    hashed = false;
    if (!app_ota_sha_match(s_man.sha256, digest)) {
        set_fail(APP_OTA_E_HASH);
        goto done;
    }
    if (esp_ota_end(ota) != ESP_OK) {
        began = false;
        set_fail(APP_OTA_E_HASH);
        goto done;
    }
    began = false;
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        set_fail(APP_OTA_E_NET);
        goto done;
    }
    ESP_LOGI(TAG, "reboot to %s", s_man.version);
    bsp_wifi_ps_release();
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    esp_restart();
    return;

done:
    if (hashed) mbedtls_sha256_free(&sha);
    if (began) esp_ota_abort(ota);
    if (cli) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
    }
    bsp_wifi_ps_release();
}

static void ota_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int job = s_job;
        s_job = 0;
        if (job == 1) do_check();
        else if (job == 2) do_apply();
    }
}

static void kick(int job)
{
    if (!s_task) xTaskCreate(ota_task, "ota", 10240, NULL, 4, &s_task);
    if (!s_task) {
        set_fail(APP_OTA_E_NET);
        return;
    }
    s_job = job;
    xTaskNotifyGive(s_task);
}

void app_ota_init(void)
{
    const esp_app_desc_t *d = esp_app_get_description();

    s_cur[0] = 0;
    if (d && d->version[0]) {
        strncpy(s_cur, d->version, sizeof(s_cur) - 1);
        s_cur[sizeof(s_cur) - 1] = 0;
    }
    load_skip();
    s_st = APP_OTA_IDLE;
    s_err = APP_OTA_E_NONE;
    esp_ota_mark_app_valid_cancel_rollback();
}

void app_ota_tick(bool allow_auto)
{
    if (s_st == APP_OTA_CHECKING || s_st == APP_OTA_APPLYING) return;
    if (s_checked || !allow_auto) return;
    if (!wifi_ok()) return;
    s_checked = true;
    kick(1);
}

app_ota_state_t app_ota_state(void)
{
    return s_st;
}

app_ota_err_t app_ota_err(void)
{
    return s_err;
}

const char *app_ota_cur_ver(void)
{
    return s_cur[0] ? s_cur : "0.0.0";
}

const char *app_ota_new_ver(void)
{
    return s_man.version;
}

const char *app_ota_channel(void)
{
    return APP_OTA_CHANNEL;
}

int app_ota_progress(void)
{
    return s_prog;
}

bool app_ota_busy(void)
{
    return s_st == APP_OTA_CHECKING || s_st == APP_OTA_APPLYING;
}

bool app_ota_prompt(void)
{
    if (s_st != APP_OTA_AVAILABLE) return false;
    if (!wifi_ok()) return false;
    if (s_skip[0] && strcmp(s_skip, s_man.version) == 0) return false;
    return true;
}

void app_ota_check(void)
{
    if (app_ota_busy()) return;
    bsp_wifi_radio_resume();
    s_checked = true;
    kick(1);
}

void app_ota_apply(void)
{
    if (app_ota_busy()) return;
    if (s_st != APP_OTA_AVAILABLE) return;
    bsp_wifi_radio_resume();
    kick(2);
}

void app_ota_skip(void)
{
    if (s_st != APP_OTA_AVAILABLE || !s_man.version[0]) return;
    strncpy(s_skip, s_man.version, sizeof(s_skip) - 1);
    s_skip[sizeof(s_skip) - 1] = 0;
    save_skip();
}

void app_ota_cancel(void)
{
    if (s_st == APP_OTA_APPLYING) s_cancel = true;
}
