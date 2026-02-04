#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static int mock_io_callback(struct uds_ctx *ctx, uint16_t id, uint8_t type, const uint8_t *data,
                            uint16_t len, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) data;
    (void) len;
    (void) max_len;
    if (id == 0x0123 && type == 0x03) {
        out_buf[0] = 0x55;
        return 1;
    }
    return -0x31;
}

static void test_io_control_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    
    static const uds_did_entry_t dids[] = {
        {0x0123, 1, 0, 0, NULL, NULL, NULL},
    };
    cfg.did_table.entries = dids;
    cfg.did_table.count = 1;
    cfg.fn_io_control = mock_io_callback;

    /* 2F 01 23 03 (Short Term Adjustment) */
    uint8_t req[] = {0x2F, 0x01, 0x23, 0x03, 0xAA};

    /* 2 calls confirmed by failure analysis */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 4); /* 6F 01 23 55 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);
    assert_int_equal(g_tx_buf[0], 0x6F);
    assert_int_equal(g_tx_buf[3], 0x55);
}

static void test_io_control_did_not_found(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_io_control = mock_io_callback;

    /* 2F FF FF 03 */
    uint8_t req[] = {0x2F, 0xFF, 0xFF, 0x03};

    /* 2 calls confirmed by failure analysis */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 2F 31 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[2], 0x31);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_io_control_success),
        cmocka_unit_test(test_io_control_did_not_found),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
