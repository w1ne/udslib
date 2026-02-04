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

    /* C-13: Sequence counter starts at 0x01. If 0x02 received first -> NRC 0x24 */
    uint8_t req[] = {0x36, 0x02, 0xDE, 0xAD};
    ctx.flash_sequence = 0;

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 36 24 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x24);
}

static void test_transfer_data_last_block_replay(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_transfer_data = mock_transfer_data;
    cfg.transfer_accept_last_block_replay = true;

    /* First block (0x01) */
    uint8_t req1[] = {0x36, 0x01, 0xDE, 0xAD};
    ctx.flash_sequence = 0;

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 76 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req1, 4);
    assert_int_equal(ctx.flash_sequence, 0x01);

    /* Repeat block (0x01) - Should be accepted without re-invoking callback increment or sequence error */
    uint8_t req2[] = {0x36, 0x01, 0xDE, 0xAD};
    
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 76 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req2, 4);
    assert_int_equal(ctx.flash_sequence, 0x01);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_request_download_alfid_invalid),
        cmocka_unit_test(test_transfer_data_sequence_error),
        cmocka_unit_test(test_transfer_data_last_block_replay),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
