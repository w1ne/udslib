/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

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

/** Max issued-seed length cached for the key verifier (SID 0x27). */
#ifndef UDS_SECURITY_SEED_MAX
#define UDS_SECURITY_SEED_MAX 16u
#endif

/** ResponseOnEvent (0x86): number of storable event slots (0 compiles it out). */
#ifndef UDS_ROE_MAX_EVENTS
#define UDS_ROE_MAX_EVENTS 4u
#endif

/** ResponseOnEvent: max bytes of a stored serviceToRespondToRecord. */
#ifndef UDS_ROE_STR_MAX
#define UDS_ROE_STR_MAX 8u
#endif

/** ResponseOnEvent: default active window for a non-infinite eventWindowTime. */
#ifndef UDS_ROE_WINDOW_MS
#define UDS_ROE_WINDOW_MS 5000u
#endif

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
typedef int (*uds_did_write_fn)(struct uds_ctx *ctx, uint16_t did, const uint8_t *data,
                                uint16_t len);

/**
 * @brief Security Access Callbacks (SID 0x27)
 */
typedef int (*uds_security_seed_fn)(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf,
                                    uint16_t max_len);
typedef int (*uds_security_key_fn)(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed,
                                   const uint8_t *key, uint16_t key_len);

/**
 * @brief DID Registry Entry
 */
typedef struct
{
    uint16_t id;            /**< Data Identifier (e.g., 0xF190) */
    uint16_t size;          /**< Expected data size in bytes */
    uint8_t session_mask;   /**< Allowed sessions bitmask (0=All) */
    uint16_t security_mask; /**< Required security level (0=None) */
    uds_did_read_fn read;   /**< Optional: Dynamic read callback */
    uds_did_write_fn write; /**< Optional: Dynamic write callback */
    void *storage;          /**< Optional: Direct data storage pointer */
} uds_did_entry_t;

/**
 * @brief DID Table Registry
 */
typedef struct
{
    const uds_did_entry_t *entries; /**< Pointer to an array of entries */
    uint16_t count;                 /**< Number of entries in the table */
} uds_did_table_t;

/**
 * @brief Diagnostic Trouble Code record (ISO 14229-1).
 *
 * Supplied by the application via fn_dtc_list(); the library formats the
 * ISO wire layout for ReadDTCInformation (0x19) subfunctions itself.
 */
typedef struct
{
    uint32_t dtc;                   /**< 3-byte DTC, right-aligned (high byte ignored) */
    uint8_t status;                 /**< statusOfDTC byte (ISO 14229-1 Annex D) */
    uint8_t severity;               /**< DTCSeverity bits (0x08/0x09/0x42) */
    uint8_t functional_unit;        /**< DTCFunctionalUnit (0x08/0x09) */
    int8_t fault_detection_counter; /**< signed -128..127 (0x14) */
    uint8_t aging_counter;          /**< operation cycles since last fault */
    uint8_t functional_group;       /**< WWH-OBD functional group (0x33/0xD0/0xFE) */
} uds_dtc_record_t;

/**
 * @brief ResponseOnEvent (0x86) stored event definition.
 */
typedef struct
{
    bool in_use;                  /**< Slot occupied (a setup completed). */
    bool active;                  /**< Started (0x05); cleared by stop (0x00). */
    uint8_t event_type;           /**< 0x01 onDTCStatusChange / 0x03 onChangeOfDID. */
    uint32_t event_param;         /**< DID (0x03) or DTCStatusMask (0x01). */
    uint8_t window_byte;          /**< eventWindowTime byte (0x02 = infinite). */
    uint32_t window_deadline;     /**< Absolute ms; 0 = infinite. */
    uint32_t next_fire;           /**< onTimerInterrupt (0x02): next fire (0 = unprimed). */
    uint8_t str[UDS_ROE_STR_MAX]; /**< serviceToRespondToRecord bytes. */
    uint8_t str_len;              /**< Length of str. */
} uds_roe_slot_t;

/* --- Service Handler Interface --- */

/**
 * @brief Service Session Mask
 */
#define UDS_SESSION_DEFAULT (1 << 0)
#define UDS_SESSION_EXTENDED (1 << 1)
#define UDS_SESSION_PROGRAMMING (1 << 2)
/** Secured channel: granted only while a request is unwrapped from
 *  SecuredDataTransmission (0x84). A service whose session_mask is exactly
 *  UDS_SESSION_SECURED is reachable only through 0x84. */
#define UDS_SESSION_SECURED (1 << 3)
/** safetySystemDiagnosticSession ($04, ISO 14229-1). */
#define UDS_SESSION_SAFETY (1 << 4)
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
typedef struct
{
    uint8_t sid;                   /**< Service ID (e.g., 0x22) */
    uint16_t min_len;              /**< Minimum required request length */
    uint8_t session_mask;          /**< Allowed sessions bitmask */
    uint16_t security_mask;        /**< Minimum security level required (bitmask or level) */
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
typedef struct
{
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
     * @brief Apply a built-in session policy to the core services.
     *
     * When true, core services that would otherwise be reachable in every
     * session are restricted to ISO-sensible sessions: reprogramming services
     * (0x34/0x35/0x36/0x37/0x87/0x3D) to the programming session, and other
     * privileged services (0x27/0x11/0x28/0x29/0x2E/0x2F/0x31/0x83/0x85/0x23)
     * to extended or programming. Requests in a disallowed session return
     * NRC 0x7F (serviceNotSupportedInActiveSession).
     *
     * Default false (every core service is reachable in every session).
     * The policy is applied only to services left fully permissive
     * (session_mask == UDS_SESSION_ALL); give a service an explicit
     * session_mask to opt it out.
     */
    bool restrict_sessions;

    /**
     * @brief Global log level filter for the stack.
     * Logs below this level will be suppressed before the callback is called.
     */
    uint8_t log_level;

    /* --- Security Hardening (C-14, C-15) --- */
    /** Time delay after failed security attempts (ms). Default: 10000ms */
    uint32_t security_delay_ms;
    /** Max failed attempts before triggering delay. Default: 3 */
    uint8_t security_max_attempts;

    /** ISO 14229-1: Max allowed NRC 0x78 (ResponsePending) repetitions. 0=Infinite (C-07) */
    uint16_t rcrrp_limit;

    /**
     * @brief Flash transfer robustness: accept last-block replay (SID 0x36).
     *
     * When true, the server will accept a repeated BlockSequenceCounter equal to the
     * most recently accepted block and respond positively without re-invoking the
     * application transfer callback. This can help interoperability with clients
     * that retry after a lost positive response.
     *
     * Default: false (strict sequence enforcement).
     */
    bool transfer_accept_last_block_replay;

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
     * @brief Optional: Session-transition policy hook (SID 0x10).
     *
     * Called before a DiagnosticSessionControl request changes the active
     * session, with the current (@p from) and requested (@p to) session IDs.
     * Return true to allow the transition, false to reject it with NRC 0x22
     * (conditionsNotCorrect); on rejection the active session is left unchanged.
     *
     * When NULL (the default) every ISO-valid session may be entered from any
     * session, matching ISO 14229-1, which places no restriction on
     * session-to-session transitions. Use this hook to enforce an OEM-specific
     * transition graph (e.g. requiring the extended session before the
     * programming session).
     *
     * @param ctx   UDS context.
     * @param from  Currently active session ID.
     * @param to    Requested (already range-validated) session ID.
     * @return      true to allow, false to reject with NRC 0x22.
     */
    bool (*fn_session_transition_allowed)(struct uds_ctx *ctx, uint8_t from, uint8_t to);

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
     * @brief Optional: Structured DTC enumeration (SID 0x19, subfunctions
     *        0x01/0x02/0x0A). The library formats the ISO wire layout.
     *
     * Fill @p out with up to @p max records whose status matches
     * @p status_mask (a record matches when (record.status & status_mask)
     * is non-zero; a @p status_mask of 0x00 matches every record, used by
     * reportSupportedDTC 0x0A). When @p out is NULL or @p max is 0, only
     * the count is required (used by reportNumberOfDTCByStatusMask 0x01).
     *
     * @return Number of matching DTCs (which may exceed @p max), or a
     *         negative NRC on failure.
     */
    int (*fn_dtc_list)(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                       uint16_t max);

    /**
     * @brief Optional: DTC snapshot record bytes (SID 0x19, subfunction 0x04).
     *        Write the snapshot payload (record-number + identifiers + data)
     *        for @p dtc / @p record_num; the library frames the response.
     * @return Bytes written, 0 if no such record, or a negative NRC.
     */
    int (*fn_dtc_snapshot)(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num, uint8_t *out_buf,
                           uint16_t max_len);

    /**
     * @brief Optional: DTC extended-data record bytes (SID 0x19, sub 0x06).
     * @return Bytes written, 0 if no such record, or a negative NRC.
     */
    int (*fn_dtc_extdata)(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num, uint8_t *out_buf,
                          uint16_t max_len);

    /** DTCStatusAvailabilityMask reported in 0x01/0x02/0x0A responses. */
    uint8_t dtc_status_availability_mask;

    /** DTCFormatIdentifier reported in 0x01 (default 0x01 = ISO_14229-1). */
    uint8_t dtc_format_id;

    /** DTCSeverityAvailabilityMask reported in 0x42 responses. */
    uint8_t dtc_severity_availability_mask;

    /** Opaque application handle, recoverable inside callbacks via
     *  ctx->config->app_data (e.g. a uds_dtc_store_t* for the reference store). */
    void *app_data;

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
    int (*fn_auth)(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *data, uint16_t len,
                   uint8_t *out_buf, uint16_t max_len);

    /**
     * @brief Optional: Read Scaling Data By Identifier (SID 0x24).
     *        Write the scalingByte/scalingData bytes for @p did; the library
     *        frames the `0x64 <DID>` response prefix.
     * @return Bytes written, or a negative NRC on failure.
     */
    int (*fn_read_scaling)(struct uds_ctx *ctx, uint16_t did, uint8_t *out_buf, uint16_t max_len);

    /**
     * @brief Optional: Dynamically Define Data Identifier (SID 0x2C).
     *
     * The library validates the sub-function (0x01 defineByIdentifier,
     * 0x02 defineByMemoryAddress, 0x03 clear) and frames the response; the
     * application records or clears the definition.
     *
     * @param subfn        Sub-function (0x01/0x02/0x03).
     * @param defined_did  The dynamically-defined DID (0 when absent, e.g.
     *                     clear-all).
     * @param data         Sub-function payload (starting at the defined DID).
     * @param len          Length of @p data.
     * @return             0 on success, or a negative NRC.
     */
    int (*fn_dynamic_did)(struct uds_ctx *ctx, uint8_t subfn, uint16_t defined_did,
                          const uint8_t *data, uint16_t len);

    /**
     * @brief Optional: Request File Transfer (SID 0x38).
     *
     * The library validates the mode-of-operation and the filePathAndName
     * length, then hands the parsed fields to the application, which performs
     * the file operation and writes the response body (everything after
     * `0x78 <modeOfOperation>`).
     *
     * @param mode        modeOfOperation (1=AddFile..5=ResumeFile).
     * @param path        filePathAndName bytes.
     * @param path_len    filePathAndName length.
     * @param params      Trailing parameters (dataFormatIdentifier, sizes...).
     * @param params_len  Length of @p params.
     * @param out_buf     Response-body buffer (after SID + mode).
     * @param max_len     Capacity of @p out_buf.
     * @return            Response-body length, or a negative NRC.
     */
    int (*fn_file_transfer)(struct uds_ctx *ctx, uint8_t mode, const uint8_t *path,
                            uint16_t path_len, const uint8_t *params, uint16_t params_len,
                            uint8_t *out_buf, uint16_t max_len);

    /* --- Secured Data Transmission (SID 0x84) --- */

    /**
     * @brief Optional: verify + decrypt an incoming secured message body.
     *
     * The library parses the Administrative Parameter and passes the secured
     * payload (everything after the 2-byte APAR). Recover the plaintext inner
     * UDS request into @p out; the library then dispatches it as if it had
     * arrived directly, with the secured-session bit granted.
     *
     * @param apar     Administrative Parameter (16-bit, big-endian on the wire).
     * @param in       Secured payload bytes.
     * @param in_len   Secured payload length.
     * @param out      Buffer for the recovered inner request.
     * @param out_max  Capacity of @p out.
     * @return         Inner-request length, or a negative NRC (e.g. -0x33 on a
     *                 failed MAC) on rejection.
     */
    int (*fn_secure_decode)(struct uds_ctx *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                            uint8_t *out, uint16_t out_max);

    /**
     * @brief Optional: sign + encrypt an outgoing inner response.
     * @return Secured-response length written to @p out, or a negative NRC.
     */
    int (*fn_secure_encode)(struct uds_ctx *ctx, uint16_t apar, const uint8_t *in, uint16_t in_len,
                            uint8_t *out, uint16_t out_max);

    /**
     * @brief Optional symmetric key for the built-in crypto (only consulted
     *        when UDS_ENABLE_BUILTIN_CRYPTO is set and the hooks above are
     *        NULL). Ignored otherwise.
     */
    const uint8_t *secure_key;
    uint16_t secure_key_len; /**< Length of secure_key in bytes. */

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
    int (*fn_routine_control)(struct uds_ctx *ctx, uint8_t type, uint16_t id, const uint8_t *data,
                              uint16_t len, uint8_t *out_buf, uint16_t max_len);

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
    int (*fn_transfer_data)(struct uds_ctx *ctx, uint8_t sequence, const uint8_t *data,
                            uint16_t len);

    /**
     * @brief Optional: Request Transfer Exit (SID 0x37).
     * @param ctx       Pointer to context.
     * @return          UDS_OK or negative NRC.
     */
    int (*fn_transfer_exit)(struct uds_ctx *ctx);

    /**
     * @brief Optional: Link Control (SID 0x87).
     *
     * Two-step baud-rate transition. Called on verify (subfunction 0x01/0x02)
     * to validate the requested link parameters, and again on transition
     * (0x03) to apply the previously verified rate. Apply the actual link
     * change only after the positive response is transmitted.
     *
     * @param ctx          Pointer to context.
     * @param subfunction  0x01 verifyFixed, 0x02 verifySpecific, 0x03 transition.
     * @param link_param   Mode identifier (0x01), 3-byte baud value (0x02), or
     *                     the latched value from the verify step (0x03).
     * @return             UDS_OK to accept, or a negative NRC to reject.
     */
    int (*fn_link_control)(struct uds_ctx *ctx, uint8_t subfunction, uint32_t link_param);

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

    /** Callback for Write Memory By Address (0x3D) */
    int (*fn_mem_write)(struct uds_ctx *ctx, uint32_t addr, uint32_t size, const uint8_t *data);

    /* --- New Service Callbacks (0x2A, 0x2F, 0x35) --- */
    /** Optional: IO Control callback (SID 0x2F) */
    int (*fn_io_control)(struct uds_ctx *ctx, uint16_t id, uint8_t type, const uint8_t *data,
                         uint16_t len, uint8_t *out_buf, uint16_t max_len);
    /** Optional: Request Upload callback (SID 0x35) */
    int (*fn_request_upload)(struct uds_ctx *ctx, uint32_t addr, uint32_t size);
    /** Optional: Periodic Data Read (Used for SID 0x2A) */
    int (*fn_periodic_read)(struct uds_ctx *ctx, uint8_t periodic_id, uint8_t *out_buf,
                            uint16_t max_len);

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
    /* --- Timing Parameters (C-19) --- */
    /** Default P2 Server Max (ms). Recommended: 50ms */
    uint16_t p2_server_max;
    /** Default P2* Server Max (ms). Recommended: 5000ms */
    uint16_t p2_star_server_max;

} uds_config_t;

/* --- Internal Context --- */

/**
 * @brief UDS Internal Context
 *
 * Stores the runtime state of the stack.
 * The user allocates this (stack/static), but should treat members as private.
 */
typedef struct uds_ctx
{
    /** Config pointer (must remain valid) */
    const uds_config_t *config;

    /* --- State Machine --- */
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

    /* --- Server role: in-progress asynchronous request --- */
    /** SID of the request currently awaiting an async (0x78) response */
    uint8_t server_pending_sid;

    /* --- Client role: outstanding request awaiting a response --- */
    /** Callback to invoke when the awaited response arrives */
    void *client_cb;
    /** SID of the request we sent and are awaiting a response for (0 = none) */
    uint8_t client_pending_sid;

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

    /* --- Link Control State (SID 0x87) --- */
    /** True once a verify subfunction (0x01/0x02) has been accepted */
    bool link_ctrl_verified;
    /** Link parameter latched at verify, applied on transition (0x03) */
    uint32_t link_ctrl_param;

    /* --- Security State (C-14, C-15) --- */
    /** Timestamp when security delay expires */
    uint32_t security_delay_end;
    /** Counter for failed security attempts */
    uint8_t security_attempts;
    /** Level for which a seed is currently outstanding (0 = none requested) */
    uint8_t security_seed_level;
    /** Length of the issued seed cached in security_seed */
    uint8_t security_seed_len;
    /** Copy of the last issued seed, passed back to the key verifier */
    uint8_t security_seed[UDS_SECURITY_SEED_MAX];

    /** ISO 14229-1: Counter for NRC 0x78 repetitions (C-07) */
    uint16_t rcrrp_count;

    /* --- Periodic Data State (SID 0x2A) --- */
    uint8_t periodic_ids[8];     /**< Active periodic IDs */
    uint8_t periodic_rates[8];   /**< Subfunction rates (1-3) */
    uint32_t periodic_timers[8]; /**< Next transmission deadline */
    uint8_t periodic_count;      /**< Number of active periodic IDs */

#if (UDS_ROE_MAX_EVENTS > 0)
    /* --- ResponseOnEvent State (SID 0x86) --- */
    uds_roe_slot_t roe[UDS_ROE_MAX_EVENTS];
#endif

    /* --- Secured Data Transmission (SID 0x84) --- */
    /** True while dispatching a request unwrapped from 0x84 (grants the
     *  UDS_SESSION_SECURED gate to the inner service). */
    bool in_secured_session;
    /** True while the inner response is being captured instead of sent. */
    bool secure_capturing;
    /** Capture target for the inner response (points to caller stack). */
    uint8_t *secure_capture_buf;
    uint16_t secure_capture_size; /**< Capacity of secure_capture_buf. */
    uint16_t secure_capture_len;  /**< Bytes captured for the inner response. */
} uds_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* UDS_CONFIG_H */
