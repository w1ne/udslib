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

/**
 * @brief Helper: Align length to valid CAN-FD DLC.
 * Sizing: 0-8, 12, 16, 20, 24, 32, 48, 64.
 */
static uint8_t uds_dlc_align(uint8_t len)
{
    if (len <= ISOTP_MAX_DL_CAN) return len;
    if (len <= 12) return 12;
    if (len <= 16) return 16;
    if (len <= 20) return 20;
    if (len <= 24) return 24;
    if (len <= 32) return 32;
    if (len <= 48) return 48;
    return ISOTP_MAX_DL_CANFD;
}

/* --- Public API --- */

// cppcheck-suppress unusedFunction
void uds_tp_isotp_init(uds_can_send_fn can_send, uint32_t tx_id, uint32_t rx_id)
{
    memset(&g_isotp_ctx, 0, sizeof(g_isotp_ctx));
    g_isotp_ctx.can_send = can_send;
    g_isotp_ctx.tx_id = tx_id;
    g_isotp_ctx.rx_id = rx_id;
    g_isotp_ctx.block_size = 8; /* Default Block Size */
    g_isotp_ctx.st_min = 0;     /* Default No Delay */
    g_isotp_ctx.use_can_fd = 0; /* Default: Classic CAN */
    g_isotp_ctx.tx_dl = ISOTP_MAX_DL_CAN;      /* Default: 8 bytes */
}

void uds_tp_isotp_set_fd(bool enabled)
{
    g_isotp_ctx.use_can_fd = enabled ? 1 : 0;
    g_isotp_ctx.tx_dl = enabled ? ISOTP_MAX_DL_CANFD : ISOTP_MAX_DL_CAN;
}

// cppcheck-suppress unusedFunction
/**
 * @brief Internal: Send Single Frame.
 */
static int uds_send_sf(const uint8_t *data, uint16_t len)
{
    uint8_t frame[ISOTP_MAX_DL_CANFD] = {0};
    uint8_t dl = ISOTP_MAX_DL_CAN;

    if (len <= ISOTP_SF_MAX_DL_CAN) {
            /* Standard SF: [PCI+DL] [Data...] */
        frame[0] = (uint8_t) ((uint8_t) ISOTP_PCI_SF | (uint8_t) len);
        memcpy(&frame[1], data, len);
        dl = ISOTP_MAX_DL_CAN; 
    } else {
        /* CAN-FD SF: [00] [DL] [Data...] */
        frame[0] = ISOTP_PCI_SF; /* 0x00 */
        frame[1] = (uint8_t)len; /* Data Length */
        memcpy(&frame[2], data, len);
        /* Calculate valid DLC for FD */
        dl = uds_dlc_align(len + 2);
    }

    return uds_internal_tp_send_frame(&g_isotp_ctx, frame, dl);
}

/**
 * @brief Internal: Start Multi-Frame Transmission.
 */
static int uds_send_mf(const uint8_t *data, uint16_t len)
{
    if (len > ISOTP_MAX_SDU_LEN_STD || len > sizeof(g_pending_tx_sdu)) {
        return -2;
    }

    memcpy(g_pending_tx_sdu, data, len);
    g_pending_tx_len = len;

    g_isotp_ctx.msg_len = len;
    g_isotp_ctx.bytes_processed = 0;
    g_isotp_ctx.state = ISOTP_TX_WAIT_FC;

    uint8_t frame[ISOTP_MAX_DL_CANFD] = {0};
    uint8_t dl = ISOTP_MAX_DL_CAN;
    
    /* FF Header: [1n] [nn] */
    frame[0] = (uint8_t) ((uint8_t) ISOTP_PCI_FF | (uint8_t) ((len >> 8u) & 0x0Fu));
    frame[1] = (uint8_t) (len & 0xFFu);
    
    uint8_t max_data_in_ff = (g_isotp_ctx.use_can_fd) ? ISOTP_FF_MAX_DATA_CANFD : ISOTP_FF_MAX_DATA_CAN;

    /* Copy as much as fits in FF */
    uint8_t to_copy = (len > max_data_in_ff) ? max_data_in_ff : (uint8_t)len;
    memcpy(&frame[2], data, to_copy);
    
    g_isotp_ctx.bytes_processed = to_copy;
    g_isotp_ctx.sn = 1u;

    if (g_isotp_ctx.use_can_fd) {
        /* FF in FD is usually full, unless minimal data? 
                    But standard says FF_DL > 4095 uses escape. 
                    If we use FD, we should use full frame capacity for efficiency 
                    or at least align DLC. */
        dl = uds_dlc_align(2 + to_copy);
    } else {
        dl = ISOTP_MAX_DL_CAN;
    }

    if (uds_internal_tp_send_frame(&g_isotp_ctx, frame, dl) != 0) {
        return -1;
    }

    return 0; /* Multi-Frame started successfully */
}

// cppcheck-suppress unusedFunction
int uds_isotp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;

    /* Check if we can use Single Frame */
    uint8_t max_sf_len = (g_isotp_ctx.use_can_fd) ? ISOTP_SF_MAX_DL_CANFD : ISOTP_SF_MAX_DL_CAN;
    
    if (len <= max_sf_len) {
        return uds_send_sf(data, len);
    }
    
    return uds_send_mf(data, len);
}

// cppcheck-suppress unusedFunction
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
        }
        else if (required_st > 0x7F) {
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

        /* Calculate max payload per CF */
        uint8_t max_cf_payload = (g_isotp_ctx.use_can_fd) ? (ISOTP_MAX_DL_CANFD - 1) : (ISOTP_MAX_DL_CAN - 1); /* Header is 1 byte (PCI+SN) */
        
        uint8_t to_copy = (remaining > max_cf_payload) ? max_cf_payload : (uint8_t) remaining;
        uint8_t frame[ISOTP_MAX_DL_CANFD] = {0};
        frame[0] = (uint8_t) (ISOTP_PCI_CF | g_isotp_ctx.sn);
        memcpy(&frame[1], &g_pending_tx_sdu[g_isotp_ctx.bytes_processed], to_copy);
        
        uint8_t dl = ISOTP_MAX_DL_CAN;
        if (g_isotp_ctx.use_can_fd) {
            dl = uds_dlc_align(1 + to_copy);
        }

        if (uds_internal_tp_send_frame(&g_isotp_ctx, frame, dl) == 0) {
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

static void uds_rx_sf(struct uds_ctx *uds_ctx, const uint8_t *data, uint8_t len)
{
    /* Abort any active multi-frame on new Single Frame */
    g_isotp_ctx.state = ISOTP_IDLE;

    uint8_t sdu_len = (uint8_t) (data[0] & 0x0Fu);
    uint8_t data_offset = 1;

    if (sdu_len == 0u) {
        /* CAN-FD SF: Byte 0 is 0x00, Byte 1 is Length */
        sdu_len = data[1];
        data_offset = 2;
        if (sdu_len == 0) return; /* Invalid */
    }

    if (sdu_len > (len - data_offset)) {
        /* Not enough data in frame */
        return;
    }
    
    uds_input_sdu(uds_ctx, &data[data_offset], (uint16_t) sdu_len);
}

static void uds_rx_ff(struct uds_ctx *uds_ctx, const uint8_t *data, uint8_t len)
{
    /* Abort any active multi-frame on new First Frame */
    g_isotp_ctx.state = ISOTP_IDLE;

    uint16_t sdu_len =
        (uint16_t) ((uint16_t) ((uint16_t) data[0] & 0x0Fu) << 8u) | (uint16_t) data[1];
    if (sdu_len < 8u) {
        return; /* Multi-frame must be > 7 bytes (Standard) or handled by SF */
    }

    g_isotp_ctx.msg_len = sdu_len;
    
    /* Determine data in FF */
    uint8_t data_in_ff;
    if (len > ISOTP_MAX_DL_CAN) {
        /* CAN-FD FF */
        data_in_ff = len - 2; 
    } else {
        /* Classic CAN FF */
            data_in_ff = ISOTP_FF_MAX_DATA_CAN;
    }
    
    g_isotp_ctx.bytes_processed = data_in_ff;
    g_isotp_ctx.sn = 1;
    g_isotp_ctx.state = ISOTP_RX_WAIT_CF;

    if (uds_ctx->config->rx_buffer_size < sdu_len) {
        g_isotp_ctx.state = ISOTP_IDLE;
        return;
    }
    memcpy(uds_ctx->config->rx_buffer, &data[2], data_in_ff);

    /* Send Flow Control (CTS) */
    uint8_t fc[8] = {0};
    fc[0] = (uint8_t) (ISOTP_PCI_FC | ISOTP_FC_CTS);
    fc[1] = g_isotp_ctx.block_size;
    fc[2] = g_isotp_ctx.st_min;
    uds_internal_tp_send_frame(&g_isotp_ctx, fc, 8);
}

static void uds_rx_cf(struct uds_ctx *uds_ctx, const uint8_t *data, uint8_t len)
{
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
    
    /* Max payload in CF depends on whether we received FD frame (len > 8) or not.
        Actually receiving node infers FD from frame length. */
    uint8_t data_capacity = len - 1; /* Byte 0 is PCI+SN */
    
    uint8_t to_copy = (remaining > data_capacity) ? data_capacity : (uint8_t) remaining;

    memcpy(&uds_ctx->config->rx_buffer[g_isotp_ctx.bytes_processed], &data[1], to_copy);
    g_isotp_ctx.bytes_processed += to_copy;

    if (g_isotp_ctx.bytes_processed >= g_isotp_ctx.msg_len) {
        g_isotp_ctx.state = ISOTP_IDLE;
        uds_input_sdu(uds_ctx, uds_ctx->config->rx_buffer, g_isotp_ctx.msg_len);
    }
}

static void uds_rx_fc(const uint8_t *data)
{
    if (g_isotp_ctx.state != ISOTP_TX_WAIT_FC) {
        return;
    }

    uint8_t fs = data[0] & 0x0F;
    if (fs == ISOTP_FC_CTS) {
        g_isotp_ctx.state = ISOTP_TX_SENDING_CF;
        g_isotp_ctx.block_size = data[1];
        g_isotp_ctx.st_min = data[2];
    }
}

// cppcheck-suppress unusedFunction
void uds_isotp_rx_callback(struct uds_ctx *uds_ctx, uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) len;
    if (id != g_isotp_ctx.rx_id) {
        return;
    }

    uint8_t pci = data[0] & 0xF0;

    switch (pci) {
        case ISOTP_PCI_SF:
            uds_rx_sf(uds_ctx, data, len);
            break;

        case ISOTP_PCI_FF:
            uds_rx_ff(uds_ctx, data, len);
            break;

        case ISOTP_PCI_CF:
            uds_rx_cf(uds_ctx, data, len);
            break;

        case ISOTP_PCI_FC:
            uds_rx_fc(data);
            break;

        default:
            break;
    }
}
