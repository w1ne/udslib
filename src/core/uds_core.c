/**
 * @file uds_core.c
 * @brief Core UDS Logic Implementation
 */

#include "uds/uds_core.h"
#include <string.h>

/**
 * @brief Internal Helper: Safe Log Wrapper 
 * Routes logs to the user-provided callback if available.
 */
static void uds_log(uds_ctx_t* ctx, uint8_t level, const char* msg) {
    if (ctx && ctx->config && ctx->config->fn_log) {
        ctx->config->fn_log(level, msg);
    }
}

int uds_init(uds_ctx_t* ctx, const uds_config_t* config) {
    if (!ctx || !config) {
        return UDS_ERR_INVALID_ARG;
    }

    /* Validate Mandatory Configs */
    if (!config->get_time_ms || !config->fn_tp_send) {
        return UDS_ERR_INVALID_ARG;
    }
    
    if (!config->rx_buffer || config->rx_buffer_size == 0) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    /* Initialize Context */
    memset(ctx, 0, sizeof(uds_ctx_t));
    ctx->config = config;
    ctx->state = 0; // Idle / Default Session
    ctx->active_session = 0x01; // Default Diagnostic Session
    
    uds_log(ctx, UDS_LOG_INFO, "UDS Stack Initialized");
    return UDS_OK;
}

void uds_process(uds_ctx_t* ctx) {
    if (!ctx) return;
    
    uint32_t now = ctx->config->get_time_ms();
    
    /* S3 Timer: Revert to Default Session if no activity */
    if (ctx->active_session != 0x01) {
        if ((now - ctx->last_msg_time) > 5000) { // 5s S3 Timeout
            ctx->active_session = 0x01;
            ctx->security_level = 0;
            uds_log(ctx, UDS_LOG_INFO, "S3 Timeout: Reverted to Default Session");
        }
    }

    /* P2/P2* Timing: Manage Response Deadlines */
    if (ctx->p2_msg_pending) {
        uint32_t timeout = ctx->p2_star_active ? ctx->config->p2_star_ms : ctx->config->p2_ms;
        
        if ((now - ctx->p2_timer_start) >= timeout) {
            /* Deadline exceeded - send 0x78 (Response Pending) */
            uds_send_nrc(ctx, ctx->pending_sid, 0x78);
            
            /* Reset timer and switch to P2* scale */
            ctx->p2_timer_start = now;
            ctx->p2_star_active = true;
            uds_log(ctx, UDS_LOG_DEBUG, "Sent NRC 0x78 - Extended Timeout (P2*) active");
        }
    }
}

static void uds_dispatch_service(uds_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    if (len == 0) return;
    uint8_t sid = data[0];
    ctx->pending_sid = sid; 

    uds_log(ctx, UDS_LOG_DEBUG, "Processing SDU...");
    
    /* Mark start of P2 timer */
    ctx->p2_timer_start = ctx->config->get_time_ms();
    ctx->p2_msg_pending = false;
    ctx->p2_star_active = false;

    /* Simple Echo / Hardcoded Response for validation */
    if (sid == 0x10 && len >= 2) {
        // ... (existing simplified session control)
        uint8_t sub = data[1];
        if (sub == 0x03) { // Extended Session
            ctx->active_session = 0x03;
            ctx->config->tx_buffer[0] = 0x50;
            ctx->config->tx_buffer[1] = 0x03;
            ctx->config->tx_buffer[2] = 0x00; 
            ctx->config->tx_buffer[3] = 0x32; 
            ctx->config->tx_buffer[4] = 0x01; 
            ctx->config->tx_buffer[5] = 0xF4; 
            uds_send_response(ctx, 6);
            uds_log(ctx, UDS_LOG_INFO, "Session changed to Extended");
        } else {
            ctx->active_session = 0x01;
            ctx->config->tx_buffer[0] = 0x50;
            ctx->config->tx_buffer[1] = sub;
            uds_send_response(ctx, 2);
        }
    } else if (sid == 0x3E && len >= 2) {
        // Tester Present
        uint8_t sub = data[1];
        if ((sub & 0x7F) == 0x00) {
            if (!(sub & 0x80)) { // Suppress Pos Response bit
                ctx->config->tx_buffer[0] = 0x7E;
                ctx->config->tx_buffer[1] = sub & 0x7F;
                uds_send_response(ctx, 2);
            }
        }
    } else if (sid == 0x22 && len >= 3) {
        // Read Data By Identifier
        uint16_t id = (data[1] << 8) | data[2];
        if (id == 0xF190) { // VIN Identifier
            ctx->config->tx_buffer[0] = 0x62;
            ctx->config->tx_buffer[1] = 0xF1;
            ctx->config->tx_buffer[2] = 0x90;
            memcpy(&ctx->config->tx_buffer[3], "LIBUDS_SIM_001", 14);
            uds_send_response(ctx, 3 + 14);
        } else {
            uds_send_nrc(ctx, sid, 0x31);
        }
    } else if (sid == 0x27 && len >= 2) {
        // Security Access
        uint8_t sub = data[1];
        if (sub == 0x01) { // Request Seed
            ctx->config->tx_buffer[0] = 0x67;
            ctx->config->tx_buffer[1] = 0x01;
            ctx->config->tx_buffer[2] = 0xDE; 
            ctx->config->tx_buffer[3] = 0xAD;
            ctx->config->tx_buffer[4] = 0xBE;
            ctx->config->tx_buffer[5] = 0xEF;
            uds_send_response(ctx, 6);
        } else if (sub == 0x02 && len >= 6) { // Send Key
            if (data[2] == 0xDF && data[3] == 0xAE && data[4] == 0xBF && data[5] == 0xF0) {
                ctx->security_level = 1;
                ctx->config->tx_buffer[0] = 0x67;
                ctx->config->tx_buffer[1] = 0x02;
                uds_send_response(ctx, 2);
            } else {
                uds_send_nrc(ctx, sid, 0x35);
            }
        }
    } else if (sid == 0x31) {
        /* Mock Routine Control: Mark as pending to test P2/P2* timing logic */
        ctx->p2_msg_pending = true;
        uds_log(ctx, UDS_LOG_INFO, "Service 0x31 is PENDING - Waiting for application...");
    } else {
        uds_send_nrc(ctx, sid, 0x11);
    }
}

/**
 * @brief UDS Client Request Dispatcher
 * 
 * Prepares the tx_buffer with the SID and payload, stores the user callback
 * and the pending SID in the context, and triggers the transport layer send.
 */
int uds_client_request(uds_ctx_t* ctx, uint8_t sid, const uint8_t* data, uint16_t len, uds_response_cb callback) {
    if (!ctx || !ctx->config) return UDS_ERR_INVALID_ARG;
    
    ctx->config->tx_buffer[0] = sid;
    if (data && len > 0) {
        memcpy(&ctx->config->tx_buffer[1], data, len);
    }
    
    ctx->client_cb = (void*)callback;
    ctx->pending_sid = sid;
    
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 1 + len);
}

/**
 * @brief Unified SDU Input Handler
 * 
 * This function is the entry point for all incoming UDS messages.
 * It implements a symmetric design:
 * 1. It first checks if the incoming message is a response to a pending Client request.
 * 2. If no client request is pending, it dispatches the message to the Server engine.
 */
void uds_input_sdu(uds_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    if (!ctx || !data || len == 0) return;
    
    ctx->last_msg_time = ctx->config->get_time_ms();
    
    uint8_t sid = data[0];
    
    /* Check if this is a response to a client request */
    if (ctx->pending_sid != 0) {
        bool is_pos = (sid == (ctx->pending_sid + 0x40));
        bool is_neg = (sid == 0x7F && len >= 2 && data[1] == ctx->pending_sid);
        
        if (is_pos || is_neg) {
            uds_response_cb cb = (uds_response_cb)ctx->client_cb;
            if (cb) {
                cb(ctx, sid, data, len);
            }
            ctx->pending_sid = 0;
            ctx->client_cb = NULL;
            return;
        }
    }
    
    // Fallback: handle as a server service if no client request is pending or matched
    uds_dispatch_service(ctx, data, len);
}

int uds_send_response(uds_ctx_t* ctx, uint16_t len) {
    if (!ctx) return UDS_ERR_NOT_INIT;
    ctx->p2_msg_pending = false;
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len);
}

int uds_send_nrc(uds_ctx_t* ctx, uint8_t sid, uint8_t nrc) {
    if (!ctx) return UDS_ERR_NOT_INIT;
    
    /* Clear pending flag unless this is the 0x78 (Response Pending) NRC */
    if (nrc != 0x78) {
        ctx->p2_msg_pending = false;
    }
    
    ctx->config->tx_buffer[0] = 0x7F;
    ctx->config->tx_buffer[1] = sid;
    ctx->config->tx_buffer[2] = nrc;
    
    uds_log(ctx, UDS_LOG_INFO, "Sending NRC");
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 3);
}
