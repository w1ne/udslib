/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

static int mock_dtc_read(struct uds_ctx *ctx, uint8_t subfn, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) subfn;
    (void) max_len;
    out_buf[0] = 0xAA;
    return 1;
}

static void test_read_dtc_info_mask_missing(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_read = mock_dtc_read;

    /* C-10: Subfunction 0x01 requires a status mask. Total len 3. If len 2 -> NRC 0x13 */
    uint8_t req[] = {0x19, 0x01};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 19 13 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[2], 0x13);
}

static void test_control_dtc_setting_suppress(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* C-16: 0x85 0x81 (Suppress response) */
    uint8_t req[] = {0x85, 0x81};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* No tp_send expected for positive response */

    uds_input_sdu(&ctx, req, 2);
    assert_false(ctx.p2_msg_pending);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_dtc_info_mask_missing),
        cmocka_unit_test(test_control_dtc_setting_suppress),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
