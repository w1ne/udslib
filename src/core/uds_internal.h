/**
 * @file uds_internal.h
 * @brief Internal UDS Core & Service Declarations
 */

#ifndef UDS_INTERNAL_H
#define UDS_INTERNAL_H

#include "uds/uds_core.h"

/* --- Internal Helpers --- */
void uds_internal_log(uds_ctx_t *ctx, uint8_t level, const char *msg);
const uds_did_entry_t *uds_internal_find_did(uds_ctx_t *ctx, uint16_t id);

/* --- Core Service Handlers --- */

/* Session Services (0x10, 0x3E) */
int uds_internal_handle_session_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_tester_present(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Data Services (0x22, 0x2E) */
int uds_internal_handle_read_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_write_data_by_id(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Security Services (0x27) */
int uds_internal_handle_security_access(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

/* Maintenance Services (0x11, 0x28) */
int uds_internal_handle_ecu_reset(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);
int uds_internal_handle_comm_control(uds_ctx_t *ctx, const uint8_t *data, uint16_t len);

#endif /* UDS_INTERNAL_H */
