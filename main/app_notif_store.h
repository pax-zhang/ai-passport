#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 通知历史。纯逻辑,可在 host 上测试。
// 每条通知都进这里,包括被规则判为静默的,分档结果记在 alert 上。

#define APP_NOTIF_STORE_N 20
#define APP_NOTIF_APP_ID   40
#define APP_NOTIF_APP_NAME 32
#define APP_NOTIF_TITLE    40
#define APP_NOTIF_SUB      32
#define APP_NOTIF_MSG      80
#define APP_NOTIF_DATE     15
#define APP_NOTIF_CAT_N    12

typedef enum {
    APP_ALERT_SILENT = 0,  // 只记录 + 未读角标,不弹不响
    APP_ALERT_POPUP = 1,   // 弹窗 + 消息音 + 自动隐藏
    APP_ALERT_URGENT = 2,  // 弹窗常驻 + 警报音
    APP_ALERT_DROP = 3,    // 未匹配丢弃,不写历史
} app_alert_t;

typedef struct {
    char app_id[APP_NOTIF_APP_ID + 1];
    char app_name[APP_NOTIF_APP_NAME + 1];
    char title[APP_NOTIF_TITLE + 1];
    char subtitle[APP_NOTIF_SUB + 1];
    char message[APP_NOTIF_MSG + 1];
    char date[APP_NOTIF_DATE + 1];
    uint32_t uid;  // ANCS UID,0 表示无法对齐
    uint8_t category;
    uint8_t alert;
    uint8_t unread;
} app_notif_rec_t;

typedef struct {
    app_notif_rec_t items[APP_NOTIF_STORE_N];
    int head;
    int count;
} app_notif_store_t;

typedef struct {
    char app_id[APP_NOTIF_APP_ID + 1];
    char app_name[APP_NOTIF_APP_NAME + 1];
    uint8_t category;
    int count;
} app_notif_group_t;

void app_notif_store_init(app_notif_store_t *s);
void app_notif_store_clear(app_notif_store_t *s);
// uid 非 0 且已存在时原地更新(ANCS MODIFIED),否则插入新条目。
bool app_notif_store_push(app_notif_store_t *s, const app_notif_rec_t *r);
bool app_notif_store_remove_uid(app_notif_store_t *s, uint32_t uid);
int app_notif_store_count(const app_notif_store_t *s);
// newest_i 0 = 最新
const app_notif_rec_t *app_notif_store_at(const app_notif_store_t *s, int newest_i);
bool app_notif_store_remove(app_notif_store_t *s, int newest_i);
int app_notif_store_find_uid(const app_notif_store_t *s, uint32_t uid);

int app_notif_store_unread(const app_notif_store_t *s);
bool app_notif_store_mark_read(app_notif_store_t *s, int newest_i);
bool app_notif_store_mark_unread(app_notif_store_t *s, int newest_i);
bool app_notif_store_mark_read_uid(app_notif_store_t *s, uint32_t uid);
void app_notif_store_mark_all_read(app_notif_store_t *s);

const char *app_notif_rec_key(const app_notif_rec_t *r);
void app_notif_rec_label(const app_notif_rec_t *r, char *out, size_t n);
int app_notif_store_apps(const app_notif_store_t *s, app_notif_group_t *out, int max);
int app_notif_store_cats(const app_notif_store_t *s, app_notif_group_t *out, int max);
int app_notif_store_match_app(const app_notif_store_t *s, const char *app_id,
                              int *idx, int max);
int app_notif_store_match_cat(const app_notif_store_t *s, uint8_t category,
                              int *idx, int max);

// 旧到新排列,便于跨版本改容量后仍能回灌。返回写入字节数,0 表示放不下。
size_t app_notif_store_blob_size(const app_notif_store_t *s);
size_t app_notif_store_serialize(const app_notif_store_t *s, void *buf, size_t n);
bool app_notif_store_deserialize(app_notif_store_t *s, const void *buf, size_t n);
