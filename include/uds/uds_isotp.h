#ifndef UDS_ISOTP_H
#define UDS_ISOTP_H

#include <stdint.h>
#include <stdbool.h>

/* ISO-TP Frame Types (PCI) */
#define ISOTP_PCI_SF  0x00
#define ISOTP_PCI_FF  0x10
#define ISOTP_PCI_CF  0x20
#define ISOTP_PCI_FC  0x30

/* Flow Control Flags */
#define ISOTP_FC_CTS  0
#define ISOTP_FC_WAIT 1
#define ISOTP_FC_OVA  2

/* Internal State Machine */
typedef enum {
    ISOTP_IDLE = 0,
    
    /* Reception States */
    ISOTP_RX_WAIT_CF,       /**< Received FF, sent FC, waiting for CFs */
    
    /* Transmission States */
    ISOTP_TX_WAIT_FC,       /**< Sent FF, waiting for FC */
    ISOTP_TX_SENDING_CF     /**< Received CTS, sending CFs */
} uds_isotp_state_e;

/**
 * @brief CAN Frame Structure (Platform Agnostic)
 */
typedef struct {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
} uds_can_frame_t;

/**
 * @brief User-provided CAN Send Function
 */
typedef int (*uds_can_send_fn)(uint32_t id, const uint8_t* data, uint8_t len);

/**
 * @brief ISO-TP Runtime Context
 * Attached to uds_ctx_t via the Transport Layer wrapper.
 */
typedef struct {
    uds_can_send_fn can_send;
    
    /* Configuration */
    uint32_t tx_id;
    uint32_t rx_id;
    uint8_t  block_size;    /* BS: Blocks before next FC */
    uint8_t  st_min;        /* STmin: Min separation time */

    /* State */
    uds_isotp_state_e state;
    uint16_t msg_len;       /* Total length of current message */
    uint16_t bytes_processed; /* Bytes received/sent so far */
    uint8_t  sn;            /* Sequence Number */
    uint8_t  bs_counter;    /* Counts frames until next FC check */
    
    /* Timers */
    uint32_t timer_n_cr;    /* Timeout for next CF (Rx) */
    uint32_t timer_n_bs;    /* Timeout for FC (Tx) */
    
} uds_isotp_ctx_t;

/* --- Public API --- */

/**
 * @brief Initialize the ISO-TP Layer
 * @param can_send Pointer to the user's CAN send function
 * @param tx_id    CAN ID to transmit on
 * @param rx_id    CAN ID to listen to
 */
void uds_tp_isotp_init(uds_can_send_fn can_send, uint32_t tx_id, uint32_t rx_id);

/**
 * @brief Send an SDU via ISO-TP (fits uds_tp_send_fn signature)
 */
int uds_isotp_send(struct uds_ctx* ctx, const uint8_t* data, uint16_t len);

/**
 * @brief Process an incoming CAN frame into the ISO-TP layer
 * @param uds_ctx Pointer to the main stack context
 * @param id      CAN ID of the received frame
 * @param data    Pointer to CAN data
 * @param len     Length of CAN data (DLC)
 */
void uds_isotp_rx_callback(struct uds_ctx* uds_ctx, uint32_t id, const uint8_t* data, uint8_t len);

/**
 * @brief Process ISO-TP Timers and CF Transmission
 */
void uds_tp_isotp_process(void);

#endif /* UDS_ISOTP_H */
