/**
 * @file uds_core.c
 * @brief Core UDS Logic Implementation
 */

#include <string.h>

#include "uds/uds_core.h"
#include "uds_internal.h"

/* --- Core Service Table --- */

static const uds_service_entry_t core_services[] = {
    {0x10, 2, UDS_SESSION_ALL, 0, uds_internal_handle_session_control},
    {0x11, 2, UDS_SESSION_ALL, 0, uds_internal_handle_ecu_reset},
    {0x22, 3, UDS_SESSION_ALL, 0, uds_internal_handle_read_data_by_id},
    {0x27, 2, UDS_SESSION_ALL, 0, uds_internal_handle_security_access},
    {0x28, 2, UDS_SESSION_ALL, 0, uds_internal_handle_comm_control},
    {0x2E, 3, UDS_SESSION_ALL, 0, uds_internal_handle_write_data_by_id},
    {0x3E, 2, UDS_SESSION_ALL, 0, uds_internal_handle_tester_present},
};

#define CORE_SERVICE_COUNT (sizeof(core_services) / sizeof(core_services[0]))

/* --- Internal Helpers --- */

void uds_internal_log(uds_ctx_t *ctx, uint8_t level, const char *msg)
{
    if (ctx && ctx->config && ctx->config->fn_log) {
        ctx->config->fn_log(level, msg);
    }
}

const uds_did_entry_t *uds_internal_find_did(uds_ctx_t *ctx, uint16_t id)
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

static const uds_service_entry_t *find_service(uds_ctx_t *ctx, uint8_t sid)
{
    /* 1. Check User Services first (Override capability) */
    if (ctx->config->user_services) {
        for (uint16_t i = 0; i < ctx->config->user_service_count; i++) {
            if (ctx->config->user_services[i].sid == sid) {
                return &ctx->config->user_services[i];
            }
        }
    }

    /* 2. Check Core Services */
    for (uint16_t i = 0; i < CORE_SERVICE_COUNT; i++) {
        if (core_services[i].sid == sid) {
            return &core_services[i];
        }
    }

    return NULL;
}

static uint8_t get_session_bit(uint8_t session)
{
    switch (session) {
    case 0x01:
        return UDS_SESSION_DEFAULT;
    case 0x02:
        return UDS_SESSION_PROGRAMMING;
    case 0x03:
        return UDS_SESSION_EXTENDED;
    default:
        return 0;
    }
}

/**
 * @brief Internal Helper: Service Dispatcher.
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

    /* 1. Find Service */
    const uds_service_entry_t *service = find_service(ctx, sid);
    if (!service) {
        /* Special case for 0x31 mock while it's not fully modularized */
        if (sid == 0x31) {
            ctx->p2_msg_pending = true;
            uds_internal_log(ctx, UDS_LOG_INFO, "Service 0x31 is PENDING - Waiting for app...");
            return;
        }
        uds_send_nrc(ctx, sid, 0x11); /* Service Not Supported */
        return;
    }

    /* 2. Validate Session */
    uint8_t sess_bit = get_session_bit(ctx->active_session);
    if (!(service->session_mask & sess_bit)) {
        uds_send_nrc(ctx, sid, 0x7F); /* Service Not Supported in Active Session */
        return;
    }

    /* 3. Validate Length */
    if (len < service->min_len) {
        uds_send_nrc(ctx, sid, 0x13); /* Incorrect Message Length */
        return;
    }

    /* 4. Validate Security (Simple level check) */
    if (ctx->security_level < service->security_mask) {
        uds_send_nrc(ctx, sid, 0x33); /* Security Access Denied */
        return;
    }

    /* 5. Dispatch to Handler */
    int res = service->handler(ctx, data, len);
    if (res < 0) {
        if (res == -1) {
            uds_send_nrc(ctx, sid, 0x12); /* Subfunction Not Supported */
        }
        /* Other negative values assumed to have sent their own NRC or handled already */
    } else if (res == 1) { /* UDS_PENDING */
        ctx->p2_msg_pending = true;
    }
}

/* --- Public API --- */

int uds_init(uds_ctx_t *ctx, const uds_config_t *config)
{
    if (!ctx || !config) {
        return UDS_ERR_INVALID_ARG;
    }

    /* Validate mandatory config members */
    if (!config->get_time_ms || !config->fn_tp_send || !config->rx_buffer || !config->tx_buffer) {
        return UDS_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(uds_ctx_t));
    ctx->config = config;
    ctx->active_session = 0x01; /* Default Session */
    ctx->security_level = 0;    /* Locked */
    ctx->comm_state = 0x00;     /* Enable Rx/Tx */

    uds_internal_log(ctx, UDS_LOG_INFO, "UDS Stack Initialized");
    return UDS_OK;
}

void uds_process(uds_ctx_t *ctx)
{
    if (!ctx || !ctx->config) {
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
        uint32_t elapsed = now - ctx->p2_timer_start;
        uint32_t limit = ctx->p2_star_active ? ctx->config->p2_star_ms : ctx->config->p2_ms;

        if (elapsed >= limit) {
            /* Send NRC 0x78 (Response Pending) */
            uds_send_nrc(ctx, ctx->pending_sid, 0x78);
            ctx->p2_star_active = true;
            ctx->p2_timer_start = now; /* Reset timer for P2* */
        }
    }
}

int uds_client_request(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb callback)
{
    if (!ctx || !ctx->config) {
        return UDS_ERR_NOT_INIT;
    }

    ctx->pending_sid = sid;
    ctx->client_cb = (void *)callback;

    ctx->config->tx_buffer[0] = sid;
    if (data && len > 0) {
        memcpy(&ctx->config->tx_buffer[1], data, len);
    }

    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len + 1);
}

void uds_input_sdu(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (!ctx || !data || len == 0) {
        return;
    }

    /* Update S3 timer tracking */
    ctx->last_msg_time = ctx->config->get_time_ms();

    uint8_t sid = data[0];

    /* Check if this is a response to our previous request */
    if (ctx->pending_sid != 0) {
        bool is_pos = (sid == (ctx->pending_sid | 0x40));
        bool is_neg = (sid == 0x7F && len >= 2 && data[1] == ctx->pending_sid);

        if (is_pos || is_neg) {
            if (ctx->client_cb) {
                uds_response_cb cb = (uds_response_cb)ctx->client_cb;
                cb(ctx, sid, &data[1], len - 1);
                ctx->client_cb = NULL;
            }
            ctx->pending_sid = 0;
            return;
        }
    }

    /* Otherwise, process as a request to us (the server) */
    uds_internal_dispatch_service(ctx, data, len);
}

int uds_send_response(uds_ctx_t *ctx, uint16_t len)
{
    if (!ctx || !ctx->config) {
        return UDS_ERR_NOT_INIT;
    }

    ctx->p2_msg_pending = false;
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len);
}

int uds_send_nrc(uds_ctx_t *ctx, uint8_t sid, uint8_t nrc)
{
    if (!ctx || !ctx->config) {
        return UDS_ERR_NOT_INIT;
    }

    /* NRC 0x78 does not clear the pending flag, others do */
    if (nrc != 0x78) {
        ctx->p2_msg_pending = false;
    }

    ctx->config->tx_buffer[0] = 0x7F;
    ctx->config->tx_buffer[1] = sid;
    ctx->config->tx_buffer[2] = nrc;

    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 3);
}
