/**
 * @file uds_internal.h
 * @brief Internal UDS Core & Service Declarations
 */

#ifndef UDS_INTERNAL_H
#define UDS_INTERNAL_H

#include "uds/uds_core.h"

/* --- UDS Constants (ISO 14229-1) --- */

#define UDS_NRC_SERVICE_NOT_SUPPORTED           0x11u
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED       0x12u
#define UDS_NRC_INCORRECT_LENGTH                0x13u
#define UDS_NRC_RESPONSE_TOO_LONG               0x14u
#define UDS_NRC_BUSY_REPEAT_REQUEST             0x21u
#define UDS_NRC_CONDITIONS_NOT_CORRECT          0x22u
#define UDS_NRC_REQUEST_SEQUENCE_ERROR          0x24u
#define UDS_NRC_REQUEST_OUT_OF_RANGE            0x31u
#define UDS_NRC_SECURITY_ACCESS_DENIED          0x33u
#define UDS_NRC_INVALID_KEY                     0x35u
#define UDS_NRC_EXCEEDED_ATTEMPTS               0x36u
#define UDS_NRC_REQUIRED_TIME_DELAY             0x37u
#define UDS_NRC_RESPONSE_PENDING                0x78u
#define UDS_NRC_SERVICE_NOT_SUPP_IN_SESS        0x7Fu

#define UDS_SID_SESSION_CONTROL                 0x10u
#define UDS_SID_ECU_RESET                       0x11u
#define UDS_SID_CLEAR_DTC                       0x14u
#define UDS_SID_READ_DTC_INFO                   0x19u
#define UDS_SID_READ_DATA_BY_ID                 0x22u
#define UDS_SID_READ_MEM_BY_ADDR                0x23u
#define UDS_SID_SECURITY_ACCESS                 0x27u
#define UDS_SID_COMM_CONTROL                    0x28u
#define UDS_SID_AUTHENTICATION                  0x29u
#define UDS_SID_WRITE_DATA_BY_ID                0x2Eu
#define UDS_SID_ROUTINE_CONTROL                 0x31u
#define UDS_SID_REQUEST_DOWNLOAD                0x34u
#define UDS_SID_TRANSFER_DATA                   0x36u
#define UDS_SID_TRANSFER_EXIT                   0x37u
#define UDS_SID_WRITE_MEM_BY_ADDR               0x3Du
#define UDS_SID_TESTER_PRESENT                  0x3Eu
#define UDS_SID_CONTROL_DTC_SETTING             0x85u

#define UDS_S3_TIMEOUT_MS                       5000u
#define UDS_P2_MIN_SAFE_MS                      20u
#define UDS_P2_STAR_MIN_SAFE_MS                 1000u

#define UDS_RESPONSE_OFFSET                     0x40u

/* Protocol Bitmasks */
#define UDS_MASK_NIBBLE                         0x0Fu
#define UDS_MASK_SUBFUNCTION                    0x7Fu
#define UDS_MASK_SUPPRESS_POS_RESP              0x80u

/* Session IDs (ISO 14229-1) */
#define UDS_SESSION_ID_DEFAULT                  0x01u
#define UDS_SESSION_ID_PROGRAMMING              0x02u
#define UDS_SESSION_ID_EXTENDED                 0x03u

/* Subfunction Masks (16 bytes) */
#define UDS_MASK_SUB_10 {0x0Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define UDS_MASK_SUB_11 {0x0Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define UDS_MASK_SUB_19 {0x57u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define UDS_MASK_SUB_27 {0xFEu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}
#define UDS_MASK_SUB_28 {0x3Fu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define UDS_MASK_SUB_31 {0x0Eu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define UDS_MASK_SUB_3E {0x01u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define UDS_MASK_SUB_85 {0x06u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

const uds_did_entry_t *uds_internal_find_did(uds_ctx_t *ctx, uint16_t id);
bool uds_internal_parse_addr_len(const uint8_t *data, uint16_t len, uint8_t format, uint32_t *addr, uint32_t *size);
void uds_internal_log(uds_ctx_t *ctx, uint8_t level, const char *msg);

/* --- Core Service Handlers --- */

/* Session Services (0x10, 0x3E) */
int uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_tester_present(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Data Services (0x22, 0x2E) */
int uds_internal_handle_read_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
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

/* Memory Services (0x23, 0x3D) */
int uds_internal_handle_read_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_write_memory_by_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

#endif /* UDS_INTERNAL_H */
