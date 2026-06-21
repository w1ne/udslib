/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_did_ids.h
 * @brief Standardized Data Identifier (DID) constants for ReadDataByIdentifier
 *        (SID 0x22) and WriteDataByIdentifier (SID 0x2E).
 *
 * These are the ISO 14229-1 "identification" Data Identifiers in the 0xF180 -
 * 0xF19F range. They name *which* datum is being addressed; the library does
 * not store the values behind them. DIDs are application-owned: each ECU
 * supplies its own VIN, serial number, fingerprints, etc. via its
 * @ref uds_did_table_t. Use these constants so a DID table reads as
 *
 *     {UDS_DID_VIN, 17, 0, 0, NULL, NULL, my_vin},
 *
 * instead of carrying bare hex literals.
 *
 * @see uds_config.h for uds_did_entry_t / uds_did_table_t.
 */

#ifndef UDS_DID_IDS_H
#define UDS_DID_IDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- ISO 14229-1 standardized identification DIDs (0xF180 - 0xF19F) --- */

/** Boot Software Identification. */
#define UDS_DID_BOOT_SOFTWARE_IDENTIFICATION 0xF180u
/** Application Software Identification. */
#define UDS_DID_APPLICATION_SOFTWARE_IDENTIFICATION 0xF181u
/** Application Data Identification. */
#define UDS_DID_APPLICATION_DATA_IDENTIFICATION 0xF182u
/** Boot Software Fingerprint. */
#define UDS_DID_BOOT_SOFTWARE_FINGERPRINT 0xF183u
/** Application Software Fingerprint. */
#define UDS_DID_APPLICATION_SOFTWARE_FINGERPRINT 0xF184u
/** Application Data Fingerprint. */
#define UDS_DID_APPLICATION_DATA_FINGERPRINT 0xF185u
/** Active Diagnostic Session. */
#define UDS_DID_ACTIVE_DIAGNOSTIC_SESSION 0xF186u
/** Vehicle Manufacturer Spare Part Number. */
#define UDS_DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER 0xF187u
/** Vehicle Manufacturer ECU Software Number. */
#define UDS_DID_VEHICLE_MANUFACTURER_ECU_SOFTWARE_NUMBER 0xF188u
/** Vehicle Manufacturer ECU Software Version Number. */
#define UDS_DID_VEHICLE_MANUFACTURER_ECU_SOFTWARE_VERSION_NUMBER 0xF189u
/** System Supplier Identifier. */
#define UDS_DID_SYSTEM_SUPPLIER_IDENTIFIER 0xF18Au
/** ECU Manufacturing Date. */
#define UDS_DID_ECU_MANUFACTURING_DATE 0xF18Bu
/** ECU Serial Number. */
#define UDS_DID_ECU_SERIAL_NUMBER 0xF18Cu
/** Supported Functional Units. */
#define UDS_DID_SUPPORTED_FUNCTIONAL_UNITS 0xF18Du
/** Vehicle Manufacturer Kit Assembly Part Number. */
#define UDS_DID_VEHICLE_MANUFACTURER_KIT_ASSEMBLY_PART_NUMBER 0xF18Eu
/** ISO/SAE Reserved Standardized. */
#define UDS_DID_ISO_SAE_RESERVED_STANDARDIZED 0xF18Fu
/** Vehicle Identification Number (VIN). */
#define UDS_DID_VIN 0xF190u
/** Vehicle Manufacturer ECU Hardware Number. */
#define UDS_DID_VEHICLE_MANUFACTURER_ECU_HARDWARE_NUMBER 0xF191u
/** System Supplier ECU Hardware Number. */
#define UDS_DID_SYSTEM_SUPPLIER_ECU_HARDWARE_NUMBER 0xF192u
/** System Supplier ECU Hardware Version Number. */
#define UDS_DID_SYSTEM_SUPPLIER_ECU_HARDWARE_VERSION_NUMBER 0xF193u
/** System Supplier ECU Software Number. */
#define UDS_DID_SYSTEM_SUPPLIER_ECU_SOFTWARE_NUMBER 0xF194u
/** System Supplier ECU Software Version Number. */
#define UDS_DID_SYSTEM_SUPPLIER_ECU_SOFTWARE_VERSION_NUMBER 0xF195u
/** Exhaust Regulation Or Type Approval Number. */
#define UDS_DID_EXHAUST_REGULATION_OR_TYPE_APPROVAL_NUMBER 0xF196u
/** System Name Or Engine Type. */
#define UDS_DID_SYSTEM_NAME_OR_ENGINE_TYPE 0xF197u
/** Repair Shop Code Or Tester Serial Number. */
#define UDS_DID_REPAIR_SHOP_CODE_OR_TESTER_SERIAL_NUMBER 0xF198u
/** Programming Date. */
#define UDS_DID_PROGRAMMING_DATE 0xF199u
/** Calibration Repair Shop Code Or Calibration Equipment Serial Number. */
#define UDS_DID_CALIBRATION_REPAIR_SHOP_CODE_OR_EQUIPMENT_SERIAL_NUMBER 0xF19Au
/** Calibration Date. */
#define UDS_DID_CALIBRATION_DATE 0xF19Bu
/** Calibration Equipment Software Number. */
#define UDS_DID_CALIBRATION_EQUIPMENT_SOFTWARE_NUMBER 0xF19Cu
/** ECU Installation Date. */
#define UDS_DID_ECU_INSTALLATION_DATE 0xF19Du
/** ODX File. */
#define UDS_DID_ODX_FILE 0xF19Eu
/** Entity. */
#define UDS_DID_ENTITY 0xF19Fu

#ifdef __cplusplus
}
#endif

#endif /* UDS_DID_IDS_H */
