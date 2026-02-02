#include "test_helpers.h"

static int mock_comm_control(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type)
{
    (void) ctx;
    (void) ctrl_type;
    (void) comm_type;
    return UDS_OK;
}

static void test_suppress_tester_present(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* 0xBE = 0x3E (Tester Present) with bit 7 set (Suppress Pos Response) */
    uint8_t request[] = {0x3E, 0x80};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* expect NO call to mock_tp_send */

    uds_input_sdu(&ctx, request, sizeof(request));
    assert_true(ctx.suppress_pos_resp == false); /* Should be cleared after processing */
}

static void test_suppress_comm_control(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_comm_control = mock_comm_control;

    /* 0x28 subfunction 0x00 (Enable Rx/Tx) with bit 7 set -> 0x80 */
    uint8_t request[] = {0x28, 0x80, 0x01};

    will_return(mock_get_time, 2000);
    will_return(mock_get_time, 2000);
    /* expect NO call to mock_tp_send */

    uds_input_sdu(&ctx, request, sizeof(request));
}

static void test_no_suppress_nrc(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* Request 0x3E with bit 7 set, but invalid subfunction 0x01 -> Should send NRC 0x12 */
    uint8_t request[] = {0x3E, 0x81};

    will_return(mock_get_time, 3000);
    will_return(mock_get_time, 3000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 7F 3E 12 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, request, sizeof(request));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_suppress_tester_present),
        cmocka_unit_test(test_suppress_comm_control),
        cmocka_unit_test(test_no_suppress_nrc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
