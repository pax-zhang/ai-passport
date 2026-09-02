#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_NET_WEATHER = 0,
    APP_NET_SCAN,
    APP_NET_WEB,
    APP_NET_OTA
} app_net_owner_t;

void app_net_init(void);
bool app_net_acquire(app_net_owner_t owner, uint32_t timeout_ms);
void app_net_release(app_net_owner_t owner);
bool app_net_heap_ready(size_t min_block);
