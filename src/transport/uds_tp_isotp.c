/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_tp_isotp.c
 * @brief Lightweight ISO-TP Implementation (Zephyr-Ready Fallback)
 *
 * Fully instance-based: all state lives in a caller-owned uds_isotp_ctx_t and
 * the SDU TX cache is a caller-provided buffer. Multiple channels may run
 * concurrently without shared state.
 */

#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

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

void uds_tp_isotp_init(uds_isotp_ctx_t *iso, uds_can_send_fn can_send, uint32_t tx_id,
                       uint32_t rx_id, uint8_t *tx_sdu_buf, uint16_t tx_sdu_size)
{
    if (!iso) {
        return;
    }
    memset(iso, 0, sizeof(*iso));
    iso->can_send = can_send;
    iso->tx_id = tx_id;
    iso->rx_id = rx_id;
    iso->rx_id_func = 0u;                   /* functional reception disabled until configured */
    iso->block_size = 8;                    /* BS we advertise as receiver */
    iso->st_min = 0;                        /* STmin we advertise as receiver */
    iso->use_can_fd = 0;                    /* Default: Classic CAN */
    iso->tx_dl = ISOTP_MAX_DL_CAN;          /* Default: 8 bytes */
    iso->mode = ISOTP_HALF_DUPLEX;          /* Default: conservative, prior behavior */
    iso->pad_byte = ISOTP_PAD_BYTE_DEFAULT; /* ISO 15765-2 default: 0xCC */
    iso->rx_state = ISOTP_RX_IDLE;
    iso->tx_state = ISOTP_TX_IDLE;
    iso->tx_sdu_buf = tx_sdu_buf;
    iso->tx_sdu_size = tx_sdu_size;
    iso->tx_sdu_len = 0;
    iso->n_cr_ms = ISOTP_N_CR_DEFAULT_MS;
    iso->n_bs_ms = ISOTP_N_BS_DEFAULT_MS;
}

void uds_tp_isotp_set_fd(uds_isotp_ctx_t *iso, bool enabled)
{
    if (!iso) {
        return;
    }
    iso->use_can_fd = enabled ? 1 : 0;
    iso->tx_dl = enabled ? ISOTP_MAX_DL_CANFD : ISOTP_MAX_DL_CAN;
}

void uds_tp_isotp_set_pad_byte(uds_isotp_ctx_t *iso, uint8_t pad_byte)
{
    if (!iso) {
        return;
    }
    iso->pad_byte = pad_byte;
}

void uds_tp_isotp_set_mode(uds_isotp_ctx_t *iso, uds_isotp_duplex_t mode)
{
    if (!iso) {
        return;
    }
    iso->mode = mode;
}

void uds_tp_isotp_set_functional_id(uds_isotp_ctx_t *iso, uint32_t rx_id_func)
{
    if (!iso) {
        return;
    }
    iso->rx_id_func = rx_id_func;
}

/**
 * @brief Internal: Send Single Frame.
 */
static int uds_send_sf(uds_isotp_ctx_t *iso, const uint8_t *data, uint16_t len)
{
    uint8_t frame[ISOTP_MAX_DL_CANFD];
    uint8_t dl = ISOTP_MAX_DL_CAN;
    memset(frame, iso->pad_byte, sizeof(frame));

    if (len <= ISOTP_SF_MAX_DL_CAN) {
        /* Standard SF: [PCI+DL] [Data...] */
        frame[0] = (uint8_t) ((uint8_t) ISOTP_PCI_SF | (uint8_t) len);
        memcpy(&frame[1], data, len);
        dl = ISOTP_MAX_DL_CAN;
    }
    else {
        /* CAN-FD SF: [00] [DL] [Data...] */
        frame[0] = ISOTP_PCI_SF;  /* 0x00 */
        frame[1] = (uint8_t) len; /* Data Length */
        memcpy(&frame[2], data, len);
        /* Calculate valid DLC for FD */
        dl = uds_dlc_align(len + 2);
    }

    return uds_internal_tp_send_frame(iso, frame, dl);
}

/**
 * @brief Internal: Start Multi-Frame Transmission.
 */
static int uds_send_mf(uds_isotp_ctx_t *iso, const uint8_t *data, uint16_t len)
{
    /* msg_len is uint16_t, so the SDU is inherently capped at 65535 bytes; the
       only hard limit here is the caller-provided TX cache. */
    if (len > iso->tx_sdu_size) {
        return -2;
    }

    memcpy(iso->tx_sdu_buf, data, len);
    iso->tx_sdu_len = len;

    iso->tx_msg_len = len;
    iso->tx_bytes_processed = 0;
    iso->tx_bs_counter = 0; /* fresh block accounting for this transfer */
    iso->tx_state = ISOTP_TX_WAIT_FC;
    iso->timer_n_bs = 0u; /* armed on the first process() tick in WAIT_FC */

    uint8_t frame[ISOTP_MAX_DL_CANFD];
    uint8_t dl = ISOTP_MAX_DL_CAN;
    uint8_t header_len;
    memset(frame, iso->pad_byte, sizeof(frame));

    if (len > ISOTP_MAX_SDU_LEN_STD) {
        /* Escape FF (ISO 15765-2, FF_DL > 4095): [10] [00] [32-bit FF_DL].
           FF_DL is a 32-bit field; widen before shifting (MISRA 12.2). */
        uint32_t ff_dl = (uint32_t) len;
        frame[0] = (uint8_t) ISOTP_PCI_FF;
        frame[1] = 0x00u;
        frame[2] = (uint8_t) ((ff_dl >> 24u) & 0xFFu);
        frame[3] = (uint8_t) ((ff_dl >> 16u) & 0xFFu);
        frame[4] = (uint8_t) ((ff_dl >> 8u) & 0xFFu);
        frame[5] = (uint8_t) (ff_dl & 0xFFu);
        header_len = 6u;
    }
    else {
        /* Standard FF Header: [1n] [nn] */
        frame[0] = (uint8_t) ((uint8_t) ISOTP_PCI_FF | (uint8_t) ((len >> 8u) & 0x0Fu));
        frame[1] = (uint8_t) (len & 0xFFu);
        header_len = 2u;
    }

    uint8_t frame_capacity = (iso->use_can_fd) ? ISOTP_MAX_DL_CANFD : ISOTP_MAX_DL_CAN;
    uint8_t max_data_in_ff = (uint8_t) (frame_capacity - header_len);

    /* Copy as much as fits in FF */
    uint8_t to_copy = (len > max_data_in_ff) ? max_data_in_ff : (uint8_t) len;
    memcpy(&frame[header_len], data, to_copy);

    iso->tx_bytes_processed = to_copy;
    iso->tx_sn = 1u;

    if (iso->use_can_fd) {
        dl = uds_dlc_align((uint8_t) (header_len + to_copy));
    }
    else {
        dl = ISOTP_MAX_DL_CAN;
    }

    if (uds_internal_tp_send_frame(iso, frame, dl) != 0) {
        return -1;
    }

    return 0; /* Multi-Frame started successfully */
}

int uds_isotp_send(uds_isotp_ctx_t *iso, const uint8_t *data, uint16_t len)
{
    if (!iso) {
        return -1;
    }

    /* Half-duplex: starting a transmission terminates an in-flight reception. */
    if (iso->mode == ISOTP_HALF_DUPLEX) {
        iso->rx_state = ISOTP_RX_IDLE;
    }

    /* Check if we can use Single Frame */
    uint8_t max_sf_len = (iso->use_can_fd) ? ISOTP_SF_MAX_DL_CANFD : ISOTP_SF_MAX_DL_CAN;

    if (len <= max_sf_len) {
        return uds_send_sf(iso, data, len);
    }

    return uds_send_mf(iso, data, len);
}

void uds_tp_isotp_process(uds_isotp_ctx_t *iso, uint32_t time_ms)
{
    if (!iso) {
        return;
    }

    /* --- RX tick: N_Cr (reception stalls if a CF never arrives) --- */
    if (iso->rx_state == ISOTP_RX_WAIT_CF) {
        if ((time_ms - iso->timer_n_cr) >= iso->n_cr_ms) {
            iso->rx_state = ISOTP_RX_IDLE;
        }
    }

    /* --- TX tick: N_Bs (waiting for FC) --- */
    if (iso->tx_state == ISOTP_TX_WAIT_FC) {
        if (iso->timer_n_bs == 0u) {
            /* Arm on first observation; avoid 0 which means "unarmed". */
            iso->timer_n_bs = (time_ms == 0u) ? 1u : time_ms;
        }
        else if ((time_ms - iso->timer_n_bs) >= iso->n_bs_ms) {
            iso->tx_state = ISOTP_TX_IDLE;
            iso->timer_n_bs = 0u;
        }
        return;
    }

    /* --- TX tick: sending consecutive frames --- */
    if (iso->tx_state == ISOTP_TX_SENDING_CF) {
        uint16_t remaining = iso->tx_msg_len - iso->tx_bytes_processed;
        if (remaining == 0) {
            iso->tx_state = ISOTP_TX_IDLE;
            return;
        }

        /* Check STmin (Separation Time) using the receiver-honored value. */
        uint32_t elapsed = time_ms - iso->timer_st;
        uint32_t required_st = iso->tx_st_min;

        /* Decode ISO-TP STmin:
           0x00 - 0x7F: 0ms - 127ms
           0xF1 - 0xF9: 100us - 900us (treated as 1ms at ms resolution) */
        if (required_st >= 0xF1 && required_st <= 0xF9) {
            required_st = 1;
        }
        else if (required_st > 0x7F) {
            required_st = 0; /* Reserved or invalid */
        }

        if (elapsed < required_st) {
            return; /* Wait for STmin */
        }

        /* Check Block Size (BS) the receiver told us to honor. */
        if (iso->tx_block_size > 0 && iso->tx_bs_counter >= iso->tx_block_size) {
            iso->tx_state = ISOTP_TX_WAIT_FC;
            iso->tx_bs_counter = 0;
            iso->timer_n_bs = 0u; /* re-arm N_Bs while waiting for the next FC */
            return;
        }

        /* Calculate max payload per CF (header is 1 byte). */
        uint8_t max_cf_payload =
            (iso->use_can_fd) ? (ISOTP_MAX_DL_CANFD - 1) : (ISOTP_MAX_DL_CAN - 1);

        uint8_t to_copy = (remaining > max_cf_payload) ? max_cf_payload : (uint8_t) remaining;
        uint8_t frame[ISOTP_MAX_DL_CANFD];
        memset(frame, iso->pad_byte, sizeof(frame));
        frame[0] = (uint8_t) (ISOTP_PCI_CF | iso->tx_sn);
        memcpy(&frame[1], &iso->tx_sdu_buf[iso->tx_bytes_processed], to_copy);

        uint8_t dl = ISOTP_MAX_DL_CAN;
        if (iso->use_can_fd) {
            dl = uds_dlc_align(1 + to_copy);
        }

        if (uds_internal_tp_send_frame(iso, frame, dl) == 0) {
            iso->tx_bytes_processed += to_copy;
            iso->tx_sn = (iso->tx_sn + 1) & 0x0F;
            iso->tx_bs_counter++;
            iso->timer_st = time_ms; /* Reset ST timer */

            if (iso->tx_bytes_processed >= iso->tx_msg_len) {
                iso->tx_state = ISOTP_TX_IDLE;
            }
        }
    }
}

static void uds_rx_sf(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len,
                      uint8_t addr)
{
    /* A new reception supersedes any in-progress reception. */
    iso->rx_state = ISOTP_RX_IDLE;

    /* Half-duplex: a new inbound message terminates an in-flight transmission. */
    if (iso->mode == ISOTP_HALF_DUPLEX) {
        iso->tx_state = ISOTP_TX_IDLE;
        iso->timer_n_bs = 0u;
    }

    uint8_t sdu_len = (uint8_t) (data[0] & 0x0Fu);
    uint8_t data_offset = 1;

    if (sdu_len == 0u) {
        /* CAN-FD SF: Byte 0 is 0x00, Byte 1 is Length */
        if (len < 2u) return;
        sdu_len = data[1];
        data_offset = 2;
        if (sdu_len == 0) return;
    }

    if (sdu_len > (len - data_offset)) {
        return;
    }

    if (addr == (uint8_t) UDS_ADDR_FUNCTIONAL) {
        uds_input_sdu_addr(uds, &data[data_offset], (uint16_t) sdu_len, UDS_ADDR_FUNCTIONAL);
    }
    else {
        uds_input_sdu(uds, &data[data_offset], (uint16_t) sdu_len);
    }
}

static void uds_rx_ff(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
{
    /* A new reception supersedes any in-progress reception. */
    iso->rx_state = ISOTP_RX_IDLE;

    /* Half-duplex: a new inbound message terminates an in-flight transmission. */
    if (iso->mode == ISOTP_HALF_DUPLEX) {
        iso->tx_state = ISOTP_TX_IDLE;
        iso->timer_n_bs = 0u;
    }

    if (len < 2u) {
        return; /* FF requires at least PCI + length byte */
    }

    uint32_t sdu_len;
    uint8_t header_len;

    if ((data[0] & 0x0Fu) == 0u && data[1] == 0u) {
        /* Escape FF (ISO 15765-2): 32-bit FF_DL in bytes 2..5 (MSB first). */
        if (len < 6u) {
            return; /* Not enough bytes for the escape length field */
        }
        sdu_len = ((uint32_t) data[2] << 24u) | ((uint32_t) data[3] << 16u) |
                  ((uint32_t) data[4] << 8u) | (uint32_t) data[5];
        if (sdu_len <= ISOTP_MAX_SDU_LEN_STD) {
            return; /* Escape sequence with FF_DL <= 4095 is invalid; ignore (9.6.3.2). */
        }
        header_len = 6u;
    }
    else {
        sdu_len = ((uint32_t) (data[0] & 0x0Fu) << 8u) | (uint32_t) data[1];
        if (sdu_len < 8u) {
            return; /* Multi-frame must be > 7 bytes (Standard) or handled by SF */
        }
        header_len = 2u;
    }

    /* FF_DL exceeding the receive buffer: cancel and notify the sender (9.6.3.2). */
    if (sdu_len > uds->config->rx_buffer_size) {
        iso->rx_state = ISOTP_RX_IDLE;
        uint8_t fc_ov[8];
        memset(fc_ov, iso->pad_byte, sizeof(fc_ov));
        fc_ov[0] = (uint8_t) (ISOTP_PCI_FC | ISOTP_FC_OVA);
        uds_internal_tp_send_frame(iso, fc_ov, 8);
        return;
    }

    iso->rx_msg_len = (uint16_t) sdu_len;

    uint8_t data_in_ff = (uint8_t) (len - header_len);

    iso->rx_bytes_processed = data_in_ff;
    iso->rx_sn = 1;
    iso->rx_state = ISOTP_RX_WAIT_CF;
    iso->timer_n_cr = uds->config->get_time_ms ? uds->config->get_time_ms() : 0u;

    memcpy(uds->config->rx_buffer, &data[header_len], data_in_ff);

    /* Send Flow Control (CTS) advertising OUR receiver BS/STmin. */
    uint8_t fc[8];
    memset(fc, iso->pad_byte, sizeof(fc));
    fc[0] = (uint8_t) (ISOTP_PCI_FC | ISOTP_FC_CTS);
    fc[1] = iso->block_size;
    fc[2] = iso->st_min;
    uds_internal_tp_send_frame(iso, fc, 8);
}

static void uds_rx_cf(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
{
    if (iso->rx_state != ISOTP_RX_WAIT_CF) {
        return;
    }

    uint8_t sn = data[0] & 0x0F;
    if (sn != iso->rx_sn) {
        iso->rx_state = ISOTP_RX_IDLE;
        return;
    }
    iso->rx_sn = (iso->rx_sn + 1) & 0x0F;

    uint16_t remaining = iso->rx_msg_len - iso->rx_bytes_processed;

    uint8_t data_capacity = len - 1; /* Byte 0 is PCI+SN */
    uint8_t to_copy = (remaining > data_capacity) ? data_capacity : (uint8_t) remaining;

    memcpy(&uds->config->rx_buffer[iso->rx_bytes_processed], &data[1], to_copy);
    iso->rx_bytes_processed += to_copy;
    iso->timer_n_cr = uds->config->get_time_ms ? uds->config->get_time_ms() : 0u;

    if (iso->rx_bytes_processed >= iso->rx_msg_len) {
        iso->rx_state = ISOTP_RX_IDLE;
        uds_input_sdu(uds, uds->config->rx_buffer, iso->rx_msg_len);
    }
}

static void uds_rx_fc(uds_isotp_ctx_t *iso, const uint8_t *data, uint8_t len)
{
    if (iso->tx_state != ISOTP_TX_WAIT_FC) {
        return;
    }
    if (len < 3u) {
        return; /* FC requires flow status, block size and STmin */
    }

    uint8_t fs = data[0] & 0x0F;
    switch (fs) {
        case ISOTP_FC_CTS:
            /* ClearToSend: latch the receiver's BS/STmin and resume CFs. */
            iso->tx_state = ISOTP_TX_SENDING_CF;
            iso->tx_block_size = data[1];
            iso->tx_st_min = data[2];
            iso->tx_bs_counter = 0u;
            iso->timer_n_bs = 0u; /* FC arrived: disarm N_Bs */
            break;

        case ISOTP_FC_WAIT:
            /* Wait: keep waiting for a further FC and restart N_Bs. */
            iso->tx_state = ISOTP_TX_WAIT_FC;
            iso->timer_n_bs = 0u; /* re-armed on the next process() tick */
            break;

        case ISOTP_FC_OVA:
        default:
            /* Overflow or reserved/invalid FS: cancel the transmission. */
            iso->tx_state = ISOTP_TX_IDLE;
            iso->timer_n_bs = 0u;
            break;
    }
}

void uds_isotp_rx_callback(uds_isotp_ctx_t *iso, struct uds_ctx *uds, uint32_t id,
                           const uint8_t *data, uint8_t len)
{
    if (!iso || !data || len == 0u) {
        return;
    }

    uint8_t addr;
    if (id == iso->rx_id) {
        addr = (uint8_t) UDS_ADDR_PHYSICAL;
    }
    else if ((iso->rx_id_func != 0u) && (id == iso->rx_id_func)) {
        addr = (uint8_t) UDS_ADDR_FUNCTIONAL;
    }
    else {
        return; /* not for us */
    }

    uint8_t pci = data[0] & 0xF0;

    if (addr == (uint8_t) UDS_ADDR_FUNCTIONAL) {
        /* Functional addressing is Single-Frame only (ISO 15765-2):
           segmented transfer and flow control are undefined for one-to-many. */
        if (pci == ISOTP_PCI_SF) {
            uds_rx_sf(iso, uds, data, len, (uint8_t) UDS_ADDR_FUNCTIONAL);
        }
        return;
    }

    switch (pci) {
        case ISOTP_PCI_SF:
            uds_rx_sf(iso, uds, data, len, (uint8_t) UDS_ADDR_PHYSICAL);
            break;
        case ISOTP_PCI_FF:
            uds_rx_ff(iso, uds, data, len);
            break;
        case ISOTP_PCI_CF:
            uds_rx_cf(iso, uds, data, len);
            break;
        case ISOTP_PCI_FC:
            uds_rx_fc(iso, data, len);
            break;
        default:
            break;
    }
}
