#include "app_wifi_flow.h"

#include <assert.h>

int main(void)
{
    assert(app_wifi_item_count(-1, false) == 3);
    assert(app_wifi_item_count(0, true) == 4);
    assert(app_wifi_item_count(16, false) == 19);

    assert(app_wifi_window_start(0, 10, 7) == 0);
    assert(app_wifi_window_start(5, 10, 7) == 2);
    assert(app_wifi_window_start(9, 10, 7) == 3);
    assert(app_wifi_window_start(99, 10, 7) == 3);
    assert(app_wifi_window_start(-1, 10, 7) == 0);
    assert(app_wifi_window_start(0, 0, 7) == 0);

    assert(app_wifi_scan_result_current(3, 3, true));
    assert(!app_wifi_scan_result_current(2, 3, true));
    assert(!app_wifi_scan_result_current(3, 3, false));
    return 0;
}
