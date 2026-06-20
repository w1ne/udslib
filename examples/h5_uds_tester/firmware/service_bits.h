/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file service_bits.h
 * @brief Per-service result bit positions for the all-services gate.
 *
 * 27 ISO-14229-1 services indexed by their document order (SID ascending).
 * The accumulated mask is written to g_service_results at 0x20010000.
 * ALL_SERVICES_MASK = bits 0-26 (27 services).
 */

#ifndef SERVICE_BITS_H
#define SERVICE_BITS_H

/* bit  0 */ #define BIT_10 (1u << 0u)  /* DiagnosticSessionControl    0x10 */
/* bit  1 */ #define BIT_11 (1u << 1u)  /* EcuReset                    0x11 */
/* bit  2 */ #define BIT_14 (1u << 2u)  /* ClearDiagnosticInformation  0x14 */
/* bit  3 */ #define BIT_19 (1u << 3u)  /* ReadDTCInformation          0x19 */
/* bit  4 */ #define BIT_22 (1u << 4u)  /* ReadDataByIdentifier        0x22 */
/* bit  5 */ #define BIT_23 (1u << 5u)  /* ReadMemoryByAddress         0x23 */
/* bit  6 */ #define BIT_24 (1u << 6u)  /* ReadScalingDataByIdentifier 0x24 */
/* bit  7 */ #define BIT_27 (1u << 7u)  /* SecurityAccess              0x27 */
/* bit  8 */ #define BIT_28 (1u << 8u)  /* CommunicationControl        0x28 */
/* bit  9 */ #define BIT_29 (1u << 9u)  /* Authentication              0x29 */
/* bit 10 */ #define BIT_2A (1u << 10u) /* ReadDataByPeriodicIdentifier 0x2A */
/* bit 11 */ #define BIT_2C (1u << 11u) /* DynamicallyDefineDataIdentifier 0x2C */
/* bit 12 */ #define BIT_2E (1u << 12u) /* WriteDataByIdentifier       0x2E */
/* bit 13 */ #define BIT_2F (1u << 13u) /* InputOutputControlByIdentifier 0x2F */
/* bit 14 */ #define BIT_31 (1u << 14u) /* RoutineControl              0x31 */
/* bit 15 */ #define BIT_34 (1u << 15u) /* RequestDownload             0x34 */
/* bit 16 */ #define BIT_35 (1u << 16u) /* RequestUpload               0x35 */
/* bit 17 */ #define BIT_36 (1u << 17u) /* TransferData                0x36 */
/* bit 18 */ #define BIT_37 (1u << 18u) /* RequestTransferExit         0x37 */
/* bit 19 */ #define BIT_38 (1u << 19u) /* RequestFileTransfer         0x38 */
/* bit 20 */ #define BIT_3D (1u << 20u) /* WriteMemoryByAddress        0x3D */
/* bit 21 */ #define BIT_3E (1u << 21u) /* TesterPresent               0x3E */
/* bit 22 */ #define BIT_83 (1u << 22u) /* AccessTimingParameter       0x83 */
/* bit 23 */ #define BIT_84 (1u << 23u) /* SecuredDataTransmission     0x84 */
/* bit 24 */ #define BIT_85 (1u << 24u) /* ControlDTCSetting           0x85 */
/* bit 25 */ #define BIT_86 (1u << 25u) /* ResponseOnEvent             0x86 */
/* bit 26 */ #define BIT_87 (1u << 26u) /* LinkControl                 0x87 */

/** All 27 services passing. */
#define ALL_SERVICES_MASK 0x07FFFFFFu

#endif /* SERVICE_BITS_H */
