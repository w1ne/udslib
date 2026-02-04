# Integration Guide: Zephyr RTOS

This guide demonstrates how to integrate UDSLib with the Zephyr Project RTOS using the native `isotp` socket wrapper.

## Prerequisites
- Zephyr SDK installed.
- `CONFIG_ISOTP=y` and `CONFIG_NET_SOCKETS=y` in your `prj.conf`.

## 1. Define the Transport Bridge
UDSLib is transport-agnostic. You simply need to wrap the Zephyr socket send call.

```c
#include <zephyr/net/socket.h>
#include <uds/uds_core.h>

int zephyr_tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len) {
    int sock = (int)ctx->config->user_data; // Store socket in user_data
    return zsock_send(sock, data, len, 0);
}
```

## 2. Platform Callbacks
Provide the mandatory time and logging source.

```c
uint32_t get_time_ms(void) {
    return k_uptime_get_32();
}

void uds_log(uint8_t level, const char *msg) {
    printk("[UDS] %s\n", msg);
}
```

## 3. The RX Thread
 SDUs (Service Data Units) are received from the socket and fed directly into the stack.

```c
void rx_thread(void *p1, void *p2, void *p3) {
    uint8_t buf[1024];
    while (1) {
        int received = zsock_recv(sock, buf, sizeof(buf), 0);
        if (received > 0) {
            uds_input_sdu(&ctx, buf, received);
        }
    }
}
```

## 4. Periodic Processing
Call `uds_process` in a dedicated timer or idle task to handle protocol timeouts.

```c
void timer_handler(struct k_timer *dummy) {
    uds_process(&ctx);
}
K_TIMER_DEFINE(uds_timer, timer_handler, NULL);
k_timer_start(&uds_timer, K_MSEC(1), K_MSEC(1));
```

## Why this is better than "Native" stacks
By using UDSLib on top of Zephyr's `isotp`, you gain:
1. **ISO 14229-1 Compliance**: Zephyr only handles the transport layer (segmentation). UDSLib handles the protocol logic.
2. **Persistence**: Use UDSLib NVM hooks to save session state in Zephyr's NVS.
3. **Safety**: Use UDSLib Safety Gates to check vehicle speed before resetting.
