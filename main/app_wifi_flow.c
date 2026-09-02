#include "app_wifi_flow.h"

int app_wifi_item_count(int ap_count, bool has_saved)
{
    if (ap_count < 0) ap_count = 0;
    return 3 + ap_count + (has_saved ? 1 : 0);
}

int app_wifi_window_start(int selected, int items, int rows)
{
    if (items <= 0 || rows <= 0) return 0;
    if (selected < 0) selected = 0;
    if (selected >= items) selected = items - 1;
    int start = selected - rows / 2;
    if (start < 0) start = 0;
    if (start + rows > items) start = items > rows ? items - rows : 0;
    return start;
}

bool app_wifi_scan_result_current(uint32_t token, uint32_t epoch, bool open)
{
    return open && token == epoch;
}
