/**
 * @file uds_core.c
 * @brief Core UDS Logic Implementation
 */

#include <string.h>

#include "uds/uds_core.h"
#include "uds_internal.h"

/* --- Subfunction Masks (16 bytes = 128 bits for 0x00-0x7F) --- */
static const uint8_t mask_sub_10[] = {0x0E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* 1, 2, 3 */
static const uint8_t mask_sub_11[] = {0x0E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* 1, 2, 3 */
static const uint8_t mask_sub_19[] = {0x57, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* 01, 02, 04, 06, 0A */
static const uint8_t mask_sub_27[] = {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; /* 1..127 */
static const uint8_t mask_sub_28[] = {0x3F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* 0, 1, 2, 3, 4, 5 */
static const uint8_t mask_sub_31[] = {0x0E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* 1, 2, 3 */
static const uint8_t mask_sub_3E[] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* 0 */
static const uint8_t mask_sub_85[] = {0x06, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* 1, 2 */

static const uds_service_entry_t core_services[] = {
    {0x10, 2, UDS_SESSION_ALL, 0, uds_internal_handle_session_control, mask_sub_10},
    {0x11, 2, UDS_SESSION_ALL, 0, uds_internal_handle_ecu_reset, mask_sub_11},
    {0x14, 4, UDS_SESSION_ALL, 0, uds_internal_handle_clear_dtc, NULL},
    {0x19, 2, UDS_SESSION_ALL, 0, uds_internal_handle_read_dtc_info, mask_sub_19}, /* Sub handled in handler due to complex payload */
    {0x22, 3, UDS_SESSION_ALL, 0, uds_internal_handle_read_data_by_id, NULL},
    {0x23, 3, UDS_SESSION_ALL, 0, uds_internal_handle_read_memory_by_addr, NULL},
    {0x27, 2, UDS_SESSION_ALL, 0, uds_internal_handle_security_access, mask_sub_27},
    {0x28, 2, UDS_SESSION_ALL, 0, uds_internal_handle_comm_control, mask_sub_28},
    {0x29, 2, UDS_SESSION_ALL, 0, uds_internal_handle_authentication, NULL},
    {0x2E, 3, UDS_SESSION_ALL, 0, uds_internal_handle_write_data_by_id, NULL},
    {0x31, 4, UDS_SESSION_ALL, 0, uds_internal_handle_routine_control, mask_sub_31},
    {0x34, 4, UDS_SESSION_ALL, 0, uds_internal_handle_request_download, NULL},
    {0x36, 2, UDS_SESSION_ALL, 0, uds_internal_handle_transfer_data, NULL},
    {0x37, 1, UDS_SESSION_ALL, 0, uds_internal_handle_request_transfer_exit, NULL},
    {0x3D, 3, UDS_SESSION_ALL, 0, uds_internal_handle_write_memory_by_addr, NULL},
    {0x3E, 2, UDS_SESSION_ALL, 0, uds_internal_handle_tester_present, mask_sub_3E},
    {0x85, 2, UDS_SESSION_ALL, 0, uds_internal_handle_control_dtc_setting, mask_sub_85},
};

#define CORE_SERVICE_COUNT (sizeof(core_services) / sizeof(core_services[0]))

/* --- Internal Helpers --- */

void uds_internal_log(uds_ctx_t *ctx, uint8_t level, const char *msg)
{
    if (ctx && ctx->config && ctx->config->fn_log) {
        if (level <= ctx->config->log_level) {
            ctx->config->fn_log(level, msg);
        }
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

bool uds_internal_parse_addr_len(const uint8_t *data, uint16_t len, uint8_t format, uint32_t *addr, uint32_t *size)
{
    uint8_t addr_len = format & 0x0F;
    uint8_t size_len = (format >> 4) & 0x0F;

    if (len < (addr_len + size_len)) {
        return false;
    }

    *addr = 0;
    for (int i = 0; i < addr_len; i++) {
        *addr = (*addr << 8) | data[i];
    }

    *size = 0;
    for (int i = 0; i < size_len; i++) {
        *size = (*size << 8) | data[addr_len + i];
    }

    return true;
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

/* --- Validation Helpers --- */

static bool is_session_supported(uds_ctx_t *ctx, const uds_service_entry_t *service) {
    uint8_t sess_bit = get_session_bit(ctx->active_session);
    return (service->session_mask & sess_bit) != 0;
}

static bool is_subfunction_supported(const uds_service_entry_t *service, uint8_t sub) {
    if (!service->sub_mask) return true;
    return (service->sub_mask[sub >> 3] & (1 << (sub & 0x7))) != 0;
}

static int execute_handler(uds_ctx_t *ctx, const uds_service_entry_t *service, 
                          const uint8_t *data, uint16_t len) {
    int res = service->handler(ctx, data, len);
    if (res == UDS_PENDING) {
        uds_send_nrc(ctx, data[0], 0x78);
        ctx->p2_msg_pending = true;
        ctx->p2_star_active = true;
        ctx->p2_timer_start = ctx->config->get_time_ms();
        ctx->pending_sid = data[0]; 
    }
    return res;
}

static void handle_request(uds_ctx_t *ctx, const uint8_t *data, uint16_t len) {
    uint8_t sid = data[0];
    const uds_service_entry_t *service = find_service(ctx, sid);

    if (!service) {
        uds_send_nrc(ctx, sid, 0x11); /* Service Not Supported */
        return;
    }

    /* ISO 14229-1 Priority: Session -> Subfunction -> Length -> Security -> Safety */

    if (!is_session_supported(ctx, service)) {
        uds_send_nrc(ctx, sid, 0x7F); /* Service Not Supported In Active Session */
        return;
    }

    bool has_sub = (service->sub_mask != NULL);
    uint8_t sub = (len >= 2) ? (data[1] & 0x7F) : 0;

    if (has_sub) {
        if (len < 2) {
            uds_send_nrc(ctx, sid, 0x13); /* Length error for subfunction services */
            return;
        }
        if (!is_subfunction_supported(service, sub)) {
            uds_send_nrc(ctx, sid, 0x12); /* Subfunction Not Supported */
            return;
        }
        ctx->suppress_pos_resp = (data[1] & 0x80) != 0;
    }

    if (len < service->min_len) {
        uds_send_nrc(ctx, sid, 0x13); /* Incorrect Message Length Or Invalid Format */
        return;
    }

    if (service->security_mask > ctx->security_level) {
        uds_send_nrc(ctx, sid, 0x33); /* Security Access Denied */
        return;
    }

    if (ctx->config->fn_is_safe && !ctx->config->fn_is_safe(ctx, sid, data, len)) {
        uds_send_nrc(ctx, sid, 0x22); /* Conditions Not Correct */
        return;
    }

    execute_handler(ctx, service, data, len);
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
    ctx->suppress_pos_resp = false;

    /* Enforce Timing Safety (ISO 14229-1 requires reasonable timeouts) */
    ctx->p2_ms = (config->p2_ms > 0) ? config->p2_ms : 50;
    ctx->p2_star_ms = (config->p2_star_ms > 0) ? config->p2_star_ms : 5000;

    if (config->strict_compliance) {
        if (ctx->p2_ms < 20) ctx->p2_ms = 20;
        if (ctx->p2_star_ms < 1000) ctx->p2_star_ms = 1000;
        uds_internal_log(ctx, UDS_LOG_INFO, "Strict Compliance: Enforcing minimum P2/P2* durations");
    }

    uds_internal_log(ctx, UDS_LOG_INFO, "UDS Stack Initialized");

    /* NVM Persistence: Load State */
    if (config->fn_nvm_load) {
        uint8_t state[2] = {0};
        if (config->fn_nvm_load(ctx, state, 2) == 2) {
            ctx->active_session = state[0];
            ctx->security_level = state[1];
            uds_internal_log(ctx, UDS_LOG_INFO, "NVM State Loaded");
        }
    }

    return UDS_OK;
}

void uds_process(uds_ctx_t *ctx)
{
    if (!ctx || !ctx->config) {
        return;
    }


    if (ctx->config->fn_mutex_lock) {
        ctx->config->fn_mutex_lock(ctx->config->mutex_handle);
    }

    if (ctx->p2_msg_pending) {
        /* If we are waiting for the app to finish a routine, do nothing in tick */
        /* But we still need to check for timeouts if we were doing the timing... 
           Actually, if p2_msg_pending is true, it means we sent 0x78.
           We are waiting for the app to call a "job done" function or simple update?
           For now, assume app handles logic. */
    }
    
    /* ... existing timer logic ... */ 
    /* Simplified for this diff, just wrapping the function logic effectively */
    /* Implementation detail: we need to careful not to hold lock during callbacks if callbacks re-enter? 
       Core LibUDS is usually single threaded logic, so lock protects THE CONTEXT from being accessed by 
       uds_process (timer task) and uds_input_sdu (CAN RX ISR) at the same time. */

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
        uint32_t limit = ctx->p2_star_active ? ctx->p2_star_ms : ctx->p2_ms;

        if (elapsed >= limit) {
            /* Send NRC 0x78 (Response Pending) */
            uds_send_nrc(ctx, ctx->pending_sid, 0x78);
            ctx->p2_star_active = true;
            ctx->p2_timer_start = now; /* Reset timer for P2* */
        }
    }

    if (ctx->config->fn_mutex_unlock) {
        ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
    }
}

int uds_client_request(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb callback)
{
    if (!ctx || !ctx->config || !ctx->config->tx_buffer) {
        return UDS_ERR_NOT_INIT;
    }

    if (len > 0 && !data) {
        return UDS_ERR_INVALID_ARG;
    }

    if (len + 1 > ctx->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    if (ctx->config->fn_mutex_lock) {
        ctx->config->fn_mutex_lock(ctx->config->mutex_handle);
    }

    ctx->pending_sid = sid;
    ctx->client_cb = (void *)callback;

    ctx->config->tx_buffer[0] = sid;
    if (data && len > 0) {
        memcpy(&ctx->config->tx_buffer[1], data, len);
    }

    int result = ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len + 1);

    if (ctx->config->fn_mutex_unlock) {
        ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
    }

    return result;
}

void uds_input_sdu(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (ctx->config->fn_mutex_lock) {
        ctx->config->fn_mutex_lock(ctx->config->mutex_handle);
    }

    if (!ctx || !data || len == 0) {
        if (ctx->config->fn_mutex_unlock) ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
        return;
    }

    uint8_t sid = data[0];
    ctx->last_msg_time = ctx->config->get_time_ms();

    /* 1. Concurrent Request Check (Busy) */
    if (ctx->p2_msg_pending) {
        if (sid == 0x3E && len >= 2 && (data[1] & 0x80)) {
            /* Suppressed TesterPresent: Just update S3, don't interrupt */
            if (ctx->config->fn_mutex_unlock) ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
            return;
        }
        uds_send_nrc(ctx, sid, 0x21); /* Busy Repeat Request */
        if (ctx->config->fn_mutex_unlock) ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
        return;
    }

    /* 2. Response to our previous request? (Client Mode) */
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
            if (ctx->config->fn_mutex_unlock) ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
            return;
        }
    }

    /* 3. Start Timing & Dispatch */
    ctx->p2_timer_start = ctx->config->get_time_ms();
    ctx->p2_msg_pending = false;
    ctx->p2_star_active = false;

    handle_request(ctx, data, len);

    if (ctx->config->fn_mutex_unlock) {
        ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
    }
}

int uds_send_response(uds_ctx_t *ctx, uint16_t len)
{
    if (!ctx || !ctx->config || !ctx->config->tx_buffer) {
        return UDS_ERR_NOT_INIT;
    }

    if (len > ctx->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    ctx->p2_msg_pending = false;
    
    if (ctx->suppress_pos_resp) {
        ctx->suppress_pos_resp = false;
        return UDS_OK;
    }

    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len);
}

int uds_send_nrc(uds_ctx_t *ctx, uint8_t sid, uint8_t nrc)
{
    if (!ctx || !ctx->config || !ctx->config->tx_buffer) {
        return UDS_ERR_NOT_INIT;
    }

    if (ctx->config->tx_buffer_size < 3) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    /* NRC 0x78 does not clear the pending flag. 
       Others only clear if they refer to the actual pending SID. */
    if (nrc != 0x78 && sid == ctx->pending_sid) {
        ctx->p2_msg_pending = false;
    }

    /* NRCs are NEVER suppressed by bit 7 */
    ctx->config->tx_buffer[0] = 0x7F;
    ctx->config->tx_buffer[1] = sid;
    ctx->config->tx_buffer[2] = nrc;

    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 3);
}
