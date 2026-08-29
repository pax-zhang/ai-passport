#pragma once

#include "app_farm.h"
#include <stdbool.h>
#include <stdint.h>

#define APP_FARM_NET_PEER_MAX 16

typedef enum {
    APP_FARM_NET_IDLE = 0,
    APP_FARM_NET_BUSY,
    APP_FARM_NET_OK,
    APP_FARM_NET_FAIL
} app_farm_net_st_t;

void app_farm_net_init(void);
app_farm_net_st_t app_farm_net_state(void);
bool app_farm_net_busy(void);
const char *app_farm_net_detail(void);

bool app_farm_net_register(const app_farm_t *f, const uint8_t mac[6]);
bool app_farm_net_sync(const app_farm_t *f);
bool app_farm_net_pull(const app_farm_t *f);
bool app_farm_net_random(const app_farm_t *f);
bool app_farm_net_visit(const app_farm_t *f, uint32_t id);
bool app_farm_net_steal(const app_farm_t *f, uint32_t target, int plot);
bool app_farm_net_help(const app_farm_t *f, uint32_t target, int plot, int act);
bool app_farm_net_friends(const app_farm_t *f);
bool app_farm_net_inbox(const app_farm_t *f);
bool app_farm_net_add(const app_farm_t *f, uint32_t id);
bool app_farm_net_reply(const app_farm_t *f, uint32_t id, bool accept);
bool app_farm_net_remove(const app_farm_t *f, uint32_t id);
bool app_farm_net_rank(const app_farm_t *f);

const app_farm_view_t *app_farm_net_view(void);
int app_farm_net_peers(app_farm_peer_t *out, int max);
int app_farm_net_mail(app_farm_mail_t *out, int max);
bool app_farm_net_already(void);
bool app_farm_net_linked(void);
uint16_t app_farm_net_last_coins(void);
void app_farm_net_quota(uint8_t out[APP_FARM_ACT_N]);
bool app_farm_net_take_token(char *out, size_t n);
bool app_farm_net_take_self(app_farm_t *f);
void app_farm_net_clear(void);
