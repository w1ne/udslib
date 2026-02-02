/**
 * @file uds_core.c
 * @brief Core UDS Logic Implementation
 */

#include <string.h>

#include "uds/uds_core.h"
#include "uds_internal.h"

/* --- Subfunction Masks --- */
static const uint8_t mask_sub_10[] = UDS_MASK_SUB_10;
static const uint8_t mask_sub_11[] = UDS_MASK_SUB_11;
static const uint8_t mask_sub_19[] = UDS_MASK_SUB_19;
static const uint8_t mask_sub_27[] = UDS_MASK_SUB_27;
static const uint8_t mask_sub_28[] = UDS_MASK_SUB_28;
static const uint8_t mask_sub_31[] = UDS_MASK_SUB_31;
static const uint8_t mask_sub_3E[] = UDS_MASK_SUB_3E;
static const uint8_t mask_sub_85[] = UDS_MASK_SUB_85;

static const uds_service_entry_t core_services[] = {
    {UDS_SID_SESSION_CONTROL, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_session_control,
     mask_sub_10},
    {UDS_SID_ECU_RESET, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_ecu_reset, mask_sub_11},
    {UDS_SID_CLEAR_DTC, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_clear_dtc, NULL},
    {UDS_SID_READ_DTC_INFO, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_read_dtc_info,
     mask_sub_19},
    {UDS_SID_READ_DATA_BY_ID, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_read_data_by_id, NULL},
    {UDS_SID_READ_MEM_BY_ADDR, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_read_memory_by_addr,
     NULL},
    {UDS_SID_SECURITY_ACCESS, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_security_access,
     mask_sub_27},
    {UDS_SID_COMM_CONTROL, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_comm_control, mask_sub_28},
    {UDS_SID_AUTHENTICATION, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_authentication, NULL},
    {UDS_SID_WRITE_DATA_BY_ID, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_write_data_by_id, NULL},
    {UDS_SID_ROUTINE_CONTROL, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_routine_control,
     mask_sub_31},
    {UDS_SID_REQUEST_DOWNLOAD, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_request_download, NULL},
    {UDS_SID_TRANSFER_DATA, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_transfer_data, NULL},
    {UDS_SID_TRANSFER_EXIT, 1u, UDS_SESSION_ALL, 0u, uds_internal_handle_request_transfer_exit,
     NULL},
    {UDS_SID_WRITE_MEM_BY_ADDR, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_write_memory_by_addr,
     NULL},
    {UDS_SID_TESTER_PRESENT, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_tester_present,
     mask_sub_3E},
    {UDS_SID_CONTROL_DTC_SETTING, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_control_dtc_setting,
     mask_sub_85},
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
    for (uint16_t i = 0u; i < table->count; i++) {
        if (table->entries[i].id == id) {
            return &table->entries[i];
        }
    }
    return NULL;
}

bool uds_internal_parse_addr_len(const uint8_t *data, uint16_t len, uint8_t format, uint32_t *addr,
                                 uint32_t *size)
{
    uint8_t addr_len = (uint8_t) (format & UDS_MASK_NIBBLE);
    uint8_t size_len = (uint8_t) ((format >> 4u) & UDS_MASK_NIBBLE);

    if ((addr_len == 0u) || (addr_len > 4u) || (size_len == 0u) || (size_len > 4u)) {
        return false;
    }

    if (len < (uint16_t) ((uint16_t) addr_len + (uint16_t) size_len)) {
        return false;
    }

    *addr = 0u;
    for (uint8_t i = 0u; i < addr_len; i++) {
        *addr = (uint32_t) ((uint32_t) *addr << 8u) | (uint32_t) data[i];
    }

    *size = 0u;
    for (uint8_t i = 0u; i < size_len; i++) {
        *size = (uint32_t) ((uint32_t) *size << 8u) |
                (uint32_t) data[(uint16_t) addr_len + (uint16_t) i];
    }

    return true;
}

static const uds_service_entry_t *find_service(uds_ctx_t *ctx, uint8_t sid)
{
    /* 1. Check User Services first (Override capability) */
    if (ctx->config->user_services != NULL) {
        for (uint16_t i = 0u; i < ctx->config->user_service_count; i++) {
            if (ctx->config->user_services[i].sid == sid) {
                return &ctx->config->user_services[i];
            }
        }
    }

    /* 2. Check Core Services */
    for (uint16_t i = 0u; i < (uint16_t) CORE_SERVICE_COUNT; i++) {
        if (core_services[i].sid == sid) {
            return &core_services[i];
        }
    }

    return NULL;
}

static uint8_t get_session_bit(uint8_t session)
{
    switch (session) {
        case UDS_SESSION_ID_DEFAULT:
            return UDS_SESSION_DEFAULT;
        case UDS_SESSION_ID_PROGRAMMING:
            return UDS_SESSION_PROGRAMMING;
        case UDS_SESSION_ID_EXTENDED:
            return UDS_SESSION_EXTENDED;
        default:
            return (uint8_t) 0u;
    }
}

/* --- Validation Helpers --- */

static bool is_session_supported(const uds_ctx_t *ctx, const uds_service_entry_t *service)
{
    uint8_t sess_bit = get_session_bit(ctx->active_session);
    return (service->session_mask & (uint16_t) sess_bit) != 0u;
}

static bool is_subfunction_supported(const uds_service_entry_t *service, uint8_t sub)
{
    if (service->sub_mask == NULL) {
        return true;
    }
    uint8_t index = (uint8_t) (sub >> 3u);
    uint8_t bit = (uint8_t) (1u << (sub & 0x7u));
    return (service->sub_mask[index] & bit) != 0u;
}

static int execute_handler(uds_ctx_t *ctx, const uds_service_entry_t *service, const uint8_t *data,
                           uint16_t len)
{
    int res = service->handler(ctx, data, len);
    if (res == UDS_PENDING) {
        uds_send_nrc(ctx, data[0], UDS_NRC_RESPONSE_PENDING);
        ctx->p2_msg_pending = true;
        ctx->p2_star_active = true;
        ctx->p2_timer_start = ctx->config->get_time_ms();
        ctx->pending_sid = data[0];
    }
    return res;
}

static void handle_request(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sid = data[0];
    const uds_service_entry_t *service = find_service(ctx, sid);

    if (!service) {
        uds_send_nrc(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED); /* Service Not Supported */
        return;
    }

    /* ISO 14229-1 Priority: Session -> Subfunction -> Length -> Security -> Safety */

    if (!is_session_supported(ctx, service)) {
        uds_send_nrc(
            ctx, sid,
            UDS_NRC_SERVICE_NOT_SUPP_IN_SESS); /* Service Not Supported In Active Session */
        return;
    }

    bool has_sub = (service->sub_mask != NULL);
    uint8_t sub = (len >= 2u) ? (uint8_t) (data[1] & UDS_MASK_SUBFUNCTION) : 0u;

    if (has_sub) {
        if (len < 2u) {
            uds_send_nrc(ctx, sid,
                         UDS_NRC_INCORRECT_LENGTH); /* Length error for subfunction services */
            return;
        }
        if (!is_subfunction_supported(service, sub)) {
            uds_send_nrc(ctx, sid,
                         UDS_NRC_SUBFUNCTION_NOT_SUPPORTED); /* Subfunction Not Supported */
            return;
        }
        ctx->suppress_pos_resp = (data[1] & UDS_MASK_SUPPRESS_POS_RESP) != 0u;
    }

    if (len < service->min_len) {
        uds_send_nrc(ctx, sid,
                     UDS_NRC_INCORRECT_LENGTH); /* Incorrect Message Length Or Invalid Format */
        return;
    }

    if (service->security_mask > ctx->security_level) {
        uds_send_nrc(ctx, sid, UDS_NRC_SECURITY_ACCESS_DENIED); /* Security Access Denied */
        return;
    }

    if (ctx->config->fn_is_safe && !ctx->config->fn_is_safe(ctx, sid, data, len)) {
        uds_send_nrc(ctx, sid, UDS_NRC_CONDITIONS_NOT_CORRECT); /* Conditions Not Correct */
        return;
    }

    execute_handler(ctx, service, data, len);
}

/* --- Public API --- */

// cppcheck-suppress unusedFunction
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
    ctx->active_session = UDS_SESSION_ID_DEFAULT; /* Default Session */
    ctx->security_level = 0u;                     /* Locked */
    ctx->comm_state = 0x00u;                      /* Enable Rx/Tx */
    ctx->suppress_pos_resp = false;

    ctx->rcrrp_count = 0u;

    /* Enforce Timing Safety (ISO 14229-1 requires reasonable timeouts) */
    ctx->p2_ms = (config->p2_ms > 0u) ? config->p2_ms : 50u;
    ctx->p2_star_ms = (config->p2_star_ms > 0u) ? config->p2_star_ms : 5000u;

    if (config->strict_compliance) {
        if (ctx->p2_ms < UDS_P2_MIN_SAFE_MS) ctx->p2_ms = UDS_P2_MIN_SAFE_MS;
        if (ctx->p2_star_ms < UDS_P2_STAR_MIN_SAFE_MS) ctx->p2_star_ms = UDS_P2_STAR_MIN_SAFE_MS;
        uds_internal_log(ctx, UDS_LOG_INFO,
                         "Strict Compliance: Enforcing minimum P2/P2* durations");
    }

    uds_internal_log(ctx, UDS_LOG_INFO, "UDS Stack Initialized");

    /* NVM Persistence: Load State */
    if (config->fn_nvm_load) {
        uint8_t state[2] = {0};
        if (config->fn_nvm_load(ctx, state, 2u) == 2) {
            ctx->active_session = state[0];
            ctx->security_level = state[1];
            uds_internal_log(ctx, UDS_LOG_INFO, "NVM State Loaded");
        }
    }

    return UDS_OK;
}

// cppcheck-suppress unusedFunction
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
    }

    uint32_t now = ctx->config->get_time_ms();

    /* S3 Timer: Revert to Default Session if no activity */
    if (ctx->active_session != UDS_SESSION_ID_DEFAULT) {
        if ((now - ctx->last_msg_time) > UDS_S3_TIMEOUT_MS) {
            ctx->active_session = UDS_SESSION_ID_DEFAULT;
            ctx->security_level = 0u;
            uds_internal_log(ctx, UDS_LOG_INFO, "S3 Timeout: Reverted to Default Session");
        }
    }

    /* P2/P2* Timing: Manage Response Deadlines */
    if (ctx->p2_msg_pending) {
        uint32_t elapsed = now - ctx->p2_timer_start;
        uint32_t limit = ctx->p2_star_active ? ctx->p2_star_ms : ctx->p2_ms;

        if (elapsed >= limit) {
            /* C-07: RCRRP Limit Check */
            if (ctx->config->rcrrp_limit > 0u && ctx->rcrrp_count >= ctx->config->rcrrp_limit) {
                uds_send_nrc(ctx, ctx->pending_sid, UDS_NRC_CONDITIONS_NOT_CORRECT);
                ctx->rcrrp_count = 0u;
                return;
            }

            /* Send NRC 0x78 (Response Pending) */
            uds_send_nrc(ctx, ctx->pending_sid, UDS_NRC_RESPONSE_PENDING);
            ctx->rcrrp_count++;
            ctx->p2_star_active = true;
            ctx->p2_timer_start = now; /* Reset timer for P2* */
        }
    }

    if (ctx->config->fn_mutex_unlock) {
        ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
    }
}

// cppcheck-suppress unusedFunction
int uds_client_request(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb callback)
{
    if (!ctx || !ctx->config || !ctx->config->tx_buffer) {
        return UDS_ERR_NOT_INIT;
    }

    if (len > 0 && !data) {
        return UDS_ERR_INVALID_ARG;
    }

    if (len + 1u > ctx->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    if (ctx->config->fn_mutex_lock != NULL) {
        ctx->config->fn_mutex_lock(ctx->config->mutex_handle);
    }

    ctx->pending_sid = sid;
    ctx->client_cb = (void *) callback;

    ctx->config->tx_buffer[0] = sid;
    if (data && len > 0u) {
        memcpy(&ctx->config->tx_buffer[1], data, len);
    }

    int result = ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, (uint16_t) (len + 1u));

    if (ctx->config->fn_mutex_unlock != NULL) {
        ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
    }

    return result;
}

void uds_input_sdu(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (!ctx || !ctx->config) {
        return;
    }

    if (ctx->config->fn_mutex_lock != NULL) {
        ctx->config->fn_mutex_lock(ctx->config->mutex_handle);
    }

    if (!data || len == 0u) {
        if (ctx->config->fn_mutex_unlock != NULL)
            ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
        return;
    }

    uint8_t sid = data[0];
    ctx->last_msg_time = ctx->config->get_time_ms();

    /* 1. Concurrent Request Check (Busy) */
    if (ctx->p2_msg_pending) {
        if (sid == UDS_SID_TESTER_PRESENT && len >= 2u && (data[1] & 0x80u)) {
            /* Suppressed TesterPresent: Just update S3, don't interrupt */
            if (ctx->config->fn_mutex_unlock != NULL) {
                ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
            }
            return;
        }
        uds_send_nrc(ctx, sid, UDS_NRC_BUSY_REPEAT_REQUEST); /* Busy Repeat Request */
        if (ctx->config->fn_mutex_unlock != NULL) {
            ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
        }
        return;
    }

    /* 2. Response to our previous request? (Client Mode) */
    if (ctx->pending_sid != 0u) {
        bool is_pos = (sid == (uint8_t) ((uint16_t) ctx->pending_sid | UDS_RESPONSE_OFFSET));
        bool is_neg =
            (sid == UDS_NRC_SERVICE_NOT_SUPP_IN_SESS && len >= 2u && data[1] == ctx->pending_sid);
        if (is_pos || is_neg) {
            if (ctx->client_cb != NULL) {
                uds_response_cb cb = (uds_response_cb) ctx->client_cb;
                cb(ctx, sid, &data[1], (uint16_t) (len - 1u));
                ctx->client_cb = NULL;
            }
            ctx->pending_sid = 0u;
            if (ctx->config->fn_mutex_unlock != NULL) {
                ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
            }
            return;
        }
    }

    /* 3. Start Timing & Dispatch */
    ctx->p2_timer_start = ctx->config->get_time_ms();
    ctx->p2_msg_pending = false;
    ctx->p2_star_active = false;
    ctx->rcrrp_count = 0u;

    handle_request(ctx, data, len);

    if (ctx->config->fn_mutex_unlock != NULL) {
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
        ctx->rcrrp_count = 0u;
        return UDS_OK;
    }

    ctx->rcrrp_count = 0u;
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len);
}

int uds_send_nrc(uds_ctx_t *ctx, uint8_t sid, uint8_t nrc)
{
    if (!ctx || !ctx->config || !ctx->config->tx_buffer) {
        return UDS_ERR_NOT_INIT;
    }

    if (ctx->config->tx_buffer_size < 3u) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    /* NRC 0x78 does not clear the pending flag.
       Others only clear if they refer to the actual pending SID. */
    if (nrc != UDS_NRC_RESPONSE_PENDING && sid == ctx->pending_sid) {
        ctx->p2_msg_pending = false;
    }

    /* NRCs are NEVER suppressed by bit 7 */
    ctx->config->tx_buffer[0] = UDS_NRC_SERVICE_NOT_SUPP_IN_SESS;
    ctx->config->tx_buffer[1] = sid;
    ctx->config->tx_buffer[2] = nrc;

    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 3u);
}
