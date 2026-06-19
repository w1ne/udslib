/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_core.h
 * @brief ISO-14229 UDS Protocol Stack Core API
 */

#ifndef UDS_CORE_H
#define UDS_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "uds_config.h"

/* --- Return Codes --- */

/** Successful operation */
#define UDS_OK 0

/** One or more arguments are invalid or NULL */
#define UDS_ERR_INVALID_ARG -1

/** The provided buffer is too small for the requested operation */
#define UDS_ERR_BUFFER_TOO_SMALL -2

/** The stack context has not been initialized */
#define UDS_ERR_NOT_INIT -3

/** The service operation is currently in progress (used for NRC 0x78) */
#define UDS_PENDING 1

/* --- Type Definitions --- */

/**
 * @brief ECU Reset Types (SID 0x11).
 */
typedef enum
{
    UDS_RESET_HARD = 0x01,       /**< Power cycle reset */
    UDS_RESET_SOFT = 0x02,       /**< Software-driven reset */
    UDS_RESET_KEY_OFF_ON = 0x03, /**< Simulated key cycle */
} uds_reset_type_t;

/**
 * @brief Communication Control Types (SID 0x28).
 */
typedef enum
{
    UDS_COMM_ENABLE_RX_TX = 0x00,         /**< Normal communication */
    UDS_COMM_ENABLE_RX_DISABLE_TX = 0x01, /**< Receive only */
    UDS_COMM_DISABLE_RX_ENABLE_TX = 0x02, /**< Transmit only */
    UDS_COMM_DISABLE_RX_TX = 0x03         /**< Total communication silence */
} uds_comm_control_type_t;

/**
 * @brief Authentication Sub-functions (SID 0x29).
 */
typedef enum
{
    UDS_AUTH_DEAUTHENTICATE = 0x00,
    UDS_AUTH_VERIFY_CERT_UNI = 0x01,
    UDS_AUTH_VERIFY_CERT_BI = 0x02,
    UDS_AUTH_PROOF_OF_OWNERSHIP = 0x03,
    UDS_AUTH_TRANSMIT_CERT = 0x04,
    UDS_AUTH_REQUEST_CHALLENGE = 0x05,
    UDS_AUTH_VERIFY_PROOF_UNI = 0x06,
    UDS_AUTH_VERIFY_PROOF_BI = 0x07,
    UDS_AUTH_CONFIGURATION = 0x08,
} uds_auth_type_t;

/**
 * @brief Client Response Callback
 *
 * @param ctx   Pointer to the UDS context.
 * @param sid   Service ID of the response message.
 * @param data  Pointer to the response payload data.
 * @param len   Length of the payload data in bytes.
 */
typedef void (*uds_response_cb)(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len);

/* --- Public API --- */

/**
 * @brief Initialize the UDS Stack.
 *
 * Validates the configuration and resets all internal state machines.
 *
 * @param ctx    Pointer to an allocated context structure.
 * @param config Pointer to a constant configuration structure.
 *               This pointer is stored in the context and must remain valid.
 * @return UDS_OK on success, or a negative error code (e.g., UDS_ERR_INVALID_ARG).
 */
int uds_init(uds_ctx_t *ctx, const uds_config_t *config);

/**
 * @brief Process the UDS Stack.
 *
 * Handles periodic tasks such as session timeouts (S3), P2/P2* deadlines,
 * and asynchronous status monitoring. Should be called at a fixed interval (e.g., 1ms).
 *
 * @param ctx Pointer to the initialized context.
 */
void uds_process(uds_ctx_t *ctx);

/**
 * @brief Input a UDS SDU (Service Data Unit).
 *
 * Feeds a fully assembled UDS message into the stack. This is the entry point
 * for incoming CAN/ISO-TP messages.
 *
 * @param ctx  Pointer to the initialized context.
 * @param data Pointer to the buffer containing the SDU.
 * @param len  Length of the data in bytes.
 */
void uds_input_sdu(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/**
 * @brief Notify the stack that a monitored event occurred (ResponseOnEvent,
 *        SID 0x86).
 *
 * The application calls this when a real-world change happens. For each active
 * ROE definition matching @p event_type and @p param, the library runs the
 * stored serviceToRespondTo and emits its response as a 0xC6 message.
 *
 * @param ctx        Pointer to the initialized context.
 * @param event_type 0x01 onDTCStatusChange (param = changed statusOfDTC,
 *                   matched against the stored DTCStatusMask) or
 *                   0x03 onChangeOfDataIdentifier (param = the DID that
 *                   changed).
 * @param param      DID or status, per @p event_type.
 * @return Number of events emitted (>= 0), or a negative error code.
 */
int uds_roe_trigger(uds_ctx_t *ctx, uint8_t event_type, uint32_t param);

/**
 * @brief Serialize the stored ResponseOnEvent (0x86) definitions to a buffer
 *        so the application can persist them across a reset (it owns the NVM).
 *
 * Writes a self-describing blob of all currently-stored event definitions
 * (type, parameter, window, serviceToRespondTo) — not their volatile runtime
 * state. Pair with uds_roe_deserialize() at startup.
 *
 * @param ctx  Pointer to the initialized context.
 * @param buf  Destination buffer.
 * @param max  Capacity of @p buf.
 * @return Bytes written (>= 0), or a negative error code if @p buf is too small.
 */
int uds_roe_serialize(uds_ctx_t *ctx, uint8_t *buf, uint16_t max);

/**
 * @brief Restore ResponseOnEvent definitions previously produced by
 *        uds_roe_serialize(). Restored events are stored but inactive until a
 *        startResponseOnEvent (0x86 0x05).
 *
 * @param ctx  Pointer to the initialized context.
 * @param buf  Source blob.
 * @param len  Length of @p buf.
 * @return Number of definitions restored (>= 0), or a negative error code on a
 *         malformed/incompatible blob.
 */
int uds_roe_deserialize(uds_ctx_t *ctx, const uint8_t *buf, uint16_t len);

/**
 * @brief Send a UDS Request as a Client.
 *
 * @param ctx      Pointer to the initialized context.
 * @param sid      The Service ID to request (e.g., 0x22).
 * @param data     Pointer to the request payload (excluding SID).
 * @param len      Length of the payload data.
 * @param callback Function to call when a response is received from the ECU.
 * @return UDS_OK if the request was successfully passed to the transport layer.
 */
int uds_client_request(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len,
                       uds_response_cb callback);

/**
 * @brief Send a positive response manually.
 *
 * Used by asynchronous service handlers that returned UDS_PENDING.
 *
 * @param ctx  Pointer to the initialized context.
 * @param len  Length of the response data already written to the context's tx_buffer.
 * @return UDS_OK on success.
 */
int uds_send_response(uds_ctx_t *ctx, uint16_t len);

/**
 * @brief Send a negative response (NRC) manually.
 *
 * Used by asynchronous service handlers to report failure after a delay.
 *
 * @param ctx  Pointer to the initialized context.
 * @param sid  The Service ID that failed.
 * @param nrc  The Negative Response Code (e.g., 0x22 for ConditionsNotCorrect).
 * @return UDS_OK on success.
 */
int uds_send_nrc(uds_ctx_t *ctx, uint8_t sid, uint8_t nrc);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CORE_H */
