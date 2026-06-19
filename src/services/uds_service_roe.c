/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_service_roe.c
 * @brief ResponseOnEvent (SID 0x86) — stateful autonomous event engine.
 */

#include "uds_internal.h"
#include <string.h>

#if (UDS_ROE_MAX_EVENTS > 0)

#define ROE_RESP_SID (UDS_SID_RESPONSE_ON_EVENT + UDS_RESPONSE_OFFSET) /* 0xC6 */
#define ROE_WINDOW_INFINITE 0x02u

/* Count currently-stored (set up) ROE events. */
static uint8_t roe_count(const uds_ctx_t *ctx)
{
    uint8_t n = 0u;
    for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
        if (ctx->roe[i].in_use) {
            n++;
        }
    }
    return n;
}

/* Common store path for setup sub-functions 0x01 / 0x03. */
static int roe_setup(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data, uint16_t len)
{
    uint16_t str_off;
    uint32_t param;

    if (sub == 0x03u) { /* onChangeOfDataIdentifier: window + DID(2) + STR */
        if (len < 6u) {
            return uds_send_nrc(ctx, UDS_SID_RESPONSE_ON_EVENT, UDS_NRC_INCORRECT_LENGTH);
        }
        param = (uint32_t) (((uint32_t) data[3] << 8u) | (uint32_t) data[4]);
        str_off = 5u;
    }
    else { /* 0x01 onDTCStatusChange: window + DTCStatusMask(1) + STR */
        if (len < 5u) {
            return uds_send_nrc(ctx, UDS_SID_RESPONSE_ON_EVENT, UDS_NRC_INCORRECT_LENGTH);
        }
        param = (uint32_t) data[3];
        str_off = 4u;
    }

    uint16_t str_len = (uint16_t) (len - str_off);
    if ((str_len == 0u) || (str_len > (uint16_t) UDS_ROE_STR_MAX)) {
        return uds_send_nrc(ctx, UDS_SID_RESPONSE_ON_EVENT, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }
    /* No nesting: serviceToRespondTo may not be ROE or SecuredDataTransmission. */
    if ((data[str_off] == UDS_SID_RESPONSE_ON_EVENT) ||
        (data[str_off] == UDS_SID_SECURED_DATA_TRANS)) {
        return uds_send_nrc(ctx, UDS_SID_RESPONSE_ON_EVENT, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    /* Claim an unused slot. */
    uds_roe_slot_t *slot = NULL;
    for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
        if (!ctx->roe[i].in_use) {
            slot = &ctx->roe[i];
            break;
        }
    }
    if (slot == NULL) {
        return uds_send_nrc(ctx, UDS_SID_RESPONSE_ON_EVENT, UDS_NRC_CONDITIONS_NOT_CORRECT);
    }

    slot->in_use = true;
    slot->active = false;
    slot->event_type = sub;
    slot->event_param = param;
    slot->window_byte = data[2];
    slot->window_deadline = 0u;
    slot->next_fire = 0u; /* onTimerInterrupt: primed on first service tick */
    slot->str_len = (uint8_t) str_len;
    memcpy(slot->str, &data[str_off], str_len);

    if (ctx->suppress_pos_resp) {
        return UDS_OK;
    }

    /* Positive response: C6 <sub> <count> <echo of request body>. */
    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) ROE_RESP_SID;
    tx[1] = sub;
    tx[2] = roe_count(ctx);
    uint16_t body = (uint16_t) (len - 2u);
    memcpy(&tx[3], &data[2], body);
    return uds_send_response(ctx, (uint16_t) (3u + body));
}

int uds_internal_handle_response_on_event(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint8_t sub = (uint8_t) (data[1] & 0x7Fu);

    if ((sub == 0x01u) || (sub == 0x02u) || (sub == 0x03u)) {
        return roe_setup(ctx, sub, data, len);
    }

    if (sub == 0x05u) { /* startResponseOnEvent */
        bool need_time = false;
        for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
            if (ctx->roe[i].in_use && (ctx->roe[i].window_byte != ROE_WINDOW_INFINITE)) {
                need_time = true;
            }
        }
        uint32_t now = need_time ? ctx->config->get_time_ms() : 0u;
        for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
            if (ctx->roe[i].in_use) {
                ctx->roe[i].active = true;
                ctx->roe[i].window_deadline = (ctx->roe[i].window_byte == ROE_WINDOW_INFINITE)
                                                  ? 0u
                                                  : (now + (uint32_t) UDS_ROE_WINDOW_MS);
            }
        }
    }
    else if (sub == 0x00u) { /* stopResponseOnEvent: deactivate, keep stored */
        for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
            ctx->roe[i].active = false;
        }
    }
    else if (sub == 0x06u) { /* clearResponseOnEvent */
        memset(ctx->roe, 0, sizeof(ctx->roe));
    }
    else { /* 0x04 reportActivatedEvents */
        if (ctx->suppress_pos_resp) {
            return UDS_OK;
        }
        uint8_t *tx = ctx->config->tx_buffer;
        tx[0] = (uint8_t) ROE_RESP_SID;
        tx[1] = sub;
        uint16_t pos = 3u;
        uint8_t active = 0u;
        for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
            if (ctx->roe[i].in_use && ctx->roe[i].active) {
                tx[pos] = ctx->roe[i].event_type;
                pos++;
                active++;
            }
        }
        tx[2] = active;
        return uds_send_response(ctx, pos);
    }

    if (ctx->suppress_pos_resp) {
        return UDS_OK;
    }
    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) ROE_RESP_SID;
    tx[1] = sub;
    tx[2] = roe_count(ctx);
    return uds_send_response(ctx, 3u);
}

/* Run the slot's stored serviceToRespondTo and emit its response as 0xC6. */
static void roe_emit_slot(uds_ctx_t *ctx, const uds_roe_slot_t *slot)
{
    uint8_t captured[UDS_ROE_STR_MAX + 8u];
    int cap = uds_internal_dispatch_captured(ctx, slot->str, slot->str_len, captured,
                                             (uint16_t) sizeof(captured));
    if (cap <= 0) {
        return;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) ROE_RESP_SID;
    tx[1] = slot->event_type;
    tx[2] = 0x01u; /* numberOfIdentifiedEvents */
    memcpy(&tx[3], captured, (size_t) cap);
    (void) ctx->config->fn_tp_send(ctx, tx, (uint16_t) (3 + cap));
}

/* onTimerInterrupt rate (eventTypeRecord byte) -> interval in ms. */
static uint32_t roe_rate_to_ms(uint32_t rate)
{
    if (rate == 0x03u) {
        return (uint32_t) UDS_PERIODIC_FAST_INTERVAL_MS;
    }
    if (rate == 0x02u) {
        return (uint32_t) UDS_PERIODIC_MEDIUM_INTERVAL_MS;
    }
    return (uint32_t) UDS_PERIODIC_SLOW_INTERVAL_MS; /* 0x01 slow / default */
}

void uds_internal_roe_service(uds_ctx_t *ctx, uint32_t now)
{
    for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
        uds_roe_slot_t *slot = &ctx->roe[i];
        if (!slot->in_use || !slot->active) {
            continue;
        }

        /* Event window expiry deactivates the slot. */
        if ((slot->window_deadline != 0u) && ((int32_t) (now - slot->window_deadline) >= 0)) {
            slot->active = false;
            continue;
        }

        /* onTimerInterrupt (0x02): emit periodically at the configured rate. */
        if (slot->event_type == 0x02u) {
            uint32_t period = roe_rate_to_ms(slot->event_param);
            if (slot->next_fire == 0u) {
                slot->next_fire = now + period; /* lazy prime on first tick */
            }
            else if ((int32_t) (now - slot->next_fire) >= 0) {
                roe_emit_slot(ctx, slot);
                slot->next_fire = now + period;
            }
        }
    }
}

int uds_roe_trigger(uds_ctx_t *ctx, uint8_t event_type, uint32_t param)
{
    if ((ctx == NULL) || (ctx->config == NULL)) {
        return UDS_ERR_NOT_INIT;
    }

    int emitted = 0;
    for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
        const uds_roe_slot_t *slot = &ctx->roe[i];
        if (!slot->in_use || !slot->active || (slot->event_type != event_type)) {
            continue;
        }

        bool match;
        if (event_type == 0x03u) {
            match = (slot->event_param == param);
        }
        else { /* 0x01 onDTCStatusChange: match if any masked status bit set */
            match = ((param & slot->event_param) != 0u);
        }
        if (!match) {
            continue;
        }

        roe_emit_slot(ctx, slot);
        emitted++;
    }
    return emitted;
}

#else /* ROE compiled out */

int uds_roe_trigger(uds_ctx_t *ctx, uint8_t event_type, uint32_t param)
{
    (void) ctx;
    (void) event_type;
    (void) param;
    return 0;
}

#endif /* UDS_ROE_MAX_EVENTS > 0 */
