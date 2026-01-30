#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

#if defined(CONFIG_LIBUDS_TRANSPORT_FALLBACK)

static const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static struct uds_ctx *current_uds_ctx = NULL;

static int zephyr_can_send_wrapper(uint32_t id, const uint8_t *data, uint8_t len) {
    if (!device_is_ready(can_dev)) {
        return -1;
    }

    struct can_frame frame = {
        .id = id,
        .dlc = len,
        .flags = 0
    };
    memcpy(frame.data, data, len);

    return can_send(can_dev, &frame, K_MSEC(10), NULL, NULL);
}

static void zephyr_can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data) {
    (void)dev;
    if (current_uds_ctx) {
        uds_isotp_rx_callback(current_uds_ctx, frame->id, frame->data, frame->dlc);
    }
}

int uds_zephyr_tp_fallback_init(struct uds_ctx *uds_ctx, uint32_t rx_id, uint32_t tx_id) {
    if (!device_is_ready(can_dev)) {
        printk("CAN device not ready\n");
        return -1;
    }

    current_uds_ctx = uds_ctx;
    uds_tp_isotp_init(zephyr_can_send_wrapper, tx_id, rx_id);

    struct can_filter filter = {
        .id = rx_id,
        .mask = CAN_STD_ID_MASK,
        .flags = 0
    };

    return can_add_rx_filter(can_dev, zephyr_can_rx_callback, NULL, &filter);
}

#endif
