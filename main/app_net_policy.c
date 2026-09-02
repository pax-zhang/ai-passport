#include "app_net_policy.h"

bool app_net_owner_valid(app_net_owner_t owner)
{
    return owner >= APP_NET_WEATHER && owner <= APP_NET_OTA;
}

bool app_net_should_yield(app_net_owner_t owner, unsigned ota_waiting)
{
    return owner != APP_NET_OTA && ota_waiting > 0;
}
