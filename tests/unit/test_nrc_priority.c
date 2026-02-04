/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* Mock Transport Send */
static int mock_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    check_expected_ptr(data);
    check_expected(len);
    return 0;
}

/* Dummy Handler */
static int dummy_handler(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    return UDS_OK;
}

static uint32_t mock_get_time(void)
{
    return 0;
}

/* 1. Verify NRC Priority: Subfunction (0x12) > Security (0x33) */
static void test_nrc_priority_sub_vs_security(void **state)
{
    (void) state;
    uint8_t rx_buf[64], tx_buf[64];

    /* Subfunction mask for sub 0x01 ONLY */
    static const uint8_t mask_sub_01[] = {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uds_service_entry_t user_services[] = {
        {0x44, 2, UDS_SESSION_ALL, 1, dummy_handler, mask_sub_01} /* Requires security level 1 */
    };

    uds_config_t cfg = {.fn_tp_send = mock_tp_send,
                        .rx_buffer = rx_buf,
                        .rx_buffer_size = 64,
                        .tx_buffer = tx_buf,
                        .tx_buffer_size = 64,
                        .user_services = user_services,
                        .user_service_count = 1,
                        .get_time_ms = mock_get_time};

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);
    ctx.security_level = 0; /* Security is locked */

    /* Case A: Request with VALID subfunction (0x01), but LOCKED security */
    /* Expect NRC 0x33 (Security Access Denied) */
    uint8_t req_valid_sub[] = {0x44, 0x01};
    uint8_t exp_nrc33[] = {0x7F, 0x44, 0x33};
    expect_memory(mock_tp_send, data, exp_nrc33, 3);
    expect_value(mock_tp_send, len, 3);
    uds_input_sdu(&ctx, req_valid_sub, 2);

    /* Case B: Request with INVALID subfunction (0x02), and LOCKED security */
    /* Expect NRC 0x12 (Subfunction Not Supported) - This is the Figure 10 requirement */
    uint8_t req_invalid_sub[] = {0x44, 0x02};
    uint8_t exp_nrc12[] = {0x7F, 0x44, 0x12};
    expect_memory(mock_tp_send, data, exp_nrc12, 3);
    expect_value(mock_tp_send, len, 3);
    uds_input_sdu(&ctx, req_invalid_sub, 2);
}

/* 2. Verify NRC Priority: Subfunction (0x12) > Length (0x13) */
static void test_nrc_priority_sub_vs_length(void **state)
{
    (void) state;
    uint8_t rx_buf[64], tx_buf[64];

    /* Subfunction mask for sub 0x01 ONLY */
    static const uint8_t mask_sub_01[] = {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uds_service_entry_t user_services[] = {
        {0x44, 4, UDS_SESSION_ALL, 0, dummy_handler, mask_sub_01} /* Requires length 4 */
    };

    uds_config_t cfg = {.fn_tp_send = mock_tp_send,
                        .rx_buffer = rx_buf,
                        .rx_buffer_size = 64,
                        .tx_buffer = tx_buf,
                        .tx_buffer_size = 64,
                        .user_services = user_services,
                        .user_service_count = 1,
                        .get_time_ms = mock_get_time};

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    /* Case: Request with INVALID subfunction (0x02) and INVALID length (length 2 instead of 4) */
    /* Expect NRC 0x12 (Subfunction Not Supported) */
    uint8_t req_both_invalid[] = {0x44, 0x02};
    uint8_t exp_nrc12[] = {0x7F, 0x44, 0x12};
    expect_memory(mock_tp_send, data, exp_nrc12, 3);
    expect_value(mock_tp_send, len, 3);
    uds_input_sdu(&ctx, req_both_invalid, 2);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_nrc_priority_sub_vs_security),
        cmocka_unit_test(test_nrc_priority_sub_vs_length),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
