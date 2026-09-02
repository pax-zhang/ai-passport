#include "app_ota.h"

#include "app_ota_logic.h"
#include "app_net.h"
#include "app_web.h"
#include "bsp_battery.h"
#include "bsp_ble.h"
#include "bsp_wifi.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "ota";

#define NVS_NS   "app"
#define NVS_SKIP "ota_skip"
#define JSON_MAX 1536
#define HTTP_BUF 1024
#define HDR_BUF  2048
#define LOC_MAX  2048
#define MIN_SOC  20
#define SLOT_MAX 0x3F0000u
#define GET_TO_MS 12000
#define SLOW_MS   12000
#define SLOW_MIN  16384

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
static bool s_ble_suspended;

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
    esp_err_t e = nvs_set_str(h, NVS_SKIP, s_skip);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "save skip: %s", esp_err_to_name(e));
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

static char s_loc[LOC_MAX];
static char s_hop_url[LOC_MAX];

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER &&
        e->header_key && e->header_value &&
        strcasecmp(e->header_key, "location") == 0) {
        strlcpy(s_loc, e->header_value, sizeof(s_loc));
    }
    return ESP_OK;
}

static void http_cfg(esp_http_client_config_t *cfg, const char *url, int timeout_ms)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->url = url;
    cfg->timeout_ms = timeout_ms;
    cfg->buffer_size = HDR_BUF;
    cfg->buffer_size_tx = 512;
    cfg->crt_bundle_attach = esp_crt_bundle_attach;
    cfg->keep_alive_enable = false;
    cfg->event_handler = http_evt;
}

static bool http_redirect(int status)
{
    return status == 301 || status == 302 || status == 303
        || status == 307 || status == 308;
}

static bool is_gh_rel(const char *url)
{
    return url && strncmp(url, "https://github.com/", 19) == 0 &&
           strstr(url, "/releases/download/");
}

static bool add_try(const char **list, int *n, int cap, const char *url)
{
    int i;

    if (!url || !url[0] || *n >= cap) return false;
    for (i = 0; i < *n; i++) {
        if (strcmp(list[i], url) == 0) return false;
    }
    list[(*n)++] = url;
    return true;
}

/* 检查已经走 jsDelivr。bin 放在同目录后安装也能直连，不经 GitHub。 */
static bool jsdelivr_bin(char *out, size_t n, const char *url)
{
    const char *name = strrchr(url, '/');
    int w;

    if (!name || !name[1]) return false;
    w = snprintf(out, n,
                 "https://cdn.jsdelivr.net/gh/pax-zhang/ai-passport@"
                 APP_OTA_REF "/ota/" APP_OTA_CHANNEL "/%s", name + 1);
    return w > 0 && w < (int)n;
}

static void http_drop(esp_http_client_handle_t *cli)
{
    if (!cli || !*cli) return;
    esp_http_client_close(*cli);
    esp_http_client_cleanup(*cli);
    *cli = NULL;
}

/* 每跳新建 client。GitHub Location 很长,复用 set_redirection 容易失败。 */
static bool http_open_get(esp_http_client_handle_t *cli, const char *start)
{
    strlcpy(s_hop_url, start, sizeof(s_hop_url));

    for (int hop = 0; hop < 6; hop++) {
        esp_http_client_config_t cfg;

        http_drop(cli);
        s_loc[0] = 0;
        http_cfg(&cfg, s_hop_url, GET_TO_MS);
        *cli = esp_http_client_init(&cfg);
        if (!*cli) return false;
        esp_http_client_set_header(*cli, "User-Agent", "FoloToy-AI-Passport");
        if (esp_http_client_open(*cli, 0) != ESP_OK) {
            ESP_LOGW(TAG, "open fail hop=%d", hop);
            http_drop(cli);
            return false;
        }
        if (esp_http_client_fetch_headers(*cli) < 0) {
            ESP_LOGW(TAG, "hdr fail hop=%d", hop);
            http_drop(cli);
            return false;
        }
        int status = esp_http_client_get_status_code(*cli);
        ESP_LOGI(TAG, "HTTP %d hop=%d cl=%d", status, hop,
                 (int)esp_http_client_get_content_length(*cli));
        if (status == 200) return true;
        if (!http_redirect(status) || strncmp(s_loc, "https://", 8) != 0) {
            http_drop(cli);
            return false;
        }
        strlcpy(s_hop_url, s_loc, sizeof(s_hop_url));
    }
    http_drop(cli);
    ESP_LOGW(TAG, "too many redirects");
    return false;
}

static int fetch_json(const char *url, char *out, size_t n)
{
    esp_http_client_config_t cfg;
    esp_http_client_handle_t cli;
    int got = 0, r, status;

    http_cfg(&cfg, url, 20000);
    cli = esp_http_client_init(&cfg);
    if (!cli) return -1;
    esp_http_client_set_header(cli, "User-Agent", "FoloToy-AI-Passport");
    if (esp_http_client_open(cli, 0) != ESP_OK) {
        ESP_LOGW(TAG, "manifest open %s", url);
        esp_http_client_cleanup(cli);
        return -1;
    }
    if (esp_http_client_fetch_headers(cli) < 0) {
        ESP_LOGW(TAG, "manifest hdr %s", url);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return -1;
    }
    status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGW(TAG, "manifest HTTP %d %s", status, url);
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
    n = fetch_json(APP_OTA_MANIFEST_URL, json, sizeof(json));
    if (n <= 0) n = fetch_json(APP_OTA_MANIFEST_URL_ALT, json, sizeof(json));
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

/* 1 = 换源再试, 0 = 已 set_fail 或已重启 */
static int apply_one(const char *url, const esp_partition_t *part)
{
    esp_http_client_handle_t cli = NULL;
    esp_ota_handle_t ota = 0;
    mbedtls_sha256_context sha;
    uint8_t buf[HTTP_BUF];
    uint8_t digest[32];
    int n, got = 0;
    int64_t content = -1;
    int64_t t0;
    bool began = false;
    bool hashed = false;

    ESP_LOGI(TAG, "get %s", url);
    if (!http_open_get(&cli, url)) return 1;
    s_prog = 1;
    content = esp_http_client_get_content_length(cli);
    if (content > (int64_t)SLOT_MAX || (s_man.size && content > 0 &&
                                        (uint32_t)content != s_man.size)) {
        ESP_LOGW(TAG, "bad length %d", (int)content);
        http_drop(&cli);
        return 1;
    }
    if (esp_ota_begin(part, content > 0 ? (size_t)content : OTA_WITH_SEQUENTIAL_WRITES,
                      &ota) != ESP_OK) {
        set_fail(APP_OTA_E_NET);
        http_drop(&cli);
        return 0;
    }
    began = true;
    s_prog = 2;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    hashed = true;
    t0 = esp_timer_get_time();

    while (1) {
        if (s_cancel) {
            set_fail(APP_OTA_E_CANCEL);
            goto fail;
        }
        n = esp_http_client_read(cli, (char *)buf, sizeof(buf));
        if (n < 0) {
            ESP_LOGW(TAG, "read fail at %d", got);
            goto next;
        }
        if (n == 0) break;
        if ((uint32_t)(got + n) > SLOT_MAX) {
            set_fail(APP_OTA_E_PARSE);
            goto fail;
        }
        if (esp_ota_write(ota, buf, (size_t)n) != ESP_OK) {
            set_fail(APP_OTA_E_NET);
            goto fail;
        }
        mbedtls_sha256_update(&sha, buf, (size_t)n);
        got += n;
        if (got < SLOW_MIN &&
            (esp_timer_get_time() - t0) / 1000 > SLOW_MS) {
            ESP_LOGW(TAG, "slow %d B, next", got);
            goto next;
        }
        if (content > 0) s_prog = (int)((int64_t)got * 100 / content);
        else if (s_man.size) s_prog = got * 100 / (int)s_man.size;
        else s_prog = 0;
        if (s_prog > 99) s_prog = 99;
        vTaskDelay(1);
    }

    if (s_man.size && (uint32_t)got != s_man.size) {
        ESP_LOGW(TAG, "size %d != %u", got, (unsigned)s_man.size);
        goto next;
    }
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    hashed = false;
    if (!app_ota_sha_match(s_man.sha256, digest)) {
        ESP_LOGW(TAG, "hash mismatch, next");
        goto next;
    }
    if (esp_ota_end(ota) != ESP_OK) {
        began = false;
        set_fail(APP_OTA_E_HASH);
        goto fail;
    }
    began = false;
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        set_fail(APP_OTA_E_NET);
        goto fail;
    }
    ESP_LOGI(TAG, "reboot to %s", s_man.version);
    http_drop(&cli);
    bsp_wifi_ps_release();
    esp_restart();
    return 0;

next:
    if (hashed) mbedtls_sha256_free(&sha);
    if (began) esp_ota_abort(ota);
    http_drop(&cli);
    return 1;

fail:
    if (hashed) mbedtls_sha256_free(&sha);
    if (began) esp_ota_abort(ota);
    http_drop(&cli);
    return 0;
}

static void do_apply(void)
{
    const esp_partition_t *part;
    char cdn[APP_OTA_URL_MAX];
    char m1[APP_OTA_URL_MAX + 32];
    char m2[APP_OTA_URL_MAX + 32];
    char m3[APP_OTA_URL_MAX + 32];
    const char *try_url[6];
    int tries = 0, i;

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

    if (jsdelivr_bin(cdn, sizeof(cdn), s_man.url)) add_try(try_url, &tries, 6, cdn);
    if (is_gh_rel(s_man.url)) {
        snprintf(m1, sizeof(m1), "https://gh-proxy.com/%s", s_man.url);
        snprintf(m2, sizeof(m2), "https://github.akams.cn/%s", s_man.url);
        snprintf(m3, sizeof(m3), "https://ghfast.top/%s", s_man.url);
        add_try(try_url, &tries, 6, m1);
        add_try(try_url, &tries, 6, m2);
        add_try(try_url, &tries, 6, m3);
    }
    add_try(try_url, &tries, 6, s_man.url);

    bsp_wifi_ps_hold();
    for (i = 0; i < tries; i++) {
        if (apply_one(try_url[i], part) == 0) {
            if (s_st != APP_OTA_APPLYING) bsp_wifi_ps_release();
            return;
        }
        s_prog = 0;
    }
    set_fail(APP_OTA_E_NET);
    bsp_wifi_ps_release();
}

static void ota_task(void *arg)
{
    (void)arg;
    int job = s_job;
    s_job = 0;
    if (app_net_acquire(APP_NET_OTA, 30000)) {
        if (job == 1) do_check();
        else if (job == 2) do_apply();
        app_net_release(APP_NET_OTA);
    } else {
        set_fail(APP_OTA_E_NET);
    }
    ESP_LOGI(TAG, "task done stack=%u",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    if (s_ble_suspended) {
        s_ble_suspended = false;
        vTaskDelay(pdMS_TO_TICKS(200));
        bsp_ble_resume();
    }
    app_web_suspend_for_ota(false);
    s_task = NULL;
    vTaskDelete(NULL);
}

static void kick(int job)
{
    if (s_task) return;
    app_web_suspend_for_ota(true);
    bool drop_ble = (job == 2) || (bsp_ble_conn_count() == 0);
    if (drop_ble && bsp_ble_stack_up()) {
        if (bsp_ble_suspend() != ESP_OK) {
            app_web_suspend_for_ota(false);
            set_fail(APP_OTA_E_NET);
            return;
        }
        s_ble_suspended = true;
    }
    s_job = job;
    s_st = (job == 2) ? APP_OTA_APPLYING : APP_OTA_CHECKING;
    s_err = APP_OTA_E_NONE;
    if (!app_net_heap_ready(18 * 1024) ||
        xTaskCreate(ota_task, "ota", 12288, NULL, 4, &s_task) != pdPASS) {
        s_task = NULL;
    }
    if (!s_task) {
        s_job = 0;
        s_st = APP_OTA_IDLE;
        if (s_ble_suspended) {
            s_ble_suspended = false;
            bsp_ble_resume();
        }
        app_web_suspend_for_ota(false);
        set_fail(APP_OTA_E_NET);
        return;
    }
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
    if (s_task || s_st == APP_OTA_CHECKING || s_st == APP_OTA_APPLYING) return;
    if (s_checked || !allow_auto) return;
    if (!wifi_ok()) return;
    if (bsp_wifi_ap_active() || app_web_qr_visible()) return;
    if (bsp_ble_conn_count() > 0) return;
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
    return s_task != NULL || s_st == APP_OTA_CHECKING || s_st == APP_OTA_APPLYING;
}

bool app_ota_prompt(void)
{
    if (s_st != APP_OTA_AVAILABLE) return false;
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
