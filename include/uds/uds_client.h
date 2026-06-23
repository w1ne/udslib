/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef UDS_CLIENT_H
#define UDS_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "uds/uds_config.h"

struct uds_client_ctx;

/* Fired when the response to a uds_client_request() arrives.
 * data/len are the response payload AFTER the SID byte. */
typedef void (*uds_response_cb)(struct uds_client_ctx *c, uint8_t sid, const uint8_t *data,
                                uint16_t len);

/* UDS client role: one outstanding request + completion callback. Reuses a
 * uds_config_t for transport only (tx_buffer, fn_tp_send, mutex); the server
 * hook fields are unused. Independent of the server uds_ctx_t. */
typedef struct uds_client_ctx
{
    const uds_config_t *config; /* transport binding (server hooks unused) */
    uint8_t pending_sid;        /* SID awaiting a response (0 = none) */
    uds_response_cb cb;         /* fired on the matching response */
} uds_client_ctx_t;

/* Build {sid, data...} in config->tx_buffer and send via config->fn_tp_send
 * (called with ctx == NULL), then arm cb. Returns the transport result or a
 * negative UDS_ERR_*. */
int uds_client_request(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb cb);

/* Feed an incoming frame. If it is the response to the outstanding request
 * (positive sid == pending|0x40, or a 0x7F negative response echoing pending),
 * fire cb with the payload after the SID, clear pending, and return true.
 * Otherwise return false (caller routes the frame elsewhere). */
bool uds_client_handle_response(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data,
                                uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CLIENT_H */
