/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef UDS_DTC_H
#define UDS_DTC_H

#include <stdint.h>

/* statusOfDTC bits (ISO 14229-1 Annex D) */
#define UDS_DTC_STATUS_TEST_FAILED 0x01u
#define UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE 0x02u
#define UDS_DTC_STATUS_PENDING 0x04u
#define UDS_DTC_STATUS_CONFIRMED 0x08u
#define UDS_DTC_STATUS_TEST_NOT_COMPLETED_SINCE_CLEAR 0x10u
#define UDS_DTC_STATUS_TEST_FAILED_SINCE_CLEAR 0x20u
#define UDS_DTC_STATUS_TEST_NOT_COMPLETED_THIS_OP_CYCLE 0x40u
#define UDS_DTC_STATUS_WARNING_INDICATOR_REQUESTED 0x80u

/* DTCSeverity bits (ISO 14229-1) */
#define UDS_DTC_SEVERITY_MAINTENANCE_ONLY 0x20u
#define UDS_DTC_SEVERITY_CHECK_AT_NEXT_HALT 0x40u
#define UDS_DTC_SEVERITY_CHECK_IMMEDIATELY 0x80u

/* FunctionalGroupIdentifier values (ISO 14229-1) */
#define UDS_DTC_FGID_EMISSIONS 0x33u
#define UDS_DTC_FGID_SAFETY 0xD0u
#define UDS_DTC_FGID_VOBD 0xFEu

/** DTC category from the top two bits of the high DTC byte. */
typedef enum
{
    UDS_DTC_POWERTRAIN = 0, /**< P, bit pattern 00 */
    UDS_DTC_CHASSIS = 1,    /**< C, bit pattern 01 */
    UDS_DTC_BODY = 2,       /**< B, bit pattern 10 */
    UDS_DTC_NETWORK = 3     /**< U, bit pattern 11 */
} uds_dtc_category_t;

/**
 * @brief Classify a 3-byte DTC as Powertrain/Chassis/Body/Network.
 * @param dtc 3-byte DTC, right-aligned.
 * @return Category from bits 23..22 of the DTC.
 */
static inline uds_dtc_category_t uds_dtc_category(uint32_t dtc)
{
    return (uds_dtc_category_t) ((dtc >> 22) & 0x3u);
}

#endif /* UDS_DTC_H */
