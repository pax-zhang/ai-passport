#include "app_net_policy.h"

#include <assert.h>

int main(void)
{
    assert(app_net_owner_valid(APP_NET_WEATHER));
    assert(app_net_owner_valid(APP_NET_SCAN));
    assert(app_net_owner_valid(APP_NET_WEB));
    assert(app_net_owner_valid(APP_NET_OTA));
    assert(!app_net_owner_valid((app_net_owner_t)-1));
    assert(!app_net_owner_valid((app_net_owner_t)99));

    assert(!app_net_should_yield(APP_NET_WEATHER, 0));
    assert(app_net_should_yield(APP_NET_WEATHER, 1));
    assert(app_net_should_yield(APP_NET_SCAN, 1));
    assert(app_net_should_yield(APP_NET_WEB, 1));
    assert(!app_net_should_yield(APP_NET_OTA, 1));
    return 0;
}
