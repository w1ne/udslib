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
typedef enum {
    UDS_RESET_HARD = 0x01,        /**< Power cycle reset */
    UDS_RESET_SOFT = 0x02,        /**< Software-driven reset */
    UDS_RESET_KEY_OFF_ON = 0x03, /**< Simulated key cycle */
} uds_reset_type_t;

/**
 * @brief Communication Control Types (SID 0x28).
 */
typedef enum {
    UDS_COMM_ENABLE_RX_TX = 0x00,         /**< Normal communication */
    UDS_COMM_ENABLE_RX_DISABLE_TX = 0x01, /**< Receive only */
    UDS_COMM_DISABLE_RX_ENABLE_TX = 0x02, /**< Transmit only */
    UDS_COMM_DISABLE_RX_TX = 0x03          /**< Total communication silence */
} uds_comm_control_type_t;

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
