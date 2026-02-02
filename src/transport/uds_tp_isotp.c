/**
 * @file uds_tp_isotp.c
 * @brief Lightweight ISO-TP Implementation (Zephyr-Ready Fallback)
 */

#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

/* --- Internal State --- */

/** Static ISO-TP context instance */
static uds_isotp_ctx_t g_isotp_ctx;

/** Cache for multi-frame transmission */
static uint8_t g_pending_tx_sdu[1024];

/** Length of cached multi-frame SDU */
static uint16_t g_pending_tx_len = 0;

/* --- Internal Helpers --- */

/**
 * @brief Internal Helper: Raw CAN Frame Transmitter.
 *
 * @param iso   Pointer to the ISO-TP context.
 * @param data  Pointer to the 8-byte CAN frame payload.
 * @param len   Payload length (usually 8).
 * @return      0 on success, negative error code on failure.
 */
static int uds_internal_tp_send_frame(uds_isotp_ctx_t *iso, const uint8_t *data, uint8_t len)
{
    if (iso && iso->can_send) {
        return iso->can_send(iso->tx_id, data, len);
    }
    return -1;
}

/* --- Public API --- */

void uds_tp_isotp_init(uds_can_send_fn can_send, uint32_t tx_id, uint32_t rx_id)
{
    memset(&g_isotp_ctx, 0, sizeof(g_isotp_ctx));
    g_isotp_ctx.can_send = can_send;
    g_isotp_ctx.tx_id = tx_id;
    g_isotp_ctx.rx_id = rx_id;
    g_isotp_ctx.block_size = 8; /* Default Block Size */
    g_isotp_ctx.st_min = 0;     /* Default No Delay */
}

int uds_isotp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;

    if (len <= 7) {
        /* Single Frame */
        uint8_t frame[8] = {0};
        frame[0] = (uint8_t)((uint8_t)ISOTP_PCI_SF | (uint8_t)len);
        memcpy(&frame[1], data, len);

        return uds_internal_tp_send_frame(&g_isotp_ctx, frame, 8u);
    } else {
        /* Multi-Frame: Send First Frame */
        if (len > 4095 || len > sizeof(g_pending_tx_sdu)) {
            return -2;
        }

        memcpy(g_pending_tx_sdu, data, len);
        g_pending_tx_len = len;

        g_isotp_ctx.msg_len = len;
        g_isotp_ctx.bytes_processed = 0;
        g_isotp_ctx.state = ISOTP_TX_WAIT_FC;

        uint8_t frame[8] = {0};
        frame[0] = (uint8_t)((uint8_t)ISOTP_PCI_FF | (uint8_t)((len >> 8u) & 0x0Fu));
        frame[1] = (uint8_t)(len & 0xFFu);
        memcpy(&frame[2], data, 6u);
        g_isotp_ctx.bytes_processed = 6u;
        g_isotp_ctx.sn = 1u;

        if (uds_internal_tp_send_frame(&g_isotp_ctx, frame, 8) != 0) {
            return -1;
        }

        return 0; /* Multi-Frame started successfully */
    }
}

void uds_tp_isotp_process(uint32_t time_ms)
{
    if (g_isotp_ctx.state == ISOTP_TX_SENDING_CF) {
        uint16_t remaining = g_isotp_ctx.msg_len - g_isotp_ctx.bytes_processed;
        if (remaining == 0) {
            g_isotp_ctx.state = ISOTP_IDLE;
            return;
        }

        /* Check STmin (Separation Time) */
        uint32_t elapsed = time_ms - g_isotp_ctx.timer_st;
        uint32_t required_st = g_isotp_ctx.st_min;

        /* Decode ISO-TP STmin: 
           0x00 - 0x7F: 0ms - 127ms
           0xF1 - 0xF9: 100us - 900us (we'll treat as 1ms for now as we have ms resolution)
        */
        if (required_st >= 0xF1 && required_st <= 0xF9) {
            required_st = 1; 
        } else if (required_st > 0x7F) {
            required_st = 0; /* Reserved or invalid */
        }

        if (elapsed < required_st) {
            return; /* Wait for STmin */
        }

        /* Check Block Size (BS) */
        if (g_isotp_ctx.block_size > 0 && g_isotp_ctx.bs_counter >= g_isotp_ctx.block_size) {
            g_isotp_ctx.state = ISOTP_TX_WAIT_FC;
            g_isotp_ctx.bs_counter = 0;
            return;
        }

        uint8_t to_copy = (remaining > 7) ? 7 : (uint8_t)remaining;
        uint8_t frame[8] = {0};
        frame[0] = (uint8_t)(ISOTP_PCI_CF | g_isotp_ctx.sn);
        memcpy(&frame[1], &g_pending_tx_sdu[g_isotp_ctx.bytes_processed], to_copy);

        if (uds_internal_tp_send_frame(&g_isotp_ctx, frame, 8) == 0) {
            g_isotp_ctx.bytes_processed += to_copy;
            g_isotp_ctx.sn = (g_isotp_ctx.sn + 1) & 0x0F;
            g_isotp_ctx.bs_counter++;
            g_isotp_ctx.timer_st = time_ms; /* Reset ST timer */

            if (g_isotp_ctx.bytes_processed >= g_isotp_ctx.msg_len) {
                g_isotp_ctx.state = ISOTP_IDLE;
            }
        }
    }
}

void uds_isotp_rx_callback(struct uds_ctx *uds_ctx, uint32_t id, const uint8_t *data, uint8_t len)
{
    (void)len;
    if (id != g_isotp_ctx.rx_id) {
        return;
    }

    uint8_t pci = data[0] & 0xF0;

    switch (pci) {
        case ISOTP_PCI_SF: {
            /* Abort any active multi-frame on new Single Frame */
            g_isotp_ctx.state = ISOTP_IDLE;

            uint8_t sdu_len = (uint8_t)(data[0] & 0x0Fu);
            if ((sdu_len == 0u) || (sdu_len > 7u)) {
                return;
            }
            uds_input_sdu(uds_ctx, &data[1], sdu_len, UDS_NET_ADDR_PHYSICAL);
            break;
        }

        case ISOTP_PCI_FF: {
            /* Abort any active multi-frame on new First Frame */
            g_isotp_ctx.state = ISOTP_IDLE;

            uint16_t sdu_len = (uint16_t)((uint16_t)((uint16_t)data[0] & 0x0Fu) << 8u) | (uint16_t)data[1];
            if (sdu_len < 8u) {
                return; /* Multi-frame must be > 7 bytes */
            }

            g_isotp_ctx.msg_len = sdu_len;
            g_isotp_ctx.bytes_processed = 6;
            g_isotp_ctx.sn = 1;
            g_isotp_ctx.state = ISOTP_RX_WAIT_CF;

            if (uds_ctx->config->rx_buffer_size < sdu_len) {
                g_isotp_ctx.state = ISOTP_IDLE;
                return;
            }
            memcpy(uds_ctx->config->rx_buffer, &data[2], 6);

            /* Send Flow Control (CTS) */
            uint8_t fc[8] = {0};
            fc[0] = (uint8_t)(ISOTP_PCI_FC | ISOTP_FC_CTS);
            fc[1] = g_isotp_ctx.block_size;
            fc[2] = g_isotp_ctx.st_min;
            uds_internal_tp_send_frame(&g_isotp_ctx, fc, 8);
            break;
        }

        case ISOTP_PCI_CF: {
            if (g_isotp_ctx.state != ISOTP_RX_WAIT_CF) {
                return;
            }

            uint8_t sn = data[0] & 0x0F;
            if (sn != g_isotp_ctx.sn) {
                g_isotp_ctx.state = ISOTP_IDLE;
                return;
            }
            g_isotp_ctx.sn = (g_isotp_ctx.sn + 1) & 0x0F;

            uint16_t remaining = g_isotp_ctx.msg_len - g_isotp_ctx.bytes_processed;
            uint8_t to_copy = (remaining > 7) ? 7 : (uint8_t)remaining;

            memcpy(&uds_ctx->config->rx_buffer[g_isotp_ctx.bytes_processed], &data[1], to_copy);
            g_isotp_ctx.bytes_processed += to_copy;

            if (g_isotp_ctx.bytes_processed >= g_isotp_ctx.msg_len) {
                g_isotp_ctx.state = ISOTP_IDLE;
                uds_input_sdu(uds_ctx, uds_ctx->config->rx_buffer, g_isotp_ctx.msg_len, UDS_NET_ADDR_PHYSICAL);

            }
            break;
        }

        case ISOTP_PCI_FC: {
            if (g_isotp_ctx.state != ISOTP_TX_WAIT_FC) {
                return;
            }

            uint8_t fs = data[0] & 0x0F;
            if (fs == ISOTP_FC_CTS) {
                g_isotp_ctx.state = ISOTP_TX_SENDING_CF;
                g_isotp_ctx.block_size = data[1];
                g_isotp_ctx.st_min = data[2];
            }
            break;
        }

        default:
            break;
    }
}
