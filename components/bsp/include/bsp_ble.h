// components/bsp/include/bsp_ble.h
// ESP32-C3 BLE 外设 + ANCS 客户端:广播、多设备绑定、订阅 iPhone 通知。
// 最多同时 2 路连接,绑定槽 6。ANCS 仅 iPhone;Mac 等只当 HID。
//
// 线程:GAP/GATT 回调在 NimBLE host 任务。不要在回调里操作 LVGL。
// 应用用 bsp_ble_state() / bsp_ble_take_notif() 轮询。不要在日志里打印通知正文。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BSP_BLE_NAME_MAX     24
#define BSP_BLE_APP_ID_MAX   48
#define BSP_BLE_APP_NAME_MAX 32
#define BSP_BLE_TITLE_MAX    48
#define BSP_BLE_SUBTITLE_MAX 48
#define BSP_BLE_MSG_MAX      128
#define BSP_BLE_DATE_MAX     15
#define BSP_BLE_LABEL_MAX    24
#define BSP_BLE_ACT_POS      0
#define BSP_BLE_ACT_NEG      1
#define BSP_BLE_FLAG_POS     (1u << 3)
#define BSP_BLE_FLAG_NEG     (1u << 4)
#define BSP_BLE_CAT_INCOMING 1
#define BSP_BLE_EVT_ADDED    0
#define BSP_BLE_EVT_MODIFIED 1

typedef enum {
    BSP_BLE_IDLE = 0,
    BSP_BLE_ADVERTISING,
    BSP_BLE_PAIRING,
    BSP_BLE_WAIT_NOTIFY,  // 新绑定后短暂断开,等 iPhone 打开分享通知;广播仍可连接
    BSP_BLE_CONNECTED,
    BSP_BLE_ANCS,
} bsp_ble_state_t;

typedef struct {
    char app_id[BSP_BLE_APP_ID_MAX + 1];
    char app_name[BSP_BLE_APP_NAME_MAX + 1];
    char title[BSP_BLE_TITLE_MAX + 1];
    char subtitle[BSP_BLE_SUBTITLE_MAX + 1];
    char message[BSP_BLE_MSG_MAX + 1];
    char date[BSP_BLE_DATE_MAX + 1];  // ANCS ISO 8601: YYYYMMDDTHHMMSS
    char pos_label[BSP_BLE_LABEL_MAX + 1];
    char neg_label[BSP_BLE_LABEL_MAX + 1];
    uint32_t uid;
    uint16_t conn;
    uint8_t flags;
    uint8_t category;
    uint8_t event;  // BSP_BLE_EVT_ADDED / BSP_BLE_EVT_MODIFIED
} bsp_ble_notif_t;

// 初始化 NimBLE、开始广播。幂等。NVS 用于绑定密钥。
esp_err_t bsp_ble_init(void);

bsp_ble_state_t bsp_ble_state(void);

// 广播名,形如 Passport-B4EC。指针指向内部缓冲。
const char *bsp_ble_name(void);

// PAIRING 时的 6 位码;其它状态为 0。
uint32_t bsp_ble_passkey(void);

// true = 数字对比,需要 bsp_ble_pair_reply(); false = 仅展示,iPhone 端输入。
bool bsp_ble_pair_needs_confirm(void);

esp_err_t bsp_ble_pair_reply(bool accept);

// 忘掉当前已连接的对端并断开;其它绑定保留。无连接时清除全部绑定。
esp_err_t bsp_ble_unpair(void);

// 断开全部连接并清除所有绑定,随后重新广播。
esp_err_t bsp_ble_forget_all(void);

// 换新的随机静态地址并广播,避免 iPhone 把旧地址从列表里抹掉。
esp_err_t bsp_ble_new_identity(void);

int bsp_ble_conn_count(void);
int bsp_ble_conn_max(void);
int bsp_ble_bond_count(void);

bool bsp_ble_paired(void);

// 仍有空闲连接槽时开始广播。进入 BLE 演示页时调用。
// 未满员时也会广播,方便再配 Mac / 另一部手机。
esp_err_t bsp_ble_ensure_advertising(void);

// 当前是否正在广播(可连接)。已连接时也可能同时广播。
bool bsp_ble_adv_active(void);

// 用户手动开/关可发现。打开时即使 quiet 也会广播,直到再次关闭或满员。
esp_err_t bsp_ble_set_advertising(bool on);

// 结束 WAIT_NOTIFY,立即重新广播。绑定密钥保留。
esp_err_t bsp_ble_resume_advertising(void);

// 若有尚未取走的完整通知,拷到 out 并返回 true。内部是深度 4 的队列,连发的
// 通知不会互相覆盖;调用方应循环取到返回 false 为止。
bool bsp_ble_take_notif(bsp_ble_notif_t *out);

// 对端已撤掉该 UID(接听、拒接、清除)。可连取。
bool bsp_ble_take_removed(uint32_t *uid);

// ANCS Perform Notification Action。action: BSP_BLE_ACT_POS / NEG。
esp_err_t bsp_ble_notif_action(uint16_t conn, uint32_t uid, uint8_t action);

// 完整 ANCS 或进入配对时调用。在 NimBLE host 任务,禁止直接操作 LVGL。
void bsp_ble_set_activity_cb(void (*cb)(void));

// 射频/广播开关。关闭后停止广播、断开,并释放 NimBLE 主机/控制器堆。
// 未开启时图标应隐藏。关射频不会清绑定。
bool bsp_ble_enabled(void);
esp_err_t bsp_ble_set_enabled(bool on);

// NimBLE 是否在跑。关射频或 suspend 后为 false,此时不可调用 GAP/GATT。
bool bsp_ble_stack_up(void);
bool bsp_ble_synced(void);
// 卸栈但不改开关/NVS。用来腾出 ESP32-C3 内部 RAM。
esp_err_t bsp_ble_suspend(void);
// 若开关仍为开,则重新拉起协议栈并广播。
esp_err_t bsp_ble_resume(void);

// 列表中已绑定设备都连上后停止广播。无绑定时仍广播以便首对。
bool bsp_ble_quiet(void);
esp_err_t bsp_ble_set_quiet(bool on);

#define BSP_BLE_PEER_MAX 6

typedef struct {
    char name[BSP_BLE_NAME_MAX + 1];
    char addr[18];
    bool connected;
    bool bonded;
} bsp_ble_peer_t;

// 绑定设备 + 当前已连接但尚未绑定的对端。返回条数。
int bsp_ble_list_peers(bsp_ble_peer_t *out, int max);

// 忘掉列表中第 index 项(先断开再删绑定)。
esp_err_t bsp_ble_forget_at(int index);

// 在 bsp_ble_init() 之前登记额外 GATT 服务(NimBLE ble_gatt_svc_def 数组)。
void bsp_ble_set_extra_svcs(const void *svcs);

// 扫描响应里放 128-bit 服务 UUID,便于另一台 Passport 主动扫描发现。
void bsp_ble_set_scan_uuid128(const uint8_t uuid128[16]);

// 扫描响应厂商数据,最多 8 字节。
void bsp_ble_set_scan_mfg(const uint8_t *data, size_t n);

// GAP 事件旁路。回调在 NimBLE host 任务,参数为 struct ble_gap_event *。
void bsp_ble_set_gap_cb(void (*cb)(void *event));

// 应用自己占用的连接。计入空闲槽,避免再广播把控制器打满。
esp_err_t bsp_ble_note_app_conn(int delta);

// 按当前 scan UUID/厂商数据重发广播。
esp_err_t bsp_ble_refresh_adv(void);

uint8_t bsp_ble_own_addr_type(void);

typedef enum {
    BSP_BLE_HID_PLAY = 0,
    BSP_BLE_HID_PREV,
    BSP_BLE_HID_NEXT,
    BSP_BLE_HID_VOL_DOWN,
    BSP_BLE_HID_VOL_UP,
    BSP_BLE_HID_LEFT,
    BSP_BLE_HID_RIGHT,
} bsp_ble_hid_key_t;

bool bsp_ble_hid_ready(void);
// 按下并松开。需已连接。改报告描述后需在手机上忘记并重配。
esp_err_t bsp_ble_hid_tap(bsp_ble_hid_key_t key);

