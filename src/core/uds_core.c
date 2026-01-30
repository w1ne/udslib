/**
 * @file uds_core.c
 * @brief Core UDS Logic Implementation
 */

#include <string.h>

#include "uds/uds_core.h"

/* --- Internal Helpers --- */

/**
 * @brief Internal Helper: Safe Log Wrapper.
 *
 * Routes logs to the user-provided callback if available.
 *
 * @param ctx   Pointer to the contextual state.
 * @param level Log severity level.
 * @param msg   Message string to log.
 */
static void uds_internal_log(uds_ctx_t *ctx, uint8_t level, const char *msg)
{
    if (ctx && ctx->config && ctx->config->fn_log) {
        ctx->config->fn_log(level, msg);
    }
}

/**
 * @brief Internal Helper: Find DID in registry.
 *
 * Searches the config table for a specific Data Identifier.
 *
 * @param ctx Pointer to the contextual state.
 * @param id  DID to search for.
 * @return    Pointer to the entry if found, NULL otherwise.
 */
static const uds_did_entry_t *uds_internal_find_did(uds_ctx_t *ctx, uint16_t id)
{
    if (!ctx || !ctx->config) {
        return NULL;
    }
    const uds_did_table_t *table = &ctx->config->did_table;
    for (uint16_t i = 0; i < table->count; i++) {
        if (table->entries[i].id == id) {
            return &table->entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Internal Helper: Service Dispatcher.
 *
 * Matches the incoming Service ID (SID) and executes the corresponding service logic.
 *
 * @param ctx  Pointer to the contextual state.
 * @param data Pointer to the buffer containing the SDU.
 * @param len  Length of the data in bytes.
 */
static void uds_internal_dispatch_service(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return;
    }
    uint8_t sid = data[0];
    ctx->pending_sid = sid;

    uds_internal_log(ctx, UDS_LOG_DEBUG, "Processing SDU...");

    /* Mark start of P2 timer */
    ctx->p2_timer_start = ctx->config->get_time_ms();
    ctx->p2_msg_pending = false;
    ctx->p2_star_active = false;

    /* Simple Echo / Hardcoded Response for validation */
    if (sid == 0x10 && len >= 2) {
        uint8_t sub = data[1];
        if (sub == 0x03) { /* Extended Session */
            ctx->active_session = 0x03;
            ctx->config->tx_buffer[0] = 0x50;
            ctx->config->tx_buffer[1] = 0x03;
            ctx->config->tx_buffer[2] = 0x00;
            ctx->config->tx_buffer[3] = 0x32;
            ctx->config->tx_buffer[4] = 0x01;
            ctx->config->tx_buffer[5] = 0xF4;
            uds_send_response(ctx, 6);
            uds_internal_log(ctx, UDS_LOG_INFO, "Session changed to Extended");
        } else {
            ctx->active_session = 0x01;
            ctx->config->tx_buffer[0] = 0x50;
            ctx->config->tx_buffer[1] = sub;
            uds_send_response(ctx, 2);
        }
    } else if (sid == 0x3E && len >= 2) {
        /* Tester Present */
        uint8_t sub = data[1];
        if ((sub & 0x7F) == 0x00) {
            if (!(sub & 0x80)) { /* Suppress Pos Response bit */
                ctx->config->tx_buffer[0] = 0x7E;
                ctx->config->tx_buffer[1] = sub & 0x7F;
                uds_send_response(ctx, 2);
            }
        }
    } else if (sid == 0x22 && len >= 3) {
        /* Read Data By Identifier */
        uint16_t tx_len = 1; /* SID 0x62 set later */
        uint16_t i = 1;
        bool any_error = false;

        while (i + 1 < len) {
            uint16_t did = (data[i] << 8) | data[i + 1];
            const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

            if (entry) {
                ctx->config->tx_buffer[tx_len++] = (did >> 8) & 0xFF;
                ctx->config->tx_buffer[tx_len++] = did & 0xFF;

                if (entry->read) {
                    int res = entry->read(ctx, did, &ctx->config->tx_buffer[tx_len], entry->size);
                    if (res >= 0) {
                        tx_len += entry->size;
                    } else {
                        any_error = true;
                        break;
                    }
                } else if (entry->storage) {
                    memcpy(&ctx->config->tx_buffer[tx_len], entry->storage, entry->size);
                    tx_len += entry->size;
                } else {
                    any_error = true;
                    break;
                }
            } else {
                any_error = true;
                break;
            }
            i += 2;
        }

        if (any_error) {
            uds_send_nrc(ctx, sid, 0x31);
        } else {
            ctx->config->tx_buffer[0] = 0x62;
            uds_send_response(ctx, tx_len);
        }
    } else if (sid == 0x27 && len >= 2) {
        /* Security Access */
        uint8_t sub = data[1];
        if (sub == 0x01) { /* Request Seed */
            ctx->config->tx_buffer[0] = 0x67;
            ctx->config->tx_buffer[1] = 0x01;
            ctx->config->tx_buffer[2] = 0xDE;
            ctx->config->tx_buffer[3] = 0xAD;
            ctx->config->tx_buffer[4] = 0xBE;
            ctx->config->tx_buffer[5] = 0xEF;
            uds_send_response(ctx, 6);
        } else if (sub == 0x02 && len >= 6) { /* Send Key */
            if (data[2] == 0xDF && data[3] == 0xAE && data[4] == 0xBF && data[5] == 0xF0) {
                ctx->security_level = 1;
                ctx->config->tx_buffer[0] = 0x67;
                ctx->config->tx_buffer[1] = 0x02;
                uds_send_response(ctx, 2);
            } else {
                uds_send_nrc(ctx, sid, 0x35);
            }
        }
    } else if (sid == 0x11 && len >= 2) {
        /* ECU Reset */
        uint8_t sub = data[1];
        if (sub >= 0x01 && sub <= 0x03) {
            ctx->config->tx_buffer[0] = 0x51;
            ctx->config->tx_buffer[1] = sub;
            uds_send_response(ctx, 2);

            if (ctx->config->fn_reset) {
                ctx->config->fn_reset(ctx, sub);
            }
        } else {
            uds_send_nrc(ctx, sid, 0x12); /* Subfunction Not Supported */
        }
    } else if (sid == 0x28 && len >= 2) {
        /* Communication Control */
        uint8_t ctrl_type = data[1] & 0x7F;
        if (ctrl_type <= 0x03) {
            ctx->comm_state = ctrl_type;
            if (!(data[1] & 0x80)) { /* Suppress Pos Response */
                ctx->config->tx_buffer[0] = 0x68;
                ctx->config->tx_buffer[1] = data[1];
                uds_send_response(ctx, 2);
            }
            uds_internal_log(ctx, UDS_LOG_INFO, "Communication state changed");
        } else {
            uds_send_nrc(ctx, sid, 0x12);
        }
    } else if (sid == 0x2E && len >= 3) {
        /* Write Data By Identifier */
        uint16_t did = (data[1] << 8) | data[2];
        const uds_did_entry_t *entry = uds_internal_find_did(ctx, did);

        if (entry && (len == (3 + entry->size))) {
            bool write_ok = false;
            if (entry->write) {
                int res = entry->write(ctx, did, &data[3], entry->size);
                if (res == 0) {
                    write_ok = true;
                }
            } else if (entry->storage) {
                memcpy(entry->storage, &data[3], entry->size);
                write_ok = true;
            }

            if (write_ok) {
                ctx->config->tx_buffer[0] = 0x6E;
                ctx->config->tx_buffer[1] = data[1];
                ctx->config->tx_buffer[2] = data[2];
                uds_send_response(ctx, 3);
            } else {
                uds_send_nrc(ctx, sid, 0x31);
            }
        } else {
            uds_send_nrc(ctx, sid, 0x31);
        }
    } else if (sid == 0x31) {
        /* Mock Routine Control: Mark as pending to test P2/P2* timing logic */
        ctx->p2_msg_pending = true;
        uds_internal_log(ctx, UDS_LOG_INFO, "Service 0x31 is PENDING - Waiting for app...");
    } else {
        uds_send_nrc(ctx, sid, 0x11);
    }
}

/* --- Public API --- */

int uds_init(uds_ctx_t *ctx, const uds_config_t *config)
{
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
    ctx->active_session = 0x01; /* Default Diagnostic Session */

    uds_internal_log(ctx, UDS_LOG_INFO, "UDS Stack Initialized");
    return UDS_OK;
}

void uds_process(uds_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    uint32_t now = ctx->config->get_time_ms();

    /* S3 Timer: Revert to Default Session if no activity */
    if (ctx->active_session != 0x01) {
        if ((now - ctx->last_msg_time) > 5000) { /* 5s S3 Timeout */
            ctx->active_session = 0x01;
            ctx->security_level = 0;
            uds_internal_log(ctx, UDS_LOG_INFO, "S3 Timeout: Reverted to Default Session");
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
            uds_internal_log(ctx, UDS_LOG_DEBUG, "Sent NRC 0x78 - P2* active");
        }
    }
}

int uds_client_request(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb callback)
{
    if (!ctx || !ctx->config) {
        return UDS_ERR_INVALID_ARG;
    }

    ctx->config->tx_buffer[0] = sid;
    if (data && len > 0) {
        memcpy(&ctx->config->tx_buffer[1], data, len);
    }

    ctx->client_cb = (void *)callback;
    ctx->pending_sid = sid;

    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 1 + len);
}

void uds_input_sdu(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (!ctx || !data || len == 0) {
        return;
    }

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

    /* Handle as server service */
    uds_internal_dispatch_service(ctx, data, len);
}

int uds_send_response(uds_ctx_t *ctx, uint16_t len)
{
    if (!ctx) {
        return UDS_ERR_NOT_INIT;
    }
    ctx->p2_msg_pending = false;
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len);
}

int uds_send_nrc(uds_ctx_t *ctx, uint8_t sid, uint8_t nrc)
{
    if (!ctx) {
        return UDS_ERR_NOT_INIT;
    }

    /* Clear pending flag unless this is 0x78 */
    if (nrc != 0x78) {
        ctx->p2_msg_pending = false;
    }

    ctx->config->tx_buffer[0] = 0x7F;
    ctx->config->tx_buffer[1] = sid;
    ctx->config->tx_buffer[2] = nrc;

    uds_internal_log(ctx, UDS_LOG_INFO, "Sending NRC");
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 3);
}
