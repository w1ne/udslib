/**
 * @file uds_config.h
 * @brief UDS Stack Configuration & Dependency Injection
 *
 * This header defines the configuration structures used to initialize the UDS stack.
 * It strictly follows the "Dependency Injection" pattern to ensure zero coupling
 * to specific hardware, OS, or C standard libraries.
 */

#ifndef UDS_CONFIG_H
#define UDS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declaration of the opaque context */
struct uds_ctx;

/**
 * @brief Log Levels
 */
#define UDS_LOG_ERROR 0
#define UDS_LOG_INFO  1
#define UDS_LOG_DEBUG 2

/**
 * @brief UDS Event Logger Callback
 *
 * @param level Log severity (UDS_LOG_*)
 * @param msg   Null-terminated message string
 */
typedef void (*uds_log_fn)(uint8_t level, const char* msg);

/**
 * @brief System Time Provider
 *
 * @return Current system timestamp in milliseconds.
 *         Rollover is handled safely by the stack's interval logic.
 */
typedef uint32_t (*uds_get_time_fn)(void);

/**
 * @brief Transport Layer Send Function (SDU Level)
 *
 * The stack calls this to send a fully assembled UDS message (SDU).
 * The implementation (the user) is responsible for:
 * 1. Handling ISO-TP segmentation if necessary (Bare Metal).
 * 2. Or writing the buffer to an ISO-TP socket (Zephyr/Linux).
 *
 * @param ctx   Pointer to the UDS stack context.
 * @param data  Pointer to the SDU buffer (SID + Data).
 * @param len   Length of the SDU in bytes.
 * @return      0 on success, negative error code on failure.
 */
typedef int (*uds_tp_send_fn)(struct uds_ctx* ctx, const uint8_t* data, uint16_t len);

/**
 * @brief UDS Stack Configuration Structure
 *
 * This struct must be populated by the user and passed to uds_init().
 * It persists for the lifetime of the stack.
 */
typedef struct {
    /* --- Identity --- */
    uint8_t ecu_address;         /**< The logical address of this ECU (optional usage) */

    /* --- Platform Integration --- */
    uds_get_time_fn get_time_ms; /**< Mandatory: Monotonic time source */
    uds_log_fn      fn_log;      /**< Optional: Logging callback (can be NULL) */

    /* --- Transport Interface --- */
    uds_tp_send_fn  fn_tp_send;  /**< Mandatory: Output function for UDS SDUs */

    /* --- Timing Configuration (ISO 14229-1) --- */
    uint16_t        p2_ms;       /**< Default P2 server timeout (usually 50ms) */
    uint32_t        p2_star_ms;  /**< P2* server timeout after NRC 0x78 (usually 5000ms) */

    /* --- Memory Management (Zero Malloc) --- */
    /* The user must provide static buffers for the stack to use. */
    
    uint8_t* rx_buffer;          /**< Working buffer for reassembling incoming requests */
    uint16_t rx_buffer_size;     /**< Size of rx_buffer. Determines Max Request Size */
    
    uint8_t* tx_buffer;          /**< Working buffer for constructing responses */
    uint16_t tx_buffer_size;     /**< Size of tx_buffer. Determines Max Response Size */

} uds_config_t;

/**
 * @brief UDS Internal Context
 *
 * Stores the runtime state of the stack.
 * The user allocates this (stack/static), but should treat members as private.
 */
typedef struct uds_ctx {
    const uds_config_t* config;  /**< Config pointer (must remain valid) */
    
    /* State Machine */
    uint8_t  state;              /**< Current Session / Security State */
    uint8_t  active_session;     /**< Currently active session (Default, Programming, etc.) */
    uint8_t  security_level;     /**< Current security level (0 = Locked) */
    
    /* Timing */
    uint32_t last_msg_time;      /**< Timestamp of last received valid message (for S3 timer) */
    uint32_t p2_timer_start;     /**< Start time for P2 performance tracking */
    bool     p2_msg_pending;     /**< True if a service returned UDS_PENDING */
    bool     p2_star_active;     /**< True if we have already sent the first 0x78 NRC */

    /* Processing */
    bool     response_pending;   /**< True if the application is processing a request asynchronously */
    void*    client_cb;          /**< Client callback for current request */
    uint8_t  pending_sid;        /**< SID we are waiting for a response for */
    
} uds_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* UDS_CONFIG_H */
