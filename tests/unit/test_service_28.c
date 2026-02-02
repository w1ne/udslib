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

/* --- Mocks --- */
static uint32_t mock_get_time(void) { return 0; }
static int mock_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    check_expected(data);
    check_expected(len);
    return 0;
}

/* --- Callback Mock --- */
static int mock_comm_control(struct uds_ctx *ctx, uint8_t ctrl_type, uint8_t comm_type) {
    check_expected(ctrl_type);
    check_expected(comm_type);
    return (int)mock();
}

static void setup_test(void **state) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = mock_get_time;
    cfg.fn_tp_send = mock_tp_send;
    cfg.fn_comm_control = mock_comm_control;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);
    
    /* Missing table */
    static const uds_did_entry_t dids[] = {{0, 0, UDS_SESSION_ALL, 0, NULL, NULL, NULL}};
    cfg.did_table.entries = dids;
    cfg.did_table.count = 0;

    uds_init(&ctx, &cfg);
}

static void test_comm_control_accept(void **state) {
    setup_test(state);

    /* 28 01 01 (EnableRxAndDisableTx, Application) */
    uint8_t req[] = {0x28, 0x01, 0x01};

    /* Expect callback with ctrl=1, comm=1 */
    expect_value(mock_comm_control, ctrl_type, 0x01);
    expect_value(mock_comm_control, comm_type, 0x01);
    will_return(mock_comm_control, UDS_OK);

    /* Expect Positive Response 68 01 */
    uint8_t resp[] = {0x68, 0x01};
    expect_memory(mock_tp_send, data, resp, 2);
    expect_value(mock_tp_send, len, 2);

    uds_input_sdu(&ctx, req, sizeof(req));
}

static void test_comm_control_reject(void **state) {
    setup_test(state);

    /* 28 03 01 (DisableRxAndTx, Application) */
    uint8_t req[] = {0x28, 0x03, 0x01};

    /* Expect callback */
    expect_value(mock_comm_control, ctrl_type, 0x03);
    expect_value(mock_comm_control, comm_type, 0x01);
    /* Mock failure: Return -0x22 (ConditionsNotCorrect) */
    will_return(mock_comm_control, -0x22);

    /* Expect NRC 7F 28 22 */
    uint8_t resp[] = {0x7F, 0x28, 0x22};
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);

    uds_input_sdu(&ctx, req, sizeof(req));
}

static void test_comm_control_invalid_length_nrc(void **state) {
    setup_test(state);

    /* 28 01 (Short request, missing CommType) */
    uint8_t req[] = {0x28, 0x01};

    /* Expect NRC 7F 28 13 (IncorrectMessageLength) */
    uint8_t resp[] = {0x7F, 0x28, 0x13};
    expect_memory(mock_tp_send, data, resp, 3);
    expect_value(mock_tp_send, len, 3);

    uds_input_sdu(&ctx, req, sizeof(req));
}

static void test_comm_control_suppress_pos_resp(void **state) {
    setup_test(state);

    /* 28 81 01 (EnableRxAndDisableTx + SuppressBit, Application) */
    uint8_t req[] = {0x28, 0x81, 0x01};

    expect_value(mock_comm_control, ctrl_type, 0x01);
    expect_value(mock_comm_control, comm_type, 0x01);
    will_return(mock_comm_control, UDS_OK);
    
    /* NO expect_memory(mock_tp_send, ...) because suppressed */

    uds_input_sdu(&ctx, req, sizeof(req));
    
    assert_int_equal(ctx.comm_state, 0x01);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_comm_control_accept),
        cmocka_unit_test(test_comm_control_reject),
        cmocka_unit_test(test_comm_control_invalid_length_nrc),
        cmocka_unit_test(test_comm_control_suppress_pos_resp),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
