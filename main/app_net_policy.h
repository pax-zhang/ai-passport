#pragma once

#include "app_net.h"

bool app_net_owner_valid(app_net_owner_t owner);
bool app_net_should_yield(app_net_owner_t owner, unsigned ota_waiting);
