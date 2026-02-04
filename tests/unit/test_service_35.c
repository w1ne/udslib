#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static uint32_t g_upload_addr = 0;
static uint32_t g_upload_size = 0;

static int mock_request_upload(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    g_upload_addr = addr;
    g_upload_size = size;
    return UDS_OK;
}

static void test_request_upload_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_upload = mock_request_upload;

    /* 35 00 22 11 22 33 44 (Format 0x22: 2 bytes addr, 2 bytes size) */
    uint8_t req[] = {0x35, 0x00, 0x22, 0x11, 0x22, 0x33, 0x44};

    /* 2 calls confirmed by consistency with 2F/2A */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);

    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6); /* 75 20 ... */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 7);
    assert_int_equal(g_tx_buf[0], 0x75);
    assert_int_equal(g_upload_addr, 0x1122);
    assert_int_equal(g_upload_size, 0x3344);
    assert_int_equal(ctx.flash_sequence, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_request_upload_success),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
