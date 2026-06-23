/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <string.h>

#include "uds/uds_client.h"
#include "uds/uds_core.h" /* UDS_ERR_*, UDS_OK */

#define UDS_CLIENT_RESPONSE_OFFSET 0x40u
#define UDS_CLIENT_NEG_RESPONSE_SID 0x7Fu

/* Largest client request frame copied to a stack snapshot before releasing the
 * lock, so fn_tp_send can run outside the critical section without a concurrent
 * context tearing a shared tx_buffer (mirrors UDS_TX_FLUSH_SNAPSHOT_MAX on the
 * server emit path). A larger frame is sent while the lock is held. */
#ifndef UDS_CLIENT_TX_SNAPSHOT_MAX
#define UDS_CLIENT_TX_SNAPSHOT_MAX 512u
#endif

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

    /* Snapshot the frame before releasing the lock, mirroring the server emit path
     * (uds_internal_unlock_and_flush_x): if a concurrent context shares this
     * tx_buffer and rebuilds it after the unlock, the in-flight send must not tear.
     * A small frame is copied to a bounded stack snapshot and sent with the lock
     * released (the common case); an oversized frame (only with a large tx_buffer)
     * is sent WHILE THE LOCK IS HELD so a concurrent context cannot overwrite
     * tx_buffer mid-send. */
    int ret;
    if (frame_len <= (uint16_t) UDS_CLIENT_TX_SNAPSHOT_MAX) {
        uint8_t snapshot[UDS_CLIENT_TX_SNAPSHOT_MAX];
        memcpy(snapshot, c->config->tx_buffer, frame_len);
        if (c->config->fn_mutex_unlock != NULL) {
            c->config->fn_mutex_unlock(c->config->mutex_handle);
        }
        ret = c->config->fn_tp_send(NULL, snapshot, frame_len);
    }
    else {
        ret = c->config->fn_tp_send(NULL, c->config->tx_buffer, frame_len);
        if (c->config->fn_mutex_unlock != NULL) {
            c->config->fn_mutex_unlock(c->config->mutex_handle);
        }
    }
    return ret;
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
