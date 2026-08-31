/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef UDS_DTC_STORE_H
#define UDS_DTC_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "uds/uds_config.h"

/**
 * @brief Optional reference DTC store (opt-in; not used by the core).
 *
 * The application supplies the backing array (no allocation). The store
 * implements the ReadDTCInformation callbacks and owns the diagnostic
 * policy the protocol core deliberately avoids (fault-detection counter,
 * aging, self-heal). Wire it up by setting cfg.app_data = &store and
 * cfg.fn_dtc_list = uds_dtc_store_list_cb (plus extdata/clear as needed).
 */
typedef struct
{
    uds_dtc_record_t *entries; /**< Application-provided backing array. */
    uint16_t capacity;         /**< Number of slots in @ref entries. */
    uint16_t count;            /**< Registered DTCs. */
    uint8_t aging_threshold;   /**< Operation cycles to self-heal (e.g. 40). */
} uds_dtc_store_t;

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
 * failed=false: counter decrements (floors at -128), testFailed cleared.
 */
void uds_dtc_store_report_test(uds_dtc_store_t *s, uint32_t dtc, bool failed);

/**
 * @brief Advance one operation cycle: age DTCs not failed this cycle; when a
 * DTC's aging counter reaches the threshold it self-heals (status cleared).
 * Per-cycle status bits and the fault-detection counter are reset.
 */
void uds_dtc_store_operation_cycle(uds_dtc_store_t *s);

/** Clear DTC(s): group 0xFFFFFF clears all, else clears the matching DTC. */
void uds_dtc_store_clear(uds_dtc_store_t *s, uint32_t group);

/* --- Ready-made uds_config_t callbacks (store reached via ctx->config->app_data) --- */
int uds_dtc_store_list_cb(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                          uint16_t max);
int uds_dtc_store_extdata_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len);
int uds_dtc_store_clear_cb(struct uds_ctx *ctx, uint32_t group);

/**
 * @brief fn_dtc_list_mem for the memory-scoped 0x19 sub-functions
 *        (0x0F/0x11 mirror, 0x12/0x13 emissions-related OBD, 0x17 user-defined).
 *
 * The reference store keeps a single set of records, so mirror and
 * user-defined memory report that set and @p mem_selection is ignored;
 * emissions-related OBD reports the subset registered in functional group
 * @ref UDS_DTC_FGID_EMISSIONS. An ECU with genuinely separate memories should
 * supply its own hook instead.
 */
int uds_dtc_store_list_mem_cb(struct uds_ctx *ctx, uds_dtc_memory_t memory, uint8_t mem_selection,
                              uint8_t status_mask, uds_dtc_record_t *out, uint16_t max);

/** fn_dtc_extdata_mem for 0x10/0x19; serves the same payload as
 *  uds_dtc_store_extdata_cb from the store's single record set. */
int uds_dtc_store_extdata_mem_cb(struct uds_ctx *ctx, uds_dtc_memory_t memory,
                                 uint8_t mem_selection, uint32_t dtc, uint8_t record_num,
                                 uint8_t *out_buf, uint16_t max_len);

#endif /* UDS_DTC_STORE_H */
