/**
 * @file uds_zephyr_isotp.c
 * @brief Zephyr Native ISO-TP Socket Transport
 */

#include <zephyr/kernel.h>
#include <zephyr/net/can.h>
#include <zephyr/net/socket.h>

#include "uds/uds_core.h"

#ifdef CONFIG_LIBUDS_TRANSPORT_NATIVE

/** Static file descriptor for the ISO-TP socket */
static int g_isotp_fd = -1;

/**
 * @brief Initialize the Zephyr native ISO-TP socket.
 *
 * @param rx_id CAN ID to listen for.
 * @param tx_id CAN ID to transmit on.
 * @return      0 on success, -1 on failure.
 */
int uds_zephyr_isotp_init(uint32_t rx_id, uint32_t tx_id)
{
    struct sockaddr_can addr;

    g_isotp_fd = socket(AF_CAN, SOCK_DGRAM, CAN_ISOTP);
    if (g_isotp_fd < 0) {
        printk("Failed to create ISO-TP socket: %d\n", errno);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = 0; /* Default interface */
    addr.can_addr.tp.rx_id = rx_id;
    addr.can_addr.tp.tx_id = tx_id;

    if (bind(g_isotp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printk("Failed to bind ISO-TP socket: %d\n", errno);
        close(g_isotp_fd);
        g_isotp_fd = -1;
        return -1;
    }

    /* Set non-blocking */
    int flags = fcntl(g_isotp_fd, F_GETFL, 0);
    fcntl(g_isotp_fd, F_SETFL, flags | O_NONBLOCK);

    return 0;
}

/**
 * @brief Send an SDU via the native ISO-TP socket.
 *
 * @param ctx  Pointer to the UDS context.
 * @param data Pointer to the buffer containing the SDU.
 * @param len  Length of the SDU.
 * @return     0 on success, -1 on failure.
 */
int uds_zephyr_isotp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (g_isotp_fd < 0) {
        return -1;
    }
    ssize_t ret = send(g_isotp_fd, data, len, 0);
    return (ret == (ssize_t)len) ? 0 : -1;
}

/**
 * @brief Receive an SDU from the native ISO-TP socket.
 *
 * @param buf  Pointer to the destination buffer.
 * @param size Maximum size of the buffer.
 * @return     Number of bytes received, or -1 on error.
 */
int uds_zephyr_isotp_recv(uint8_t *buf, uint16_t size)
{
    if (g_isotp_fd < 0) {
        return 0;
    }
    ssize_t ret = recv(g_isotp_fd, buf, size, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return (int)ret;
}

#endif /* CONFIG_LIBUDS_TRANSPORT_NATIVE */
