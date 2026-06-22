/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static uint32_t g_download_addr = 0;
static uint32_t g_download_size = 0;
static uint8_t g_transfer_seq = 0;

static int mock_request_download(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    g_download_addr = addr;
    g_download_size = size;
    return UDS_OK;
}

static int mock_transfer_data(struct uds_ctx *ctx, uint8_t sequence, const uint8_t *data,
                              uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    g_transfer_seq = sequence;
    return UDS_OK;
}

static void test_request_download_alfid_invalid(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = mock_request_download;

    /* C-08: ALFID 0x01 (1 byte addr, 0 byte size) -> Reject with NRC 0x31 */
    uint8_t req[] = {0x34, 0x00, 0x01, 0x00};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 34 31 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x31);
}

static void test_transfer_data_sequence_error(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data;

    /* Sequence counter starts at 0x01. If 0x02 received first (inside an active
     * transfer) -> NRC 0x73 wrongBlockSequenceCounter (ISO 14229-1). */
    uint8_t req[] = {0x36, 0x02, 0xDE, 0xAD};
    ctx.transfer_active = true; /* as if RequestDownload already accepted */
    ctx.flash_sequence = 0;

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 36 73 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x73);
}

static void test_transfer_data_last_block_replay(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data;
    cfg.transfer_accept_last_block_replay = true;

    /* First block (0x01) */
    uint8_t req1[] = {0x36, 0x01, 0xDE, 0xAD};
    ctx.transfer_active = true; /* as if RequestDownload already accepted */
    ctx.flash_sequence = 0;

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 76 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req1, 4);
    assert_int_equal(ctx.flash_sequence, 0x01);

    /* Repeat block (0x01) - Should be accepted without re-invoking callback increment or sequence
     * error */
    uint8_t req2[] = {0x36, 0x01, 0xDE, 0xAD};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 76 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req2, 4);
    assert_int_equal(ctx.flash_sequence, 0x01);
}

static int mock_transfer_exit(struct uds_ctx *ctx)
{
    (void) ctx;
    return UDS_OK;
}

/* TransferData (0x36) before any RequestDownload/Upload must be rejected with
 * requestSequenceError (0x24) and must not reach the application callback. */
static void test_transfer_data_requires_active_transfer(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data;

    uint8_t req[] = {0x36, 0x01, 0xDE, 0xAD};
    g_transfer_seq = 0xFFu; /* sentinel: callback must not run */

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 36 24 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[0], 0x7F);
    assert_int_equal(g_tx_buf[1], 0x36);
    assert_int_equal(g_tx_buf[2], 0x24);
    assert_int_equal(g_transfer_seq, 0xFFu); /* fn_transfer_data not invoked */
}

/* RequestDownload arms the transfer; TransferExit disarms it, so a subsequent
 * TransferData is again rejected with 0x24. */
static void test_transfer_exit_disarms_transfer(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = mock_request_download;
    cfg.fn_transfer_data = mock_transfer_data;
    cfg.fn_transfer_exit = mock_transfer_exit;

    /* RequestDownload (ALFID 0x11: 1-byte addr + 1-byte size) */
    uint8_t dl[] = {0x34, 0x00, 0x11, 0x20, 0x10};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, dl, sizeof(dl));
    assert_int_equal(g_tx_buf[0], 0x74);
    assert_true(ctx.transfer_active);

    /* First TransferData block accepted */
    uint8_t td[] = {0x36, 0x01, 0xDE, 0xAD};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, td, sizeof(td));
    assert_int_equal(g_tx_buf[0], 0x76);

    /* TransferExit disarms */
    uint8_t te[] = {0x37};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, te, sizeof(te));
    assert_int_equal(g_tx_buf[0], 0x77);
    assert_false(ctx.transfer_active);

    /* TransferData after exit -> 0x24 again */
    uint8_t td2[] = {0x36, 0x02, 0xBE, 0xEF};
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3);
    will_return(mock_tp_send, 0);
    uds_input_sdu(&ctx, td2, sizeof(td2));
    assert_int_equal(g_tx_buf[2], 0x24);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_request_download_alfid_invalid),
        cmocka_unit_test(test_transfer_data_sequence_error),
        cmocka_unit_test(test_transfer_data_last_block_replay),
        cmocka_unit_test(test_transfer_data_requires_active_transfer),
        cmocka_unit_test(test_transfer_exit_disarms_transfer),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
