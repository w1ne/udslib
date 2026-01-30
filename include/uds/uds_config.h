/**
 * @file uds_config.h
 * @brief UDS Stack Configuration & Dependency Injection
 */

#ifndef UDS_CONFIG_H
#define UDS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration of the opaque context */
struct uds_ctx;

/* --- Log Levels --- */

/** Error level logging */
#define UDS_LOG_ERROR 0

/** Informational logging */
#define UDS_LOG_INFO 1

/** Debug level logging */
#define UDS_LOG_DEBUG 2

/* --- Configuration Callbacks --- */

/**
 * @brief UDS Event Logger Callback
 *
 * @param level Log severity (UDS_LOG_*)
 * @param msg   Null-terminated message string
 */
typedef void (*uds_log_fn)(uint8_t level, const char *msg);

/**
 * @brief System Time Provider
 *
 * @return Current system timestamp in milliseconds.
 */
typedef uint32_t (*uds_get_time_fn)(void);

/**
 * @brief Transport Layer Send Function (SDU Level)
 *
 * @param ctx   Pointer to the UDS stack context.
 * @param data  Pointer to the SDU buffer (SID + Data).
 * @param len   Length of the SDU in bytes.
 * @return      0 on success, negative error code on failure.
 */
typedef int (*uds_tp_send_fn)(struct uds_ctx *ctx, const uint8_t *data, uint16_t len);

/**
 * @brief ECU Reset Callback (SID 0x11)
 *
 * @param ctx   Pointer to the UDS context.
 * @param type  The type of reset requested (uds_reset_type_t).
 */
typedef void (*uds_reset_fn)(struct uds_ctx *ctx, uint8_t type);

/**
 * @brief DID Data Access Callbacks (SID 0x22 / 0x2E)
 */
typedef int (*uds_did_read_fn)(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len);
typedef int (*uds_did_write_fn)(struct uds_ctx *ctx, uint16_t did, const uint8_t *data, uint16_t len);

/**
 * @brief DID Registry Entry
 */
typedef struct {
    uint16_t id;             /**< Data Identifier (e.g., 0xF190) */
    uint16_t size;           /**< Expected data size in bytes */
    uds_did_read_fn read;    /**< Optional: Dynamic read callback */
    uds_did_write_fn write;  /**< Optional: Dynamic write callback */
    void *storage;           /**< Optional: Direct data storage pointer */
} uds_did_entry_t;

/**
 * @brief DID Table Registry
 */
typedef struct {
    const uds_did_entry_t *entries; /**< Pointer to an array of entries */
    uint16_t count;                 /**< Number of entries in the table */
} uds_did_table_t;

/* --- Configuration Structure --- */

/**
 * @brief UDS Stack Configuration Structure
 *
 * This struct must be populated by the user and passed to uds_init().
 * It persists for the lifetime of the stack.
 */
typedef struct {
    /** The logical address of this ECU (optional usage) */
    uint8_t ecu_address;

    /* --- Platform Integration --- */
    /** Mandatory: Monotonic time source */
    uds_get_time_fn get_time_ms;
    /** Optional: Logging callback (can be NULL) */
    uds_log_fn fn_log;

    /* --- Transport Interface --- */
    /** Mandatory: Output function for UDS SDUs */
    uds_tp_send_fn fn_tp_send;

    /* --- Timing Configuration (ISO 14229-1) --- */
    /** Default P2 server timeout (usually 50ms) */
    uint16_t p2_ms;
    /** P2* server timeout after NRC 0x78 (usually 5000ms) */
    uint32_t p2_star_ms;

    /* --- Service Callbacks --- */
    /** Optional: ECU Reset callback for SID 0x11 */
    uds_reset_fn fn_reset;

    /* --- Memory Management (Zero Malloc) --- */

    /** Working buffer for reassembling incoming requests */
    uint8_t *rx_buffer;
    /** Size of rx_buffer. Determines Max Request Size */
    uint16_t rx_buffer_size;

    /** Working buffer for constructing responses */
    uint8_t *tx_buffer;
    /** Size of tx_buffer. Determines Max Response Size */
    uint16_t tx_buffer_size;

    /* --- Data Identifiers (SID 0x22 / 0x2E) --- */
    /** Mandatory for RDBI/WDBI: Table of supported DIDs */
    uds_did_table_t did_table;
} uds_config_t;

/* --- Internal Context --- */

/**
 * @brief UDS Internal Context
 *
 * Stores the runtime state of the stack.
 * The user allocates this (stack/static), but should treat members as private.
 */
typedef struct uds_ctx {
    /** Config pointer (must remain valid) */
    const uds_config_t *config;

    /* --- State Machine --- */
    /** Current Session / Security State */
    uint8_t state;
    /** Currently active session (Default, Programming, etc.) */
    uint8_t active_session;
    /** Current security level (0 = Locked) */
    uint8_t security_level;

    /* --- Timing --- */
    /** Timestamp of last received valid message (for S3 timer) */
    uint32_t last_msg_time;
    /** Start time for P2 performance tracking */
    uint32_t p2_timer_start;
    /** True if a service returned UDS_PENDING */
    bool p2_msg_pending;
    /** True if we have already sent the first 0x78 NRC */
    bool p2_star_active;

    /* --- Processing --- */
    /** True if the application is processing a request asynchronously */
    bool response_pending;
    /** Client callback for current request */
    void *client_cb;
    /** SID we are waiting for a response for */
    uint8_t pending_sid;

    /** Communication control state for SID 0x28 (uds_comm_control_type_t) */
    uint8_t comm_state;
} uds_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* UDS_CONFIG_H */
