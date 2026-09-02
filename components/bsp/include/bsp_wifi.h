// components/bsp/include/bsp_wifi.h
// ESP32-C3 2.4 GHz STA:扫描、连接、NVS 记忆、开机自动重连。
//
// 线程:bsp_wifi_scan() / bsp_wifi_connect() 会阻塞,必须在工作任务中调用,
// 禁止放进按键回调或 LVGL 任务。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BSP_WIFI_SSID_MAX 32
#define BSP_WIFI_PASS_MAX 64
#define BSP_WIFI_SCAN_MAX 16

typedef enum {
    BSP_WIFI_IDLE = 0,
    BSP_WIFI_CONNECTING,
    BSP_WIFI_CONNECTED,
    BSP_WIFI_FAILED,
} bsp_wifi_state_t;

typedef struct {
    char ssid[BSP_WIFI_SSID_MAX + 1];
    int8_t rssi;
    bool open;          // true = 无需密码
} bsp_wifi_ap_t;

// 初始化 NVS、netif、STA。幂等。
// 若 NVS 里已有成功过的凭据,会在后台自动连接,不阻塞。
esp_err_t bsp_wifi_init(void);

bsp_wifi_state_t bsp_wifi_state(void);

// 当前目标/已连 SSID。从未配置时返回空字符串。指针指向内部缓冲,不要保存后跨调用修改。
const char *bsp_wifi_ssid(void);

// 把当前 IPv4 写进 buf,未获得地址时写 "0.0.0.0"。
esp_err_t bsp_wifi_ip(char *buf, size_t n);

// 阻塞扫描。返回 AP 数量(0..max);失败返回负数。
// 同一 SSID 只保留 RSSI 最强的一条,结果按信号从强到弱排序。
int bsp_wifi_scan(bsp_wifi_ap_t *out, int max);
void bsp_wifi_scan_cancel(void);

// 启动连接。password 在开放网络上可传 ""。成功拿到 IP 后才写入 NVS。
esp_err_t bsp_wifi_connect(const char *ssid, const char *password);

// 断开并删除已存凭据。之后不会自动重连,直到再次 connect 成功。
esp_err_t bsp_wifi_forget(void);

bool bsp_wifi_has_saved(void);

// 若 ssid 与已存网络相同,把密码拷到 pass(用于重新输入时预填)。否则返回 false。
bool bsp_wifi_saved_pass(const char *ssid, char *pass, size_t n);

// 射频开关。关闭后断开并 stop,图标应隐藏。持久化到 NVS。
bool bsp_wifi_enabled(void);
esp_err_t bsp_wifi_set_enabled(bool on);

// 开机/掉线后是否自动重连已存网络。手动连接不受影响。
bool bsp_wifi_auto_connect(void);
esp_err_t bsp_wifi_set_auto_connect(bool on);

// Modem 省电。true = WIFI_PS_MIN_MODEM(已连接时的默认);
// false = WIFI_PS_NONE。BLE 快速广播/配对会暂时关掉省电。
bool bsp_wifi_power_save(void);
esp_err_t bsp_wifi_set_power_save(bool on);
// 嵌套:天气 HTTP 拉取期间强制 WIFI_PS_NONE,避免 BLE 把省电又打开导致收包失败。
void bsp_wifi_ps_hold(void);
void bsp_wifi_ps_release(void);

// 息屏时停射频,不改 NVS 开关。联网功能自行 resume。不要为了 BLE 停 Wi-Fi。
// 热点开启时 suspend 为空操作,避免手机掉线。
esp_err_t bsp_wifi_radio_suspend(void);
esp_err_t bsp_wifi_radio_resume(void);

#define BSP_WIFI_AP_SSID_MAX 32

esp_err_t bsp_wifi_ap_start(void);
esp_err_t bsp_wifi_ap_stop(void);
bool bsp_wifi_ap_active(void);
const char *bsp_wifi_ap_ssid(void);
esp_err_t bsp_wifi_ap_ip(char *buf, size_t n);
