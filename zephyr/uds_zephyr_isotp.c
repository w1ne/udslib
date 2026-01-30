#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/can.h>
#include "uds/uds_core.h"

#ifdef CONFIG_LIBUDS_TRANSPORT_NATIVE

static int isotp_fd = -1;

int uds_zephyr_isotp_init(uint32_t rx_id, uint32_t tx_id) {
    struct sockaddr_can addr;
    struct can_filter rfilter;

    isotp_fd = socket(AF_CAN, SOCK_DGRAM, CAN_ISOTP);
    if (isotp_fd < 0) {
        printk("Failed to create ISO-TP socket: %d\n", errno);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = 0; // Default interface
    addr.can_addr.tp.rx_id = rx_id;
    addr.can_addr.tp.tx_id = tx_id;

    if (bind(isotp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printk("Failed to bind ISO-TP socket: %d\n", errno);
        close(isotp_fd);
        isotp_fd = -1;
        return -1;
    }

    // Set non-blocking
    int flags = fcntl(isotp_fd, F_GETFL, 0);
    fcntl(isotp_fd, F_SETFL, flags | O_NONBLOCK);

    return 0;
}

int uds_zephyr_isotp_send(uds_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    if (isotp_fd < 0) return -1;
    ssize_t ret = send(isotp_fd, data, len, 0);
    return (ret == (ssize_t)len) ? 0 : -1;
}

int uds_zephyr_isotp_recv(uint8_t* buf, uint16_t size) {
    if (isotp_fd < 0) return 0;
    ssize_t ret = recv(isotp_fd, buf, size, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return (int)ret;
}

#endif // CONFIG_LIBUDS_TRANSPORT_NATIVE
