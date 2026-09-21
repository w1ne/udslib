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

/**
 * @brief Calendar stamp stored in a DTC snapshot (freeze frame).
 *
 * Field order matches the usual ECU snapshot block: second through year.
 * ReadDTCInformation 0x04 emits DID 0x1001 as year, month, day, hour,
 * minute, second.
 */
typedef struct
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year; /**< Years since 2000. */
} uds_dtc_snapshot_time_t;

/**
 * @brief Global DTC snapshot captured when a fault is stored.
 *
 * Supply voltage and power mode are manufacturer-scaled (the reference
 * store does not interpret them). ReadDTCInformation 0x04 emits DID 0x1002
 * as voltage then power mode.
 */
typedef struct
{
    uint8_t voltage;
    uint8_t power_mode;
    uds_dtc_snapshot_time_t time;
} uds_dtc_snapshot_t;

/**
 * @brief Extended data counters served by ReadDTCInformation 0x06.
 *
 * One record (number 0x01): occurrence, pending, aged, then ageing.
 * `ageing_counter` counts clean operation cycles since the last fault and
 * is the same value as @ref uds_dtc_record_t::aging_counter.
 */
typedef struct
{
    uint8_t fault_occur_counter;   /**< Confirmed occurrences, one per op cycle. */
    uint8_t fault_pending_counter; /**< 1 while the fault is pending, else 0. */
    uint8_t aged_counter;          /**< Times the DTC has aged out. */
    uint8_t ageing_counter;        /**< Clean op cycles toward aging out. */
} uds_dtc_extended_data_t;

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
