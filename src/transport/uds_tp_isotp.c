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

// cppcheck-suppress unusedFunction
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
    iso->block_size = 8;           /* Default Block Size */
    iso->st_min = 0;               /* Default No Delay */
    iso->use_can_fd = 0;           /* Default: Classic CAN */
    iso->tx_dl = ISOTP_MAX_DL_CAN; /* Default: 8 bytes */
    iso->tx_sdu_buf = tx_sdu_buf;
    iso->tx_sdu_size = tx_sdu_size;
    iso->tx_sdu_len = 0;
    iso->n_cr_ms = ISOTP_N_CR_DEFAULT_MS;
    iso->n_bs_ms = ISOTP_N_BS_DEFAULT_MS;
}

// cppcheck-suppress unusedFunction
void uds_tp_isotp_set_fd(uds_isotp_ctx_t *iso, bool enabled)
{
    if (!iso) {
        return;
    }
    iso->use_can_fd = enabled ? 1 : 0;
    iso->tx_dl = enabled ? ISOTP_MAX_DL_CANFD : ISOTP_MAX_DL_CAN;
}

/**
 * @brief Internal: Send Single Frame.
 */
static int uds_send_sf(uds_isotp_ctx_t *iso, const uint8_t *data, uint16_t len)
{
    uint8_t frame[ISOTP_MAX_DL_CANFD] = {0};
    uint8_t dl = ISOTP_MAX_DL_CAN;

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
    if (len > ISOTP_MAX_SDU_LEN_STD || len > iso->tx_sdu_size) {
        return -2;
    }

    memcpy(iso->tx_sdu_buf, data, len);
    iso->tx_sdu_len = len;

    iso->msg_len = len;
    iso->bytes_processed = 0;
    iso->state = ISOTP_TX_WAIT_FC;
    iso->timer_n_bs = 0u; /* armed on the first process() tick in WAIT_FC */

    uint8_t frame[ISOTP_MAX_DL_CANFD] = {0};
    uint8_t dl = ISOTP_MAX_DL_CAN;

    /* FF Header: [1n] [nn] */
    frame[0] = (uint8_t) ((uint8_t) ISOTP_PCI_FF | (uint8_t) ((len >> 8u) & 0x0Fu));
    frame[1] = (uint8_t) (len & 0xFFu);

    uint8_t max_data_in_ff = (iso->use_can_fd) ? ISOTP_FF_MAX_DATA_CANFD : ISOTP_FF_MAX_DATA_CAN;

    /* Copy as much as fits in FF */
    uint8_t to_copy = (len > max_data_in_ff) ? max_data_in_ff : (uint8_t) len;
    memcpy(&frame[2], data, to_copy);

    iso->bytes_processed = to_copy;
    iso->sn = 1u;

    if (iso->use_can_fd) {
        dl = uds_dlc_align(2 + to_copy);
    }
    else {
        dl = ISOTP_MAX_DL_CAN;
    }

    if (uds_internal_tp_send_frame(iso, frame, dl) != 0) {
        return -1;
    }

    return 0; /* Multi-Frame started successfully */
}

// cppcheck-suppress unusedFunction
int uds_isotp_send(uds_isotp_ctx_t *iso, const uint8_t *data, uint16_t len)
{
    if (!iso) {
        return -1;
    }

    /* Check if we can use Single Frame */
    uint8_t max_sf_len = (iso->use_can_fd) ? ISOTP_SF_MAX_DL_CANFD : ISOTP_SF_MAX_DL_CAN;

    if (len <= max_sf_len) {
        return uds_send_sf(iso, data, len);
    }

    return uds_send_mf(iso, data, len);
}

// cppcheck-suppress unusedFunction
void uds_tp_isotp_process(uds_isotp_ctx_t *iso, uint32_t time_ms)
{
    if (!iso) {
        return;
    }

    /* N_Cr: reception stalls if a consecutive frame never arrives. */
    if (iso->state == ISOTP_RX_WAIT_CF) {
        if ((time_ms - iso->timer_n_cr) >= iso->n_cr_ms) {
            iso->state = ISOTP_IDLE;
        }
        return;
    }

    /* N_Bs: transmission stalls if flow control never arrives after the FF. */
    if (iso->state == ISOTP_TX_WAIT_FC) {
        if (iso->timer_n_bs == 0u) {
            /* Arm on first observation; avoid 0 which means "unarmed". */
            iso->timer_n_bs = (time_ms == 0u) ? 1u : time_ms;
        }
        else if ((time_ms - iso->timer_n_bs) >= iso->n_bs_ms) {
            iso->state = ISOTP_IDLE;
            iso->timer_n_bs = 0u;
        }
        return;
    }

    if (iso->state == ISOTP_TX_SENDING_CF) {
        uint16_t remaining = iso->msg_len - iso->bytes_processed;
        if (remaining == 0) {
            iso->state = ISOTP_IDLE;
            return;
        }

        /* Check STmin (Separation Time) */
        uint32_t elapsed = time_ms - iso->timer_st;
        uint32_t required_st = iso->st_min;

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
        if (iso->block_size > 0 && iso->bs_counter >= iso->block_size) {
            iso->state = ISOTP_TX_WAIT_FC;
            iso->bs_counter = 0;
            return;
        }

        /* Calculate max payload per CF */
        uint8_t max_cf_payload = (iso->use_can_fd) ? (ISOTP_MAX_DL_CANFD - 1)
                                                   : (ISOTP_MAX_DL_CAN - 1); /* Header is 1 byte */

        uint8_t to_copy = (remaining > max_cf_payload) ? max_cf_payload : (uint8_t) remaining;
        uint8_t frame[ISOTP_MAX_DL_CANFD] = {0};
        frame[0] = (uint8_t) (ISOTP_PCI_CF | iso->sn);
        memcpy(&frame[1], &iso->tx_sdu_buf[iso->bytes_processed], to_copy);

        uint8_t dl = ISOTP_MAX_DL_CAN;
        if (iso->use_can_fd) {
            dl = uds_dlc_align(1 + to_copy);
        }

        if (uds_internal_tp_send_frame(iso, frame, dl) == 0) {
            iso->bytes_processed += to_copy;
            iso->sn = (iso->sn + 1) & 0x0F;
            iso->bs_counter++;
            iso->timer_st = time_ms; /* Reset ST timer */

            if (iso->bytes_processed >= iso->msg_len) {
                iso->state = ISOTP_IDLE;
            }
        }
    }
}

static void uds_rx_sf(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
{
    /* Abort any active multi-frame on new Single Frame */
    iso->state = ISOTP_IDLE;

    uint8_t sdu_len = (uint8_t) (data[0] & 0x0Fu);
    uint8_t data_offset = 1;

    if (sdu_len == 0u) {
        /* CAN-FD SF: Byte 0 is 0x00, Byte 1 is Length */
        if (len < 2u) return; /* Not enough data for the FD length byte */
        sdu_len = data[1];
        data_offset = 2;
        if (sdu_len == 0) return; /* Invalid */
    }

    if (sdu_len > (len - data_offset)) {
        /* Not enough data in frame */
        return;
    }

    uds_input_sdu(uds, &data[data_offset], (uint16_t) sdu_len);
}

static void uds_rx_ff(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
{
    /* Abort any active multi-frame on new First Frame */
    iso->state = ISOTP_IDLE;

    if (len < 2u) {
        return; /* FF requires at least PCI + length byte */
    }

    uint16_t sdu_len =
        (uint16_t) ((uint16_t) ((uint16_t) data[0] & 0x0Fu) << 8u) | (uint16_t) data[1];
    if (sdu_len < 8u) {
        return; /* Multi-frame must be > 7 bytes (Standard) or handled by SF */
    }

    iso->msg_len = sdu_len;

    /* Determine data in FF */
    uint8_t data_in_ff;
    if (len > ISOTP_MAX_DL_CAN) {
        /* CAN-FD FF */
        data_in_ff = len - 2;
    }
    else {
        /* Classic CAN FF */
        data_in_ff = ISOTP_FF_MAX_DATA_CAN;
    }

    iso->bytes_processed = data_in_ff;
    iso->sn = 1;
    iso->state = ISOTP_RX_WAIT_CF;
    iso->timer_n_cr = uds->config->get_time_ms ? uds->config->get_time_ms() : 0u;

    if (uds->config->rx_buffer_size < sdu_len) {
        iso->state = ISOTP_IDLE;
        return;
    }
    memcpy(uds->config->rx_buffer, &data[2], data_in_ff);

    /* Send Flow Control (CTS) */
    uint8_t fc[8] = {0};
    fc[0] = (uint8_t) (ISOTP_PCI_FC | ISOTP_FC_CTS);
    fc[1] = iso->block_size;
    fc[2] = iso->st_min;
    uds_internal_tp_send_frame(iso, fc, 8);
}

static void uds_rx_cf(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
{
    if (iso->state != ISOTP_RX_WAIT_CF) {
        return;
    }

    uint8_t sn = data[0] & 0x0F;
    if (sn != iso->sn) {
        iso->state = ISOTP_IDLE;
        return;
    }
    iso->sn = (iso->sn + 1) & 0x0F;

    uint16_t remaining = iso->msg_len - iso->bytes_processed;

    /* Max payload in CF depends on whether we received FD frame (len > 8) or not. */
    uint8_t data_capacity = len - 1; /* Byte 0 is PCI+SN */

    uint8_t to_copy = (remaining > data_capacity) ? data_capacity : (uint8_t) remaining;

    memcpy(&uds->config->rx_buffer[iso->bytes_processed], &data[1], to_copy);
    iso->bytes_processed += to_copy;
    iso->timer_n_cr = uds->config->get_time_ms ? uds->config->get_time_ms() : 0u;

    if (iso->bytes_processed >= iso->msg_len) {
        iso->state = ISOTP_IDLE;
        uds_input_sdu(uds, uds->config->rx_buffer, iso->msg_len);
    }
}

static void uds_rx_fc(uds_isotp_ctx_t *iso, const uint8_t *data, uint8_t len)
{
    if (iso->state != ISOTP_TX_WAIT_FC) {
        return;
    }
    if (len < 3u) {
        return; /* FC requires flow status, block size and STmin */
    }

    uint8_t fs = data[0] & 0x0F;
    if (fs == ISOTP_FC_CTS) {
        iso->state = ISOTP_TX_SENDING_CF;
        iso->block_size = data[1];
        iso->st_min = data[2];
        iso->timer_n_bs = 0u; /* FC arrived: disarm N_Bs */
    }
}

// cppcheck-suppress unusedFunction
void uds_isotp_rx_callback(uds_isotp_ctx_t *iso, struct uds_ctx *uds, uint32_t id,
                           const uint8_t *data, uint8_t len)
{
    if (!iso || !data || len == 0u) {
        return;
    }
    if (id != iso->rx_id) {
        return;
    }

    uint8_t pci = data[0] & 0xF0;

    switch (pci) {
        case ISOTP_PCI_SF:
            uds_rx_sf(iso, uds, data, len);
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
