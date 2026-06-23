/**
 * @file uds_zephyr_tp_fallback.c
 * @brief Zephyr ISO-TP Fallback (Classical CAN) Transport
 */

#include <errno.h>

#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

#if defined(CONFIG_UDSLIB_TRANSPORT_FALLBACK)

/** Static reference to the CAN controller device */
static const struct device *g_can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

/** CAN frames handed to the controller but not yet confirmed on the wire.
 *  Lets uds_zephyr_tp_fallback_tx_idle() back a deterministic fn_tx_complete so
 *  a deferred ECUReset/LinkControl fires only once the response has drained. */
static atomic_t g_tx_inflight = ATOMIC_INIT(0);

/** Pointer to the current UDS context (for RX callback matching) */
static struct uds_ctx *g_current_uds_ctx = NULL;

/** ISO-TP transport instance and its multi-frame TX cache. */
static uds_isotp_ctx_t g_isotp;
static uint8_t g_isotp_tx_sdu[CONFIG_UDSLIB_TX_BUFFER_SIZE];

/**
 * @brief Internal Helper: Zephyr CAN Transmission Wrapper.
 *
 * @param id   CAN ID to transmit.
 * @param data Pointer to the 8-byte frame data.
 * @param len  Length of the data (DLC).
 * @return     0 on success, negative error code on failure.
 */
/* TX-done callback: the frame has physically left the controller. */
static void uds_internal_zephyr_tx_done(const struct device *dev, int error, void *user_data)
{
    (void) dev;
    (void) error;
    (void) user_data;
    atomic_dec(&g_tx_inflight);
}

static int uds_internal_zephyr_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (!device_is_ready(g_can_dev)) {
        return -1;
    }

    struct can_frame frame = {.id = id, .dlc = len, .flags = 0};
    memcpy(frame.data, data, len);

    /* Async send: completion is reported via uds_internal_zephyr_tx_done so the
     * UDS thread never blocks on transmission (the timeout only bounds the wait
     * for a free TX mailbox). */
    atomic_inc(&g_tx_inflight);
    int ret = can_send(g_can_dev, &frame, K_MSEC(10), uds_internal_zephyr_tx_done, NULL);
    if (ret != 0) {
        atomic_dec(&g_tx_inflight); /* the callback will not run */
    }
    return ret;
}

/* True once every queued CAN frame has been confirmed on the wire. Wire this as
 * config.fn_tx_complete so a deferred post-TX action (ECUReset, LinkControl)
 * fires deterministically rather than after a fixed delay. */
bool uds_zephyr_tp_fallback_tx_idle(void)
{
    return atomic_get(&g_tx_inflight) == 0;
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
    (void) dev;
    (void) user_data;
    if (g_current_uds_ctx) {
        uds_isotp_rx_callback(&g_isotp, g_current_uds_ctx, frame->id, frame->data, frame->dlc);
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
    uds_tp_isotp_init(&g_isotp, uds_internal_zephyr_can_send, tx_id, rx_id, g_isotp_tx_sdu,
                      sizeof(g_isotp_tx_sdu));

    struct can_filter filter = {.id = rx_id, .mask = CAN_STD_ID_MASK, .flags = 0};

    int filter_id = can_add_rx_filter(g_can_dev, uds_internal_zephyr_can_rx_cb, NULL, &filter);
    if (filter_id < 0) {
        printk("Failed to add CAN RX filter (%d)\n", filter_id);
        return -1;
    }

    /* The Zephyr CAN controller powers up in the stopped state; without an
     * explicit start, can_send() returns -ENETDOWN and the RX filter never
     * fires, so the transport would silently move no frames. -EALREADY means
     * something already started the controller, which is fine. */
    int ret = can_start(g_can_dev);
    if (ret < 0 && ret != -EALREADY) {
        printk("Failed to start CAN controller (%d)\n", ret);
        return -1;
    }

    return 0;
}

/**
 * @brief fn_tp_send adapter binding the core contract to this instance.
 *
 * The ISO-TP context is owned privately by this module, so the application
 * wires this function (which matches uds_tp_send_fn) as config.fn_tp_send.
 */
int uds_zephyr_tp_fallback_send(struct uds_ctx *uds_ctx, const uint8_t *data, uint16_t len)
{
    (void) uds_ctx;
    return uds_isotp_send(&g_isotp, data, len);
}

/**
 * @brief Drive the fallback ISO-TP timers/CF transmission.
 *
 * @param time_ms Current system time in milliseconds.
 */
void uds_zephyr_tp_fallback_process(uint32_t time_ms)
{
    uds_tp_isotp_process(&g_isotp, time_ms);
}

#endif /* CONFIG_UDSLIB_TRANSPORT_FALLBACK */
