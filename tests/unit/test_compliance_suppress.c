/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "test_helpers.h"

static int mock_comm_control(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type,
                             uint16_t node_id)
{
    (void) ctx;
    (void) ctrl_type;
    (void) comm_type;
    (void) node_id;
    return UDS_OK;
}

static int mock_security_seed_suppress(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf,
                                       uint16_t max_len)
{
    (void) ctx;
    (void) level;
    (void) max_len;
    seed_buf[0] = 0xAA;
    seed_buf[1] = 0xBB;
    return 2;
}

static int mock_routine_control_suppress(struct uds_ctx *ctx, uint8_t type, uint16_t id,
                                         const uint8_t *data, uint16_t len, uint8_t *out,
                                         uint16_t max_out)
{
    (void) ctx;
    (void) type;
    (void) id;
    (void) data;
    (void) len;
    (void) out;
    (void) max_out;
    return 0;
}

static int mock_link_control_suppress(struct uds_ctx *ctx, uint8_t subfn, uint32_t param)
{
    (void) ctx;
    (void) subfn;
    (void) param;
    return 0;
}

static int mock_dynamic_did_suppress(uds_ctx_t *ctx, uint8_t subfn, uint16_t defined_did,
                                     const uint8_t *data, uint16_t len)
{
    (void) ctx;
    (void) subfn;
    (void) defined_did;
    (void) data;
    (void) len;
    return 0;
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
    assert_true(ctx.scratch.suppress_pos_resp == false); /* Should be cleared after processing */
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

/* 0x10 DiagnosticSessionControl: sub 0x03 (extended) with bit 7 set */
static void test_suppress_session_control(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* 0x83 = 0x03 | 0x80 */
    uint8_t req[] = {0x10, 0x83};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* NO expect_*(mock_tp_send): positive response must be suppressed */
    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 0x27 SecurityAccess: requestSeed sub 0x01 with bit 7 set */
static void test_suppress_security_access(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_security_seed = mock_security_seed_suppress;

    /* 0x81 = 0x01 | 0x80 */
    uint8_t req[] = {0x27, 0x81};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    will_return(mock_get_time, 1000); /* handler: security delay check */
    /* NO expect_*(mock_tp_send): positive response must be suppressed */
    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 0x2C DynamicDefineDID: clearDynamically sub 0x03 with bit 7 set */
static void test_suppress_dynamic_define_did(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dynamic_did = mock_dynamic_did_suppress;

    /* 0x83 = 0x03 | 0x80 */
    uint8_t req[] = {0x2C, 0x83};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* NO expect_*(mock_tp_send): positive response must be suppressed */
    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 0x31 RoutineControl: startRoutine sub 0x01 with bit 7 set */
static void test_suppress_routine_control(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_routine_control = mock_routine_control_suppress;

    /* 0x81 = 0x01 | 0x80; routine id = 0xFF00 */
    uint8_t req[] = {0x31, 0x81, 0xFF, 0x00};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* NO expect_*(mock_tp_send): positive response must be suppressed */
    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 0x83 AccessTimingParameter: setTimingParametersToDefaultValues sub 0x02 with bit 7 set */
static void test_suppress_access_timing(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    /* 0x82 = 0x02 | 0x80 */
    uint8_t req[] = {0x83, 0x82};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* NO expect_*(mock_tp_send): positive response must be suppressed */
    uds_input_sdu(&ctx, req, sizeof(req));
}

/* 0x87 LinkControl: verifyModeTransitionWithFixedParameter sub 0x01 with bit 7 set */
static void test_suppress_link_control(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_link_control = mock_link_control_suppress;

    /* 0x81 = 0x01 | 0x80; mode id = 0x11 */
    uint8_t req[] = {0x87, 0x81, 0x11};
    will_return(mock_get_time, 1000); /* Input */
    will_return(mock_get_time, 1000); /* Dispatch */
    /* NO expect_*(mock_tp_send): positive response must be suppressed */
    uds_input_sdu(&ctx, req, sizeof(req));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_suppress_tester_present),
        cmocka_unit_test(test_suppress_comm_control),
        cmocka_unit_test(test_no_suppress_nrc),
        cmocka_unit_test(test_suppress_session_control),
        cmocka_unit_test(test_suppress_security_access),
        cmocka_unit_test(test_suppress_dynamic_define_did),
        cmocka_unit_test(test_suppress_routine_control),
        cmocka_unit_test(test_suppress_access_timing),
        cmocka_unit_test(test_suppress_link_control),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
