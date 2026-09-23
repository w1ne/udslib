/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef UDS_DTC_STORE_H
#define UDS_DTC_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_config.h"

/* First word of a bind object. A uds_dtc_store_t starts with a pointer, so the
 * 2.1.0 app_data = &store wire-up does not match. */
#define UDS_DTC_STORE_BIND_TAG 0xA5C3D7B1u

/**
 * @brief Optional reference DTC store (opt-in; not used by the core).
 *
 * The application supplies the backing array (no allocation). The store
 * implements the ReadDTCInformation callbacks and owns the diagnostic
 * policy the protocol core deliberately avoids (fault-detection counter,
 * aging, self-heal, freeze frame). Wire it with @ref uds_dtc_store_bind —
 * do not assign store callbacks or @c app_data = &store by hand.
 *
 * Pick one DTC backend: @ref uds_dtc_store_bind (this store) **or**
 * @ref uds_dtc_bind (custom ops). Last bind wins. Library-framed 0x19
 * (list / snapshot / extdata) and the raw @c fn_dtc_read leftovers are
 * complementary slots on one backend, not two stacks.
 *
 * The store is RAM. It does not write flash. To keep DTC status across a
 * reset, the application calls uds_dtc_store_serialize() and stores that
 * buffer in its own NVM, then uds_dtc_store_deserialize() once at boot
 * after the DTC numbers are registered again.
 */
typedef struct
{
    uds_dtc_record_t *entries;      /**< Application-provided backing array. */
    uint16_t capacity;              /**< Number of slots in @ref entries. */
    uint16_t count;                 /**< Registered DTCs. */
    uint8_t aging_threshold;        /**< Operation cycles to self-heal (e.g. 40). */
    uds_dtc_snapshot_t environment; /**< Latest voltage / power mode / time. */
    bool environment_set;           /**< @ref uds_dtc_store_set_environment was called. */
} uds_dtc_store_t;

/**
 * @brief Stable bind handle for @ref uds_dtc_store_bind (app-owned, no malloc).
 *
 * Keep this object alive for the lifetime of the stack. @ref uds_dtc_store_bind
 * writes @c tag. Callbacks ignore @c app_data unless that tag is present, so a
 * raw store pointer is not walked as a bind. After bind, @c cfg.app_data points
 * here and @c app_data still holds the ECU / application pointer.
 *
 * @code
 * static uds_dtc_store_bind_t bind = { .store = &store, .app_data = &ecu };
 * uds_dtc_store_bind(&cfg, &bind);
 * @endcode
 */
typedef struct
{
    uint32_t tag;           /**< Written by @ref uds_dtc_store_bind. Leave zero. */
    uds_dtc_store_t *store; /**< Reference store instance. */
    void *app_data;         /**< Nested ECU / app pointer (non-DTC callbacks). */
} uds_dtc_store_bind_t;

/** Initialise a store over an application-provided backing array. */
void uds_dtc_store_init(uds_dtc_store_t *s, uds_dtc_record_t *backing, uint16_t capacity,
                        uint8_t aging_threshold);

/**
 * @brief Register (or update) a DTC.
 *
 * New entries start with zeroed status/counters; registering an already-present
 * DTC updates only its severity/functional_unit/functional_group metadata and
 * leaves runtime status/counters intact.
 *
 * @return Index (>=0) on success, or -1 if the store is full.
 */
int uds_dtc_store_register(uds_dtc_store_t *s, uint32_t dtc, uint8_t severity,
                           uint8_t functional_unit, uint8_t functional_group);

/** Find a registered DTC, or NULL. */
uds_dtc_record_t *uds_dtc_store_get(uds_dtc_store_t *s, uint32_t dtc);

/**
 * @brief Report a self-test result for a DTC.
 *
 * failed=true: fault-detection counter increments (saturates at +127),
 * testFailed/testFailedThisOperationCycle set; at +127 the DTC is confirmed.
 * The first confirmation, and each later operation cycle that fails again,
 * increments the occurrence counter, sets the pending counter, clears the
 * ageing counter, and copies the current environment into the snapshot.
 * failed=false: counter decrements (floors at -128), testFailed cleared,
 * pending counter cleared.
 */
void uds_dtc_store_report_test(uds_dtc_store_t *s, uint32_t dtc, bool failed);

/**
 * @brief Publish the environment the next stored fault will freeze.
 *
 * Voltage and power mode are stored as given. A NULL @p s or @p env is ignored.
 */
void uds_dtc_store_set_environment(uds_dtc_store_t *s, const uds_dtc_snapshot_t *env);

/**
 * @brief Advance one operation cycle: age DTCs not failed this cycle; when a
 * DTC's aging counter reaches the threshold it self-heals (status cleared).
 * Per-cycle status bits and the fault-detection counter are reset.
 */
void uds_dtc_store_operation_cycle(uds_dtc_store_t *s);

/** Clear DTC(s): group 0xFFFFFF clears all, else clears the matching DTC. */
void uds_dtc_store_clear(uds_dtc_store_t *s, uint32_t group);

/**
 * @brief Copy runtime DTC state into a caller-owned buffer.
 *
 * The library does not touch NVM. Call this after report_test, operation_cycle,
 * or clear, then write @p buf with the ECU's own flash or EEPROM driver.
 * Do not erase flash inside a 0x19 handler.
 *
 * Only runtime bytes are stored: the 24-bit DTC, statusOfDTC, the
 * fault-detection counter, the aging counter, the extended-data counters,
 * and the snapshot. Severity, functional unit, functional group, and the
 * aging threshold are configuration — register them again at boot, then
 * deserialize.
 *
 * Layout version 0x02: version (1), count (2, big-endian), then 18 bytes
 * per DTC (DTC high, mid, low, status, fault-detection counter, aging
 * counter, occurrence, pending, aged, snapshot_valid, voltage, power mode,
 * second, minute, hour, day, month, year). Version 0x01 blobs (6 bytes per
 * DTC: DTC, status, fault-detection counter, aging counter) still load.
 *
 * @param s    Store to read.
 * @param buf  Destination buffer.
 * @param max  Capacity of @p buf in bytes.
 * @return Bytes written (>= 0), UDS_ERR_INVALID_ARG, or UDS_ERR_BUFFER_TOO_SMALL.
 */
int uds_dtc_store_serialize(const uds_dtc_store_t *s, uint8_t *buf, uint16_t max);

/**
 * @brief Restore runtime DTC state previously written by uds_dtc_store_serialize().
 *
 * Register the DTC catalog first. Records whose DTC is not registered are
 * skipped. Registered DTCs missing from the blob keep their current runtime
 * bytes (normally zero). A short or wrong-version blob changes nothing.
 * Bytes after the last record are ignored, so a whole flash page may be passed
 * when @p len is the page size and the header count is intact.
 *
 * @param s    Store whose DTC numbers are already registered.
 * @param buf  Source blob.
 * @param len  Number of valid bytes in @p buf.
 * @return Number of DTCs updated (>= 0), or UDS_ERR_INVALID_ARG.
 */
int uds_dtc_store_deserialize(uds_dtc_store_t *s, const uint8_t *buf, uint16_t len);

/**
 * @brief Wire the reference store into @p cfg (replaces hand-assigning store cbs).
 *
 * Sets @c fn_dtc_list / snapshot / extdata / clear to the store implementation,
 * points @c app_data at @p bind (must remain valid), and clears @c fn_dtc_read
 * so a prior custom read hook cannot silently mix with the store. Last bind
 * wins over @ref uds_dtc_bind. Call before @c uds_init(). Zero-malloc.
 *
 * @param cfg   Mutable configuration.
 * @param bind  App-owned stable bind object (@c store + nested @c app_data).
 *              Not const: bind writes @c tag.
 */
void uds_dtc_store_bind(uds_config_t *cfg, uds_dtc_store_bind_t *bind);

/**
 * @brief Bind object installed by @ref uds_dtc_store_bind, or NULL.
 *
 * Reads four bytes of @c app_data. A raw @c uds_dtc_store_t does not carry
 * @ref UDS_DTC_STORE_BIND_TAG, so the 2.1.0 wire-up fails closed.
 */
static inline const uds_dtc_store_bind_t *uds_dtc_store_bind_from_ctx(const struct uds_ctx *ctx)
{
    uint32_t tag;

    if ((ctx == NULL) || (ctx->config == NULL) || (ctx->config->app_data == NULL)) {
        return NULL;
    }
    memcpy(&tag, ctx->config->app_data, sizeof(tag));
    if (tag != UDS_DTC_STORE_BIND_TAG) {
        return NULL;
    }
    return (const uds_dtc_store_bind_t *) ctx->config->app_data;
}

/**
 * @brief Recover the store pointer after @ref uds_dtc_store_bind.
 * @return Store, or NULL if unbound / incomplete / not a bind object.
 */
static inline uds_dtc_store_t *uds_dtc_store_from_ctx(const struct uds_ctx *ctx)
{
    const uds_dtc_store_bind_t *bind = uds_dtc_store_bind_from_ctx(ctx);

    if (bind == NULL) {
        return NULL;
    }
    return bind->store;
}

/**
 * @brief Recover nested app_data after @ref uds_dtc_store_bind.
 * @return The bind's @c app_data, or NULL if unbound / not a bind object.
 */
static inline void *uds_app_data_from_ctx(const struct uds_ctx *ctx)
{
    const uds_dtc_store_bind_t *bind = uds_dtc_store_bind_from_ctx(ctx);

    if (bind == NULL) {
        return NULL;
    }
    return bind->app_data;
}

/*
 * DEPRECATED as the public wire-up path — use @ref uds_dtc_store_bind.
 * These remain callable for advanced / test use. They return NRC 0x22 unless
 * cfg.app_data is a uds_dtc_store_bind_t installed by uds_dtc_store_bind.
 */
int uds_dtc_store_list_cb(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                          uint16_t max);
int uds_dtc_store_snapshot_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                              uint8_t *out_buf, uint16_t max_len);
int uds_dtc_store_extdata_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len);
int uds_dtc_store_clear_cb(struct uds_ctx *ctx, uint32_t group);

#endif /* UDS_DTC_STORE_H */
