#pragma once

#include "app_logic.h"
#include "app_notif_store.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *app_id;
    const char *app_name;
    const char *title;
    const char *subtitle;
    const char *message;
    uint8_t category;
} app_notif_ctx_t;

#define APP_RULE_VER 1

// 规则有序,首条命中即定档;都不命中返回 def。
// dnd 为真时把 POPUP 降级为 SILENT,URGENT 不受影响。
app_alert_t app_notif_decide(const app_notif_ctx_t *ctx, const app_kw_t *rules,
                             int rule_n, bool dnd, app_alert_t def);

// 版本 0 的规则只有 0=普通/1=高两档,且未命中即静默。升级成三档并把这条隐含
// 语义写进 def,老用户的行为保持不变。
void app_notif_rules_upgrade(app_kw_t *rules, int rule_n, uint8_t *def);

void app_rule_short(const char *expr, char *out, size_t n);
void app_rule_term(char *out, size_t n, int field, int op, const char *val);
void app_rule_append(char *expr, size_t n, int join, const char *term, bool first);

enum {
    APP_RULE_F_ANY = 0,
    APP_RULE_F_TITLE,
    APP_RULE_F_SUB,
    APP_RULE_F_MSG,
    APP_RULE_F_APP,
    APP_RULE_F_NAME,
    APP_RULE_F_CAT,
    APP_RULE_F_N
};

enum {
    APP_RULE_OP_HAS = 0,
    APP_RULE_OP_NOT,
    APP_RULE_OP_HEAD,
    APP_RULE_OP_TAIL,
    APP_RULE_OP_MATCH,
    APP_RULE_OP_EMPTY,
    APP_RULE_OP_N
};

enum {
    APP_RULE_J_AND = 0,
    APP_RULE_J_OR,
    APP_RULE_J_ANDNOT,
    APP_RULE_J_ORNOT,
    APP_RULE_J_N
};
