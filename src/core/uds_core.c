/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

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
static const uint8_t mask_sub_29[] = UDS_MASK_SUB_29;
static const uint8_t mask_sub_31[] = UDS_MASK_SUB_31;
static const uint8_t mask_sub_3E[] = UDS_MASK_SUB_3E;
static const uint8_t mask_sub_85[] = UDS_MASK_SUB_85;
static const uint8_t mask_sub_2A[] = UDS_MASK_SUB_2A;
static const uint8_t mask_sub_2C[] = UDS_MASK_SUB_2C;
#if (UDS_ROE_MAX_EVENTS > 0)
static const uint8_t mask_sub_86[] = UDS_MASK_SUB_86;
#endif
static const uint8_t mask_sub_87[] = UDS_MASK_SUB_87;
static const uint8_t mask_sub_83[] = UDS_MASK_SUB_83;

/* Service dispatch table. Columns mirror uds_service_entry_t:
 *   { SID, min_len, session_mask, security_mask, handler, sub_mask, address_mode }
 *     SID          - service identifier (UDS_SID_*)
 *     min_len      - shortest accepted request (incl. SID); shorter -> NRC 0x13
 *     session_mask - sessions the service is allowed in (UDS_SESSION_*)
 *     security_mask- required security level (0 = none)
 *     handler      - service implementation
 *     sub_mask     - allowed sub-functions bitmap, or NULL if none
 *     address_mode - allowed addressing (UDS_ADDR_* bitmask); 0u = both
 * Applications add or override entries via config.user_services without
 * editing this table (see examples/custom_service). */
static const uds_service_entry_t core_services[] = {
    {UDS_SID_SESSION_CONTROL, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_session_control,
     mask_sub_10, 0u},
    {UDS_SID_ECU_RESET, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_ecu_reset, mask_sub_11, 0u},
    {UDS_SID_CLEAR_DTC, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_clear_dtc, NULL, 0u},
    {UDS_SID_READ_DTC_INFO, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_read_dtc_info, mask_sub_19,
     0u},
    {UDS_SID_READ_DATA_BY_ID, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_read_data_by_id, NULL,
     0u},
    {UDS_SID_READ_MEM_BY_ADDR, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_read_memory_by_addr,
     NULL, 0u},
    {UDS_SID_READ_SCALING, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_read_scaling, NULL, 0u},
    {UDS_SID_DYNAMIC_DID, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_dynamic_did, mask_sub_2C,
     0u},
    {UDS_SID_SECURITY_ACCESS, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_security_access,
     mask_sub_27, 0u},
    {UDS_SID_COMM_CONTROL, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_comm_control, mask_sub_28,
     0u},
    {UDS_SID_AUTHENTICATION, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_authentication,
     mask_sub_29, 0u},
    {UDS_SID_WRITE_DATA_BY_ID, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_write_data_by_id, NULL,
     0u},
    {UDS_SID_ROUTINE_CONTROL, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_routine_control,
     mask_sub_31, 0u},
    {UDS_SID_REQUEST_DOWNLOAD, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_request_download, NULL,
     0u},
    {UDS_SID_TRANSFER_DATA, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_transfer_data, NULL, 0u},
    {UDS_SID_TRANSFER_EXIT, 1u, UDS_SESSION_ALL, 0u, uds_internal_handle_request_transfer_exit,
     NULL, 0u},
    {UDS_SID_REQUEST_FILE_TRANSFER, 4u, UDS_SESSION_ALL, 0u,
     uds_internal_handle_request_file_transfer, NULL, 0u},
    {UDS_SID_WRITE_MEM_BY_ADDR, 3u, UDS_SESSION_ALL, 0u, uds_internal_handle_write_memory_by_addr,
     NULL, 0u},
    {UDS_SID_TESTER_PRESENT, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_tester_present,
     mask_sub_3E, 0u},
    {UDS_SID_CONTROL_DTC_SETTING, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_control_dtc_setting,
     mask_sub_85, 0u},
    {UDS_SID_READ_BY_PER_ID, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_periodic_read,
     mask_sub_2A, 0u},
    {UDS_SID_IO_CONTROL_BY_ID, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_io_control, NULL, 0u},
    {UDS_SID_REQUEST_UPLOAD, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_request_upload, NULL, 0u},
    {UDS_SID_LINK_CONTROL, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_link_control, mask_sub_87,
     0u},
    {UDS_SID_ACCESS_TIMING, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_access_timing, mask_sub_83,
     0u},
    {UDS_SID_SECURED_DATA_TRANS, 4u, UDS_SESSION_ALL, 0u, uds_internal_handle_secured_data, NULL,
     0u},
#if (UDS_ROE_MAX_EVENTS > 0)
    {UDS_SID_RESPONSE_ON_EVENT, 2u, UDS_SESSION_ALL, 0u, uds_internal_handle_response_on_event,
     mask_sub_86, 0u},
#endif
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

uint8_t uds_internal_session_bit(uint8_t session)
{
    switch (session) {
        case UDS_SESSION_ID_DEFAULT:
            return UDS_SESSION_DEFAULT;
        case UDS_SESSION_ID_PROGRAMMING:
            return UDS_SESSION_PROGRAMMING;
        case UDS_SESSION_ID_EXTENDED:
            return UDS_SESSION_EXTENDED;
        case UDS_SESSION_ID_SAFETY:
            return UDS_SESSION_SAFETY;
        default:
            return (uint8_t) 0u;
    }
}

/* Built-in ISO-sensible session policy applied when config.restrict_sessions
   is set (and only to services left fully permissive). */
uint8_t uds_internal_strict_session_mask(uint8_t sid)
{
    switch (sid) {
        /* Reprogramming services: programming session only. */
        case UDS_SID_REQUEST_DOWNLOAD:
        case UDS_SID_REQUEST_UPLOAD:
        case UDS_SID_TRANSFER_DATA:
        case UDS_SID_TRANSFER_EXIT:
        case UDS_SID_LINK_CONTROL:
        case UDS_SID_WRITE_MEM_BY_ADDR:
            return (uint8_t) UDS_SESSION_PROGRAMMING;

        /* Other privileged services: extended or programming (not default). */
        case UDS_SID_SECURITY_ACCESS:
        case UDS_SID_ECU_RESET:
        case UDS_SID_COMM_CONTROL:
        case UDS_SID_AUTHENTICATION:
        case UDS_SID_WRITE_DATA_BY_ID:
        case UDS_SID_IO_CONTROL_BY_ID:
        case UDS_SID_ROUTINE_CONTROL:
        case UDS_SID_ACCESS_TIMING:
        case UDS_SID_CONTROL_DTC_SETTING:
        case UDS_SID_READ_MEM_BY_ADDR:
            return (uint8_t) (UDS_SESSION_EXTENDED | UDS_SESSION_PROGRAMMING);

        default:
            return (uint8_t) UDS_SESSION_ALL;
    }
}

/* --- Validation Helpers --- */

static bool is_session_supported(const uds_ctx_t *ctx, const uds_service_entry_t *service)
{
    uint8_t mask = service->session_mask;
    if (ctx->config->restrict_sessions && (mask == (uint8_t) UDS_SESSION_ALL)) {
        mask = uds_internal_strict_session_mask(service->sid);
    }
    uint8_t sess_bit = uds_internal_session_bit(ctx->session.active);
    if (ctx->scratch.in_secured_session) {
        sess_bit |= (uint8_t) UDS_SESSION_SECURED;
    }
    return ((uint16_t) mask & (uint16_t) sess_bit) != 0u;
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

static void execute_handler(uds_ctx_t *ctx, const uds_service_entry_t *service, const uint8_t *data,
                            uint16_t len)
{
    uds_result_t r;
    /* Fail closed: if a handler returns without describing a result, the request
     * is rejected (generalReject) rather than emitting whatever happens to be in
     * tx_buffer. Every conforming handler overwrites r.kind via uds_ok/uds_nrc/
     * uds_pending/uds_none, so this default is never the wire result for them. */
    r.kind = UDS_RESULT_NRC;
    r.len = 0u;
    r.nrc = UDS_NRC_GENERAL_REJECT;
    service->handler(ctx, data, len, &r);

    /* True unless a positive response was attempted and the transport rejected
     * it. Gates the deferred reset: if the tester never received the
     * confirmation, the ECU must not reboot (ISO 14229-1; develop #85). */
    bool emit_ok = true;

    switch (r.kind) {
        case UDS_RESULT_PENDING:
            uds_send_nrc(ctx, data[0], UDS_NRC_RESPONSE_PENDING);
            ctx->server.p2_msg_pending = true;
            ctx->server.p2_star_active = true;
            ctx->server.p2_timer_start = ctx->config->get_time_ms();
            ctx->server.pending_sid = data[0];
            break;
        case UDS_RESULT_NRC:
            uds_send_nrc(ctx, data[0], r.nrc); /* NRC never suppressed (ISO) */
            break;
        case UDS_RESULT_NONE:
            break; /* emit nothing — e.g. 0x84 when inner response was suppressed */
        case UDS_RESULT_POSITIVE:
            if (ctx->scratch.suppress_pos_resp) {
                ctx->scratch.suppress_pos_resp = false;
                ctx->server.rcrrp_count = 0u;
                ctx->server.p2_msg_pending = false;
                ctx->server.pending_sid = 0u;
                if (ctx->scratch.secure_capturing) {
                    ctx->scratch.secure_capture_len = 0u;
                }
            }
            else {
                int emit_ret = uds_emit_response(ctx, r.len);
                if (emit_ret == UDS_ERR_BUFFER_TOO_SMALL) {
                    uint8_t resp_sid = ctx->config->tx_buffer[0];
                    uint8_t req_sid = (resp_sid >= UDS_RESPONSE_OFFSET)
                                          ? (uint8_t) (resp_sid - UDS_RESPONSE_OFFSET)
                                          : resp_sid;
                    uds_send_nrc(ctx, req_sid, UDS_NRC_RESPONSE_TOO_LONG);
                    emit_ok = false; /* tester got an NRC, not the positive response */
                }
                else if (emit_ret != UDS_OK) {
                    emit_ok = false; /* transport rejected the response */
                }
            }
            break;
        default:
            /* Out-of-range kind (cannot occur via the helpers). Fail closed:
             * MISRA-16.4 default that rejects rather than emitting silently. */
            uds_send_nrc(ctx, data[0], UDS_NRC_GENERAL_REJECT);
            break;
    }

    /* Deferred reset runs only after the response is on the wire, and never
     * during a captured (0x84/0x86 inner) dispatch: a 0x11 nested in a 0x84 must
     * not reboot before the OUTER secured response is emitted, and the outer
     * handler may cancel the reset if it fails to emit (NRC). While capturing,
     * leave reset_pending set for the outer dispatch's execute_handler to run. */
    if (!ctx->scratch.secure_capturing && ctx->scratch.reset_pending) {
        ctx->scratch.reset_pending = false;
        /* Only reset if the response actually reached the transport: a tester
         * left without its confirmation must not be desynchronised by a reboot. */
        if (emit_ok && ctx->config->fn_reset != NULL) {
            ctx->config->fn_reset(ctx, ctx->scratch.reset_pending_type);
        }
    }
}

static void handle_request(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sid = data[0];
    const uds_service_entry_t *service = find_service(ctx, sid);

    /* suppressPosRsp is scoped to the request being dispatched: clear it up
     * front so a previous request can never leak it into this one. It is only
     * cleared inside uds_send_response(), so a handler that suppresses its
     * response and returns without sending (e.g. ECU Reset 0x11 with the
     * suppress bit) would otherwise leave the flag set and silently swallow the
     * next service's response (issue #80). Sub-function services re-arm it below
     * from the request's own suppress bit. */
    ctx->scratch.suppress_pos_resp = false;

    if (!service) {
        uds_send_nrc(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED); /* Service Not Supported */
        return;
    }

    /* Addressing gate: does this service accept the request's addressing mode?
       address_mode == 0 means "both" (backward compatible). */
    uint8_t allowed_addr = (service->address_mode != 0u)
                               ? service->address_mode
                               : (uint8_t) (UDS_ADDR_PHYSICAL | UDS_ADDR_FUNCTIONAL);
    if ((allowed_addr & ctx->scratch.req_addr_mode) == 0u) {
        if (ctx->scratch.req_addr_mode == (uint8_t) UDS_ADDR_FUNCTIONAL) {
            return; /* functional broadcast for an unsupported addressing: stay silent */
        }
        uds_send_nrc(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
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
        ctx->scratch.suppress_pos_resp = (data[1] & UDS_MASK_SUPPRESS_POS_RESP) != 0u;
    }

    if (len < service->min_len) {
        uds_send_nrc(ctx, sid,
                     UDS_NRC_INCORRECT_LENGTH); /* Incorrect Message Length Or Invalid Format */
        return;
    }

    if (service->security_mask > ctx->security.level) {
        uds_send_nrc(ctx, sid, UDS_NRC_SECURITY_ACCESS_DENIED); /* Security Access Denied */
        return;
    }

    if (ctx->config->fn_auth_required && ctx->config->fn_auth_required(ctx, sid) &&
        !ctx->security.authenticated) {
        uds_send_nrc(ctx, sid, UDS_NRC_AUTHENTICATION_REQUIRED); /* 0x34 */
        return;
    }

    if (ctx->config->fn_is_safe && !ctx->config->fn_is_safe(ctx, sid, data, len)) {
        uds_send_nrc(ctx, sid, UDS_NRC_CONDITIONS_NOT_CORRECT); /* Conditions Not Correct */
        return;
    }

    execute_handler(ctx, service, data, len);
}

/** Scratch sizing for the decoded inner request / captured inner response. */
#ifndef UDS_SECURE_SCRATCH
#define UDS_SECURE_SCRATCH 256u
#endif

void uds_internal_handle_secured_data(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                      uds_result_t *out)
{
    /* No crypto wired in -> the secured channel cannot be processed. */
    if ((ctx->config->fn_secure_decode == NULL) || (ctx->config->fn_secure_encode == NULL)) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    /* Request: 84 <APAR hi> <APAR lo> <secured payload...> (min_len 4 enforced). */
    uint16_t apar = (uint16_t) (((uint16_t) data[1] << 8) | (uint16_t) data[2]);
    const uint8_t *sec_in = &data[3];
    uint16_t sec_in_len = (uint16_t) (len - 3u);

    uint8_t inner[UDS_SECURE_SCRATCH];
    int inner_len = ctx->config->fn_secure_decode(ctx, apar, sec_in, sec_in_len, inner,
                                                  (uint16_t) sizeof(inner));
    if (inner_len < 0) {
        uds_nrc(out, (uint8_t) - (int32_t) inner_len);
        return;
    }
    if (inner_len < 1) {
        uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
        return;
    }
    /* No nesting: an inner SecuredDataTransmission is out of range. */
    if (inner[0] == UDS_SID_SECURED_DATA_TRANS) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    /* Dispatch the inner request with the secured-session gate granted,
     * capturing its response instead of sending it.
     * Inner dispatch must always be treated as physical (design spec §2): save and
     * force UDS_ADDR_PHYSICAL so the addressing gate does not read a stale
     * functional req_addr_mode from the preceding top-level request.
     *
     * KNOWN LIMITATION (Phase 2): the inner handler writes its response into the
     * shared config->tx_buffer; uds_emit_response copies it out to the stack
     * `captured` buffer below before the outer 0x84 reuses tx_buffer. This relies
     * on the single-threaded, non-reentrant dispatch contract — there is no
     * isolation if a handler were re-entered. Separating per-dispatch scratch from
     * the shared buffer is deferred to the Phase 2 context-regrouping work. */
    uint8_t captured[UDS_SECURE_SCRATCH];
    ctx->scratch.in_secured_session = true;
    ctx->scratch.secure_capturing = true;
    ctx->scratch.secure_capture_buf = captured;
    ctx->scratch.secure_capture_size = (uint16_t) sizeof(captured);
    ctx->scratch.secure_capture_len = 0u;
    ctx->scratch.secure_capture_overflow = false;

    uint8_t saved_addr_mode = ctx->scratch.req_addr_mode;
    ctx->scratch.req_addr_mode = (uint8_t) UDS_ADDR_PHYSICAL;
    handle_request(ctx, inner, (uint16_t) inner_len);
    ctx->scratch.req_addr_mode = saved_addr_mode;

    ctx->scratch.in_secured_session = false;
    ctx->scratch.secure_capturing = false;

    /* Drop the reference to the (stack) capture buffer before it goes away. */
    bool overflow = ctx->scratch.secure_capture_overflow;
    uint16_t captured_len = ctx->scratch.secure_capture_len;
    ctx->scratch.secure_capture_buf = NULL;
    ctx->scratch.secure_capture_size = 0u;
    ctx->scratch.secure_capture_overflow = false;

    /* Inner response overflowed the scratch buffer -> cannot encode safely.
     * If the inner request was a 0x11, the tester gets an NRC, not a
     * confirmation, so the deferred reset must be cancelled (no reboot). */
    if (overflow) {
        ctx->scratch.reset_pending = false;
        uds_nrc(out, UDS_NRC_RESPONSE_TOO_LONG);
        return;
    }

    /* Inner response suppressed -> nothing to secure or send. A nested 0x11's
     * reset_pending is intentionally LEFT set here: this returns to the outer
     * execute_handler (0x84 is always dispatched via handle_request), which
     * fires the deferred reset after this (empty) outer result. */
    if (captured_len == 0u) {
        uds_none(out);
        return;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    uint16_t hdr = 3u;
    uint16_t out_max = (uint16_t) (ctx->config->tx_buffer_size - hdr);
    int sec_out =
        ctx->config->fn_secure_encode(ctx, apar, captured, captured_len, &tx[hdr], out_max);
    if (sec_out < 0) {
        /* Encode failed -> tester gets an NRC, not a confirmation: cancel any
         * pending reset from a nested 0x11 so the ECU does not reboot. */
        ctx->scratch.reset_pending = false;
        uds_nrc(out, (uint8_t) - (int32_t) sec_out);
        return;
    }

    tx[0] = (uint8_t) (UDS_SID_SECURED_DATA_TRANS + UDS_RESPONSE_OFFSET);
    tx[1] = (uint8_t) ((apar >> 8) & 0xFFu);
    tx[2] = (uint8_t) (apar & 0xFFu);
    /* Success: a nested 0x11's reset_pending is LEFT set; the outer
     * execute_handler fires it strictly after this secured response is emitted. */
    uds_ok(out, (uint16_t) ((uint16_t) sec_out + hdr));
}

int uds_internal_dispatch_captured(uds_ctx_t *ctx, const uint8_t *inner, uint16_t inner_len,
                                   uint8_t *out, uint16_t out_size)
{
    ctx->scratch.secure_capturing = true;
    ctx->scratch.secure_capture_buf = out;
    ctx->scratch.secure_capture_size = out_size;
    ctx->scratch.secure_capture_len = 0u;
    ctx->scratch.secure_capture_overflow = false; /* reset so any prior overflow cannot leak in */

    /* Inner/captured dispatch must always be treated as physical (design spec §2):
     * save and force UDS_ADDR_PHYSICAL so a stale functional req_addr_mode from
     * the preceding top-level request does not cause the addressing gate in
     * handle_request to silently drop the ROE/secured inner response. */
    uint8_t saved_addr_mode = ctx->scratch.req_addr_mode;
    ctx->scratch.req_addr_mode = (uint8_t) UDS_ADDR_PHYSICAL;
    handle_request(ctx, inner, inner_len);
    ctx->scratch.req_addr_mode = saved_addr_mode;

    ctx->scratch.secure_capturing = false;
    ctx->scratch.secure_capture_buf = NULL;
    ctx->scratch.secure_capture_size = 0u;

    /* A captured ROE (0x86) inner dispatch must never trigger an ECU reset: the
     * serviceToRespondTo runs asynchronously, with no tester transaction to
     * confirm. Cancel any reset a nested 0x11 may have armed (it was held while
     * capturing by the execute_handler guard). */
    ctx->scratch.reset_pending = false;

    /* Overflow: the inner response did not fit in the caller's buffer.
     * Return a negative sentinel so the caller can react explicitly rather than
     * treating a truncated/empty capture as a normal empty response. */
    if (ctx->scratch.secure_capture_overflow) {
        ctx->scratch.secure_capture_overflow = false;
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    return (int) ctx->scratch.secure_capture_len;
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
    ctx->session.active = UDS_SESSION_ID_DEFAULT; /* Default Session */
    ctx->security.level = 0u;                     /* Locked */
    ctx->session.comm_state = 0x00u;              /* Enable Rx/Tx */
    ctx->scratch.suppress_pos_resp = false;

    ctx->server.rcrrp_count = 0u;

    /* Enforce Timing Safety (ISO 14229-1 requires reasonable timeouts) */
    ctx->session.p2_ms = (config->p2_ms > 0u) ? config->p2_ms : 50u;
    ctx->session.p2_star_ms = (config->p2_star_ms > 0u) ? config->p2_star_ms : 5000u;

    if (config->strict_compliance) {
        if (ctx->session.p2_ms < UDS_P2_MIN_SAFE_MS) ctx->session.p2_ms = UDS_P2_MIN_SAFE_MS;
        if (ctx->session.p2_star_ms < UDS_P2_STAR_MIN_SAFE_MS)
            ctx->session.p2_star_ms = UDS_P2_STAR_MIN_SAFE_MS;
        uds_internal_log(ctx, UDS_LOG_INFO,
                         "Strict Compliance: Enforcing minimum P2/P2* durations");
    }

    uds_internal_log(ctx, UDS_LOG_INFO, "UDS Stack Initialized");

    /* NVM Persistence: Load State */
    if (config->fn_nvm_load) {
        uint8_t state[2] = {0};
        if (config->fn_nvm_load(ctx, state, 2u) == 2) {
            ctx->session.active = state[0];
            ctx->security.level = state[1];
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

    uint32_t now = ctx->config->get_time_ms();

    /* S3 Timer: Revert to Default Session if no activity */
    if (ctx->session.active != UDS_SESSION_ID_DEFAULT) {
        if ((now - ctx->session.last_msg_time) > UDS_S3_TIMEOUT_MS) {
            ctx->session.active = UDS_SESSION_ID_DEFAULT;
            ctx->security.level = 0u;
            ctx->security.authenticated = false;
            ctx->security.seed_level = 0u;
            ctx->security.seed_len = 0u;
            uds_internal_log(ctx, UDS_LOG_INFO, "S3 Timeout: Reverted to Default Session");
        }
    }

    /* P2/P2* Timing: Manage Response Deadlines */
    if (ctx->server.p2_msg_pending) {
        uint32_t elapsed = now - ctx->server.p2_timer_start;
        uint32_t limit = ctx->server.p2_star_active ? ctx->session.p2_star_ms : ctx->session.p2_ms;

        if (elapsed >= limit) {
            /* C-07: RCRRP Limit Check */
            if (ctx->config->rcrrp_limit > 0u &&
                ctx->server.rcrrp_count >= ctx->config->rcrrp_limit) {
                uds_send_nrc(ctx, ctx->server.pending_sid, UDS_NRC_CONDITIONS_NOT_CORRECT);
                ctx->server.rcrrp_count = 0u;
                if (ctx->config->fn_mutex_unlock) {
                    ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
                }
                return;
            }

            /* Send NRC 0x78 (Response Pending) */
            uds_send_nrc(ctx, ctx->server.pending_sid, UDS_NRC_RESPONSE_PENDING);
            ctx->server.rcrrp_count++;
            ctx->server.p2_star_active = true;
            ctx->server.p2_timer_start = now; /* Reset timer for P2* */
        }
    }

    /* SID 0x2A: Periodic Data Transmission Scheduler */
    if (ctx->server.periodic_count > 0u && ctx->config->fn_periodic_read != NULL) {
        for (uint8_t i = 0u; i < 8u; i++) {
            if (ctx->server.periodic_ids[i] != 0u) {
                /* Wrap-safe deadline check (signed delta), mirroring the S3/P2
                   timers; a plain >= breaks across the 32-bit ms rollover. */
                if ((int32_t) (now - ctx->server.periodic_timers[i]) >= 0) {
                    uint8_t out_buf[UDS_MAX_PERIODIC_MSG_LEN];
                    int written = ctx->config->fn_periodic_read(ctx, ctx->server.periodic_ids[i],
                                                                out_buf, UDS_MAX_PERIODIC_MSG_LEN);
                    if (written > 0) {
                        /* Send periodic message as a raw CAN/ISO-TP response if needed,
                           or via a specialized periodic tx hook. For now, use fn_tp_send. */
                        ctx->config->tx_buffer[0] = ctx->server.periodic_ids[i];
                        memcpy(&ctx->config->tx_buffer[1], out_buf, written);
                        ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, written + 1);
                    }

                    /* Reset timer based on rate: Fast (100ms), Medium (500ms), Slow (2000ms) */
                    uint32_t interval = UDS_PERIODIC_SLOW_INTERVAL_MS;
                    if (ctx->server.periodic_rates[i] == UDS_PERIODIC_RATE_FAST) {
                        interval = UDS_PERIODIC_FAST_INTERVAL_MS;
                    }
                    else if (ctx->server.periodic_rates[i] == UDS_PERIODIC_RATE_MEDIUM) {
                        interval = UDS_PERIODIC_MEDIUM_INTERVAL_MS;
                    }

                    ctx->server.periodic_timers[i] = now + interval;
                }
            }
        }
    }

#if (UDS_ROE_MAX_EVENTS > 0)
    /* SID 0x86: expire ResponseOnEvent windows. */
    uds_internal_roe_service(ctx, now);
#endif

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

    if (len + 1u > ctx->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    if (ctx->config->fn_mutex_lock != NULL) {
        ctx->config->fn_mutex_lock(ctx->config->mutex_handle);
    }

    ctx->client.pending_sid = sid;
    ctx->client.cb = (void *) callback;

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
    uds_input_sdu_addr(ctx, data, len, UDS_ADDR_PHYSICAL);
}

void uds_input_sdu_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_addr_mode_t addr)
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

    /* Defense-in-depth: a fresh top-level request starts with clean per-dispatch
     * scratch, so no stale flag (suppressPosRsp, reset_pending, capture state)
     * from a prior request can survive. NOT done in handle_request: the 0x84
     * inner dispatch runs there and must keep the outer's capture state; the
     * inner request's suppressPosRsp is still cleared per-dispatch there. */
    memset(&ctx->scratch, 0, sizeof ctx->scratch);
    ctx->scratch.req_addr_mode = (uint8_t) addr;

    uint8_t sid = data[0];
    ctx->session.last_msg_time = ctx->config->get_time_ms();

    /* 1. Concurrent Request Check (Busy) */
    if (ctx->server.p2_msg_pending) {
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
    if (ctx->client.pending_sid != 0u) {
        bool is_pos = (sid == (uint8_t) ((uint16_t) ctx->client.pending_sid | UDS_RESPONSE_OFFSET));
        bool is_neg = (sid == UDS_NRC_SERVICE_NOT_SUPP_IN_SESS && len >= 2u &&
                       data[1] == ctx->client.pending_sid);
        if (is_pos || is_neg) {
            if (ctx->client.cb != NULL) {
                uds_response_cb cb = (uds_response_cb) ctx->client.cb;
                cb(ctx, sid, &data[1], (uint16_t) (len - 1u));
                ctx->client.cb = NULL;
            }
            ctx->client.pending_sid = 0u;
            if (ctx->config->fn_mutex_unlock != NULL) {
                ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
            }
            return;
        }
    }

    /* 3. Start Timing & Dispatch */
    ctx->server.p2_timer_start = ctx->config->get_time_ms();
    ctx->server.p2_msg_pending = false;
    ctx->server.p2_star_active = false;
    ctx->server.rcrrp_count = 0u;

    handle_request(ctx, data, len);

    if (ctx->config->fn_mutex_unlock != NULL) {
        ctx->config->fn_mutex_unlock(ctx->config->mutex_handle);
    }
}

int uds_emit_response(uds_ctx_t *ctx, uint16_t len)
{
    if (!ctx || !ctx->config || !ctx->config->tx_buffer) {
        return UDS_ERR_NOT_INIT;
    }
    if (len > ctx->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }
    ctx->server.p2_msg_pending = false;
    ctx->server.pending_sid = 0u;

    if (ctx->scratch.secure_capturing) {
        if (len > ctx->scratch.secure_capture_size) {
            ctx->scratch.secure_capture_overflow = true;
            ctx->server.rcrrp_count = 0u;
            return UDS_OK;
        }
        memcpy(ctx->scratch.secure_capture_buf, ctx->config->tx_buffer, len);
        ctx->scratch.secure_capture_len = len;
        ctx->server.rcrrp_count = 0u;
        return UDS_OK;
    }
    ctx->server.rcrrp_count = 0u;
    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, len);
}

int uds_send_response(uds_ctx_t *ctx, uint16_t len) /* public compat shim */
{
    if (ctx && ctx->scratch.suppress_pos_resp) {
        ctx->scratch.suppress_pos_resp = false;
        ctx->server.rcrrp_count = 0u;
        if (ctx->scratch.secure_capturing) {
            ctx->scratch.secure_capture_len = 0u;
            ctx->scratch.secure_capture_overflow = false; /* stale overflow must not leak */
        }
        ctx->server.p2_msg_pending = false;
        ctx->server.pending_sid = 0u;
        return UDS_OK;
    }
    return uds_emit_response(ctx, len);
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
    if (nrc != UDS_NRC_RESPONSE_PENDING && sid == ctx->server.pending_sid) {
        ctx->server.p2_msg_pending = false;
    }

    /* ISO 14229-1: a functionally addressed request must not elicit these
       negative responses (avoid flooding a shared bus when many ECUs answer).
       Captured inner dispatches (SecuredDataTransmission / ResponseOnEvent) are
       never functional, hence the secure_capturing guard.
       INVARIANT: this suppress set must stay disjoint from any NRC that
       uds_process can emit on a deferred/pending path (responsePending and the
       post-RCRRP conditionsNotCorrect), since those run with a persisted
       req_addr_mode and must NOT be suppressed. */
    if (ctx->scratch.req_addr_mode == (uint8_t) UDS_ADDR_FUNCTIONAL &&
        !ctx->scratch.secure_capturing &&
        (nrc == UDS_NRC_SERVICE_NOT_SUPPORTED || nrc == UDS_NRC_SUBFUNCTION_NOT_SUPPORTED ||
         nrc == UDS_NRC_SUBFUNC_NOT_SUPP_IN_SESS || nrc == UDS_NRC_SERVICE_NOT_SUPP_IN_SESS ||
         nrc == UDS_NRC_REQUEST_OUT_OF_RANGE)) {
        return UDS_OK; /* suppressed: emit nothing on the bus */
    }

    /* NRCs are NEVER suppressed by bit 7 */
    ctx->config->tx_buffer[0] = UDS_NRC_SERVICE_NOT_SUPP_IN_SESS;
    ctx->config->tx_buffer[1] = sid;
    ctx->config->tx_buffer[2] = nrc;

    /* Capture an inner NRC so the 0x84 handler can secure it (see above). */
    if (ctx->scratch.secure_capturing) {
        memcpy(ctx->scratch.secure_capture_buf, ctx->config->tx_buffer, 3u);
        ctx->scratch.secure_capture_len = 3u;
        return UDS_OK;
    }

    return ctx->config->fn_tp_send(ctx, ctx->config->tx_buffer, 3u);
}
