#include <zephyr/kernel.h>
#include "uds/uds_core.h"

uint32_t uds_get_time_ms_zephyr(void) {
    return k_uptime_get_32();
}

void uds_log_zephyr(uint8_t level, const char* msg) {
    printk("[%u] UDS: %s\n", level, msg);
}
