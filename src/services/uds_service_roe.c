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
static void roe_setup(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data, uint16_t len,
                      uds_result_t *out)
{
    uint16_t str_off;
    uint32_t param;
    uint8_t cmp_op = 0u;

    if (sub == 0x03u) { /* onChangeOfDataIdentifier: window + DID(2) + STR */
        if (len < 6u) {
            uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
            return;
        }
        param = (uint32_t) (((uint32_t) data[3] << 8u) | (uint32_t) data[4]);
        str_off = 5u;
    }
    else if (sub == 0x07u) {
        /* onComparisonOfValues. NOTE: the ISO eventTypeRecord layout is
         * simplified here to <comparisonOperator(1)> <referenceValue(4)>; the
         * application reports the observed value via uds_roe_trigger(0x07, v).
         * Operators: 0x01 equal, 0x02 greater-than, 0x03 less-than. */
        if (len < 9u) {
            uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
            return;
        }
        cmp_op = data[3];
        param = (uint32_t) (((uint32_t) data[4] << 24) | ((uint32_t) data[5] << 16) |
                            ((uint32_t) data[6] << 8) | (uint32_t) data[7]);
        str_off = 8u;
    }
    else { /* 0x01 onDTCStatusChange / 0x02 onTimerInterrupt: window + 1 byte + STR */
        if (len < 5u) {
            uds_nrc(out, UDS_NRC_INCORRECT_LENGTH);
            return;
        }
        param = (uint32_t) data[3];
        str_off = 4u;
    }

    uint16_t str_len = (uint16_t) (len - str_off);
    if ((str_len == 0u) || (str_len > (uint16_t) UDS_ROE_STR_MAX)) {
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }
    /* No nesting: serviceToRespondTo may not be ROE or SecuredDataTransmission. */
    if ((data[str_off] == UDS_SID_RESPONSE_ON_EVENT) ||
        (data[str_off] == UDS_SID_SECURED_DATA_TRANS)) {
        uds_nrc(out, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
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
        uds_nrc(out, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    slot->in_use = true;
    slot->active = false;
    slot->event_type = sub;
    slot->event_param = param;
    slot->window_byte = data[2];
    slot->window_deadline = 0u;
    slot->next_fire = 0u; /* onTimerInterrupt: primed on first service tick */
    slot->cmp_op = cmp_op;
    slot->str_len = (uint8_t) str_len;
    memcpy(slot->str, &data[str_off], str_len);

    /* Positive response: C6 <sub> <count> <echo of request body>. */
    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) ROE_RESP_SID;
    tx[1] = sub;
    tx[2] = roe_count(ctx);
    uint16_t body = (uint16_t) (len - 2u);
    memcpy(&tx[3], &data[2], body);
    uds_ok(out, (uint16_t) (3u + body));
}

void uds_internal_handle_response_on_event(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                                           uds_result_t *out)
{
    uint8_t sub = (uint8_t) (data[1] & 0x7Fu);

    if ((sub == 0x01u) || (sub == 0x02u) || (sub == 0x03u) || (sub == 0x07u)) {
        roe_setup(ctx, sub, data, len, out);
        return;
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
        uds_ok(out, pos);
        return;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) ROE_RESP_SID;
    tx[1] = sub;
    tx[2] = roe_count(ctx);
    uds_ok(out, 3u);
}

/* Run the slot's stored serviceToRespondTo and emit its response as 0xC6. */
static void roe_emit_slot(uds_ctx_t *ctx, const uds_roe_slot_t *slot)
{
    uint8_t captured[UDS_ROE_STR_MAX + 8u];
    int cap = uds_internal_dispatch_captured(ctx, slot->str, slot->str_len, captured,
                                             (uint16_t) sizeof(captured));
    /* Negative return means the inner response overflowed the capture buffer.
     * ROE emits are asynchronous (no live request to answer with an NRC), so the
     * correct action is a safe silent drop — emitting a truncated or garbage 0xC6
     * frame would be worse than no frame at all.  Zero means empty capture (e.g.
     * suppressed positive response), which is also a no-op. */
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
        else if (event_type == 0x07u) {
            /* onComparisonOfValues: param is the observed value. */
            switch (slot->cmp_op) {
                case 0x02u:
                    match = (param > slot->event_param);
                    break;
                case 0x03u:
                    match = (param < slot->event_param);
                    break;
                default: /* 0x01 equal (and unknown ops) */
                    match = (param == slot->event_param);
                    break;
            }
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

#define ROE_BLOB_VERSION 0x01u
#define ROE_SLOT_HDR 7u /* event_type(1) + param(4) + window_byte(1) + str_len(1) */

int uds_roe_serialize(uds_ctx_t *ctx, uint8_t *buf, uint16_t max)
{
    if ((ctx == NULL) || (buf == NULL)) {
        return UDS_ERR_INVALID_ARG;
    }
    if (max < 2u) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    uint16_t pos = 2u;
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < (uint8_t) UDS_ROE_MAX_EVENTS; i++) {
        const uds_roe_slot_t *slot = &ctx->roe[i];
        if (!slot->in_use) {
            continue;
        }
        uint16_t need = (uint16_t) (ROE_SLOT_HDR + slot->str_len);
        if (((uint32_t) pos + (uint32_t) need) > (uint32_t) max) {
            return UDS_ERR_BUFFER_TOO_SMALL;
        }
        buf[pos] = slot->event_type;
        buf[pos + 1u] = (uint8_t) ((slot->event_param >> 24) & 0xFFu);
        buf[pos + 2u] = (uint8_t) ((slot->event_param >> 16) & 0xFFu);
        buf[pos + 3u] = (uint8_t) ((slot->event_param >> 8) & 0xFFu);
        buf[pos + 4u] = (uint8_t) (slot->event_param & 0xFFu);
        buf[pos + 5u] = slot->window_byte;
        buf[pos + 6u] = slot->str_len;
        memcpy(&buf[pos + ROE_SLOT_HDR], slot->str, slot->str_len);
        pos = (uint16_t) (pos + need);
        count++;
    }

    buf[0] = ROE_BLOB_VERSION;
    buf[1] = count;
    return (int) pos;
}

int uds_roe_deserialize(uds_ctx_t *ctx, const uint8_t *buf, uint16_t len)
{
    if ((ctx == NULL) || (buf == NULL)) {
        return UDS_ERR_INVALID_ARG;
    }
    if ((len < 2u) || (buf[0] != ROE_BLOB_VERSION)) {
        return UDS_ERR_INVALID_ARG;
    }

    uint8_t count = buf[1];
    uint16_t pos = 2u;
    uint8_t restored = 0u;

    for (uint8_t i = 0u; i < count; i++) {
        if (((uint32_t) pos + (uint32_t) ROE_SLOT_HDR) > (uint32_t) len) {
            return UDS_ERR_INVALID_ARG;
        }
        uint8_t event_type = buf[pos];
        uint32_t param =
            (uint32_t) (((uint32_t) buf[pos + 1u] << 24) | ((uint32_t) buf[pos + 2u] << 16) |
                        ((uint32_t) buf[pos + 3u] << 8) | (uint32_t) buf[pos + 4u]);
        uint8_t window_byte = buf[pos + 5u];
        uint8_t str_len = buf[pos + 6u];
        pos = (uint16_t) (pos + ROE_SLOT_HDR);

        if ((str_len == 0u) || (str_len > (uint8_t) UDS_ROE_STR_MAX) ||
            (((uint32_t) pos + (uint32_t) str_len) > (uint32_t) len)) {
            return UDS_ERR_INVALID_ARG;
        }

        /* Place into the next unused slot; stop if the table is full. */
        uds_roe_slot_t *slot = NULL;
        for (uint8_t j = 0u; j < (uint8_t) UDS_ROE_MAX_EVENTS; j++) {
            if (!ctx->roe[j].in_use) {
                slot = &ctx->roe[j];
                break;
            }
        }
        if (slot == NULL) {
            break;
        }

        slot->in_use = true;
        slot->active = false;
        slot->event_type = event_type;
        slot->event_param = param;
        slot->window_byte = window_byte;
        slot->window_deadline = 0u;
        slot->next_fire = 0u;
        slot->str_len = str_len;
        memcpy(slot->str, &buf[pos], str_len);
        pos = (uint16_t) (pos + str_len);
        restored++;
    }

    return (int) restored;
}

#else /* ROE compiled out */

int uds_roe_trigger(uds_ctx_t *ctx, uint8_t event_type, uint32_t param)
{
    (void) ctx;
    (void) event_type;
    (void) param;
    return 0;
}

int uds_roe_serialize(uds_ctx_t *ctx, uint8_t *buf, uint16_t max)
{
    (void) ctx;
    (void) buf;
    (void) max;
    return -1;
}

int uds_roe_deserialize(uds_ctx_t *ctx, const uint8_t *buf, uint16_t len)
{
    (void) ctx;
    (void) buf;
    (void) len;
    return -1;
}

#endif /* UDS_ROE_MAX_EVENTS > 0 */
