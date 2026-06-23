/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <string.h>

#include "uds/uds_client.h"
#include "uds/uds_core.h" /* UDS_ERR_*, UDS_OK */

#define UDS_CLIENT_RESPONSE_OFFSET 0x40u
#define UDS_CLIENT_NEG_RESPONSE_SID 0x7Fu

int uds_client_request(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb cb)
{
    if ((c == NULL) || (c->config == NULL) || (c->config->tx_buffer == NULL)) {
        return UDS_ERR_NOT_INIT;
    }
    if ((len > 0u) && (data == NULL)) {
        return UDS_ERR_INVALID_ARG;
    }
    if (((uint32_t) len + 1u) > c->config->tx_buffer_size) {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    if (c->config->fn_mutex_lock != NULL) {
        c->config->fn_mutex_lock(c->config->mutex_handle);
    }

    /* Build the request frame and arm the completion state under the lock. */
    c->pending_sid = sid;
    c->cb = cb;

    c->config->tx_buffer[0] = sid;
    if ((data != NULL) && (len > 0u)) {
        memcpy(&c->config->tx_buffer[1], data, len);
    }
    uint16_t frame_len = (uint16_t) (len + 1u);

    if (c->config->fn_mutex_unlock != NULL) {
        c->config->fn_mutex_unlock(c->config->mutex_handle);
    }

    /* Transmit with the lock released, mirroring the server emit path: a slow
     * fn_tp_send must not stall a concurrent uds_client_handle_response() (or any
     * other holder of the shared transport mutex). The frame stays in
     * tx_buffer — only one client request is outstanding at a time. */
    return c->config->fn_tp_send(NULL, c->config->tx_buffer, frame_len);
}

bool uds_client_handle_response(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len)
{
    if ((c == NULL) || (c->pending_sid == 0u)) {
        return false;
    }
    bool is_pos = (sid == (uint8_t) ((uint16_t) c->pending_sid | UDS_CLIENT_RESPONSE_OFFSET));
    bool is_neg =
        ((sid == UDS_CLIENT_NEG_RESPONSE_SID) && (len >= 2u) && (data[1] == c->pending_sid));
    if (!is_pos && !is_neg) {
        return false;
    }

    uds_response_cb cb = c->cb;
    c->cb = NULL;
    c->pending_sid = 0u; /* clear before firing so a re-entrant request is not clobbered */
    if (cb != NULL) {
        cb(c, sid, &data[1], (uint16_t) (len - 1u));
    }
    return true;
}
