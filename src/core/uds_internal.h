/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_internal.h
 * @brief Internal UDS Core & Service Declarations
 */

#ifndef UDS_INTERNAL_H
#define UDS_INTERNAL_H

#include "uds/uds_core.h"

/* --- UDS Constants (ISO 14229-1) --- */

#define UDS_NRC_SERVICE_NOT_SUPPORTED 0x11u
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED 0x12u
#define UDS_NRC_INCORRECT_LENGTH 0x13u
#define UDS_NRC_RESPONSE_TOO_LONG 0x14u
#define UDS_NRC_BUSY_REPEAT_REQUEST 0x21u
#define UDS_NRC_CONDITIONS_NOT_CORRECT 0x22u
#define UDS_NRC_REQUEST_SEQUENCE_ERROR 0x24u
#define UDS_NRC_REQUEST_OUT_OF_RANGE 0x31u
#define UDS_NRC_SECURITY_ACCESS_DENIED 0x33u
#define UDS_NRC_AUTHENTICATION_REQUIRED 0x34u
#define UDS_NRC_INVALID_KEY 0x35u
#define UDS_NRC_EXCEEDED_ATTEMPTS 0x36u
#define UDS_NRC_REQUIRED_TIME_DELAY 0x37u
#define UDS_NRC_RESPONSE_PENDING 0x78u
#define UDS_NRC_SUBFUNC_NOT_SUPP_IN_SESS 0x7Eu
#define UDS_NRC_SERVICE_NOT_SUPP_IN_SESS 0x7Fu

#define UDS_SID_SESSION_CONTROL 0x10u
#define UDS_SID_ECU_RESET 0x11u
#define UDS_SID_CLEAR_DTC 0x14u
#define UDS_SID_READ_DTC_INFO 0x19u
#define UDS_SID_READ_DATA_BY_ID 0x22u
#define UDS_SID_READ_MEM_BY_ADDR 0x23u
#define UDS_SID_READ_SCALING 0x24u
#define UDS_SID_SECURITY_ACCESS 0x27u
#define UDS_SID_COMM_CONTROL 0x28u
#define UDS_SID_AUTHENTICATION 0x29u
#define UDS_SID_READ_BY_PER_ID 0x2Au
#define UDS_SID_DYNAMIC_DID 0x2Cu
#define UDS_SID_WRITE_DATA_BY_ID 0x2Eu
#define UDS_SID_IO_CONTROL_BY_ID 0x2Fu
#define UDS_SID_ROUTINE_CONTROL 0x31u
#define UDS_SID_REQUEST_DOWNLOAD 0x34u
#define UDS_SID_REQUEST_UPLOAD 0x35u
#define UDS_SID_TRANSFER_DATA 0x36u
#define UDS_SID_TRANSFER_EXIT 0x37u
#define UDS_SID_REQUEST_FILE_TRANSFER 0x38u
#define UDS_SID_WRITE_MEM_BY_ADDR 0x3Du
#define UDS_SID_TESTER_PRESENT 0x3Eu
#define UDS_SID_CONTROL_DTC_SETTING 0x85u
#define UDS_SID_ACCESS_TIMING 0x83u
#define UDS_SID_LINK_CONTROL 0x87u
#define UDS_SID_SECURED_DATA_TRANS 0x84u
#define UDS_SID_RESPONSE_ON_EVENT 0x86u

#define UDS_S3_TIMEOUT_MS 5000u
#define UDS_P2_MIN_SAFE_MS 20u
#define UDS_P2_STAR_MIN_SAFE_MS 1000u

#define UDS_RESPONSE_OFFSET 0x40u
#define UDS_MAX_PERIODIC_MSG_LEN 8u

#define UDS_PERIODIC_RATE_FAST 0x01u
#define UDS_PERIODIC_RATE_MEDIUM 0x02u
#define UDS_PERIODIC_RATE_SLOW 0x03u

#define UDS_PERIODIC_FAST_INTERVAL_MS 100u
#define UDS_PERIODIC_MEDIUM_INTERVAL_MS 500u
#define UDS_PERIODIC_SLOW_INTERVAL_MS 1000u

/* Protocol Bitmasks */
#define UDS_MASK_NIBBLE 0x0Fu
#define UDS_MASK_SUBFUNCTION 0x7Fu
#define UDS_MASK_SUPPRESS_POS_RESP 0x80u

/* Session IDs (ISO 14229-1) */
#define UDS_SESSION_ID_DEFAULT 0x01u
#define UDS_SESSION_ID_PROGRAMMING 0x02u
#define UDS_SESSION_ID_EXTENDED 0x03u
#define UDS_SESSION_ID_SAFETY 0x04u

/* Subfunction Masks (16 bytes) */
/* Allowed 0x10 subfunctions: 0x1E = 0x01/0x02/0x03/0x04
 * (default/programming/extended/safetySystem). */
#define UDS_MASK_SUB_10                                    \
    {                                                      \
        0x1Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_11                                    \
    {                                                      \
        0x0Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
/* Allowed 0x19 subfunctions: all standard sub-functions 0x00–0x19 plus 0x42 and 0x55.
 * byte0 0xFF = 0x00–0x07; byte1 0xFF = 0x08–0x0F; byte2 0xFF = 0x10–0x17;
 * byte3 0x03 = 0x18/0x19; byte8 0x04 = 0x42; byte10 0x20 = 0x55. */
#define UDS_MASK_SUB_19                                                        \
    {                                                                          \
        0xFFu, 0xFFu, 0xFFu, 0x03u, 0, 0, 0, 0, 0x04u, 0, 0x20u, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_27                                                                            \
    {                                                                                              \
        0xFEu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, \
            0xFFu, 0xFFu, 0xFFu                                                                    \
    }
#define UDS_MASK_SUB_28                                    \
    {                                                      \
        0x3Fu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
/* 0x29 Authentication sub-functions 0x00-0x08: byte0 0xFF + byte1 0x01 (0x08). */
#define UDS_MASK_SUB_29                                        \
    {                                                          \
        0xFFu, 0x01u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_31                                    \
    {                                                      \
        0x0Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_3E                                    \
    {                                                      \
        0x01u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_85                                    \
    {                                                      \
        0x06u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_2A                                    \
    {                                                      \
        0x1Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
/* 0x2C subfunctions 0x01/0x02/0x03 -> bits 1,2,3 = 0x0E. */
#define UDS_MASK_SUB_2C                                    \
    {                                                      \
        0x0Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
/* 0x86 subfunctions 0x00-0x07 -> 0xFF. */
#define UDS_MASK_SUB_86                                    \
    {                                                      \
        0xFFu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_87                                    \
    {                                                      \
        0x0Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }
#define UDS_MASK_SUB_83                                    \
    {                                                      \
        0x1Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 \
    }

uint8_t uds_internal_session_bit(uint8_t session);
uint8_t uds_internal_strict_session_mask(uint8_t sid);
const uds_did_entry_t *uds_internal_find_did(uds_ctx_t *ctx, uint16_t id);
bool uds_internal_parse_addr_len(const uint8_t *data, uint16_t len, uint8_t format, uint32_t *addr,
                                 uint32_t *size);
void uds_internal_log(uds_ctx_t *ctx, uint8_t level, const char *msg);
int uds_emit_response(uds_ctx_t *ctx, uint16_t len);

/* --- Core Service Handlers --- */

/* Session Services (0x10, 0x3E) */
int uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_tester_present(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Data Services (0x22, 0x24, 0x2E) */
int uds_internal_handle_read_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_read_scaling(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_dynamic_did(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_write_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Security Services (0x27, 0x29) */
int uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_authentication(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Maintenance Services (0x11, 0x14, 0x19, 0x28, 0x85) */
int uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_clear_dtc(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_read_dtc_info(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_control_dtc_setting(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Flash Services (0x31, 0x34, 0x36, 0x37) */
int uds_internal_handle_routine_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_request_download(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_transfer_data(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_request_transfer_exit(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_request_file_transfer(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Memory Services (0x23, 0x3D) */
int uds_internal_handle_read_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_write_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* New Services (0x2A, 0x2F, 0x35) */
int uds_internal_handle_periodic_read(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_io_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_request_upload(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Reprogramming-negotiation Services (0x83, 0x87) */
int uds_internal_handle_link_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Secured Data Transmission (0x84) */
int uds_internal_handle_secured_data(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* ResponseOnEvent (0x86) */
int uds_internal_handle_response_on_event(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
void uds_internal_roe_service(uds_ctx_t *ctx, uint32_t now);

/* Run an inner request through the dispatcher, capturing its response into
 * @p out instead of sending it. Returns the captured length. Used by 0x86
 * (and the 0x84 secured path uses the same capture machinery). */
int uds_internal_dispatch_captured(uds_ctx_t *ctx, const uint8_t *inner, uint16_t inner_len,
                                   uint8_t *out, uint16_t out_size);
int uds_internal_handle_access_timing(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

#endif /* UDS_INTERNAL_H */
