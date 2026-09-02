#pragma once

#include <stdbool.h>
#include <stdint.h>

int app_wifi_item_count(int ap_count, bool has_saved);
int app_wifi_window_start(int selected, int items, int rows);
bool app_wifi_scan_result_current(uint32_t token, uint32_t epoch, bool open);
