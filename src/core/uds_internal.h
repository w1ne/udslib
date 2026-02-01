/**
 * @file uds_internal.h
 * @brief Internal UDS Core & Service Declarations
 */

#ifndef UDS_INTERNAL_H
#define UDS_INTERNAL_H

#include "uds/uds_core.h"

const uds_did_entry_t *uds_internal_find_did(uds_ctx_t *ctx, uint16_t id);
bool uds_internal_parse_addr_len(const uint8_t *data, uint16_t len, uint8_t format, uint32_t *addr, uint32_t *size);

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
