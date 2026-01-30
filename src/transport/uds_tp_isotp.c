/**
 * @file uds_tp_isotp.c
 * @brief Lightweight ISO-TP Implementation (Zephyr-Ready Fallback)
 */

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include <string.h>

/* Static Context Single Instance for simplicity */
static uds_isotp_ctx_t isotp_ctx;
static uint8_t  pending_tx_sdu[1024]; // Simple buffer for segmented TX
static uint16_t pending_tx_len = 0;

/* Helper: Send CAN Frame */
static int send_frame(uds_isotp_ctx_t* iso, const uint8_t* data, uint8_t len) {
    if (iso && iso->can_send) {
        return iso->can_send(iso->tx_id, data, len);
    }
    return -1;
}

/**
 * @brief Initialization of the ISO-TP Layer
 * User calls this to set up the context and CAN interface.
 */
void uds_tp_isotp_init(uds_can_send_fn can_send, uint32_t tx_id, uint32_t rx_id) {
    memset(&isotp_ctx, 0, sizeof(isotp_ctx));
    isotp_ctx.can_send = can_send;
    isotp_ctx.tx_id = tx_id;
    isotp_ctx.rx_id = rx_id;
    isotp_ctx.block_size = 8; // Default BS
    isotp_ctx.st_min = 0;     // Default No Delay
}

/**
 * @brief UDS TP Send (ISO-TP Segmenter)
 * 
 * Handles both Single Frame (SF) and Multi-Frame (FF/CF) starts.
 * For Multi-Frame, it caches the payload in pending_tx_sdu and waits for 
 * Flow Control from the receiver before continuing with Consecutive Frames.
 */
int uds_isotp_send(uds_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    (void)ctx; /* Context unused in this simple static implementation */
    
    if (len <= 7) {
        /* Single Frame */
        uint8_t frame[8];
        frame[0] = (uint8_t)(ISOTP_PCI_SF | len);
        memcpy(&frame[1], data, len);
        
        // Pad with 0xAA or 0x00 if needed (optional, standard says optimization)
        // Here we send exact DLC if platform allows, or padding if strict 8 byte
        return send_frame(&isotp_ctx, frame, len + 1);
    } else {
        /* Multi-Frame: Send First Frame */
        if (len > 4095 || len > sizeof(pending_tx_sdu)) return -2;
        
        memcpy(pending_tx_sdu, data, len);
        pending_tx_len = len;
        
        isotp_ctx.msg_len = len;
        isotp_ctx.bytes_processed = 0;
        isotp_ctx.state = ISOTP_TX_WAIT_FC;
        
        uint8_t frame[8];
        frame[0] = (uint8_t)(ISOTP_PCI_FF | ((len >> 8) & 0x0F));
        frame[1] = (uint8_t)(len & 0xFF);
        memcpy(&frame[2], data, 6);
        isotp_ctx.bytes_processed = 6;
        isotp_ctx.sn = 1;

        if (send_frame(&isotp_ctx, frame, 8) != 0) return -1;
        
        return 0; // Started successfully
    }
}

/**
 * @brief Async ISO-TP Processor
 * 
 * Must be called frequently. It handles the state machine for multi-frame
 * transmission, sending Consecutive Frames (CF) when in the appropriate state.
 * Currently supports a simple "one CF per call" model for simplicity.
 */
void uds_tp_isotp_process(void) {
    if (isotp_ctx.state == ISOTP_TX_SENDING_CF) {
        // Simple implementation: send one CF per process call if no BS/STmin required
        // In a real stack, this would respect STmin.
        
        uint16_t remaining = isotp_ctx.msg_len - isotp_ctx.bytes_processed;
        if (remaining == 0) {
            isotp_ctx.state = ISOTP_IDLE;
            return;
        }

        uint8_t to_copy = (remaining > 7) ? 7 : remaining;
        uint8_t frame[8];
        frame[0] = (uint8_t)(ISOTP_PCI_CF | isotp_ctx.sn);
        memcpy(&frame[1], &pending_tx_sdu[isotp_ctx.bytes_processed], to_copy);
        
        if (send_frame(&isotp_ctx, frame, to_copy + 1) == 0) {
            isotp_ctx.bytes_processed += to_copy;
            isotp_ctx.sn = (isotp_ctx.sn + 1) & 0x0F;
            
            if (isotp_ctx.bytes_processed >= isotp_ctx.msg_len) {
                isotp_ctx.state = ISOTP_IDLE;
            }
        }
    }
}

/**
 * @brief CAN Receive Callback (Feeds into ISO-TP)
 * Call this from your CAN Driver ISR or Polling Loop.
 */
void uds_isotp_rx_callback(uds_ctx_t* uds_ctx, uint32_t id, const uint8_t* data, uint8_t len) {
    if (id != isotp_ctx.rx_id) return;
    
    uint8_t pci = data[0] & 0xF0;
    
    switch (pci) {
        case ISOTP_PCI_SF: {
            uint8_t sdu_len = data[0] & 0x0F;
            if (sdu_len == 0) return; // CAN FD 0 termination not supported in lite
            
            // Reassembly complete logic immediately
            uds_input_sdu(uds_ctx, &data[1], sdu_len);
            break;
        }
        
        case ISOTP_PCI_FF: {
            uint16_t sdu_len = ((data[0] & 0x0F) << 8) | data[1];
            isotp_ctx.msg_len = sdu_len;
            isotp_ctx.bytes_processed = 6;
            isotp_ctx.sn = 1;
            isotp_ctx.state = ISOTP_RX_WAIT_CF;
            
            // Copy first 6 bytes to RX Buffer
            if (uds_ctx->config->rx_buffer_size < sdu_len) {
                // Buffer overflow
                isotp_ctx.state = ISOTP_IDLE;
                return;
            }
            memcpy(uds_ctx->config->rx_buffer, &data[2], 6);
            
            // Send Flow Control (CTS)
            uint8_t fc[3] = {ISOTP_PCI_FC | ISOTP_FC_CTS, isotp_ctx.block_size, isotp_ctx.st_min};
            send_frame(&isotp_ctx, fc, 3);
            break;
        }
        
        case ISOTP_PCI_CF: {
            if (isotp_ctx.state != ISOTP_RX_WAIT_CF) return;
            
            uint8_t sn = data[0] & 0x0F;
            if (sn != isotp_ctx.sn) {
                // SN Error
                isotp_ctx.state = ISOTP_IDLE;
                return;
            }
            isotp_ctx.sn = (isotp_ctx.sn + 1) & 0x0F;
            
            uint16_t remaining = isotp_ctx.msg_len - isotp_ctx.bytes_processed;
            uint8_t to_copy = (remaining > 7) ? 7 : remaining;
            
            memcpy(&uds_ctx->config->rx_buffer[isotp_ctx.bytes_processed], &data[1], to_copy);
            isotp_ctx.bytes_processed += to_copy;
            
            if (isotp_ctx.bytes_processed >= isotp_ctx.msg_len) {
                // Reception Complete
                isotp_ctx.state = ISOTP_IDLE;
                uds_input_sdu(uds_ctx, uds_ctx->config->rx_buffer, isotp_ctx.msg_len);
            }
            break;
        }
        
        case ISOTP_PCI_FC: {
            if (isotp_ctx.state != ISOTP_TX_WAIT_FC) return;
            
            uint8_t fs = data[0] & 0x0F;
            if (fs == ISOTP_FC_CTS) {
                isotp_ctx.state = ISOTP_TX_SENDING_CF;
                isotp_ctx.block_size = data[1];
                isotp_ctx.st_min = data[2];
                // Trigger Transmission of CFs (usually done in process loop)
            }
            break;
        }
    }
}
