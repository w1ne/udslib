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
 * @brief Security Access Callbacks (SID 0x27)
 */
typedef int (*uds_security_seed_fn)(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf, uint16_t max_len);
typedef int (*uds_security_key_fn)(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed, const uint8_t *key, uint16_t key_len);

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

/* --- Service Handler Interface --- */

/**
 * @brief Service Session Mask
 */
#define UDS_SESSION_DEFAULT (1 << 0)
#define UDS_SESSION_EXTENDED (1 << 1)
#define UDS_SESSION_PROGRAMMING (1 << 2)
#define UDS_SESSION_ALL (0xFF)

/**
 * @brief Service Handler Function Signature
 *
 * @param ctx   Pointer to the UDS context.
 * @param data  Pointer to the request payload (including SID).
 * @param len   Length of the request payload.
 * @return      UDS_OK, UDS_PENDING, or negative error code.
 */
typedef int (*uds_service_handler_t)(struct uds_ctx *ctx, const uint8_t *data, uint16_t len);

/**
 * @brief UDS Service Registry Entry
 */
typedef struct {
    uint8_t sid;             /**< Service ID (e.g., 0x22) */
    uint16_t min_len;        /**< Minimum required request length */
    uint8_t session_mask;    /**< Allowed sessions bitmask */
    uint16_t security_mask;  /**< Minimum security level required (bitmask or level) */
    uds_service_handler_t handler; /**< Function pointer to handler */
    const uint8_t *sub_mask; /**< Optional bitmask of supported 7-bit subfunctions (16 bytes) */
} uds_service_entry_t;

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

    /** 
     * @brief Optional: Communication Control callback (SID 0x28) 
     * @param ctx  UDS Context
     * @param ctrl_type Control Type (0-3)
     * @param comm_type Communication Type (Data Byte 2)
     * @return UDS_OK to accept, or negative NRC to reject (e.g. 0x22).
     */
    int (*fn_comm_control)(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type);

    /** Optional: Security Access Seed Provider (SID 0x27) */
    uds_security_seed_fn fn_security_seed;
    /** Optional: Security Access Key Verifier (SID 0x27) */
    uds_security_key_fn fn_security_key;

    /* --- Memory Management (Zero Malloc) --- */

    /** Working buffer for reassembling incoming requests */
    uint8_t *rx_buffer;
    /** Size of rx_buffer. Determines Max Request Size */
    uint16_t rx_buffer_size;

    /** Working buffer for constructing responses */
    uint8_t *tx_buffer;
    /** Size of tx_buffer. Determines Max Response Size */
    uint16_t tx_buffer_size;

    /* --- Enterprise Hardening --- */
    /** 
     * @brief Enable strict ISO 14229-1 compliance checks.
     * When true, the stack will perform more aggressive validation of 
     * timing parameters and request payload ranges.
     */
    bool strict_compliance;

    /** 
     * @brief Global log level filter for the stack.
     * Logs below this level will be suppressed before the callback is called.
     */
    uint8_t log_level;

    /* --- Data Identifiers (SID 0x22 / 0x2E) --- */
    /** Mandatory for RDBI/WDBI: Table of supported DIDs */
    uds_did_table_t did_table;

    /* --- Custom Services --- */
    /** Optional: Table of application-specific service handlers */
    const uds_service_entry_t *user_services;
    /** Number of entries in user_services table */
    uint16_t user_service_count;

    /* --- Advanced Policy Callbacks --- */

    /** 
     * @brief Optional: Safety Gate Check.
     * Called before executing potentially destructive services (Reset, Write, Flash).
     * @param sid   The service ID being requested.
     * @param data  The request payload.
     * @param len   Payload length.
     * @return      true if safe to proceed, false to reject with NRC 0x22 (ConditionsNotCorrect).
     */
    bool (*fn_is_safe)(struct uds_ctx *ctx, uint8_t sid, const uint8_t *data, uint16_t len);

    /**
     * @brief Optional: Non-Volatile Memory (NVM) Persistence.
     * Used to save/load stack state (session/security) across reboots.
     */
    int (*fn_nvm_save)(struct uds_ctx *ctx, const uint8_t *state, uint16_t len);
    int (*fn_nvm_load)(struct uds_ctx *ctx, uint8_t *state, uint16_t len);

    /* --- Fault Management (DTCs) --- */

    /**
     * @brief Optional: Read DTC Information (SID 0x19).
     * @param ctx       Pointer to context.
     * @param subfn     The 0x19 subfunction (e.g., 0x01, 0x02).
     * @param out_buf   Buffer to write DTC info into.
     * @param max_len   Max buffer size.
     * @return          Number of bytes written, or negative NRC on failure.
     */
    int (*fn_dtc_read)(struct uds_ctx *ctx, uint8_t subfn, uint8_t *out_buf, uint16_t max_len);

    /**
     * @brief Optional: Clear Diagnostic Information (SID 0x14).
     * @param ctx       Pointer to context.
     * @param group     The DTC group to clear (usually 0xFFFFFF for all).
     * @return          UDS_OK or negative NRC.
     */
    int (*fn_dtc_clear)(struct uds_ctx *ctx, uint32_t group);

    /**
     * @brief Optional: Authentication (SID 0x29).
     * @param ctx       Pointer to context.
     * @param subfn     The 0x29 subfunction.
     * @param data      Input data (Challenge/Certificate).
     * @param len       Input length.
     * @param out_buf   Output buffer (Response/Certificate).
     * @param max_len   Max output size.
     * @return          Bytes written to out_buf, or negative NRC.
     */
    int (*fn_auth)(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *data, uint16_t len, uint8_t *out_buf, uint16_t max_len);

    /* --- Flash Engine (OTA Support) --- */

    /**
     * @brief Optional: Routine Control (SID 0x31).
     * @param ctx       Pointer to context.
     * @param type      Routine control type (Start, Stop, RequestResults).
     * @param id        Routine Identifier (e.g., 0xFF00 for Erase).
     * @param data      Input data.
     * @param len       Input length.
     * @param out_buf   Output response data.
     * @param max_len   Max output size.
     * @return          Bytes written to out_buf, or negative NRC.
     */
    int (*fn_routine_control)(struct uds_ctx *ctx, uint8_t type, uint16_t id, const uint8_t *data, uint16_t len, uint8_t *out_buf, uint16_t max_len);

    /**
     * @brief Optional: Request Download (SID 0x34).
     * @param ctx       Pointer to context.
     * @param addr      Target memory address.
     * @param size      Total size of the download.
     * @return          UDS_OK or negative NRC.
     */
    int (*fn_request_download)(struct uds_ctx *ctx, uint32_t addr, uint32_t size);

    /**
     * @brief Optional: Transfer Data (SID 0x36).
     * @param ctx       Pointer to context.
     * @param sequence  Block sequence counter.
     * @param data      Block data.
     * @param len       Block length.
     * @return          UDS_OK or negative NRC.
     */
    int (*fn_transfer_data)(struct uds_ctx *ctx, uint8_t sequence, const uint8_t *data, uint16_t len);

    /**
     * @brief Optional: Request Transfer Exit (SID 0x37).
     * @param ctx       Pointer to context.
     * @return          UDS_OK or negative NRC.
     */
    int (*fn_transfer_exit)(struct uds_ctx *ctx);

    /**
     * @brief Callback for Read Memory By Address (0x23)
     *
     * @param[in] ctx Pointer to UDS context.
     * @param[in] addr Memory address to read.
     * @param[in] size Number of bytes to read.
     * @param[out] out_buf Buffer to store read data.
     * @return 0 on success, negative for UDS NRC (e.g. -0x31 for out of range).
     */
    int (*fn_mem_read)(struct uds_ctx *ctx, uint32_t addr, uint32_t size, uint8_t *out_buf);

    /**
     * @brief Callback for Write Memory By Address (0x3D)
     *
     * @param[in] ctx Pointer to UDS context.
     * @param[in] addr Memory address to write.
     * @param[in] size Number of bytes to write.
     * @param[in] data Data to write.
     * @return 0 on success, negative for UDS NRC (e.g. -0x31 for out of range).
     */
    int (*fn_mem_write)(struct uds_ctx *ctx, uint32_t addr, uint32_t size, const uint8_t *data);

    /* --- OS Abstraction Layer (OSAL) --- */

    /**
     * @brief Mutex handle provided by the application.
     */
    void *mutex_handle;

    /**
     * @brief Callback to lock the UDS context mutex.
     */
    void (*fn_mutex_lock)(void *mutex_handle);

    /**
     * @brief Callback to unlock the UDS context mutex.
     */
    void (*fn_mutex_unlock)(void *mutex_handle);
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

    /** ISO 14229-1: Centralized Suppression of Positive Response (bit 7 of sub-function) */
    bool suppress_pos_resp;

    /* --- Dynamic Timing Parameters --- */
    /** Current P2 server timeout */
    uint16_t p2_ms;
    /** Current P2* server timeout */
    uint32_t p2_star_ms;

    /** ISO 14229-1: Block Sequence Counter for SID 0x36 */
    uint8_t flash_sequence;
} uds_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* UDS_CONFIG_H */
