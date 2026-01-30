/**
 * @file uds_zephyr_port.c
 * @brief Zephyr OS Porting Layer (Time and Logging)
 */

#include <zephyr/kernel.h>

#include "uds/uds_core.h"

/**
 * @brief Get the monotonic uptime in milliseconds.
 *
 * @return uint32_t Timestamp in milliseconds.
 */
uint32_t uds_get_time_ms_zephyr(void)
{
    return k_uptime_get_32();
}

/**
 * @brief Zephyr-specific logging implementation.
 *
 * @param level Log severity level.
 * @param msg   Null-terminated message string.
 */
void uds_log_zephyr(uint8_t level, const char *msg)
{
    printk("[%u] UDS: %s\n", level, msg);
}
