#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_OTA_VER_MAX 16
#define APP_OTA_URL_MAX 256
#define APP_OTA_SHA_HEX 64
#define APP_OTA_CHANNEL_MAX 48

#ifndef APP_OTA_CHANNEL
#define APP_OTA_CHANNEL "demo/farm"
#endif

#ifndef APP_OTA_MANIFEST_URL
#define APP_OTA_MANIFEST_URL \
    "https://raw.githubusercontent.com/pax-zhang/ai-passport/main/ota/" \
    APP_OTA_CHANNEL "/latest.json"
#endif
/* latest.json 的 url 必须是应用分区镜像。工厂包在 factory_url，设备不下载。 */

typedef struct {
    char channel[APP_OTA_CHANNEL_MAX];
    char version[APP_OTA_VER_MAX];
    char url[APP_OTA_URL_MAX];
    char sha256[APP_OTA_SHA_HEX + 1];
    uint32_t size;
} app_ota_manifest_t;

typedef struct {
    int major;
    int minor;
    int patch;
} app_ota_ver_t;

bool app_ota_parse_ver(const char *s, app_ota_ver_t *out);
int app_ota_cmp_ver(const app_ota_ver_t *a, const app_ota_ver_t *b);
bool app_ota_is_newer(const char *cur, const char *next);
bool app_ota_parse_manifest(const char *json, app_ota_manifest_t *out);
bool app_ota_channel_ok(const char *got, const char *want);
bool app_ota_sha_match(const char *hex, const uint8_t digest[32]);
bool app_ota_url_ok(const char *url);
