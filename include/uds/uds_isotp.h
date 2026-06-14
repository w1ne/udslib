/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_isotp.h
 * @brief ISO-TP (ISO 15765-2) Transport Layer Implementation
 */

#ifndef UDS_ISOTP_H
#define UDS_ISOTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration of the opaque core context. */
struct uds_ctx;

/* --- ISO-TP Frame Types (PCI) --- */

#define ISOTP_PCI_SF 0x00 /**< Single Frame */
#define ISOTP_PCI_FF 0x10 /**< First Frame */
#define ISOTP_PCI_CF 0x20 /**< Consecutive Frame */
#define ISOTP_PCI_FC 0x30 /**< Flow Control Frame */

/* --- Protocol Constants --- */
#define ISOTP_MAX_DL_CAN 8u         /**< Max DLC for Classic CAN */
#define ISOTP_MAX_DL_CANFD 64u      /**< Max DLC for CAN-FD */
#define ISOTP_SF_MAX_DL_CAN 7u      /**< Max SF payload (Standard) */
#define ISOTP_SF_MAX_DL_CANFD 62u   /**< Max SF payload (FD) */
#define ISOTP_FF_MAX_DATA_CAN 6u    /**< Max FF payload (Standard) */
#define ISOTP_FF_MAX_DATA_CANFD 62u /**< Max FF payload (FD) */
#define ISOTP_MAX_SDU_LEN_STD 4095u /**< Max SDU size with 12-bit length */

/* --- Timeout Defaults (ISO 15765-2, milliseconds) --- */
#define ISOTP_N_CR_DEFAULT_MS 1000u /**< Max wait for a consecutive frame (RX) */
#define ISOTP_N_BS_DEFAULT_MS 1000u /**< Max wait for flow control after FF (TX) */

/* --- Flow Control Flags --- */

#define ISOTP_FC_CTS 0  /**< Continue To Send */
#define ISOTP_FC_WAIT 1 /**< Wait */
#define ISOTP_FC_OVA 2  /**< Overflow / Abort */

/* --- Type Definitions --- */

/**
 * @brief ISO-TP Internal State Machine.
 */
typedef enum
{
    ISOTP_IDLE = 0,

    /* --- Reception States --- */
    ISOTP_RX_WAIT_CF, /**< Received FF, sent FC, waiting for CFs */

    /* --- Transmission States --- */
    ISOTP_TX_WAIT_FC,   /**< Sent FF, waiting for FC */
    ISOTP_TX_SENDING_CF /**< Received CTS, sending CFs */
} uds_isotp_state_t;

/**
 * @brief CAN Frame Structure (Platform Agnostic).
 */
typedef struct
{
    uint32_t id;                      /**< CAN Identifier (Standard or Extended) */
    uint8_t len;                      /**< Data Length Code (DLC) */
    uint8_t data[ISOTP_MAX_DL_CANFD]; /**< Payload data (64 bytes max for CAN-FD) */
} uds_can_frame_t;

/**
 * @brief User-provided CAN Send Function.
 *
 * @param id   CAN ID to transmit.
 * @param data Pointer to the buffer containing the frame payload.
 * @param len  Length of the data (up to 64 for CAN-FD).
 * @return     0 on success, negative error code on failure.
 */
typedef int (*uds_can_send_fn)(uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief ISO-TP Runtime Context.
 */
typedef struct
{
    uds_can_send_fn can_send; /**< Output function for CAN frames */

    /* --- Configuration --- */
    uint32_t tx_id;     /**< CAN ID to transmit on (Source) */
    uint32_t rx_id;     /**< CAN ID to listen for (Target) */
    uint8_t block_size; /**< BS: Number of blocks before next FC */
    uint8_t st_min;     /**< STmin: Minimum separation time between frames */
    uint8_t use_can_fd; /**< Flag: Enable CAN-FD support (0=Standard, 1=FD) */

    /* --- State --- */
    uds_isotp_state_t state;  /**< Current state machine position */
    uint16_t msg_len;         /**< Total length of current message SDU */
    uint16_t bytes_processed; /**< Number of SDU bytes handled so far */
    uint8_t sn;               /**< Current Sequence Number (0-15) */
    uint8_t bs_counter;       /**< Counter tracking blocks sent/received */

    /* --- Timers --- */
    uint32_t timer_n_cr; /**< Timestamp of last RX progress (N_Cr deadline base) */
    uint32_t timer_n_bs; /**< Timestamp FF was sent (N_Bs deadline base, 0 = unarmed) */
    uint32_t timer_st;   /**< Separation Time timer (STmin) */
    uint8_t tx_dl;       /**< Transmit Data Length (Max frame size: 8 or 64) */

    /* --- Timeout limits (ms); defaulted at init, overridable by the caller --- */
    uint32_t n_cr_ms; /**< Max wait for a consecutive frame during reception */
    uint32_t n_bs_ms; /**< Max wait for flow control after sending a First Frame */

    /* --- Multi-frame TX cache (caller-provided, zero-malloc) --- */
    uint8_t *tx_sdu_buf;  /**< Buffer caching the SDU during multi-frame TX */
    uint16_t tx_sdu_size; /**< Capacity of tx_sdu_buf in bytes */
    uint16_t tx_sdu_len;  /**< Length of the SDU currently being transmitted */
} uds_isotp_ctx_t;

/* --- Public API --- */

/**
 * @brief Initialize an ISO-TP instance.
 *
 * The instance is fully caller-owned; multiple independent channels may run
 * concurrently, each with its own context and TX cache buffer.
 *
 * @param iso         Pointer to a caller-allocated ISO-TP context.
 * @param can_send    Pointer to the user's CAN send implementation.
 * @param tx_id       CAN ID to use for outbound frames.
 * @param rx_id       CAN ID to filter for inbound frames.
 * @param tx_sdu_buf  Caller-provided buffer used to cache the SDU during
 *                    multi-frame transmission (sized to the largest SDU sent).
 * @param tx_sdu_size Capacity of tx_sdu_buf in bytes.
 */
void uds_tp_isotp_init(uds_isotp_ctx_t *iso, uds_can_send_fn can_send, uint32_t tx_id,
                       uint32_t rx_id, uint8_t *tx_sdu_buf, uint16_t tx_sdu_size);

/**
 * @brief Enable or Disable CAN-FD support for an instance.
 *
 * @param iso     Pointer to the ISO-TP context.
 * @param enabled true to enable CAN-FD (64-byte frames), false for Classic CAN (8-byte).
 */
void uds_tp_isotp_set_fd(uds_isotp_ctx_t *iso, bool enabled);

/**
 * @brief Send an SDU via ISO-TP.
 *
 * This function handles segmentation into SF or FF/CF frames.
 *
 * @param iso  Pointer to the ISO-TP context.
 * @param data Pointer to the buffer containing the SDU to send.
 * @param len  Length of the SDU in bytes.
 * @return     0 on success, or a negative error code on failure.
 */
int uds_isotp_send(uds_isotp_ctx_t *iso, const uint8_t *data, uint16_t len);

/**
 * @brief CAN Receive Callback.
 *
 * Feeds a raw CAN frame into the ISO-TP engine for reassembly. Completed SDUs
 * are delivered to the core via uds_input_sdu(uds, ...).
 *
 * @param iso  Pointer to the ISO-TP context.
 * @param uds  Pointer to the core stack context that receives reassembled SDUs.
 * @param id   CAN ID of the received frame.
 * @param data Pointer to the CAN payload.
 * @param len  Length of the CAN payload (DLC).
 */
void uds_isotp_rx_callback(uds_isotp_ctx_t *iso, struct uds_ctx *uds, uint32_t id,
                           const uint8_t *data, uint8_t len);

/**
 * @brief Process ISO-TP periodic tasks for an instance.
 *
 * Must be called frequently to handle multi-frame timing and transmission.
 *
 * @param iso     Pointer to the ISO-TP context.
 * @param time_ms Current system time in milliseconds.
 */
void uds_tp_isotp_process(uds_isotp_ctx_t *iso, uint32_t time_ms);

#ifdef __cplusplus
}
#endif

#endif /* UDS_ISOTP_H */
