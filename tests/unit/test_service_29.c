/**
 * @file test_service_29.c
 * @brief Unit tests for Authentication (SID 0x29)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "test_helpers.h"

static int mock_auth_callback(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *data, uint16_t len,
                              uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) data;
    (void) len;
    (void) max_len;

    if (subfn == UDS_AUTH_DEAUTHENTICATE) {
        return 0; /* Success, no payload */
    }

    if (subfn == UDS_AUTH_VERIFY_CERT_UNI) {
        /* Mock certificate verification response */
        out_buf[0] = 0x01; /* Evaluation Status: Valid */
        return 1;
    }

    return -0x22; /* Conditions Not Correct for other sub-functions */
}

static void test_auth_deauthenticate_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_auth = mock_auth_callback;

    uint8_t req[] = {0x29, 0x01}; /* deAuthenticate */

    will_return(mock_get_time, 1000); /* input_sdu */
    will_return(mock_get_time, 1000); /* dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 2); /* 0x69 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[0], 0x69);
    assert_int_equal(g_tx_buf[1], 0x01);
}

static void test_auth_verify_cert_uni_success(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_auth = mock_auth_callback;

    uint8_t req[] = {0x29, 0x02, 0xAA, 0xBB}; /* verifyCertificateUnidirectional + dummy cert */

    will_return(mock_get_time, 1000); /* input_sdu */
    will_return(mock_get_time, 1000); /* dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 0x69 02 01 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);
    assert_int_equal(g_tx_buf[0], 0x69);
    assert_int_equal(g_tx_buf[1], 0x02);
    assert_int_equal(g_tx_buf[2], 0x01);
}

static void test_auth_no_callback_nrc(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    /* cfg.fn_auth is NULL by default via memset in setup_ctx */

    uint8_t req[] = {0x29, 0x01};

    will_return(mock_get_time, 1000); /* input_sdu */
    will_return(mock_get_time, 1000); /* dispatcher */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 3); /* 0x7F 29 22 */
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);
    assert_int_equal(g_tx_buf[2], 0x22);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_auth_deauthenticate_success),
        cmocka_unit_test(test_auth_verify_cert_uni_success),
        cmocka_unit_test(test_auth_no_callback_nrc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
