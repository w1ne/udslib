/**
 * @file uds_zephyr_tp_fallback.c
 * @brief Zephyr ISO-TP Fallback (Classical CAN) Transport
 */

#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

#if defined(CONFIG_LIBUDS_TRANSPORT_FALLBACK)

/** Static reference to the CAN controller device */
static const struct device *g_can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

/** Pointer to the current UDS context (for RX callback matching) */
static struct uds_ctx *g_current_uds_ctx = NULL;

/**
 * @brief Internal Helper: Zephyr CAN Transmission Wrapper.
 *
 * @param id   CAN ID to transmit.
 * @param data Pointer to the 8-byte frame data.
 * @param len  Length of the data (DLC).
 * @return     0 on success, negative error code on failure.
 */
static int uds_internal_zephyr_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (!device_is_ready(g_can_dev)) {
        return -1;
    }

    struct can_frame frame = {.id = id, .dlc = len, .flags = 0};
    memcpy(frame.data, data, len);

    return can_send(g_can_dev, &frame, K_MSEC(10), NULL, NULL);
}

/**
 * @brief Internal Helper: Zephyr CAN RX Filter Callback.
 *
 * @param dev       Pointer to the CAN device.
 * @param frame     Pointer to the received CAN frame.
 * @param user_data Opaque pointer to user data (unused).
 */
static void uds_internal_zephyr_can_rx_cb(const struct device *dev, struct can_frame *frame,
                                          void *user_data)
{
    (void)dev;
    (void)user_data;
    if (g_current_uds_ctx) {
        uds_isotp_rx_callback(g_current_uds_ctx, frame->id, frame->data, frame->dlc);
    }
}

/**
 * @brief Initialize the Zephyr ISO-TP fallback transport.
 *
 * @param uds_ctx Pointer to the main stack context.
 * @param rx_id   CAN ID to filter for.
 * @param tx_id   CAN ID to transmit on.
 * @return        0 on success, -1 on failure.
 */
int uds_zephyr_tp_fallback_init(struct uds_ctx *uds_ctx, uint32_t rx_id, uint32_t tx_id)
{
    if (!device_is_ready(g_can_dev)) {
        printk("CAN device not ready\n");
        return -1;
    }

    g_current_uds_ctx = uds_ctx;
    uds_tp_isotp_init(uds_internal_zephyr_can_send, tx_id, rx_id);

    struct can_filter filter = {.id = rx_id, .mask = CAN_STD_ID_MASK, .flags = 0};

    return can_add_rx_filter(g_can_dev, uds_internal_zephyr_can_rx_cb, NULL, &filter);
}

#endif /* CONFIG_LIBUDS_TRANSPORT_FALLBACK */
