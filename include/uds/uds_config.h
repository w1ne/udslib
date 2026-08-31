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

/* Default budget (ms) to wait for fn_tx_complete before forcing the reset. */
#define UDS_DEFAULT_RESET_TX_WAIT_MS 50u

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
 * Invoked only AFTER the ECUReset positive response has been handed to the
 * transport, per ISO 14229-1 (§ ECUReset: the positive response shall be sent
 * before the reset is executed). This holds even when the 0x11 is wrapped in a
 * SecuredDataTransmission (0x84): the call is deferred until the outer secured
 * response is sent, not the captured inner one. It is also skipped entirely if
 * the response could not be handed to the transport.
 *
 * The call is made from uds_process() — the periodic tick — never from the
 * receive path and never via a busy-wait, so it composes with any substrate
 * (bare-metal super-loop, FreeRTOS, Zephyr). fn_tp_send returning means
 * "queued", not "transmitted", on most drivers; supply fn_tx_complete so the
 * library can hold the reset until the frame has physically drained. Without
 * fn_tx_complete the reset fires from uds_process once reset_tx_wait_ms has
 * elapsed (a non-blocking, configurable post-response delay). A reset that
 * reboots and never returns (e.g. NVIC_SystemReset) is therefore safe.
 *
 * @param ctx   Pointer to the UDS context.
 * @param type  The type of reset requested (uds_reset_type_t).
 */
typedef void (*uds_reset_fn)(struct uds_ctx *ctx, uint8_t type);

/* Optional. Returns true once the most recently transmitted response is
 * physically on the wire (transport TX buffer/mailbox drained). When set,
 * uds_process() polls it once per tick — never spins — and releases a deferred
 * post-TX action (ECUReset 0x11, LinkControl 0x87 transition) only after it
 * returns true, bounded by reset_tx_wait_ms. NULL falls back to a pure
 * reset_tx_wait_ms time delay. get_time_ms must advance while this returns
 * false. */
typedef bool (*uds_tx_complete_fn)(struct uds_ctx *ctx);

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
 * @brief DTC memory region addressed by a ReadDTCInformation (0x19) sub-function.
 *
 * ISO 14229-1 splits several 0x19 sub-functions across storage regions that hold
 * the same record shape. The library passes the region to the multi-memory
 * callbacks so one application hook can serve all of them.
 */
typedef enum
{
    UDS_DTC_MEM_PRIMARY = 0,       /**< Primary (default) DTC memory */
    UDS_DTC_MEM_MIRROR = 1,        /**< Mirror memory (0x0F/0x10/0x11) */
    UDS_DTC_MEM_EMISSIONS_OBD = 2, /**< Emissions-related OBD DTCs (0x12/0x13) */
    UDS_DTC_MEM_USER_DEFINED = 3   /**< User-defined memory (0x17/0x18/0x19) */
} uds_dtc_memory_t;

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
    uint8_t cmp_op;               /**< onComparisonOfValues (0x07): comparison operator. */
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

/** Outcome a service handler asks the framework to emit. */
typedef enum
{
    UDS_RESULT_POSITIVE, /**< tx_buffer[0..len-1] holds the positive response */
    UDS_RESULT_NRC,      /**< emit negative response with .nrc                */
    UDS_RESULT_PENDING,  /**< async: framework emits 0x78 and tracks P2*      */
    UDS_RESULT_NONE /**< emit no response at all (e.g. 0x84 when the inner response was suppressed)
                     */
} uds_result_kind_t;

typedef struct
{
    uds_result_kind_t kind;
    uint16_t len; /**< payload length in tx_buffer (POSITIVE) */
    uint8_t nrc;  /**< NRC code (NRC)                         */
} uds_result_t;

/* Each helper fully initialises *out so a handler's result never depends on the
 * caller having pre-zeroed the unused sibling fields. */
static inline void uds_ok(uds_result_t *out, uint16_t len)
{
    out->kind = UDS_RESULT_POSITIVE;
    out->len = len;
    out->nrc = 0u;
}
static inline void uds_nrc(uds_result_t *out, uint8_t nrc)
{
    out->kind = UDS_RESULT_NRC;
    out->len = 0u;
    out->nrc = nrc;
}
static inline void uds_pending(uds_result_t *out)
{
    out->kind = UDS_RESULT_PENDING;
    out->len = 0u;
    out->nrc = 0u;
}
static inline void uds_none(uds_result_t *out)
{
    out->kind = UDS_RESULT_NONE;
    out->len = 0u;
    out->nrc = 0u;
}

/**
 * @brief Service Handler Function Signature
 *
 * @param ctx   Pointer to the UDS context.
 * @param data  Pointer to the request payload (including SID).
 * @param len   Length of the request payload.
 * @param out   Result descriptor written by the handler; the framework emits.
 */
typedef void (*uds_service_handler_t)(struct uds_ctx *ctx, const uint8_t *data, uint16_t len,
                                      uds_result_t *out);

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
    uint8_t address_mode;    /**< Allowed addressing (UDS_ADDR_* bitmask); 0 = both */
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
    /**
     * Default P2 server timeout in ms (usually 50 ms).
     *
     * Precedence (resolved once in uds_init into ctx->session.p2_ms):
     *   1. p2_ms > 0           — authoritative; used directly.
     *   2. p2_ms == 0 and p2_server_max > 0 — legacy alias; p2_server_max
     *      is folded in so existing configurations are unaffected.
     *   3. Both zero           — stack default of 50 ms is applied.
     *
     * The 0x10 DiagnosticSessionControl handler derives the advertised P2
     * from ctx->session.p2_ms; it does NOT re-read these config fields.
     */
    uint16_t p2_ms;
    /**
     * P2* server timeout in ms after NRC 0x78 (usually 5000 ms).
     *
     * Same precedence as p2_ms (see above), with p2_star_server_max as the
     * legacy alias.  The wire encoding divides by 10 (ISO 14229-1 units).
     */
    uint32_t p2_star_ms;

    /* --- Service Callbacks --- */
    /** Optional: ECU Reset callback for SID 0x11 */
    uds_reset_fn fn_reset;

    /** powerDownTime (seconds) appended to the enableRapidPowerShutDown
     *  (0x11 sub-function 0x04) positive response, per ISO 14229-1. 0xFF means
     *  "failure / time not available". Ignored for other reset types. */
    uint8_t power_down_time;

    /** Optional: returns true when the last response has left the transport.
     *  Gates fn_reset so a rebooting reset cannot drop the response. */
    uds_tx_complete_fn fn_tx_complete;
    /** Max ms to wait for fn_tx_complete before forcing the reset (0 =
     *  UDS_DEFAULT_RESET_TX_WAIT_MS). Ignored when fn_tx_complete is NULL. */
    uint16_t reset_tx_wait_ms;

    /**
     * @brief Optional: Communication Control callback (SID 0x28)
     * @param ctx  UDS Context
     * @param ctrl_type Control Type (0x00-0x05, see uds_comm_control_type_t)
     * @param comm_type Communication Type byte (see uds_comm_type_t)
     * @param node_id   nodeIdentificationNumber for the enhanced-address
     *                  sub-functions (ctrl_type 0x04/0x05); 0 otherwise.
     * @return UDS_OK to accept, or negative NRC to reject (e.g. 0x22).
     */
    int (*fn_comm_control)(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type,
                           uint16_t node_id);

    /**
     * @brief Optional: Control DTC Setting callback (SID 0x85)
     * @param ctx UDS Context
     * @param sub_function 0x01 (DTCSettingType on) or 0x02 (off); the
     *                     suppressPositiveResponse bit is already stripped.
     * @return UDS_OK to accept, or negative NRC to reject (e.g. 0x22).
     *
     * Lets the application actually start/stop recording DTCs — e.g. freeze
     * DTC storage for the duration of a reprogramming sequence. When NULL the
     * server still tracks the setting and answers positively; the callback is
     * only needed to take a real side effect.
     */
    int (*fn_control_dtc_setting)(struct uds_ctx *ctx, uint8_t sub_function);

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
     * @param req       The full ReadDTCInformation request (req[0]=SID 0x19, req[1]=sub-function,
     *                  req[2..]=sub-function parameters such as the status/severity mask, DTC,
     *                  record number, or memory selection).
     * @param req_len   Length of @p req in bytes.
     * @param out_buf   Buffer to write DTC info into.
     * @param max_len   Max buffer size.
     * @return          Number of bytes written, or negative NRC on failure.
     */
    int (*fn_dtc_read)(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req, uint16_t req_len,
                       uint8_t *out_buf, uint16_t max_len);

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

    /**
     * @brief Optional: DTC enumeration from a non-primary memory region
     *        (SID 0x19, sub-functions 0x0F/0x11 mirror memory, 0x12/0x13
     *        emissions-related OBD, 0x17 user-defined memory).
     *
     * Same contract as @ref fn_dtc_list (match when
     * (record.status & status_mask) is non-zero, a mask of 0x00 matches every
     * record, @p out may be NULL when only the count is needed), with the
     * region selected by @p memory. @p mem_selection carries the
     * MemorySelection byte for @ref UDS_DTC_MEM_USER_DEFINED and is 0 for the
     * other regions.
     *
     * When this hook is NULL those sub-functions fall through to the raw
     * @ref fn_dtc_read path.
     *
     * @return Number of matching DTCs (which may exceed @p max), or a
     *         negative NRC on failure.
     */
    int (*fn_dtc_list_mem)(struct uds_ctx *ctx, uds_dtc_memory_t memory, uint8_t mem_selection,
                           uint8_t status_mask, uds_dtc_record_t *out, uint16_t max);

    /**
     * @brief Optional: snapshot record numbers stored for @p dtc (SID 0x19,
     *        sub-function 0x03 reportDTCSnapshotIdentification).
     *
     * Write up to @p max DTCSnapshotRecordNumber values into @p out_records.
     * Requires @ref fn_dtc_list to enumerate the DTCs to ask about.
     *
     * @return Number of stored records (which may exceed @p max), 0 when the
     *         DTC has no snapshot, or a negative NRC.
     */
    int (*fn_dtc_snapshot_ids)(struct uds_ctx *ctx, uint32_t dtc, uint8_t *out_records,
                               uint16_t max);

    /**
     * @brief Optional: stored-data record bytes (SID 0x19, sub-function 0x05
     *        reportDTCStoredDataByRecordNumber).
     *
     * The library writes [0x59, 0x05, recordNumber]; write the
     * DTCAndStatusRecord and the identifier/value pairs that follow.
     *
     * @return Bytes written, 0 if no such record, or a negative NRC.
     */
    int (*fn_dtc_stored_data)(struct uds_ctx *ctx, uint8_t record_num, uint8_t *out_buf,
                              uint16_t max_len);

    /**
     * @brief Optional: extended-data records selected by record number across
     *        all DTCs (SID 0x19, sub-function 0x16
     *        reportDTCExtDataRecordByRecordNumber).
     *
     * The library writes [0x59, 0x16, recordNumber]; write the
     * [DTCAndStatusRecord, extendedData] pairs that follow.
     *
     * @return Bytes written, 0 if no such record, or a negative NRC.
     */
    int (*fn_dtc_extdata_by_record)(struct uds_ctx *ctx, uint8_t record_num, uint8_t *out_buf,
                                    uint16_t max_len);

    /**
     * @brief Optional: snapshot record bytes from user-defined memory
     *        (SID 0x19, sub-function 0x18).
     *
     * Same payload contract as @ref fn_dtc_snapshot, with the memory selected
     * by @p mem_selection.
     *
     * @return Bytes written, 0 if no such record, or a negative NRC.
     */
    int (*fn_dtc_snapshot_mem)(struct uds_ctx *ctx, uint8_t mem_selection, uint32_t dtc,
                               uint8_t record_num, uint8_t *out_buf, uint16_t max_len);

    /**
     * @brief Optional: extended-data record bytes from a non-primary memory
     *        region (SID 0x19, sub-functions 0x10 mirror memory and 0x19
     *        user-defined memory).
     *
     * Same payload contract as @ref fn_dtc_extdata. @p mem_selection carries
     * the MemorySelection byte for @ref UDS_DTC_MEM_USER_DEFINED and is 0 for
     * mirror memory.
     *
     * @return Bytes written, 0 if no such record, or a negative NRC.
     */
    int (*fn_dtc_extdata_mem)(struct uds_ctx *ctx, uds_dtc_memory_t memory, uint8_t mem_selection,
                              uint32_t dtc, uint8_t record_num, uint8_t *out_buf, uint16_t max_len);

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

    /** Authentication configuration byte returned by SID 0x29 sub-function 0x08
     *  (authenticationConfiguration). 0 = APCE not configured (default). */
    uint8_t auth_configuration;

    /**
     * @brief Optional: gate a service on an authenticated channel (SID 0x29).
     *
     * Called per request after session/sub-function/length/security checks.
     * Return true if @p sid requires authentication; when it does and
     * ctx.authenticated is false, the request is rejected with NRC 0x34
     * (authenticationRequired). NULL (default) gates nothing.
     */
    bool (*fn_auth_required)(struct uds_ctx *ctx, uint8_t sid);

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
    /**
     * Legacy P2 alias (ms).  Kept for source compatibility.
     * When p2_ms is zero and this field is non-zero, uds_init folds it into
     * the resolved ctx->session.p2_ms.  Prefer p2_ms for new configurations.
     */
    uint16_t p2_server_max;
    /**
     * Legacy P2* alias (ms).  Kept for source compatibility.
     * Folded in by uds_init when p2_star_ms is zero and this field is
     * non-zero.  Prefer p2_star_ms for new configurations.
     */
    uint16_t p2_star_server_max;

    /**
     * @brief S3 server session-timeout in ms (ISO 14229-1 parameter S3).
     *
     * When the tester is absent for longer than S3, the stack reverts to the
     * default diagnostic session and relocks security. Resolved once in
     * uds_init() into ctx->session.s3_ms.
     *
     * Precedence:
     *   s3_ms > 0   — used directly.
     *   s3_ms == 0  — UDS_S3_TIMEOUT_MS (5000 ms) is applied.
     */
    uint32_t s3_ms;

} uds_config_t;

/* --- Internal Context --- */

/**
 * @brief UDS Internal Context
 *
 * Stores the runtime state of the stack.
 * The user allocates this (stack/static), but should treat members as private.
 */
/* --- Runtime context, grouped by field lifetime/role (Phase 2) --- */

/* Concurrency model (see docs/OSAL.md, docs/ARCHITECTURE.md §8): uds_process()
 * and uds_input_sdu() run in two different contexts (e.g. a periodic task / a
 * super-loop tick vs. an RX ISR or RX task). Fields written in one context and
 * observed in the other are marked `volatile` so the compiler cannot cache a
 * stale copy across the lock when the "mutex" is a bare disable-IRQ critical
 * section (the single-core ISR-vs-main case, where there is no library call to
 * act as a compiler barrier). Fields touched in only ONE context are left plain.
 */

/** Diagnostic session and dynamic timing. */
typedef struct uds_session_state
{
    /* Cross-context: written by uds_process() S3 revert and by the 0x10 handler
     * (uds_input_sdu context); read by the dispatcher in both. */
    volatile uint8_t active; /**< Currently active session (Default, Programming, ...) */
    /* Cross-context: stamped in uds_input_sdu(), read by the uds_process() S3 timer. */
    volatile uint32_t last_msg_time; /**< Timestamp of last valid message (S3 timer) */
    uint16_t p2_ms;                  /**< Current P2 server timeout (resolved once in uds_init) */
    uint32_t p2_star_ms;             /**< Current P2* server timeout (resolved once in uds_init) */
    uint8_t comm_state; /**< CommunicationControl (0x28) state (uds_comm_control_type_t) */
    uint8_t dtc_setting_disabled; /**< ControlDTCSetting (0x85): 1 = DTC setting off, 0 = on */
    uint32_t
        s3_ms; /**< Resolved S3 server timeout; 0 at runtime is replaced by UDS_S3_TIMEOUT_MS */
} uds_session_state_t;

/** SecurityAccess (0x27) / Authentication (0x29) state. */
typedef struct uds_security_state
{
    /* Cross-context: relocked by the uds_process() S3 revert as well as written
     * by the 0x27/0x29 handlers (uds_input_sdu context), and read by the
     * dispatcher's security gate. */
    volatile uint8_t level;      /**< Current security level (0 = Locked) */
    volatile bool authenticated; /**< 0x29 state: true once fn_auth verifies ownership */
    uint32_t delay_end;          /**< Timestamp when the security delay expires (uds_input only) */
    uint8_t attempts;            /**< Counter for failed security attempts (uds_input only) */
    /* Cross-context: cleared by the uds_process() S3 revert, set by 0x27. */
    volatile uint8_t seed_level; /**< Level a seed is currently outstanding for (0 = none) */
    volatile uint8_t seed_len;   /**< Length of the issued seed cached in seed[] */
    uint8_t seed[UDS_SECURITY_SEED_MAX]; /**< Copy of the last issued seed (uds_input only) */
} uds_security_state_t;

/** Server-role persistent state: async response engine + per-service state.
 *
 * The fields driving the async-response state machine and the deferred post-TX
 * engine are shared between the uds_input_sdu() dispatch context and the
 * uds_process() tick, so they are `volatile` (see the note above
 * uds_session_state_t). Per-service bookkeeping touched only while dispatching a
 * request (uds_input_sdu context) is left plain. */
typedef struct uds_server_state
{
    /* Cross-context: P2/P2* deadline tracking is armed in uds_input_sdu() /
     * execute_handler and advanced by the uds_process() timing engine. */
    volatile uint32_t p2_timer_start; /**< Start time for P2 performance tracking */
    volatile bool p2_msg_pending;     /**< True if a service returned UDS_PENDING */
    volatile bool p2_star_active;     /**< True once the first 0x78 NRC has been sent */
    volatile uint8_t pending_sid;     /**< SID awaiting an async (0x78) response */
    volatile uint16_t rcrrp_count;    /**< Counter for NRC 0x78 repetitions (C-07) */
    uint8_t flash_sequence;           /**< Block Sequence Counter for SID 0x36 (uds_input only) */
    bool transfer_active;     /**< Between RequestDownload/Upload and TransferExit (uds_input) */
    bool link_ctrl_verified;  /**< 0x87: a verify sub-function has been accepted (uds_input) */
    uint32_t link_ctrl_param; /**< 0x87: link parameter latched at verify (post-TX engine reads) */
    /* Deferred post-transmit action (ECUReset 0x11, LinkControl 0x87 transition):
     * a disruptive operation held until its positive response is on the wire,
     * then drained by uds_process() — never via a busy-wait. See #88/#98.
     * Cross-context: armed by execute_handler (uds_input_sdu context), drained by
     * uds_internal_run_posttx_action() (uds_process context). */
    volatile uint8_t posttx_kind;        /**< uds_posttx_kind_t (0 = none) */
    volatile uint8_t posttx_arg;         /**< resetType, or LinkControl sub-function to replay */
    volatile uint32_t posttx_wait_start; /**< get_time_ms() when the transmit-wait began */
    volatile bool posttx_deadline_set;   /**< posttx_wait_start has been latched */
    /* Deferred transport handoff: a server response is built in tx_buffer under
     * the lock, then fn_tp_send() is invoked OUTSIDE the lock by the top-level
     * entry point. Only the length/flag cross the lock; the bytes stay in the
     * caller-owned tx_buffer, which the single-response-per-request contract
     * keeps stable until the flush. Set in execute_handler / uds_send_nrc
     * (uds_input_sdu context), consumed by the flush in the same call chain. */
    volatile bool tx_pending;         /**< A response is staged in tx_buffer awaiting fn_tp_send. */
    volatile uint16_t tx_pending_len; /**< Length of the staged response in tx_buffer. */
    /* Cross-context: the periodic (0x2A) schedule is edited by its handler
     * (uds_input_sdu context) and walked by the uds_process() scheduler. */
    volatile uint8_t periodic_ids[8];     /**< Active periodic IDs (SID 0x2A) */
    volatile uint8_t periodic_rates[8];   /**< Periodic sub-function rates (1-3) */
    volatile uint32_t periodic_timers[8]; /**< Next periodic transmission deadline */
    volatile uint8_t periodic_count;      /**< Number of active periodic IDs */
#if (UDS_ROE_MAX_EVENTS > 0)
    uds_roe_slot_t roe[UDS_ROE_MAX_EVENTS]; /**< ResponseOnEvent slots (SID 0x86) */
#endif
} uds_server_state_t;

/** Per-dispatch scratch: scoped to a single request, not persistent state. */
typedef struct uds_dispatch_scratch
{
    bool suppress_pos_resp; /**< Centralized suppressPosRsp (bit 7 of sub-function) */
    uint8_t req_addr_mode;  /**< Addressing mode of the request in flight (UDS_ADDR_*) */
    uint8_t posttx_kind; /**< uds_posttx_kind_t: post-TX action decided this dispatch (0 = none) */
    uint8_t
        posttx_arg; /**< sub-function the deferred action replays (resetType / LinkControl sub) */
    bool in_secured_session;      /**< Dispatching a request unwrapped from 0x84 */
    bool secure_capturing;        /**< Capturing the inner response instead of sending */
    uint8_t *secure_capture_buf;  /**< Capture target (points to caller stack) */
    uint16_t secure_capture_size; /**< Capacity of secure_capture_buf */
    uint16_t secure_capture_len;  /**< Bytes captured for the inner response */
    bool secure_capture_overflow; /**< Inner response exceeded the capture buffer */
    /* True while a request is being dispatched under the library lock (set in
     * uds_input_sdu_addr around handle_request). Lets the legacy public senders
     * uds_send_response()/uds_send_nrc() — which some handlers still call
     * directly, e.g. the 0x19 DTC formatter — STAGE the frame for the top-level
     * outside-the-lock flush instead of transmitting from inside the lock. When
     * false (an application calling uds_send_response() to complete a UDS_PENDING
     * response from its own context), the senders flush immediately so the caller
     * still sees the synchronous fn_tp_send result. */
    bool in_dispatch;
} uds_dispatch_scratch_t;

typedef struct uds_ctx
{
    /** Config pointer (must remain valid) */
    const uds_config_t *config;

    uds_session_state_t session;
    uds_security_state_t security;
    uds_server_state_t server;
    uds_dispatch_scratch_t scratch;
} uds_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* UDS_CONFIG_H */
