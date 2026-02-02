#include "test_helpers.h"

static int mock_download(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    check_expected(addr);
    check_expected(size);
    return (int) mock();
}

static void test_request_download_flex_44(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = mock_download;

    /* 0x34 f=0x00 al=0x44 addr=0x11223344 size=0xAABBCCDD */
    uint8_t request[] = {0x34, 0x00, 0x44, 0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_value(mock_download, addr, 0x11223344);
    expect_value(mock_download, size, 0xAABBCCDD);
    will_return(mock_download, UDS_OK);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));
}

static void test_request_download_flex_21(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_request_download = mock_download;

    /* 0x34 f=0x00 al=0x12 addr=0x1234 size=0xCC */
    uint8_t request[] = {0x34, 0x00, 0x12, 0x12, 0x34, 0xCC};

    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    expect_value(mock_download, addr, 0x1234);
    expect_value(mock_download, size, 0xCC);
    will_return(mock_download, UDS_OK);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_request_download_flex_44),
        cmocka_unit_test(test_request_download_flex_21),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
