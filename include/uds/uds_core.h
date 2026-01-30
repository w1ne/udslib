/**
 * @file uds_core.h
 * @brief ISO-14229 UDS Protocol Stack Core API
 *
 * This is the main entry point for the library.
 * It provides functions to initialize the stack, feed it data, and cycle the main loop.
 */

#ifndef UDS_CORE_H
#define UDS_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "uds_config.h"

/* Return Codes */
#define UDS_OK                  0
#define UDS_ERR_INVALID_ARG    -1
#define UDS_ERR_BUFFER_TOO_SMALL -2
#define UDS_ERR_NOT_INIT       -3
#define UDS_PENDING            1 /**< Service operation is pending (auto-trigger 0x78) */

/**
 * @brief Initialize the UDS Stack
 *
 * Validates the configuration and resets the internal state.
 *
 * @param ctx    Pointer to an allocated context structure.
 * @param config Pointer to a constant configuration structure.
 *               This pointer is stored in the context and must remain valid.
 * @return UDS_OK on success, error code otherwise.
 */
int uds_init(uds_ctx_t* ctx, const uds_config_t* config);

/**
 * @brief Client Response Callback
 * @param ctx   UDS context
 * @param sid   Service ID of the response
 * @param data  Payload data
 * @param len   Payload length
 */
typedef void (*uds_response_cb)(uds_ctx_t* ctx, uint8_t sid, const uint8_t* data, uint16_t len);

/**
 * @brief Process the UDS Stack (Main Loop)
 *
 * This function performs periodic tasks such as:
 * - Checking session timeouts (S3 Timer)
 * - Handling P2/P2* server timeouts
 * - Managing asynchronous service execution
 *
 * This function is non-blocking and should be called frequently:
 * - In a Super Loop: Call it once per iteration.
 * - In an RTOS: Call it from a dedicated thread or low-priority task.
 *
 * @param ctx Pointer to the initialized context.
 */
void uds_process(uds_ctx_t* ctx);

/**
 * @brief Input a UDS SDU (Service Data Unit)
 *
 * Feeds a fully assembled UDS message into the stack for processing.
 *
 * Usage with Zephyr/SocketCAN:
 *   Call this function whenever `recv()` returns a complete packet on the ISOTP socket.
 *
 * Usage with Bare Metal:
 *   Call this function from your ISO-TP reassembly callback when a complete message arrives.
 *
 * @param ctx  Pointer to the initialized context.
 * @param data Pointer to the buffer containing the SDU (at least 1 byte for SID).
 * @param len  Length of the data in bytes.
 */
void uds_input_sdu(uds_ctx_t* ctx, const uint8_t* data, uint16_t len);

/* --- Client API --- */

/**
 * @brief Send a UDS Request as a Client
 * 
 * @param ctx      Pointer to the initialized context.
 * @param sid      Service ID to request.
 * @param data     Request payload (excluding SID).
 * @param len      Length of data.
 * @param callback Callback to execute when response arrives.
 * @return UDS_OK if sent.
 */
int uds_client_request(uds_ctx_t* ctx, uint8_t sid, const uint8_t* data, uint16_t len, uds_response_cb callback);

/* --- Asynchronous Response API --- */

/**
 * @brief Send a positive response manually.
 *
 * Use this only if you deferred processing in a service handler (returned pending).
 *
 * @param ctx  Pointer to the initialized context.
 * @param len  Length of the response data already written to `tx_buffer`.
 * @return UDS_OK on success.
 */
int uds_send_response(uds_ctx_t* ctx, uint16_t len);

/**
 * @brief Send a negative response (NRC) manually.
 *
 * Use this only if you deferred processing.
 *
 * @param ctx  Pointer to the initialized context.
 * @param sid  The Service ID that failed.
 * @param nrc  The Negative Response Code (0x10, 0x11, etc.).
 * @return UDS_OK on success.
 */
int uds_send_nrc(uds_ctx_t* ctx, uint8_t sid, uint8_t nrc);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CORE_H */
