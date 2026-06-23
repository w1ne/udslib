/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/*
 * ControlDTCSetting (SID 0x85) regression coverage. Exercises every response
 * path: on/off positive responses, the application hook (accept + reject),
 * suppressPositiveResponse, the length and sub-function NRCs, the no-hook
 * fallback, and the default-session restore that re-enables DTC setting.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"

static uds_ctx_t ctx;
static uds_config_t cfg;
static uint8_t rx_buf[256];
static uint8_t tx_buf[256];

static uint32_t mock_get_time(void)
{
    return 0;
}

static int mock_tp_send(struct uds_ctx *c, const uint8_t *data, uint16_t len)
{
    (void) c;
    check_expected(data);
    check_expected(len);
    return 0;
}

static int mock_dtc_setting(struct uds_ctx *c, uint8_t sub_function)
{
    (void) c;
    check_expected(sub_function);
    return (int) mock();
}

/* hook = NULL leaves the service in its no-callback fallback path. */
static void setup_test(int (*hook)(struct uds_ctx *, uint8_t))
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.fn_control_dtc_setting = hook;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);
    uds_init(&ctx, &cfg);
}

static void test_dtc_off_positive(void **state)
{
    (void) state;
    setup_test(mock_dtc_setting);

    /* 85 02 (DTCSettingType off) */
    uint8_t req[] = {0x85, 0x02};
    expect_value(mock_dtc_setting, sub_function, 0x02);
    will_return(mock_dtc_setting, UDS_OK);

    uint8_t resp[] = {0xC5, 0x02};
    expect_memory(mock_tp_send, data, resp, 2);
    expect_value(mock_tp_send, len, 2);

    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(ctx.session.dtc_setting_disabled, 1);
}

static void test_dtc_on_positive(void **state)
{
    (void) state;
    setup_test(mock_dtc_setting);
    ctx.session.dtc_setting_disabled = 1; /* pretend a prior 85 02 disabled it */

    /* 85 01 (DTCSettingType on) */
    uint8_t req[] = {0x85, 0x01};
    expect_value(mock_dtc_setting, sub_function, 0x01);
    will_return(mock_dtc_setting, UDS_OK);

    uint8_t resp[] = {0xC5, 0x01};
    expect_memory(mock_tp_send, data, resp, 2);
    expect_value(mock_tp_send, len, 2);

    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(ctx.session.dtc_setting_disabled, 0);
}

static void test_dtc_hook_reject_nrc(void **state)
{
    (void) state;
    setup_test(mock_dtc_setting);

    /* Hook rejects with conditionsNotCorrect; the setting must stay unchanged. */
    uint8_t req[] = {0x85, 0x02};
    expect_value(mock_dtc_setting, sub_function, 0x02);
    will_return(mock_dtc_setting, -0x22);

    uint8_t resp[] = {0x7F, 0x85, 0x22};
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);

    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(ctx.session.dtc_setting_disabled, 0);
}

static void test_dtc_subfunction_not_supported(void **state)
{
    (void) state;
    setup_test(mock_dtc_setting);

    /* 85 03 — only 0x01/0x02 are defined; callback must never run. */
    uint8_t req[] = {0x85, 0x03};
    uint8_t resp[] = {0x7F, 0x85, 0x12};
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);

    uds_input_sdu(&ctx, req, sizeof(req));
}

static void test_dtc_incorrect_length(void **state)
{
    (void) state;
    setup_test(mock_dtc_setting);

    /* 85 with no sub-function byte. */
    uint8_t req[] = {0x85};
    uint8_t resp[] = {0x7F, 0x85, 0x13};
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);

    uds_input_sdu(&ctx, req, sizeof(req));
}

static void test_dtc_suppress_pos_resp(void **state)
{
    (void) state;
    setup_test(mock_dtc_setting);

    /* 85 82 — off + suppressPositiveResponse: state changes, no response sent. */
    uint8_t req[] = {0x85, 0x82};
    expect_value(mock_dtc_setting, sub_function, 0x02);
    will_return(mock_dtc_setting, UDS_OK);

    /* No expect_memory(mock_tp_send, ...) — the response is suppressed. */
    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(ctx.session.dtc_setting_disabled, 1);
}

static void test_dtc_no_hook_still_tracks(void **state)
{
    (void) state;
    setup_test(NULL); /* no application callback configured */

    /* Without a hook the server still answers positively and tracks the state —
     * the service is no longer a no-op stub. */
    uint8_t req[] = {0x85, 0x02};
    uint8_t resp[] = {0xC5, 0x02};
    expect_memory(mock_tp_send, data, resp, 2);
    expect_value(mock_tp_send, len, 2);

    uds_input_sdu(&ctx, req, sizeof(req));
    assert_int_equal(ctx.session.dtc_setting_disabled, 1);
}

static void test_dtc_restored_on_default_session(void **state)
{
    (void) state;
    setup_test(mock_dtc_setting);

    /* Disable DTC setting... */
    uint8_t off[] = {0x85, 0x02};
    expect_value(mock_dtc_setting, sub_function, 0x02);
    will_return(mock_dtc_setting, UDS_OK);
    uint8_t off_resp[] = {0xC5, 0x02};
    expect_memory(mock_tp_send, data, off_resp, 2);
    expect_value(mock_tp_send, len, 2);
    uds_input_sdu(&ctx, off, sizeof(off));
    assert_int_equal(ctx.session.dtc_setting_disabled, 1);

    /* ...then return to the default session (10 81, suppressed). The server
     * restores DTC setting to "on" without the tester re-sending 85 01. */
    uint8_t to_default[] = {0x10, 0x81};
    uds_input_sdu(&ctx, to_default, sizeof(to_default));
    assert_int_equal(ctx.session.dtc_setting_disabled, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dtc_off_positive),
        cmocka_unit_test(test_dtc_on_positive),
        cmocka_unit_test(test_dtc_hook_reject_nrc),
        cmocka_unit_test(test_dtc_subfunction_not_supported),
        cmocka_unit_test(test_dtc_incorrect_length),
        cmocka_unit_test(test_dtc_suppress_pos_resp),
        cmocka_unit_test(test_dtc_no_hook_still_tracks),
        cmocka_unit_test(test_dtc_restored_on_default_session),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
