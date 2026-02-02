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

/* --- ISO-TP Frame Types (PCI) --- */

#define ISOTP_PCI_SF 0x00 /**< Single Frame */
#define ISOTP_PCI_FF 0x10 /**< First Frame */
#define ISOTP_PCI_CF 0x20 /**< Consecutive Frame */
#define ISOTP_PCI_FC 0x30 /**< Flow Control Frame */

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
    uint32_t id;     /**< CAN Identifier (Standard or Extended) */
    uint8_t len;     /**< Data Length Code (DLC) */
    uint8_t data[8]; /**< Payload data (8 bytes max for Classical CAN) */
} uds_can_frame_t;

/**
 * @brief User-provided CAN Send Function.
 *
 * @param id   CAN ID to transmit.
 * @param data Pointer to the buffer containing the 8-byte frame payload.
 * @param len  Length of the data (usually 8 for ISO-TP).
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

    /* --- State --- */
    uds_isotp_state_t state;  /**< Current state machine position */
    uint16_t msg_len;         /**< Total length of current message SDU */
    uint16_t bytes_processed; /**< Number of SDU bytes handled so far */
    uint8_t sn;               /**< Current Sequence Number (0-15) */
    uint8_t bs_counter;       /**< Counter tracking blocks sent/received */

    /* --- Timers --- */
    uint32_t timer_n_cr; /**< Timeout N_Cr (Reception) */
    uint32_t timer_n_bs; /**< Timeout N_Bs (Transmission) */
    uint32_t timer_st;   /**< Separation Time timer (STmin) */
} uds_isotp_ctx_t;

/* --- Public API --- */

/**
 * @brief Initialize the ISO-TP Layer.
 *
 * @param can_send Pointer to the user's CAN send implementation.
 * @param tx_id    CAN ID to use for outbound frames.
 * @param rx_id    CAN ID to filter for inbound frames.
 */
void uds_tp_isotp_init(uds_can_send_fn can_send, uint32_t tx_id, uint32_t rx_id);

/**
 * @brief Send an SDU via ISO-TP.
 *
 * This function handles segmentation into SF or FF/CF frames.
 *
 * @param ctx  Pointer to the core UDS context.
 * @param data Pointer to the buffer containing the SDU to send.
 * @param len  Length of the SDU in bytes.
 * @return     0 on success, or -1 on failure.
 */
int uds_isotp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len);

/**
 * @brief CAN Receive Callback.
 *
 * Feeds a raw CAN frame into the ISO-TP engine for reassembly.
 *
 * @param uds_ctx Pointer to the main stack context.
 * @param id      CAN ID of the received frame.
 * @param data    Pointer to the 8-byte CAN payload.
 * @param len     Length of the CAN payload (DLC).
 */
void uds_isotp_rx_callback(struct uds_ctx *uds_ctx, uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Process ISO-TP periodic tasks.
 *
 * Must be called frequently to handle multi-frame timing and transmission.
 * @param time_ms Current system time in milliseconds.
 */
void uds_tp_isotp_process(uint32_t time_ms);

#ifdef __cplusplus
}
#endif

#endif /* UDS_ISOTP_H */
