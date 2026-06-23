#ifndef FDCAN_H
#define FDCAN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t id;
    uint8_t len;
    uint8_t data[64];
    bool fd;
} can_frame_t;

/* UART helpers */
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

/* FDCAN driver */
void fdcan_start(void);
int fdcan_send_frame(uint32_t id, const uint8_t *data, uint8_t len, bool fd);
bool fdcan_poll_rx_frame(can_frame_t *frame);

/* ISO-TP shim: send via FDCAN, uses fd=true when len > 8 */
int can_send(uint32_t id, const uint8_t *data, uint8_t len);

#endif /* FDCAN_H */
