#include "app_net.h"
#include "app_net_policy.h"

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static SemaphoreHandle_t s_gate;
static StaticSemaphore_t s_gate_buf;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static app_net_owner_t s_owner;
static bool s_owned;
static uint8_t s_ota_waiting;

void app_net_init(void)
{
    if (!s_gate) {
        s_gate = xSemaphoreCreateBinaryStatic(&s_gate_buf);
        if (s_gate) xSemaphoreGive(s_gate);
    }
}

bool app_net_heap_ready(size_t min_block)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
           min_block;
}

bool app_net_acquire(app_net_owner_t owner, uint32_t timeout_ms)
{
    if (!app_net_owner_valid(owner)) return false;
    app_net_init();
    if (!s_gate) return false;

    if (owner == APP_NET_OTA) {
        portENTER_CRITICAL(&s_lock);
        s_ota_waiting++;
        portEXIT_CRITICAL(&s_lock);
    } else {
        portENTER_CRITICAL(&s_lock);
        bool blocked = app_net_should_yield(owner, s_ota_waiting);
        portEXIT_CRITICAL(&s_lock);
        if (blocked) return false;
    }

    bool ok = xSemaphoreTake(s_gate, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    if (owner == APP_NET_OTA) {
        portENTER_CRITICAL(&s_lock);
        s_ota_waiting--;
        portEXIT_CRITICAL(&s_lock);
    }
    if (!ok) return false;

    portENTER_CRITICAL(&s_lock);
    s_owned = true;
    s_owner = owner;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

void app_net_release(app_net_owner_t owner)
{
    portENTER_CRITICAL(&s_lock);
    bool release = s_owned && s_owner == owner;
    if (release) s_owned = false;
    portEXIT_CRITICAL(&s_lock);
    if (release && s_gate) xSemaphoreGive(s_gate);
}
