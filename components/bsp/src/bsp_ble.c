// components/bsp/src/bsp_ble.c
// NimBLE 外设 + ANCS GATT 客户端。流程对齐 ESP-IDF nimble/ble_ancs,去掉控制台配对。
// 多连接上限 2:一台走 ANCS(iPhone),另一路 HID 或 BLE 对讲;绑定互不影响。
#include "bsp_ble.h"
#include "bsp_pm.h"
#include "bsp_wifi.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "os/os_mbuf.h"
#include "sdkconfig.h"
#include "host/ble_hs_mbuf.h"
#include "services/bas/ble_svc_bas.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/dis/ble_svc_dis.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "bsp_ble";

#define EVT_ADDED        0
#define EVT_MODIFIED     1
#define EVT_REMOVED      2
#define EVT_FLAG_PRE     (1 << 2)
#define EVT_FLAG_POS     (1 << 3)
#define EVT_FLAG_NEG     (1 << 4)
#define CAT_INCOMING     1
#define CMD_GET_ATTRS    0
#define CMD_GET_APP_ATTRS 1
#define CMD_PERFORM      2
#define ATTR_APP_ID      0
#define ATTR_TITLE       1
#define ATTR_SUBTITLE    2
#define ATTR_MESSAGE     3
#define ATTR_DATE        5
#define ATTR_POS_LABEL   6
#define ATTR_NEG_LABEL   7
#define ATTR_APP_NAME    0

#define DATA_BUF_MAX     336
#define TITLE_REQ_MAX    48
#define SUBTITLE_REQ_MAX 48
#define MSG_REQ_MAX      128
#define LABEL_REQ_MAX    24
#define RM_N             8
#define CANCEL_N         4
#define APP_CACHE_N      8
#define APP_NAME_WAIT_US 600000
#define BLE_CONN_MAX     CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define ADV_FAST_US      (30LL * 1000000)
#define ADV_FAST_MIN     BLE_GAP_ADV_FAST_INTERVAL1_MIN
#define ADV_FAST_MAX     BLE_GAP_ADV_FAST_INTERVAL1_MAX
#define ADV_SLOW_MIN     320    /* 200 ms, 单位 0.625 ms */
#define ADV_SLOW_MAX     480    /* 300 ms */
#define ADV_FIXED_BYTES  16     /* Flags + Appearance + HID UUID */
#define ADV_NAME_MAX     (31 - ADV_FIXED_BYTES)

#define SUB_IDLE  0
#define SUB_NS    1
#define SUB_DS    2
#define SUB_OK    3
#define SUB_SKIP  4

void ble_store_config_init(void);

static const ble_uuid128_t UUID_ANCS = BLE_UUID128_INIT(
    0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
    0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79);
static const ble_uuid128_t UUID_NS = BLE_UUID128_INIT(
    0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c,
    0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f);
static const ble_uuid128_t UUID_CP = BLE_UUID128_INIT(
    0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98,
    0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69);
static const ble_uuid128_t UUID_DS = BLE_UUID128_INIT(
    0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe,
    0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22);
static const ble_uuid16_t UUID_GAP_NAME = BLE_UUID16_INIT(0x2A00);

// iOS 系统蓝牙设置只展示 HID 等已知品类,普通 GATT 广播不会出现在列表里。
#define HID_APPEARANCE  0x03C1
#define HID_UUID16      0x1812

#define HID_CHR_IN   (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | \
                      BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC)
#define HID_CHR_OUT  (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | \
                      BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ_ENC | \
                      BLE_GATT_CHR_F_WRITE_ENC)
#define HID_CC_VOL_UP    0x00E9
#define HID_CC_VOL_DOWN  0x00EA
#define HID_CC_PLAY      0x00CD
#define HID_CC_NEXT      0x00B5
#define HID_CC_PREV      0x00B6

static const uint8_t s_hid_info[4] = { 0x11, 0x01, 0x00, 0x03 };
static const uint8_t s_rpt_ref_in[2] = { 0x01, 0x01 };
static const uint8_t s_rpt_ref_out[2] = { 0x01, 0x02 };
static const uint8_t s_rpt_ref_cc[2] = { 0x02, 0x01 };
static uint8_t s_hid_input[8];
static uint8_t s_hid_cc[2];
static uint8_t s_hid_output;
static uint8_t s_hid_proto = 1;
static uint16_t s_hid_kb_handle;
static uint16_t s_hid_cc_handle;
static uint8_t s_hid_rel_cc;
static esp_timer_handle_t s_hid_rel_timer;
static void hid_release_cb(void *arg);
static const uint8_t s_hid_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};

static int hid_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (uuid == 0x2A4A) return os_mbuf_append(ctxt->om, s_hid_info, sizeof(s_hid_info));
        if (uuid == 0x2A4B) return os_mbuf_append(ctxt->om, s_hid_map, sizeof(s_hid_map));
        if (uuid == 0x2A4E) return os_mbuf_append(ctxt->om, &s_hid_proto, 1);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (uuid == 0x2A4E && OS_MBUF_PKTLEN(ctxt->om) > 0) {
            os_mbuf_copydata(ctxt->om, 0, 1, &s_hid_proto);
        }
        return 0;
    }
    return 0;
}

static int hid_in_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_hid_input, sizeof(s_hid_input));
    }
    return 0;
}

static int hid_cc_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_hid_cc, sizeof(s_hid_cc));
    }
    return 0;
}

static int hid_out_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_hid_output, 1);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && OS_MBUF_PKTLEN(ctxt->om) > 0) {
        os_mbuf_copydata(ctxt->om, 0, 1, &s_hid_output);
        return 0;
    }
    return 0;
}

static int hid_ctrl_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn;
    (void)attr;
    (void)arg;
    return 0;
}

static int hid_dsc_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn;
    (void)attr;
    const uint8_t *ref = arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC && ref) {
        return os_mbuf_append(ctxt->om, ref, 2);
    }
    return 0;
}

static const struct ble_gatt_svc_def s_hid_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(HID_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A),  // HID Information
                .access_cb = hid_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B),  // Report Map,配对前 iOS 就会读
                .access_cb = hid_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4E),  // Protocol Mode
                .access_cb = hid_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = hid_in_access,
                .flags = HID_CHR_IN,
                .val_handle = &s_hid_kb_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = hid_dsc_access,
                        .arg = (void *)s_rpt_ref_in,
                    },
                    { 0 },
                },
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = hid_cc_access,
                .flags = HID_CHR_IN,
                .val_handle = &s_hid_cc_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = hid_dsc_access,
                        .arg = (void *)s_rpt_ref_cc,
                    },
                    { 0 },
                },
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = hid_out_access,
                .flags = HID_CHR_OUT,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = hid_dsc_access,
                        .arg = (void *)s_rpt_ref_out,
                    },
                    { 0 },
                },
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C),  // HID Control Point
                .access_cb = hid_ctrl_access,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },
        },
    },
    { 0 },
};

typedef struct {
    uint16_t conn;
    uint16_t ns, ds, cp;
    uint8_t sub_phase;
    uint16_t mtu;
    bool new_bond;
    bool hold;           // 新绑定后断开,让主机显示详情
    bool unpair;         // 断开后只删这个对端
    bool svc_seen;       // 已看到 ANCS 服务,避免 EDONE 误重试
    bool hid_kb;
    bool hid_cc;
    int bonds_at_connect;
    uint8_t disc_tries;
} ble_link_t;

static bool s_inited;
static bool s_stack;
static bool s_synced;
static TickType_t s_stack_tick;
static volatile bsp_ble_state_t s_state;
static char s_name[BSP_BLE_NAME_MAX + 1];
static uint8_t s_own_addr_type;
static ble_link_t s_link[BLE_CONN_MAX];
static uint32_t s_passkey;
static bool s_pair_confirm;
static uint16_t s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
static bool s_wait_notify;  // 新绑定后提示打开分享通知;广播仍可连接
static bool s_enabled = true;
static bool s_quiet = false;
static bool s_paired_ok;
static uint8_t s_rnd[6];
static bool s_have_rnd;
static bool s_want_adv = true;   // 用户/策略是否希望广播
static bool s_user_adv;          // 用户刚按 Advertise,暂时忽略 quiet
static bool s_fast_adv;
static const void *s_extra_svcs;
static uint8_t s_scan_uuid128[16];
static bool s_scan_uuid;
static uint8_t s_scan_mfg[8];
static uint8_t s_scan_mfg_n;
static void (*s_activity_cb)(void);
static void (*s_gap_cb)(void *event);
static int s_app_conns;
static bool s_forget_all_pending;

static uint8_t s_data[DATA_BUF_MAX];
static uint16_t s_data_len;
static uint16_t s_data_conn = BLE_HS_CONN_HANDLE_NONE;
static esp_timer_handle_t s_data_timer;
static esp_timer_handle_t s_disc_timer;
static esp_timer_handle_t s_adv_timer;
static esp_timer_handle_t s_app_timer;
static esp_timer_handle_t s_fast_timer;
static bool s_adv_fast_run;

// 通知队列。iPhone 会连发,单槽会丢中间态。
#define NOTIF_Q_N 4
static bsp_ble_notif_t s_notif_q[NOTIF_Q_N];
static uint8_t s_notif_head, s_notif_n;
static uint8_t s_parse_cat;
static uint8_t s_parse_flags;
static uint8_t s_parse_event;
static uint16_t s_parse_conn;
static bsp_ble_notif_t s_pending;
static bool s_pending_valid;
static uint32_t s_rm[RM_N];
static uint8_t s_rm_head, s_rm_n;
static uint32_t s_cancel[CANCEL_N];
static uint8_t s_cancel_n;

typedef struct {
    char id[BSP_BLE_APP_ID_MAX + 1];
    char name[BSP_BLE_APP_NAME_MAX + 1];
} app_name_cache_t;
static app_name_cache_t s_app_cache[APP_CACHE_N];
static uint8_t s_app_cache_next;

static int gap_event(struct ble_gap_event *event, void *arg);

static void set_state(bsp_ble_state_t st) {
    bool became_pair = (st == BSP_BLE_PAIRING && s_state != BSP_BLE_PAIRING);
    s_state = st;
    if (became_pair && s_activity_cb) s_activity_cb();
}

static int peer_bond_count(void) {
    int n = 0;
    if (!s_stack) return 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n);
    return n;
}

#define NVS_BLE_NS "bsp_ble"

static void load_ble_flags(void) {
    nvs_handle_t h;
    s_enabled = true;
    s_quiet = false;
    s_paired_ok = false;
    s_have_rnd = false;
    if (nvs_open(NVS_BLE_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, "en", &v) == ESP_OK) s_enabled = v != 0;
    if (nvs_get_u8(h, "stop_adv", &v) == ESP_OK) s_quiet = v != 0;
    if (nvs_get_u8(h, "paired", &v) == ESP_OK) s_paired_ok = v != 0;
    size_t n = sizeof(s_rnd);
    if (nvs_get_blob(h, "id5", s_rnd, &n) == ESP_OK && n == sizeof(s_rnd)) s_have_rnd = true;
    nvs_close(h);
}

static void save_ble_flags(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_BLE_NS, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t e = nvs_set_u8(h, "en", s_enabled ? 1 : 0);
    if (e == ESP_OK) e = nvs_set_u8(h, "stop_adv", s_quiet ? 1 : 0);
    if (e == ESP_OK) e = nvs_set_u8(h, "paired", s_paired_ok ? 1 : 0);
    if (e == ESP_OK) {
        if (s_have_rnd) e = nvs_set_blob(h, "id5", s_rnd, sizeof(s_rnd));
        else {
            e = nvs_erase_key(h, "id5");
            if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
        }
    }
    if (e == ESP_OK) {
        e = nvs_erase_key(h, "id4");
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    }
    if (e == ESP_OK) {
        e = nvs_erase_key(h, "rid");
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    }
    if (e == ESP_OK) e = nvs_erase_key(h, "quiet");
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "保存 BLE 标志失败: %s", esp_err_to_name(e));
}

static void mark_paired(void) {
    if (s_paired_ok) return;
    s_paired_ok = true;
    save_ble_flags();
}

static int set_static_rnd(const uint8_t rnd[6]) {
    int rc = ble_hs_id_set_rnd(rnd);
    if (rc != 0) {
        ESP_LOGW(TAG, "set_rnd rc=%d", rc);
        return rc;
    }
    s_own_addr_type = BLE_OWN_ADDR_RANDOM;
    memcpy(s_rnd, rnd, sizeof(s_rnd));
    s_have_rnd = true;
    snprintf(s_name, sizeof(s_name), "Passport-%02X%02X", s_rnd[1], s_rnd[0]);
    ble_svc_gap_device_name_set(s_name);
    return 0;
}

static void fmt_addr(char *dst, size_t n, const ble_addr_t *a) {
    snprintf(dst, n, "%02X:%02X:%02X:%02X:%02X:%02X",
             a->val[5], a->val[4], a->val[3], a->val[2], a->val[1], a->val[0]);
}

typedef struct {
    uint8_t type;
    uint8_t addr[6];
    char name[BSP_BLE_NAME_MAX + 1];
} ble_pname_t;

static ble_pname_t s_pnames[BSP_BLE_PEER_MAX];
static int s_pname_n;

static void load_pnames(void) {
    nvs_handle_t h;
    s_pname_n = 0;
    memset(s_pnames, 0, sizeof(s_pnames));
    if (nvs_open(NVS_BLE_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(s_pnames);
    if (nvs_get_blob(h, "pnames", s_pnames, &n) == ESP_OK) {
        s_pname_n = (int)(n / sizeof(s_pnames[0]));
        if (s_pname_n > BSP_BLE_PEER_MAX) s_pname_n = BSP_BLE_PEER_MAX;
    }
    nvs_close(h);
}

static void save_pnames(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_BLE_NS, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t e = nvs_set_blob(h, "pnames", s_pnames,
                               sizeof(s_pnames[0]) * (size_t)s_pname_n);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "保存设备名失败: %s", esp_err_to_name(e));
}

static void clear_pnames(void) {
    s_pname_n = 0;
    memset(s_pnames, 0, sizeof(s_pnames));
    save_pnames();
}

static int pname_find(const ble_addr_t *a) {
    if (!a) return -1;
    for (int i = 0; i < s_pname_n; i++) {
        if (s_pnames[i].type == a->type && memcmp(s_pnames[i].addr, a->val, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void remember_name(const ble_addr_t *a, const char *name) {
    if (!a || !name || !name[0]) return;
    int i = pname_find(a);
    if (i < 0) {
        if (s_pname_n < BSP_BLE_PEER_MAX) i = s_pname_n++;
        else i = 0;
        s_pnames[i].type = a->type;
        memcpy(s_pnames[i].addr, a->val, 6);
    } else if (strcmp(s_pnames[i].name, name) == 0) {
        return;
    }
    strlcpy(s_pnames[i].name, name, sizeof(s_pnames[i].name));
    save_pnames();
}

static void fill_peer_name(char *dst, size_t n, const ble_addr_t *a) {
    if (!dst || n == 0) return;
    dst[0] = 0;
    int i = pname_find(a);
    if (i >= 0 && s_pnames[i].name[0]) {
        strlcpy(dst, s_pnames[i].name, n);
        return;
    }
    if (a) fmt_addr(dst, n, a);
}

static bool addr_eq(const ble_addr_t *a, const ble_addr_t *b) {
    return a && b && a->type == b->type && memcmp(a->val, b->val, 6) == 0;
}

static bool peer_connected(const ble_addr_t *id) {
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        if (s_link[i].conn == BLE_HS_CONN_HANDLE_NONE) continue;
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(s_link[i].conn, &d) != 0) continue;
        if (addr_eq(&d.peer_id_addr, id) || addr_eq(&d.peer_ota_addr, id)) return true;
    }
    return false;
}

static bool all_bonded_connected(void) {
    if (!s_stack) return false;
    int n = 0;
    for (int i = 0; i < BSP_BLE_PEER_MAX; i++) {
        struct ble_store_key_sec key;
        struct ble_store_value_sec val;
        memset(&key, 0, sizeof(key));
        key.idx = (uint8_t)i;
        if (ble_store_read_peer_sec(&key, &val) != 0) break;
        n++;
        if (!peer_connected(&val.peer_addr)) return false;
    }
    return n > 0;
}

static int link_count(void) {
    int n = 0;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        if (s_link[i].conn != BLE_HS_CONN_HANDLE_NONE) n++;
    }
    return n;
}

static ble_link_t *link_find(uint16_t conn) {
    if (conn == BLE_HS_CONN_HANDLE_NONE) return NULL;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        if (s_link[i].conn == conn) return &s_link[i];
    }
    return NULL;
}

static void link_reset(ble_link_t *l) {
    memset(l, 0, sizeof(*l));
    l->conn = BLE_HS_CONN_HANDLE_NONE;
    l->mtu = 23;
}

static ble_link_t *link_alloc(uint16_t conn) {
    ble_link_t *l = link_find(conn);
    if (l) return l;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        if (s_link[i].conn == BLE_HS_CONN_HANDLE_NONE) {
            link_reset(&s_link[i]);
            s_link[i].conn = conn;
            return &s_link[i];
        }
    }
    return NULL;
}

static void refresh_state(void) {
    bool pairing = false;
    bool ancs = false;
    bool connected = false;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        ble_link_t *l = &s_link[i];
        if (l->conn == BLE_HS_CONN_HANDLE_NONE) continue;
        connected = true;
        if (l->sub_phase == SUB_OK) ancs = true;
        if (s_passkey && s_pair_conn == l->conn) pairing = true;
    }
    if (pairing) set_state(BSP_BLE_PAIRING);
    else if (ancs) set_state(BSP_BLE_ANCS);
    else if (connected) set_state(BSP_BLE_CONNECTED);
    else if (s_wait_notify) set_state(BSP_BLE_WAIT_NOTIFY);
    else if (s_state != BSP_BLE_IDLE) set_state(BSP_BLE_ADVERTISING);
}

static void copy_attr(char *dst, size_t cap, const uint8_t *src, uint16_t len) {
    if (len >= cap) len = (uint16_t)(cap - 1);
    memcpy(dst, src, len);
    dst[len] = 0;
}

static void push_removed(uint32_t uid) {
    if (s_rm_n == RM_N) {
        s_rm_head = (uint8_t)((s_rm_head + 1) % RM_N);
        s_rm_n--;
    }
    s_rm[(s_rm_head + s_rm_n) % RM_N] = uid;
    s_rm_n++;
}

static void cancel_uid(uint32_t uid) {
    for (int i = 0; i < s_cancel_n; i++) {
        if (s_cancel[i] == uid) return;
    }
    if (s_cancel_n < CANCEL_N) {
        s_cancel[s_cancel_n++] = uid;
        return;
    }
    memmove(s_cancel, s_cancel + 1, (CANCEL_N - 1) * sizeof(s_cancel[0]));
    s_cancel[CANCEL_N - 1] = uid;
}

static bool uid_cancelled(uint32_t uid) {
    for (int i = 0; i < s_cancel_n; i++) {
        if (s_cancel[i] == uid) return true;
    }
    return false;
}

static ble_link_t *link_ancs(uint16_t conn) {
    ble_link_t *l = link_find(conn);
    if (l && l->cp && l->sub_phase == SUB_OK) return l;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        if (s_link[i].conn != BLE_HS_CONN_HANDLE_NONE &&
            s_link[i].cp && s_link[i].sub_phase == SUB_OK) {
            return &s_link[i];
        }
    }
    return NULL;
}

static bool cache_get(const char *id, char *name, size_t n) {
    if (!id || !id[0] || !name || n == 0) return false;
    for (int i = 0; i < APP_CACHE_N; i++) {
        if (s_app_cache[i].id[0] && strcmp(s_app_cache[i].id, id) == 0) {
            copy_attr(name, n, (const uint8_t *)s_app_cache[i].name,
                      (uint16_t)strlen(s_app_cache[i].name));
            return name[0] != 0;
        }
    }
    return false;
}

static void cache_put(const char *id, const char *name) {
    if (!id || !id[0] || !name || !name[0]) return;
    for (int i = 0; i < APP_CACHE_N; i++) {
        if (strcmp(s_app_cache[i].id, id) == 0) {
            copy_attr(s_app_cache[i].name, sizeof(s_app_cache[i].name),
                      (const uint8_t *)name, (uint16_t)strlen(name));
            return;
        }
    }
    copy_attr(s_app_cache[s_app_cache_next].id, sizeof(s_app_cache[0].id),
              (const uint8_t *)id, (uint16_t)strlen(id));
    copy_attr(s_app_cache[s_app_cache_next].name, sizeof(s_app_cache[0].name),
              (const uint8_t *)name, (uint16_t)strlen(name));
    s_app_cache_next = (uint8_t)((s_app_cache_next + 1) % APP_CACHE_N);
}

// 同一 UID 的更新覆盖队列里的旧条目,其余追加;满了丢最旧的。
static void notif_q_push(const bsp_ble_notif_t *n) {
    for (uint8_t i = 0; i < s_notif_n; i++) {
        uint8_t idx = (uint8_t)((s_notif_head + i) % NOTIF_Q_N);
        if (s_notif_q[idx].uid == n->uid) {
            s_notif_q[idx] = *n;
            return;
        }
    }
    if (s_notif_n == NOTIF_Q_N) {
        s_notif_head = (uint8_t)((s_notif_head + 1) % NOTIF_Q_N);
        s_notif_n--;
    }
    s_notif_q[(s_notif_head + s_notif_n) % NOTIF_Q_N] = *n;
    s_notif_n++;
}

static void notif_q_drop(uint32_t uid) {
    uint8_t keep = 0;
    for (uint8_t i = 0; i < s_notif_n; i++) {
        uint8_t idx = (uint8_t)((s_notif_head + i) % NOTIF_Q_N);
        if (s_notif_q[idx].uid == uid) continue;
        uint8_t dst = (uint8_t)((s_notif_head + keep) % NOTIF_Q_N);
        if (dst != idx) s_notif_q[dst] = s_notif_q[idx];
        keep++;
    }
    s_notif_n = keep;
}

static void finish_notif(void) {
    if (!s_pending_valid) return;
    if (s_app_timer) esp_timer_stop(s_app_timer);
    if (uid_cancelled(s_pending.uid)) {
        s_pending_valid = false;
        return;
    }
    notif_q_push(&s_pending);
    s_pending_valid = false;
    ESP_LOGI(TAG, "ANCS ev=%u app=%s name=%s title_len=%u sub_len=%u msg_len=%u date=%s",
             (unsigned)s_pending.event,
             s_pending.app_id[0] ? s_pending.app_id : "-",
             s_pending.app_name[0] ? s_pending.app_name : "-",
             (unsigned)strlen(s_pending.title),
             (unsigned)strlen(s_pending.subtitle),
             (unsigned)strlen(s_pending.message),
             s_pending.date[0] ? s_pending.date : "-");
    if (s_activity_cb) s_activity_cb();
}

static void app_timer_cb(void *arg) {
    (void)arg;
    if (s_pending_valid) finish_notif();
}

static void advertise(void);
static esp_err_t stack_start(void);
static void stack_stop(void);

static void apply_coex_ps(void) {
    bool perf = s_stack && (ble_gap_adv_active() != 0 ||
                link_count() > 0 || s_state == BSP_BLE_PAIRING);
    bsp_wifi_set_power_save(!perf);
}

static void arm_fast_adv(void) {
    s_fast_adv = true;
    if (s_fast_timer) {
        esp_timer_stop(s_fast_timer);
        esp_timer_start_once(s_fast_timer, ADV_FAST_US);
    }
}

static bool slots_free(void) {
    return (link_count() + s_app_conns) < BLE_CONN_MAX;
}

static bool can_advertise(void) {
    if (!s_stack || !s_synced || !s_enabled || !s_want_adv || !slots_free()) {
        return false;
    }
    if (s_quiet && all_bonded_connected() && !s_user_adv) return false;
    return true;
}

static void advertise_if_room(void) {
    if (!s_stack) {
        set_state(BSP_BLE_IDLE);
        apply_coex_ps();
        return;
    }
    if (!s_synced) {
        advertise();
        return;
    }
    if (!can_advertise()) {
        ble_gap_adv_stop();
        if (!s_enabled) set_state(BSP_BLE_IDLE);
        else refresh_state();
        apply_coex_ps();
        return;
    }
    advertise();
}

static void begin_connectable_adv(void) {
    s_wait_notify = false;
    s_want_adv = true;
    if (s_adv_timer) esp_timer_stop(s_adv_timer);
    arm_fast_adv();
    advertise_if_room();
    refresh_state();
}

static void schedule_advertise(void) {
    if (!slots_free()) return;
    if (s_wait_notify) {
        advertise();
        return;
    }
    if (link_count() == 0) set_state(BSP_BLE_IDLE);
    else refresh_state();
    if (s_adv_timer) {
        esp_timer_stop(s_adv_timer);
        esp_timer_start_once(s_adv_timer, 500000);
    } else {
        advertise();
    }
}

static void adv_timer_cb(void *arg) {
    (void)arg;
    advertise_if_room();
}

static void fast_timer_cb(void *arg) {
    (void)arg;
    s_fast_adv = false;
    if (can_advertise() && s_stack && ble_gap_adv_active()) advertise();
    apply_coex_ps();
}

static void advertise(void) {
    if (!s_stack) return;
    if (!s_synced) {
        if (s_adv_timer) {
            esp_timer_stop(s_adv_timer);
            esp_timer_start_once(s_adv_timer, 300000);
        }
        return;
    }
    const char *name = s_name[0] ? s_name : "Passport";
    uint8_t adv[31];
    uint8_t rsp[31];
    size_t n = strlen(name);
    if (n > ADV_NAME_MAX) n = ADV_NAME_MAX;
    int i = 0;
    int rc;

    if (!can_advertise()) {
        apply_coex_ps();
        return;
    }

    bool want_fast = s_fast_adv || s_user_adv;
    if (ble_gap_adv_active()) {
        if (s_adv_fast_run == want_fast) {
            apply_coex_ps();
            return;
        }
        ble_gap_adv_stop();
        s_adv_fast_run = false;
    }

    // 手工组主广播:iOS 设置用被动扫描,只认 PDU 里的 Complete Local Name(0x09)。
    // 名字必须紧跟 Flags,不能只放在 scan response。
    adv[i++] = 2;
    adv[i++] = 0x01;
    adv[i++] = 0x06;
    adv[i++] = (uint8_t)(1 + n);
    adv[i++] = 0x09;
    memcpy(&adv[i], name, n);
    i += (int)n;
    adv[i++] = 3;
    adv[i++] = 0x03;
    adv[i++] = 0x12;
    adv[i++] = 0x18;
    adv[i++] = 3;
    adv[i++] = 0x19;
    adv[i++] = 0xC1;
    adv[i++] = 0x03;

    rc = ble_gap_adv_set_data(adv, i);
    ESP_LOGI(TAG, "adv name=%s bytes=%d rc=%d links=%d", name, i, rc, link_count());
    ESP_LOG_BUFFER_HEX(TAG, adv, i);
    while (rc != 0 && n > 0) {
        n--;
        i = 0;
        adv[i++] = 2;
        adv[i++] = 0x01;
        adv[i++] = 0x06;
        adv[i++] = (uint8_t)(1 + n);
        adv[i++] = 0x09;
        memcpy(&adv[i], name, n);
        i += (int)n;
        adv[i++] = 3;
        adv[i++] = 0x19;
        adv[i++] = 0xC1;
        adv[i++] = 0x03;
        adv[i++] = 3;
        adv[i++] = 0x03;
        adv[i++] = 0x12;
        adv[i++] = 0x18;
        rc = ble_gap_adv_set_data(adv, i);
        ESP_LOGW(TAG, "adv 缩短 name_len=%u bytes=%d rc=%d", (unsigned)n, i, rc);
    }
    if (rc != 0) return;

    int j = 0;
    if (s_scan_uuid) {
        rsp[j++] = 17;
        rsp[j++] = 0x07;
        memcpy(&rsp[j], s_scan_uuid128, 16);
        j += 16;
    } else {
        rsp[j++] = (uint8_t)(1 + n);
        rsp[j++] = 0x09;
        memcpy(&rsp[j], name, n);
        j += (int)n;
    }
    if (s_scan_mfg_n && j + 2 + s_scan_mfg_n <= 31) {
        rsp[j++] = (uint8_t)(1 + s_scan_mfg_n);
        rsp[j++] = 0xFF;
        memcpy(&rsp[j], s_scan_mfg, s_scan_mfg_n);
        j += s_scan_mfg_n;
    }
    ble_gap_adv_rsp_set_data(rsp, j);

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = want_fast ? ADV_FAST_MIN : ADV_SLOW_MIN;
    params.itvl_max = want_fast ? ADV_FAST_MAX : ADV_SLOW_MAX;
    params.channel_map = 0x07;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "adv start rc=%d", rc);
        s_adv_fast_run = false;
        apply_coex_ps();
        if (s_adv_timer) {
            esp_timer_stop(s_adv_timer);
            esp_timer_start_once(s_adv_timer, 300000);
        }
        return;
    }
    s_adv_fast_run = want_fast;
    if (link_count() == 0 && !s_wait_notify) set_state(BSP_BLE_ADVERTISING);
    ESP_LOGI(TAG, "广播间隔 %s active=%d rc=%d",
             want_fast ? "fast" : "slow",
             ble_gap_adv_active(), rc);
    apply_coex_ps();
}

static void subscribe_cccd(uint16_t conn, uint16_t val_handle) {
    uint8_t cccd[2] = { 0x01, 0x00 };
    int rc = ble_gattc_write_flat(conn, val_handle + 1, cccd, sizeof(cccd), NULL, NULL);
    if (rc != 0) ESP_LOGW(TAG, "CCCD write handle=%u rc=%d", val_handle, rc);
}

static void request_attrs(ble_link_t *l, const uint8_t *uid) {
    if (!l || !l->cp) return;
    uint8_t cmd[24];
    int i = 0;
    cmd[i++] = CMD_GET_ATTRS;
    memcpy(&cmd[i], uid, 4);
    i += 4;
    cmd[i++] = ATTR_APP_ID;
    cmd[i++] = ATTR_TITLE;
    cmd[i++] = (uint8_t)(TITLE_REQ_MAX & 0xFF);
    cmd[i++] = (uint8_t)(TITLE_REQ_MAX >> 8);
    cmd[i++] = ATTR_SUBTITLE;
    cmd[i++] = (uint8_t)(SUBTITLE_REQ_MAX & 0xFF);
    cmd[i++] = (uint8_t)(SUBTITLE_REQ_MAX >> 8);
    cmd[i++] = ATTR_MESSAGE;
    cmd[i++] = (uint8_t)(MSG_REQ_MAX & 0xFF);
    cmd[i++] = (uint8_t)(MSG_REQ_MAX >> 8);
    cmd[i++] = ATTR_DATE;
    if (s_parse_flags & EVT_FLAG_POS) {
        cmd[i++] = ATTR_POS_LABEL;
        cmd[i++] = (uint8_t)(LABEL_REQ_MAX & 0xFF);
        cmd[i++] = (uint8_t)(LABEL_REQ_MAX >> 8);
    }
    if (s_parse_flags & EVT_FLAG_NEG) {
        cmd[i++] = ATTR_NEG_LABEL;
        cmd[i++] = (uint8_t)(LABEL_REQ_MAX & 0xFF);
        cmd[i++] = (uint8_t)(LABEL_REQ_MAX >> 8);
    }
    int rc = ble_gattc_write_flat(l->conn, l->cp, cmd, i, NULL, NULL);
    if (rc != 0) ESP_LOGW(TAG, "get attrs rc=%d", rc);
}

static void request_app_name(ble_link_t *l, const char *app_id) {
    if (!l || !l->cp || !app_id || !app_id[0]) return;
    uint8_t cmd[BSP_BLE_APP_ID_MAX + 4];
    size_t n = strlen(app_id);
    if (n > BSP_BLE_APP_ID_MAX) n = BSP_BLE_APP_ID_MAX;
    cmd[0] = CMD_GET_APP_ATTRS;
    memcpy(&cmd[1], app_id, n);
    cmd[1 + n] = 0;
    cmd[2 + n] = ATTR_APP_NAME;
    int rc = ble_gattc_write_flat(l->conn, l->cp, cmd, (uint16_t)(3 + n), NULL, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "get app attrs rc=%d", rc);
        finish_notif();
        return;
    }
    if (s_app_timer) {
        esp_timer_stop(s_app_timer);
        esp_timer_start_once(s_app_timer, APP_NAME_WAIT_US);
    }
}

static void parse_app_attrs(const uint8_t *msg, uint16_t len) {
    if (!msg || len < 3 || msg[0] != CMD_GET_APP_ATTRS) return;
    const uint8_t *p = msg + 1;
    uint16_t remain = (uint16_t)(len - 1);
    const uint8_t *id = p;
    while (remain && *p) {
        p++;
        remain--;
    }
    if (!remain) return;
    uint16_t id_len = (uint16_t)(p - id);
    p++;
    remain--;

    char app_id[BSP_BLE_APP_ID_MAX + 1];
    copy_attr(app_id, sizeof(app_id), id, id_len);

    char name[BSP_BLE_APP_NAME_MAX + 1] = { 0 };
    while (remain >= 3) {
        uint8_t aid = p[0];
        uint16_t alen = (uint16_t)(p[1] | (p[2] << 8));
        if (alen > remain - 3) break;
        if (aid == ATTR_APP_NAME) copy_attr(name, sizeof(name), p + 3, alen);
        p += 3 + alen;
        remain = (uint16_t)(remain - 3 - alen);
    }
    if (name[0]) cache_put(app_id, name);
    if (s_pending_valid && strcmp(s_pending.app_id, app_id) == 0) {
        if (name[0]) copy_attr(s_pending.app_name, sizeof(s_pending.app_name),
                               (const uint8_t *)name, (uint16_t)strlen(name));
        finish_notif();
    }
}

static void parse_notif_attrs(ble_link_t *l, const uint8_t *msg, uint16_t len) {
    if (!msg || len < 5 || msg[0] != CMD_GET_ATTRS) return;
    uint32_t uid;
    memcpy(&uid, msg + 1, 4);
    if (uid_cancelled(uid)) return;
    bsp_ble_notif_t n = { 0 };
    n.uid = uid;
    n.conn = l ? l->conn : s_parse_conn;
    n.flags = s_parse_flags;
    n.category = s_parse_cat;
    n.event = s_parse_event;
    uint16_t remain = (uint16_t)(len - 5);
    const uint8_t *p = msg + 5;
    while (remain >= 3) {
        uint8_t id = p[0];
        uint16_t alen = (uint16_t)(p[1] | (p[2] << 8));
        if (alen > remain - 3) break;
        if (id == ATTR_APP_ID) copy_attr(n.app_id, sizeof(n.app_id), p + 3, alen);
        else if (id == ATTR_TITLE) copy_attr(n.title, sizeof(n.title), p + 3, alen);
        else if (id == ATTR_SUBTITLE) copy_attr(n.subtitle, sizeof(n.subtitle), p + 3, alen);
        else if (id == ATTR_MESSAGE) copy_attr(n.message, sizeof(n.message), p + 3, alen);
        else if (id == ATTR_DATE) copy_attr(n.date, sizeof(n.date), p + 3, alen);
        else if (id == ATTR_POS_LABEL) copy_attr(n.pos_label, sizeof(n.pos_label), p + 3, alen);
        else if (id == ATTR_NEG_LABEL) copy_attr(n.neg_label, sizeof(n.neg_label), p + 3, alen);
        p += 3 + alen;
        remain = (uint16_t)(remain - 3 - alen);
    }
    if (s_pending_valid) finish_notif();
    if (uid_cancelled(uid)) return;
    s_pending = n;
    s_pending_valid = true;
    if (n.app_id[0] &&
        cache_get(n.app_id, s_pending.app_name, sizeof(s_pending.app_name))) {
        finish_notif();
        return;
    }
    if (n.app_id[0] && l && l->cp) {
        request_app_name(l, n.app_id);
        return;
    }
    finish_notif();
}

static void parse_ds(ble_link_t *l, const uint8_t *msg, uint16_t len) {
    if (!msg || len < 1) return;
    if (msg[0] == CMD_GET_ATTRS) parse_notif_attrs(l, msg, len);
    else if (msg[0] == CMD_GET_APP_ATTRS) parse_app_attrs(msg, len);
}

static void data_timer_cb(void *arg) {
    (void)arg;
    if (s_data_len > 0) {
        parse_ds(link_find(s_data_conn), s_data, s_data_len);
        s_data_len = 0;
    }
}

static void discover_ancs(uint16_t conn);
static void read_peer_name(uint16_t conn);

static void disc_timer_cb(void *arg) {
    (void)arg;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        ble_link_t *l = &s_link[i];
        if (l->conn == BLE_HS_CONN_HANDLE_NONE) continue;
        if (l->hold) {
            ESP_LOGI(TAG, "配对完成,断开以便主机显示详情 handle=%u", l->conn);
            ble_gap_terminate(l->conn, BLE_ERR_REM_USER_CONN_TERM);
            return;
        }
    }
    bool more = false;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        ble_link_t *l = &s_link[i];
        if (l->conn == BLE_HS_CONN_HANDLE_NONE) continue;
        if (l->sub_phase != SUB_IDLE) continue;
        discover_ancs(l->conn);
        for (int j = i + 1; j < BLE_CONN_MAX; j++) {
            if (s_link[j].conn != BLE_HS_CONN_HANDLE_NONE &&
                s_link[j].sub_phase == SUB_IDLE) {
                more = true;
            }
        }
        if (more && s_disc_timer) esp_timer_start_once(s_disc_timer, 1500000);
        return;
    }
}

static int on_cccd_write(uint16_t conn, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg) {
    (void)attr;
    (void)arg;
    ble_link_t *l = link_find(conn);
    if (!l) return 0;
    if (error && error->status != 0) {
        ESP_LOGW(TAG, "CCCD status=%d", error->status);
    }
    if (l->sub_phase == SUB_NS && l->ds) {
        l->sub_phase = SUB_DS;
        uint8_t cccd[2] = { 0x01, 0x00 };
        int rc = ble_gattc_write_flat(conn, l->ds + 1, cccd, sizeof(cccd), on_cccd_write, NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "DS CCCD rc=%d", rc);
            subscribe_cccd(conn, l->ds);
            l->sub_phase = SUB_OK;
            mark_paired();
            refresh_state();
        }
        return 0;
    }
    l->sub_phase = SUB_OK;
    mark_paired();
    refresh_state();
    ESP_LOGI(TAG, "ANCS 已订阅 handle=%u", conn);
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg) {
    (void)arg;
    ble_link_t *l = link_find(conn);
    if (!l) return 0;
    if (error->status == 0 && chr) {
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_NS.u) == 0) l->ns = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &UUID_DS.u) == 0) l->ds = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &UUID_CP.u) == 0) l->cp = chr->val_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "ANCS chr ns=%u ds=%u cp=%u handle=%u", l->ns, l->ds, l->cp, conn);
        if (!l->ns || !l->ds || !l->cp) {
            l->sub_phase = SUB_IDLE;
            if (l->disc_tries < 5) {
                l->disc_tries++;
                esp_timer_start_once(s_disc_timer, 1000000);
            } else {
                l->sub_phase = SUB_SKIP;
                ESP_LOGI(TAG, "无完整 ANCS,按 HID 对端保留 handle=%u", conn);
                refresh_state();
            }
            return 0;
        }
        l->sub_phase = SUB_NS;
        uint8_t cccd[2] = { 0x01, 0x00 };
        int rc = ble_gattc_write_flat(conn, l->ns + 1, cccd, sizeof(cccd), on_cccd_write, NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "NS CCCD rc=%d, fallback", rc);
            subscribe_cccd(conn, l->ns);
            subscribe_cccd(conn, l->ds);
            l->sub_phase = SUB_OK;
            mark_paired();
            refresh_state();
        }
        return 0;
    }
    return error->status;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc, void *arg) {
    (void)arg;
    ble_link_t *l = link_find(conn);
    if (!l) return 0;
    if (error->status == 0 && svc) {
        l->ns = l->ds = l->cp = 0;
        l->svc_seen = true;
        return ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle, on_chr, NULL);
    }
    if (error->status == BLE_HS_EDONE) {
        if (!l->svc_seen && !l->ns) {
            if (l->disc_tries < 5) {
                ESP_LOGW(TAG, "未找到 ANCS,重试 handle=%u", conn);
                l->disc_tries++;
                l->sub_phase = SUB_IDLE;
                esp_timer_start_once(s_disc_timer, 1500000);
            } else {
                l->sub_phase = SUB_SKIP;
                ESP_LOGI(TAG, "对端无 ANCS,保持 HID 连接 handle=%u", conn);
                refresh_state();
            }
        }
        return 0;
    }
    ESP_LOGW(TAG, "ANCS svc err=%d", error->status);
    return error->status;
}

static void discover_ancs(uint16_t conn) {
    ble_link_t *l = link_find(conn);
    if (!l || l->sub_phase == SUB_OK || l->sub_phase == SUB_SKIP) return;
    read_peer_name(conn);
    l->svc_seen = false;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &UUID_ANCS.u, on_svc, NULL);
    if (rc != 0) ESP_LOGW(TAG, "disc ANCS rc=%d", rc);
}

static int on_gap_name(uint16_t conn, const struct ble_gatt_error *error,
                       struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (!error || error->status != 0 || !attr || !attr->om) return 0;
    uint16_t n = OS_MBUF_PKTLEN(attr->om);
    char name[BSP_BLE_NAME_MAX + 1];
    if (n > BSP_BLE_NAME_MAX) n = BSP_BLE_NAME_MAX;
    if (n == 0 || os_mbuf_copydata(attr->om, 0, n, name) != 0) return 0;
    name[n] = 0;
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) != 0) return 0;
    remember_name(&d.peer_id_addr, name);
    ESP_LOGI(TAG, "对端名称已保存");
    return 0;
}

static void read_peer_name(uint16_t conn)
{
    int rc = ble_gattc_read_by_uuid(conn, 1, 0xFFFF, &UUID_GAP_NAME.u,
                                    on_gap_name, NULL);
    if (rc != 0) ESP_LOGW(TAG, "read GAP name rc=%d", rc);
}

static void on_encrypted(uint16_t conn) {
    ble_link_t *l = link_find(conn);
    if (!l) return;
    if (s_pair_conn == conn) {
        s_passkey = 0;
        s_pair_confirm = false;
        s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
    }
    refresh_state();
    l->new_bond = false;
    l->hold = false;
    ESP_LOGI(TAG, "已加密,发现 ANCS handle=%u", conn);
    if (s_disc_timer && !esp_timer_is_active(s_disc_timer)) {
        esp_timer_start_once(s_disc_timer, 1500000);
    }
}

static void delete_peer_bond(uint16_t conn) {
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) != 0) return;
    int rc = ble_store_util_delete_peer(&d.peer_id_addr);
    ESP_LOGW(TAG, "删除失步绑定 handle=%u rc=%d", conn, rc);
}

static void finish_forget_all(void) {
    if (!s_forget_all_pending || link_count() != 0) return;
    s_forget_all_pending = false;
    ble_store_clear();
    clear_pnames();
    s_paired_ok = false;
    s_have_rnd = false;
    s_want_adv = true;
    s_user_adv = true;
    save_ble_flags();
    if (bsp_ble_new_identity() != ESP_OK) {
        arm_fast_adv();
        advertise();
    }
    ESP_LOGI(TAG, "已清除全部绑定");
}

static void handle_notify_src(ble_link_t *l, struct os_mbuf *om) {
    uint8_t buf[8];
    if (!l || OS_MBUF_PKTLEN(om) < 8) return;
    if (os_mbuf_copydata(om, 0, 8, buf) != 0) return;
    uint8_t event = buf[0];
    uint8_t flags = buf[1];
    uint8_t cat = buf[2];
    uint32_t uid;
    memcpy(&uid, &buf[4], 4);
    if (event == EVT_REMOVED) {
        cancel_uid(uid);
        if (s_pending_valid && s_pending.uid == uid) {
            s_pending_valid = false;
            if (s_app_timer) esp_timer_stop(s_app_timer);
        }
        notif_q_drop(uid);
        push_removed(uid);
        if (s_activity_cb) s_activity_cb();
        return;
    }
    if (event != EVT_ADDED && event != EVT_MODIFIED) return;
    if (flags & EVT_FLAG_PRE) return;
    if (cat == CAT_INCOMING) flags |= (uint8_t)(EVT_FLAG_POS | EVT_FLAG_NEG);
    if (s_pending_valid) finish_notif();
    s_parse_cat = cat;
    s_parse_flags = flags;
    s_parse_event = event;
    s_parse_conn = l->conn;
    request_attrs(l, &buf[4]);
}

static void handle_notify_ds(ble_link_t *l, struct os_mbuf *om) {
    uint16_t n = OS_MBUF_PKTLEN(om);
    if (!l || n == 0) return;
    if (s_data_len + n > DATA_BUF_MAX) {
        s_data_len = 0;
        ESP_LOGW(TAG, "ANCS 数据过长,丢弃");
        return;
    }
    if (os_mbuf_copydata(om, 0, n, &s_data[s_data_len]) != 0) return;
    s_data_conn = l->conn;
    s_data_len = (uint16_t)(s_data_len + n);
    uint16_t mtu = l->mtu ? l->mtu : 23;
    if (n >= mtu - 3) {
        esp_timer_stop(s_data_timer);
        esp_timer_start_once(s_data_timer, 400000);
    } else {
        esp_timer_stop(s_data_timer);
        parse_ds(l, s_data, s_data_len);
        s_data_len = 0;
    }
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    int rc;
    if (s_gap_cb) s_gap_cb(event);

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "连接失败 status=%d", event->connect.status);
            schedule_advertise();
            return 0;
        }
        if (s_wait_notify) {
            ESP_LOGI(TAG, "分享通知窗口内接受连接");
            s_wait_notify = false;
        }
        if (s_adv_timer) esp_timer_stop(s_adv_timer);
        {
            ble_link_t *l = link_alloc(event->connect.conn_handle);
            if (!l) {
                ESP_LOGW(TAG, "无空闲连接槽,拒绝 handle=%u", event->connect.conn_handle);
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_CONN_LIMIT);
                return 0;
            }
            l->bonds_at_connect = peer_bond_count();
            set_state(BSP_BLE_CONNECTED);
            s_user_adv = false;
            {
                struct ble_gap_conn_desc d;
                if (ble_gap_conn_find(l->conn, &d) == 0) {
                    ESP_LOGI(TAG,
                             "已连接 handle=%u peer=%02x:%02x:%02x:%02x:%02x:%02x t=%u links=%d bonds=%d",
                             l->conn,
                             d.peer_ota_addr.val[5], d.peer_ota_addr.val[4],
                             d.peer_ota_addr.val[3], d.peer_ota_addr.val[2],
                             d.peer_ota_addr.val[1], d.peer_ota_addr.val[0],
                             d.peer_ota_addr.type,
                             link_count(), l->bonds_at_connect);
                } else {
                    ESP_LOGI(TAG, "已连接 handle=%u links=%d bonds=%d",
                             l->conn, link_count(), l->bonds_at_connect);
                }
            }
        }
        apply_coex_ps();
        advertise_if_room();
        rc = ble_gap_security_initiate(event->connect.conn_handle);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "启动加密失败 handle=%u rc=%d",
                     event->connect.conn_handle, rc);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        uint16_t conn = event->disconnect.conn.conn_handle;
        ESP_LOGI(TAG, "断开 handle=%u reason=0x%03x", conn, event->disconnect.reason);
        ble_link_t *l = link_find(conn);
        bool hold = false;
        bool unpair = false;
        if (l) {
            hold = l->hold;
            unpair = l->unpair;
            link_reset(l);
        }
        if (s_pair_conn == conn) {
            s_passkey = 0;
            s_pair_confirm = false;
            s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
        }
        s_data_len = 0;
        if (s_disc_timer) esp_timer_stop(s_disc_timer);
        if (unpair) {
            ble_store_util_delete_peer(&event->disconnect.conn.peer_id_addr);
            ESP_LOGI(TAG, "已删除该对端绑定,剩余 bonds=%d", peer_bond_count());
        }
        if (s_forget_all_pending && link_count() == 0) {
            finish_forget_all();
            return 0;
        }
        if (hold) {
            s_wait_notify = (link_count() == 0);
            if (s_wait_notify) set_state(BSP_BLE_WAIT_NOTIFY);
            else refresh_state();
            ESP_LOGI(TAG, "可连接广播,点 Passport 重连,并打开分享通知");
            arm_fast_adv();
            advertise_if_room();
            if (s_disc_timer) esp_timer_start_once(s_disc_timer, 200000);
            return 0;
        }
        refresh_state();
        s_want_adv = true;
        arm_fast_adv();
        schedule_advertise();
        if (s_disc_timer) esp_timer_start_once(s_disc_timer, 200000);
        return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "加密 status=%d handle=%u",
                 event->enc_change.status, event->enc_change.conn_handle);
        if (event->enc_change.status == 0) {
            on_encrypted(event->enc_change.conn_handle);
            apply_coex_ps();
        } else {
            ESP_LOGW(TAG, "加密失败,清理旧绑定 handle=%u",
                     event->enc_change.conn_handle);
            delete_peer_bond(event->enc_change.conn_handle);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        ble_link_t *l = link_find(event->notify_rx.conn_handle);
        if (!l) return 0;
        if (event->notify_rx.attr_handle == l->ns) {
            handle_notify_src(l, event->notify_rx.om);
        } else if (event->notify_rx.attr_handle == l->ds) {
            handle_notify_ds(l, event->notify_rx.om);
        }
        return 0;
    }

    case BLE_GAP_EVENT_MTU: {
        ble_link_t *l = link_find(event->mtu.conn_handle);
        if (l) l->mtu = event->mtu.value;
        return 0;
    }

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "conn_update status=%d handle=%u",
                 event->conn_update.status, event->conn_update.conn_handle);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE: {
        ble_link_t *l = link_find(event->subscribe.conn_handle);
        if (!l) return 0;
        bool on = event->subscribe.cur_notify != 0;
        if (event->subscribe.attr_handle == s_hid_kb_handle) l->hid_kb = on;
        if (event->subscribe.attr_handle == s_hid_cc_handle) l->hid_cc = on;
        return 0;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!slots_free()) return 0;
        if (s_wait_notify) {
            advertise();
            return 0;
        }
        schedule_advertise();
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        delete_peer_bond(event->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = { 0 };
        pkey.action = event->passkey.params.action;
        s_pair_conn = event->passkey.conn_handle;
        ble_link_t *l = link_find(event->passkey.conn_handle);
        if (l) l->new_bond = true;
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.passkey = 100000 + (esp_random() % 900000);
            s_passkey = pkey.passkey;
            s_pair_confirm = false;
            set_state(BSP_BLE_PAIRING);
            ESP_LOGI(TAG, "展示配对码(勿在日志外传播)");
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            apply_coex_ps();
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            s_passkey = event->passkey.params.numcmp;
            s_pair_confirm = true;
            set_state(BSP_BLE_PAIRING);
            ESP_LOGI(TAG, "等待设备端确认数字对比");
            apply_coex_ps();
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            // 无键盘输入,回退为拒绝。
            pkey.passkey = 0;
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;
    }

    default:
        return 0;
    }
}

static void on_reset(int reason) {
    s_synced = false;
    ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
}

static void on_sync(void) {
    s_synced = true;
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr rc=%d", rc);
        return;
    }
    uint8_t mac[6] = { 0 };
    uint8_t wifi[6] = { 0 };
    uint8_t id[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_BT);
    esp_read_mac(wifi, ESP_MAC_WIFI_STA);
    ble_hs_id_copy_addr(s_own_addr_type, id, NULL);
    snprintf(s_name, sizeof(s_name), "Passport-%02X%02X", mac[4], mac[5]);
    ble_svc_gap_device_name_set(s_name);
    ble_svc_gap_device_appearance_set(HID_APPEARANCE);
#if CONFIG_BT_NIMBLE_DIS_SERVICE
    ble_svc_dis_manufacturer_name_set("FoloToy");
    ble_svc_dis_model_number_set("Passport");
#endif
    ESP_LOGI(TAG, "wifi=%02x%02x%02x%02x%02x%02x bt=%02x%02x%02x%02x%02x%02x id=%02x%02x%02x%02x%02x%02x type=%u",
             wifi[0], wifi[1], wifi[2], wifi[3], wifi[4], wifi[5],
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             id[5], id[4], id[3], id[2], id[1], id[0],
             s_own_addr_type);
    ESP_LOGI(TAG, "广播名 %s (HID keyboard), max_conn=%d", s_name, BLE_CONN_MAX);
    if (s_enabled) {
        arm_fast_adv();
        s_want_adv = true;
        s_user_adv = true;
        if (s_have_rnd && set_static_rnd(s_rnd) == 0) {
            ESP_LOGI(TAG, "随机地址 %02x:%02x:%02x:%02x:%02x:%02x",
                     s_rnd[5], s_rnd[4], s_rnd[3], s_rnd[2], s_rnd[1], s_rnd[0]);
            advertise();
            return;
        }
        if (bsp_ble_new_identity() == ESP_OK) return;
        advertise();
    }
    else set_state(BSP_BLE_IDLE);
}

static void host_task(void *param) {
    (void)param;
    ESP_LOGI(TAG, "NimBLE host 启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void stack_reset_links(void)
{
    for (int i = 0; i < BLE_CONN_MAX; i++) link_reset(&s_link[i]);
    s_app_conns = 0;
    s_passkey = 0;
    s_pair_confirm = false;
    s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
    s_wait_notify = false;
    s_notif_head = 0;
    s_notif_n = 0;
    s_pending_valid = false;
    s_fast_adv = false;
    s_forget_all_pending = false;
}

static esp_err_t stack_start(void)
{
    if (s_stack) return ESP_OK;

    size_t free_sz = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (free_sz < 40 * 1024 || blk < 16 * 1024) {
        ESP_LOGW(TAG, "堆不足,跳过 BLE free=%u blk=%u",
                 (unsigned)free_sz, (unsigned)blk);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t e = nimble_port_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init %s", esp_err_to_name(e));
        nimble_port_deinit();
        return e;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
#if CONFIG_BT_NIMBLE_DIS_SERVICE
    ble_svc_dis_init();
    ble_svc_dis_manufacturer_name_set("FoloToy");
    ble_svc_dis_model_number_set("Passport");
#endif
    ble_svc_gap_device_appearance_set(HID_APPEARANCE);
    ble_svc_bas_init();
    ble_svc_bas_battery_level_set(100);
    int rc = ble_gatts_count_cfg(s_hid_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_hid_svcs);
    if (rc != 0) ESP_LOGW(TAG, "HID GATT rc=%d", rc);
    if (s_extra_svcs) {
        rc = ble_gatts_count_cfg(s_extra_svcs);
        if (rc == 0) rc = ble_gatts_add_svcs(s_extra_svcs);
        if (rc != 0) ESP_LOGW(TAG, "extra GATT rc=%d", rc);
    }
    ble_store_config_init();
    ble_att_set_preferred_mtu(185);

    snprintf(s_name, sizeof(s_name), "Passport");
    s_synced = false;
    s_stack_tick = xTaskGetTickCount();
    set_state(BSP_BLE_IDLE);

    nimble_port_freertos_init(host_task);
    s_stack = true;
    bsp_pm_touch();
    ESP_LOGI(TAG, "BLE 栈已启动 max_conn=%d heap=%u largest=%u",
             BLE_CONN_MAX,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    return ESP_OK;
}

static void stack_stop(void)
{
    if (!s_stack) return;

    bool synced = s_synced;
    s_synced = false;
    s_want_adv = false;
    s_user_adv = false;
    s_adv_fast_run = false;
    if (s_adv_timer) esp_timer_stop(s_adv_timer);
    if (s_disc_timer) esp_timer_stop(s_disc_timer);
    if (s_fast_timer) esp_timer_stop(s_fast_timer);
    if (s_data_timer) esp_timer_stop(s_data_timer);
    if (s_app_timer) esp_timer_stop(s_app_timer);
    if (s_hid_rel_timer) esp_timer_stop(s_hid_rel_timer);

    if (synced) {
        ble_gap_adv_stop();
        for (int i = 0; i < BLE_CONN_MAX; i++) {
            if (s_link[i].conn == BLE_HS_CONN_HANDLE_NONE) continue;
            ble_gap_terminate(s_link[i].conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    int rc = nimble_port_stop();
    if (rc != 0) ESP_LOGW(TAG, "nimble_port_stop rc=%d", rc);
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_err_t e = nimble_port_deinit();
    if (e != ESP_OK) ESP_LOGW(TAG, "nimble_port_deinit %s", esp_err_to_name(e));

    stack_reset_links();
    s_stack = false;
    set_state(BSP_BLE_IDLE);
    apply_coex_ps();
    bsp_pm_touch();
    ESP_LOGI(TAG, "BLE 栈已释放 heap=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
}

esp_err_t bsp_ble_init(void) {
    if (s_inited) return ESP_OK;

    for (int i = 0; i < BLE_CONN_MAX; i++) link_reset(&s_link[i]);
    load_ble_flags();
    s_want_adv = true;
    load_pnames();

    const esp_timer_create_args_t data_args = {
        .callback = data_timer_cb,
        .name = "ancs_ds",
    };
    const esp_timer_create_args_t disc_args = {
        .callback = disc_timer_cb,
        .name = "ancs_disc",
    };
    const esp_timer_create_args_t adv_args = {
        .callback = adv_timer_cb,
        .name = "ble_adv",
    };
    const esp_timer_create_args_t app_args = {
        .callback = app_timer_cb,
        .name = "ancs_app",
    };
    const esp_timer_create_args_t fast_args = {
        .callback = fast_timer_cb,
        .name = "ble_fast",
    };
    const esp_timer_create_args_t hid_args = {
        .callback = hid_release_cb,
        .name = "hid_rel",
    };
    if (esp_timer_create(&data_args, &s_data_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    if (esp_timer_create(&disc_args, &s_disc_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    if (esp_timer_create(&adv_args, &s_adv_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    if (esp_timer_create(&app_args, &s_app_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    if (esp_timer_create(&fast_args, &s_fast_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    if (esp_timer_create(&hid_args, &s_hid_rel_timer) != ESP_OK) return ESP_ERR_NO_MEM;

    strlcpy(s_name, "Passport", sizeof(s_name));
    set_state(BSP_BLE_IDLE);
    s_inited = true;

    if (!s_enabled) {
        ESP_LOGI(TAG, "BLE 已关闭,跳过协议栈 heap=%u largest=%u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        return ESP_OK;
    }
    return ESP_OK;
}

bsp_ble_state_t bsp_ble_state(void) {
    return s_state;
}

const char *bsp_ble_name(void) {
    return s_name;
}

uint32_t bsp_ble_passkey(void) {
    return s_passkey;
}

int bsp_ble_conn_count(void) {
    return link_count();
}

int bsp_ble_conn_max(void) {
    return BLE_CONN_MAX;
}

int bsp_ble_bond_count(void) {
    return peer_bond_count();
}

bool bsp_ble_paired(void) {
    return s_paired_ok;
}

bool bsp_ble_pair_needs_confirm(void) {
    return s_pair_confirm && s_state == BSP_BLE_PAIRING;
}

esp_err_t bsp_ble_pair_reply(bool accept) {
    if (!bsp_ble_pair_needs_confirm() || s_pair_conn == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    struct ble_sm_io pkey = { 0 };
    pkey.action = BLE_SM_IOACT_NUMCMP;
    pkey.numcmp_accept = accept ? 1 : 0;
    int rc = ble_sm_inject_io(s_pair_conn, &pkey);
    s_pair_confirm = false;
    if (!accept) s_passkey = 0;
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_ble_unpair(void) {
    if (!s_inited || !s_stack) return ESP_ERR_INVALID_STATE;
    s_wait_notify = false;
    if (s_adv_timer) esp_timer_stop(s_adv_timer);
    if (s_disc_timer) esp_timer_stop(s_disc_timer);

    int n = 0;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        ble_link_t *l = &s_link[i];
        if (l->conn == BLE_HS_CONN_HANDLE_NONE) continue;
        l->hold = false;
        l->unpair = true;
        ble_gap_terminate(l->conn, BLE_ERR_REM_USER_CONN_TERM);
        n++;
    }
    if (n > 0) return ESP_OK;

    ble_store_clear();
    s_paired_ok = false;
    save_ble_flags();
    ESP_LOGI(TAG, "无连接,已清除全部绑定");
    advertise();
    return ESP_OK;
}

esp_err_t bsp_ble_forget_all(void) {
    if (!s_inited || !s_stack) return ESP_ERR_INVALID_STATE;
    s_wait_notify = false;
    s_forget_all_pending = true;
    if (s_adv_timer) esp_timer_stop(s_adv_timer);
    if (s_disc_timer) esp_timer_stop(s_disc_timer);
    if (s_synced) ble_gap_adv_stop();
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        ble_link_t *l = &s_link[i];
        if (l->conn == BLE_HS_CONN_HANDLE_NONE) continue;
        l->hold = false;
        l->unpair = false;
        ble_gap_terminate(l->conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    finish_forget_all();
    return ESP_OK;
}

esp_err_t bsp_ble_new_identity(void)
{
    if (!s_inited || !s_stack || !s_synced) return ESP_ERR_INVALID_STATE;
    uint8_t rnd[6];
    esp_fill_random(rnd, 6);
    rnd[5] = (uint8_t)((rnd[5] & 0x3F) | 0xC0);
    if (set_static_rnd(rnd) != 0) return ESP_FAIL;
    save_ble_flags();
    s_want_adv = true;
    s_user_adv = true;
    s_adv_fast_run = false;
    if (s_synced) ble_gap_adv_stop();
    arm_fast_adv();
    advertise();
    ESP_LOGI(TAG, "新随机地址 %02x:%02x:%02x:%02x:%02x:%02x",
             rnd[5], rnd[4], rnd[3], rnd[2], rnd[1], rnd[0]);
    return ESP_OK;
}

esp_err_t bsp_ble_ensure_advertising(void) {
    return bsp_ble_set_advertising(true);
}

bool bsp_ble_adv_active(void) {
    return s_stack && ble_gap_adv_active() != 0;
}

esp_err_t bsp_ble_set_advertising(bool on) {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!s_enabled) return ESP_ERR_INVALID_STATE;
    s_want_adv = on;
    s_user_adv = on;
    if (!on) {
        if (s_synced) ble_gap_adv_stop();
        s_adv_fast_run = false;
        refresh_state();
        apply_coex_ps();
        ESP_LOGI(TAG, "用户停止广播");
        return ESP_OK;
    }
    if (!s_stack) return bsp_ble_resume();
    if (!s_synced) {
        ESP_LOGI(TAG, "用户开始广播(等同步)");
        return ESP_OK;
    }
    if (!slots_free()) {
        ESP_LOGW(TAG, "连接已满,无法广播");
        refresh_state();
        return ESP_ERR_NO_MEM;
    }
    arm_fast_adv();
    if (s_adv_timer) esp_timer_stop(s_adv_timer);
    advertise();
    ESP_LOGI(TAG, "用户开始广播 active=%d", ble_gap_adv_active());
    refresh_state();
    return ESP_OK;
}

esp_err_t bsp_ble_resume_advertising(void) {
    if (!s_inited || !s_stack) return ESP_ERR_INVALID_STATE;
    begin_connectable_adv();
    return ESP_OK;
}

bool bsp_ble_take_notif(bsp_ble_notif_t *out) {
    if (!out || s_notif_n == 0) return false;
    *out = s_notif_q[s_notif_head];
    s_notif_head = (uint8_t)((s_notif_head + 1) % NOTIF_Q_N);
    s_notif_n--;
    return true;
}

bool bsp_ble_take_removed(uint32_t *uid) {
    if (!uid || !s_rm_n) return false;
    *uid = s_rm[s_rm_head];
    s_rm_head = (uint8_t)((s_rm_head + 1) % RM_N);
    s_rm_n--;
    return true;
}

esp_err_t bsp_ble_notif_action(uint16_t conn, uint32_t uid, uint8_t action) {
    if (!s_stack) return ESP_ERR_INVALID_STATE;
    if (action > BSP_BLE_ACT_NEG) return ESP_ERR_INVALID_ARG;
    ble_link_t *l = link_ancs(conn);
    if (!l) return ESP_ERR_INVALID_STATE;
    uint8_t cmd[6];
    cmd[0] = CMD_PERFORM;
    memcpy(&cmd[1], &uid, 4);
    cmd[5] = action;
    int rc = ble_gattc_write_flat(l->conn, l->cp, cmd, sizeof(cmd), NULL, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ancs action rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void hid_notify(uint16_t handle, const uint8_t *data, uint16_t len)
{
    if (!s_stack || !handle || !data || !len) return;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        ble_link_t *l = &s_link[i];
        uint16_t conn = l->conn;
        if (conn == BLE_HS_CONN_HANDLE_NONE) continue;
        if (handle == s_hid_kb_handle && !l->hid_kb) continue;
        if (handle == s_hid_cc_handle && !l->hid_cc) continue;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
        if (!om) continue;
        int rc = ble_gatts_notify_custom(conn, handle, om);
        if (rc != 0) ESP_LOGW(TAG, "hid notify handle=%u rc=%d", handle, rc);
        else break;
    }
}

static void hid_cc_press(uint16_t usage)
{
    s_hid_cc[0] = (uint8_t)(usage & 0xFF);
    s_hid_cc[1] = (uint8_t)(usage >> 8);
    hid_notify(s_hid_cc_handle, s_hid_cc, sizeof(s_hid_cc));
    s_hid_rel_cc = 1;
}

static void hid_release_cb(void *arg)
{
    (void)arg;
    if (s_hid_rel_cc) {
        s_hid_cc[0] = 0;
        s_hid_cc[1] = 0;
        hid_notify(s_hid_cc_handle, s_hid_cc, sizeof(s_hid_cc));
    } else {
        memset(s_hid_input, 0, sizeof(s_hid_input));
        hid_notify(s_hid_kb_handle, s_hid_input, sizeof(s_hid_input));
    }
}

bool bsp_ble_hid_ready(void)
{
    if (!s_stack) return false;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
        if (s_link[i].conn != BLE_HS_CONN_HANDLE_NONE &&
            (s_link[i].hid_kb || s_link[i].hid_cc)) {
            return true;
        }
    }
    return false;
}

esp_err_t bsp_ble_hid_tap(bsp_ble_hid_key_t key)
{
    if (!bsp_ble_hid_ready()) return ESP_ERR_INVALID_STATE;
    if (s_hid_rel_timer) {
        esp_timer_stop(s_hid_rel_timer);
        hid_release_cb(NULL);
    }
    switch (key) {
    case BSP_BLE_HID_LEFT:
        s_hid_input[2] = 0x50;
        hid_notify(s_hid_kb_handle, s_hid_input, sizeof(s_hid_input));
        s_hid_rel_cc = 0;
        break;
    case BSP_BLE_HID_RIGHT:
        s_hid_input[2] = 0x4F;
        hid_notify(s_hid_kb_handle, s_hid_input, sizeof(s_hid_input));
        s_hid_rel_cc = 0;
        break;
    case BSP_BLE_HID_VOL_UP:
        hid_cc_press(HID_CC_VOL_UP);
        break;
    case BSP_BLE_HID_VOL_DOWN:
        hid_cc_press(HID_CC_VOL_DOWN);
        break;
    case BSP_BLE_HID_PLAY:
        hid_cc_press(HID_CC_PLAY);
        break;
    case BSP_BLE_HID_NEXT:
        hid_cc_press(HID_CC_NEXT);
        break;
    case BSP_BLE_HID_PREV:
        hid_cc_press(HID_CC_PREV);
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    if (s_hid_rel_timer) esp_timer_start_once(s_hid_rel_timer, 80000);
    return ESP_OK;
}

bool bsp_ble_enabled(void) {
    return s_enabled;
}

bool bsp_ble_stack_up(void) {
    return s_stack;
}

bool bsp_ble_synced(void) {
    return s_stack && s_synced;
}

esp_err_t bsp_ble_suspend(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    bool want_adv = s_want_adv;
    bool user_adv = s_user_adv;
    stack_stop();
    s_want_adv = want_adv;
    s_user_adv = user_adv;
    return ESP_OK;
}

esp_err_t bsp_ble_resume(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!s_enabled) return ESP_OK;
    s_want_adv = true;
    s_user_adv = true;
    arm_fast_adv();
    if (s_stack && !s_synced) {
        TickType_t age = xTaskGetTickCount() - s_stack_tick;
        if (age > pdMS_TO_TICKS(2000)) {
            ESP_LOGW(TAG, "BLE 未同步,重建协议栈");
            stack_stop();
            s_want_adv = true;
            s_user_adv = true;
        } else {
            return ESP_OK;
        }
    }
    bool was_up = s_stack;
    esp_err_t e = stack_start();
    if (e != ESP_OK) return e;
    if (was_up) advertise_if_room();
    return ESP_OK;
}

esp_err_t bsp_ble_set_enabled(bool on) {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (s_enabled == on) {
        if (!on && s_stack) stack_stop();
        return ESP_OK;
    }
    s_enabled = on;
    save_ble_flags();
    if (!on) {
        s_want_adv = false;
        s_user_adv = false;
        stack_stop();
        ESP_LOGI(TAG, "BLE 已关闭");
        return ESP_OK;
    }
    s_want_adv = true;
    s_user_adv = true;
    ESP_LOGI(TAG, "BLE 已开启");
    return bsp_ble_resume();
}

bool bsp_ble_quiet(void) {
    return s_quiet;
}

esp_err_t bsp_ble_set_quiet(bool on) {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (s_quiet == on) return ESP_OK;
    s_quiet = on;
    save_ble_flags();
    advertise_if_room();
    return ESP_OK;
}

int bsp_ble_list_peers(bsp_ble_peer_t *out, int max) {
    if (!out || max <= 0 || !s_stack) return 0;
    if (max > BSP_BLE_PEER_MAX) max = BSP_BLE_PEER_MAX;
    int count = 0;
    for (int i = 0; i < max; i++) {
        struct ble_store_key_sec key;
        struct ble_store_value_sec val;
        memset(&key, 0, sizeof(key));
        key.idx = (uint8_t)i;
        if (ble_store_read_peer_sec(&key, &val) != 0) break;
        fmt_addr(out[count].addr, sizeof(out[count].addr), &val.peer_addr);
        fill_peer_name(out[count].name, sizeof(out[count].name), &val.peer_addr);
        out[count].bonded = true;
        out[count].connected = peer_connected(&val.peer_addr);
        count++;
    }
    for (int i = 0; i < BLE_CONN_MAX && count < max; i++) {
        if (s_link[i].conn == BLE_HS_CONN_HANDLE_NONE) continue;
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(s_link[i].conn, &d) != 0) continue;
        const ble_addr_t *id = &d.peer_id_addr;
        bool seen = false;
        char addr[18];
        fmt_addr(addr, sizeof(addr), id);
        for (int j = 0; j < count; j++) {
            if (strcmp(out[j].addr, addr) == 0) { seen = true; break; }
        }
        if (seen) continue;
        strlcpy(out[count].addr, addr, sizeof(out[count].addr));
        fill_peer_name(out[count].name, sizeof(out[count].name), id);
        out[count].bonded = false;
        out[count].connected = true;
        count++;
    }
    return count;
}

esp_err_t bsp_ble_forget_at(int index) {
    if (!s_inited || !s_stack || index < 0) return ESP_ERR_INVALID_ARG;
    bsp_ble_peer_t list[BSP_BLE_PEER_MAX];
    int n = bsp_ble_list_peers(list, BSP_BLE_PEER_MAX);
    if (index >= n) return ESP_ERR_NOT_FOUND;

    struct ble_store_key_sec key;
    struct ble_store_value_sec val;
    memset(&key, 0, sizeof(key));
    key.idx = (uint8_t)index;
    if (ble_store_read_peer_sec(&key, &val) == 0) {
        for (int i = 0; i < BLE_CONN_MAX; i++) {
            if (s_link[i].conn == BLE_HS_CONN_HANDLE_NONE) continue;
            struct ble_gap_conn_desc d;
            if (ble_gap_conn_find(s_link[i].conn, &d) != 0) continue;
            if (addr_eq(&d.peer_id_addr, &val.peer_addr) || addr_eq(&d.peer_ota_addr, &val.peer_addr)) {
                s_link[i].unpair = true;
                ble_gap_terminate(s_link[i].conn, BLE_ERR_REM_USER_CONN_TERM);
                return ESP_OK;
            }
        }
        ble_store_util_delete_peer(&val.peer_addr);
        advertise_if_room();
        return ESP_OK;
    }

    for (int i = 0; i < BLE_CONN_MAX; i++) {
        if (s_link[i].conn == BLE_HS_CONN_HANDLE_NONE) continue;
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(s_link[i].conn, &d) != 0) continue;
        char addr[18];
        fmt_addr(addr, sizeof(addr), &d.peer_id_addr);
        if (strcmp(addr, list[index].addr) == 0) {
            s_link[i].unpair = true;
            ble_gap_terminate(s_link[i].conn, BLE_ERR_REM_USER_CONN_TERM);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void bsp_ble_set_extra_svcs(const void *svcs)
{
    s_extra_svcs = svcs;
}

void bsp_ble_set_scan_uuid128(const uint8_t uuid128[16])
{
    if (!uuid128) {
        s_scan_uuid = false;
        return;
    }
    memcpy(s_scan_uuid128, uuid128, 16);
    s_scan_uuid = true;
}

void bsp_ble_set_scan_mfg(const uint8_t *data, size_t n)
{
    if (!data || n == 0) {
        s_scan_mfg_n = 0;
        return;
    }
    if (n > sizeof(s_scan_mfg)) n = sizeof(s_scan_mfg);
    memcpy(s_scan_mfg, data, n);
    s_scan_mfg_n = (uint8_t)n;
}

void bsp_ble_set_gap_cb(void (*cb)(void *event))
{
    s_gap_cb = cb;
}

void bsp_ble_set_activity_cb(void (*cb)(void))
{
    s_activity_cb = cb;
}

esp_err_t bsp_ble_note_app_conn(int delta)
{
    s_app_conns += delta;
    if (s_app_conns < 0) s_app_conns = 0;
    return ESP_OK;
}

esp_err_t bsp_ble_refresh_adv(void)
{
    if (!s_inited || !s_stack) return ESP_ERR_INVALID_STATE;
    advertise_if_room();
    return ESP_OK;
}

uint8_t bsp_ble_own_addr_type(void)
{
    return s_own_addr_type;
}
