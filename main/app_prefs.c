#include "app_prefs.h"

#include "app_i18n.h"
#include "app_notif_rule.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG_NS = "app";
static const char *TAG_RULES = "app_rules";
static const char *TAG_TOTP = "app_totp";
static const char *TAG = "app_prefs";
static app_prefs_t s_p;
static app_prefs_t s_saved;
static app_totp_list_t s_totp;
static bool s_dirty;
static int64_t s_dirty_us;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t item_size;
    uint16_t reserved;
    uint32_t bytes;
    uint32_t crc;
} totp_meta_t;

#define TOTP_MAGIC 0x50544F54u
#define TOTP_VERSION 3

static bool rules_write(void);
static void write_rule_ver(void);

static uint32_t crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = UINT32_MAX;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

typedef struct {
    char name[33];
    char secret[65];
    uint8_t digits;
    uint8_t period;
} totp_v1_t;

static const char *const NTP[] = {
    "pool.ntp.org",
    "time.apple.com",
    "ntp.aliyun.com",
    "time.windows.com",
};

// 出厂默认全部通知都弹窗,规则表留空,不再需要 any:* 兜底。
static void kw_defaults(void)
{
    memset(s_p.kw, 0, sizeof(s_p.kw));
    s_p.kw_n = 0;
    s_p.kw_ver = APP_RULE_VER;
    s_p.notif_def = APP_ALERT_POPUP;
}

static void set_defaults(void)
{
    memset(&s_p, 0, sizeof(s_p));
    s_p.brightness = 50;
    s_p.sleep_sec = 30;
    s_p.lock_on = 1;
    s_p.lock_stay = 0;
    s_p.lock_time = 1;
    s_p.lock_wx = 1;
    s_p.lock_quote = 1;
    s_p.volume = 70;
    s_p.muted = 0;
    s_p.tone_msg = APP_TONE_BEEP;
    s_p.tone_alert = APP_TONE_ALARM;
    s_p.ntp_on = 1;
    s_p.ntp_server = 0;
    s_p.auto_hide = 10;
    s_p.lang = APP_LANG_ZH;
    s_p.theme = UI_ST_GEEK;
    strlcpy(s_p.wx_city, "Shanghai", sizeof(s_p.wx_city));
    s_p.wx_lat_e4 = 312304;
    s_p.wx_lon_e4 = 1214737;
    s_p.wx_interval = 30;
    s_p.wx_imperial = 0;
    s_p.meow_bed = 21;
    s_p.meow_wake = 8;
    s_p.ota_auto = 0;
    kw_defaults();
}

static bool load_rules_store(void)
{
    nvs_handle_t h;
    if (nvs_open(TAG_RULES, NVS_READONLY, &h) != ESP_OK) return false;
    uint16_t count = 0;
    size_t bytes = 0;
    esp_err_t e = nvs_get_u16(h, "count", &count);
    if (e == ESP_ERR_NVS_TYPE_MISMATCH) {
        uint8_t old = 0;
        e = nvs_get_u8(h, "count", &old);
        count = old;
    }
    if (e == ESP_OK && count <= APP_KW_MAX) {
        e = nvs_get_blob(h, "items", NULL, &bytes);
        if (count == 0 && e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
        if (count > 0 && (e != ESP_OK || bytes != count * sizeof(app_kw_t))) {
            e = ESP_ERR_INVALID_SIZE;
        }
        if (e == ESP_OK) {
            memset(s_p.kw, 0, sizeof(s_p.kw));
            if (count > 0) e = nvs_get_blob(h, "items", s_p.kw, &bytes);
            if (e == ESP_OK) s_p.kw_n = count;
        }
    }
    nvs_close(h);
    return e == ESP_OK;
}

static bool load_totp_store(void)
{
    nvs_handle_t h;
    if (nvs_open(TAG_TOTP, NVS_READONLY, &h) != ESP_OK) return false;
    totp_meta_t meta;
    size_t mn = sizeof(meta);
    esp_err_t e = nvs_get_blob(h, "meta", &meta, &mn);
    if (e != ESP_OK || mn != sizeof(meta) || meta.magic != TOTP_MAGIC ||
        meta.version != TOTP_VERSION || meta.item_size != sizeof(app_totp_acct_t) ||
        meta.count > UINT16_MAX / sizeof(app_totp_acct_t) ||
        meta.bytes != (uint32_t)meta.count * sizeof(app_totp_acct_t)) {
        nvs_close(h);
        return false;
    }
    app_totp_list_t list = { 0 };
    if (meta.bytes > 0) {
        size_t bytes = meta.bytes;
        app_totp_acct_t *items = malloc(bytes);
        if (!items || nvs_get_blob(h, "items", items, &bytes) != ESP_OK ||
            bytes != meta.bytes || crc32(items, bytes) != meta.crc) {
            free(items);
            nvs_close(h);
            return false;
        }
        list.items = items;
        list.n = meta.count;
        list.cap = meta.count;
    }
    nvs_close(h);
    app_totp_list_clear(&s_totp);
    s_totp = list;
    app_totp_list_sort(&s_totp);
    return true;
}

void app_prefs_load(void)
{
    set_defaults();
    app_totp_list_clear(&s_totp);
    nvs_handle_t h;
    if (nvs_open(TAG_NS, NVS_READONLY, &h) != ESP_OK) {
        bool rules_ok = load_rules_store();
        bool totp_ok = load_totp_store();
        // 首次开机就落盘版本号,否则下次开机会被当成待迁移的老数据而误静音。
        write_rule_ver();
        s_saved = s_p;
        s_dirty = false;
        s_dirty_us = 0;
        if (!rules_ok) rules_write();
        if (!totp_ok) app_totp_persist();
        app_lang_set((app_lang_t)s_p.lang);
        return;
    }
    uint8_t u8;
    uint16_t u16;
    if (nvs_get_u8(h, "bl", &u8) == ESP_OK && u8 >= 10 && u8 <= 100) s_p.brightness = u8;
    if (nvs_get_u16(h, "sleep", &u16) == ESP_OK) s_p.sleep_sec = u16;
    if (nvs_get_u8(h, "lck", &u8) == ESP_OK) s_p.lock_on = u8 != 0;
    if (nvs_get_u8(h, "lstay", &u8) == ESP_OK) s_p.lock_stay = u8 != 0;
    if (nvs_get_u8(h, "ltm", &u8) == ESP_OK) s_p.lock_time = u8 != 0;
    if (nvs_get_u8(h, "lwx", &u8) == ESP_OK) s_p.lock_wx = u8 != 0;
    if (nvs_get_u8(h, "lq", &u8) == ESP_OK) s_p.lock_quote = u8 != 0;
    if (nvs_get_u8(h, "vol", &u8) == ESP_OK && u8 <= 100) s_p.volume = u8;
    if (nvs_get_u8(h, "mute", &u8) == ESP_OK) s_p.muted = u8 != 0;
    if (nvs_get_u8(h, "tmsg", &u8) == ESP_OK) s_p.tone_msg = u8;
    if (nvs_get_u8(h, "talert", &u8) == ESP_OK) s_p.tone_alert = u8;
    if (nvs_get_u8(h, "ntp", &u8) == ESP_OK) s_p.ntp_on = u8 != 0;
    if (nvs_get_u8(h, "ntps", &u8) == ESP_OK && u8 < APP_NTP_SERVER_N) s_p.ntp_server = u8;
    if (nvs_get_u8(h, "hide", &u8) == ESP_OK) s_p.auto_hide = u8;
    if (nvs_get_u8(h, "lang", &u8) == ESP_OK && u8 < APP_LANG_N) s_p.lang = u8;
    {
        size_t cn = sizeof(s_p.wx_city);
        if (nvs_get_str(h, "wxc", s_p.wx_city, &cn) != ESP_OK) {
            /* keep default */
        }
        int32_t i32;
        if (nvs_get_i32(h, "wxlat", &i32) == ESP_OK) s_p.wx_lat_e4 = i32;
        if (nvs_get_i32(h, "wxlon", &i32) == ESP_OK) s_p.wx_lon_e4 = i32;
        if (nvs_get_u16(h, "wxiv", &u16) == ESP_OK &&
            (u16 == 15 || u16 == 30 || u16 == 60 || u16 == 180)) {
            s_p.wx_interval = u16;
        }
        if (nvs_get_u8(h, "wxu", &u8) == ESP_OK) s_p.wx_imperial = u8 != 0;
        if (nvs_get_u8(h, "mbed", &u8) == ESP_OK && u8 <= 23) s_p.meow_bed = u8;
        if (nvs_get_u8(h, "mwake", &u8) == ESP_OK && u8 <= 23) s_p.meow_wake = u8;
        if (nvs_get_u8(h, "otaa", &u8) == ESP_OK) s_p.ota_auto = u8 != 0;
        if (nvs_get_u8(h, "ndef", &u8) == ESP_OK && u8 <= APP_ALERT_DROP) {
            s_p.notif_def = u8;
        }
        if (nvs_get_u8(h, "thm", &u8) == ESP_OK && u8 < UI_THEME_N) s_p.theme = u8;
    }
    uint8_t kw_ver = 0;
    nvs_get_u8(h, "kwv", &kw_ver);
    size_t n = 0;
    uint8_t kn = 0;
    if (nvs_get_u8(h, "kwn", &kn) == ESP_OK) {
        if (kn > APP_KW_MAX) kn = APP_KW_MAX;
        s_p.kw_n = kn;
        memset(s_p.kw, 0, sizeof(s_p.kw));
        if (kn > 0 && nvs_get_blob(h, "kw", NULL, &n) == ESP_OK && n > 0) {
            if (n == sizeof(s_p.kw)) {
                nvs_get_blob(h, "kw", s_p.kw, &n);
            } else {
                uint8_t raw[640];
                size_t rn = sizeof(raw);
                if (nvs_get_blob(h, "kw", raw, &rn) == ESP_OK && rn >= kn) {
                    size_t rec = rn / APP_KW_MAX;
                    if (rec >= 2) {
                        for (uint8_t i = 0; i < kn; i++) {
                            const uint8_t *row = raw + (size_t)i * rec;
                            size_t tlen = rec - 1;
                            if (tlen > APP_KW_LEN) tlen = APP_KW_LEN;
                            memcpy(s_p.kw[i].text, row, tlen);
                            s_p.kw[i].text[tlen] = 0;
                            s_p.kw[i].prio = row[rec - 1];
                        }
                    }
                }
            }
        }
    }
    {
        uint8_t ver = 0;
        nvs_get_u8(h, "totpv", &ver);
        if (ver == 2) {
            uint16_t kn = 0;
            esp_err_t ke = nvs_get_u16(h, "totpn", &kn);
            if (ke == ESP_ERR_NVS_TYPE_MISMATCH) {
                uint8_t old = 0;
                if (nvs_get_u8(h, "totpn", &old) == ESP_OK) kn = old;
            }
            size_t tn = 0;
            esp_err_t e = nvs_get_blob(h, "totp", NULL, &tn);
            if (kn && e == ESP_OK && tn >= sizeof(app_totp_acct_t)) {
                app_totp_acct_t *raw = malloc(tn);
                if (raw && nvs_get_blob(h, "totp", raw, &tn) == ESP_OK) {
                    uint16_t rec = (uint16_t)(tn / sizeof(app_totp_acct_t));
                    if (kn < rec) rec = kn;
                    for (uint16_t i = 0; i < rec; i++) {
                        app_totp_list_add(&s_totp, &raw[i]);
                    }
                }
                free(raw);
            }
        } else {
            uint8_t kn = 0;
            nvs_get_u8(h, "totpn", &kn);
            totp_v1_t old[8];
            size_t tn = sizeof(old);
            if (kn && nvs_get_blob(h, "totp", old, &tn) == ESP_OK) {
                uint16_t rec = (uint16_t)(tn / sizeof(totp_v1_t));
                if (kn < rec) rec = kn;
                for (uint16_t i = 0; i < rec; i++) {
                    app_totp_acct_t a;
                    memset(&a, 0, sizeof(a));
                    app_totp_split_name(old[i].name, a.issuer, sizeof(a.issuer),
                                        a.label, sizeof(a.label));
                    if (!a.issuer[0] && old[i].name[0]) {
                        size_t k = 0;
                        while (old[i].name[k] && k + 1 < sizeof(a.issuer)) {
                            a.issuer[k] = old[i].name[k];
                            k++;
                        }
                        a.issuer[k] = 0;
                    }
                    strncpy(a.secret, old[i].secret, sizeof(a.secret) - 1);
                    a.digits = old[i].digits;
                    a.period = old[i].period;
                    app_totp_list_add(&s_totp, &a);
                }
            }
        }
        app_totp_list_sort(&s_totp);
    }
    nvs_close(h);
    bool rules_ok = load_rules_store();
    bool totp_ok = load_totp_store();
    bool migrated = kw_ver < APP_RULE_VER;
    if (migrated) {
        app_notif_rules_upgrade(s_p.kw, s_p.kw_n, &s_p.notif_def);
        s_p.kw_ver = APP_RULE_VER;
        rules_ok = false;
        write_rule_ver();
    }
    s_saved = s_p;
    s_dirty = false;
    s_dirty_us = 0;
    if (!rules_ok) rules_write();
    if (!totp_ok) app_totp_persist();
    app_lang_set((app_lang_t)s_p.lang);
}

// 迁移路径独立写这两个键:此时 s_saved 还是旧值,走不了 prefs_write 的差分。
static void write_rule_ver(void)
{
    nvs_handle_t h;
    if (nvs_open(TAG_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "kwv", s_p.kw_ver);
    nvs_set_u8(h, "ndef", s_p.notif_def);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "rules upgraded to v%d, default alert=%d", APP_RULE_VER,
             s_p.notif_def);
}

static bool rules_write(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(TAG_RULES, NVS_READWRITE, &h);
    if (e != ESP_OK) return false;
    e = nvs_set_u16(h, "count", s_p.kw_n);
    if (e == ESP_OK) {
        if (s_p.kw_n > 0) {
            e = nvs_set_blob(h, "items", s_p.kw,
                             (size_t)s_p.kw_n * sizeof(app_kw_t));
        } else {
            e = nvs_erase_key(h, "items");
            if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
        }
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "save rules: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

static bool prefs_write(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(TAG_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return false;
    bool changed = false;
#define SAVE_FIELD(field, key, fn) do { \
    if (s_p.field != s_saved.field) { \
        changed = true; \
        if (e == ESP_OK) e = fn(h, key, s_p.field); \
    } \
} while (0)
    SAVE_FIELD(brightness, "bl", nvs_set_u8);
    SAVE_FIELD(sleep_sec, "sleep", nvs_set_u16);
    SAVE_FIELD(lock_on, "lck", nvs_set_u8);
    SAVE_FIELD(lock_stay, "lstay", nvs_set_u8);
    SAVE_FIELD(lock_time, "ltm", nvs_set_u8);
    SAVE_FIELD(lock_wx, "lwx", nvs_set_u8);
    SAVE_FIELD(lock_quote, "lq", nvs_set_u8);
    SAVE_FIELD(volume, "vol", nvs_set_u8);
    SAVE_FIELD(muted, "mute", nvs_set_u8);
    SAVE_FIELD(tone_msg, "tmsg", nvs_set_u8);
    SAVE_FIELD(tone_alert, "talert", nvs_set_u8);
    SAVE_FIELD(ntp_on, "ntp", nvs_set_u8);
    SAVE_FIELD(ntp_server, "ntps", nvs_set_u8);
    SAVE_FIELD(auto_hide, "hide", nvs_set_u8);
    SAVE_FIELD(lang, "lang", nvs_set_u8);
    SAVE_FIELD(wx_lat_e4, "wxlat", nvs_set_i32);
    SAVE_FIELD(wx_lon_e4, "wxlon", nvs_set_i32);
    SAVE_FIELD(wx_interval, "wxiv", nvs_set_u16);
    SAVE_FIELD(wx_imperial, "wxu", nvs_set_u8);
    SAVE_FIELD(meow_bed, "mbed", nvs_set_u8);
    SAVE_FIELD(meow_wake, "mwake", nvs_set_u8);
    SAVE_FIELD(ota_auto, "otaa", nvs_set_u8);
    SAVE_FIELD(notif_def, "ndef", nvs_set_u8);
    SAVE_FIELD(theme, "thm", nvs_set_u8);
    SAVE_FIELD(kw_ver, "kwv", nvs_set_u8);
#undef SAVE_FIELD
    if (strcmp(s_p.wx_city, s_saved.wx_city) != 0) {
        changed = true;
        if (e == ESP_OK) e = nvs_set_str(h, "wxc", s_p.wx_city);
    }
    if (changed && e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "save prefs: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

void app_prefs_flush(void)
{
    if (!s_dirty) return;
    bool rules_changed = s_p.kw_n != s_saved.kw_n ||
                         memcmp(s_p.kw, s_saved.kw, sizeof(s_p.kw)) != 0;
    bool ok = prefs_write();
    if (rules_changed) ok = rules_write() && ok;
    if (ok) {
        s_saved = s_p;
        s_dirty = false;
        s_dirty_us = 0;
    }
}

void app_prefs_save(void)
{
    s_dirty = true;
    s_dirty_us = esp_timer_get_time();
}

void app_prefs_tick(void)
{
    if (s_dirty && esp_timer_get_time() - s_dirty_us >= 1000000) {
        app_prefs_flush();
    }
}

void app_prefs_save_lang(void)
{
    app_prefs_save();
    app_prefs_flush();
}

app_prefs_t *app_prefs(void)
{
    return &s_p;
}

app_totp_list_t *app_totp_store(void)
{
    return &s_totp;
}

bool app_totp_persist(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(TAG_TOTP, NVS_READWRITE, &h);
    if (e != ESP_OK) return false;
    size_t bytes = (size_t)s_totp.n * sizeof(app_totp_acct_t);
    totp_meta_t meta = {
        .magic = TOTP_MAGIC,
        .version = TOTP_VERSION,
        .count = s_totp.n,
        .item_size = sizeof(app_totp_acct_t),
        .bytes = bytes,
        .crc = crc32(s_totp.items, bytes),
    };
    e = nvs_set_blob(h, "meta", &meta, sizeof(meta));
    if (e == ESP_OK) {
        if (bytes > 0) e = nvs_set_blob(h, "items", s_totp.items, bytes);
        else {
            e = nvs_erase_key(h, "items");
            if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
        }
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "save totp: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

const char *app_ntp_server(int index)
{
    if (index < 0 || index >= APP_NTP_SERVER_N) index = 0;
    return NTP[index];
}

void app_prefs_apply_display(void)
{
    bsp_display_backlight(s_p.brightness);
}

void app_prefs_apply_audio(void)
{
    bsp_audio_set_volume(s_p.muted ? 0 : s_p.volume);
}
