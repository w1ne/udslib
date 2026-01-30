#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"
#include "test_helpers.h"

static uds_ctx_t g_ctx;
static uds_config_t g_cfg;
static uint8_t g_nvm_storage[2] = {0x00, 0x00};

/* Mock NVM Save */
static int mock_nvm_save(struct uds_ctx *ctx, const uint8_t *state, uint16_t len) {
    if (len == 2) {
        g_nvm_storage[0] = state[0];
        g_nvm_storage[1] = state[1];
        return 2;
    }
    return -1;
}

/* Mock NVM Load */
static int mock_nvm_load(struct uds_ctx *ctx, uint8_t *state, uint16_t len) {
    if (len == 2) {
        state[0] = g_nvm_storage[0];
        state[1] = g_nvm_storage[1];
        return 2;
    }
    return -1;
}

static uint32_t my_get_time(void) { return 0; }

static int setup(void **state) {
    setup_ctx(&g_ctx, &g_cfg);
    g_cfg.get_time_ms = my_get_time;
    g_cfg.fn_nvm_save = mock_nvm_save;
    g_cfg.fn_nvm_load = mock_nvm_load;
    
    /* Reset NVM to defaults */
    g_nvm_storage[0] = 0x01; /* Default Session */
    g_nvm_storage[1] = 0x00; /* Locked */
    
    return 0;
}

static int teardown(void **state) {
    return 0;
}

/* 1. Test Load on Init */
static void test_nvm_load_on_init(void **state) {
    /* Pre-set NVM to some non-default state */
    g_nvm_storage[0] = 0x03; /* Extended */
    g_nvm_storage[1] = 0x01; /* Unlocked */
    
    uds_init(&g_ctx, &g_cfg);
    
    assert_int_equal(g_ctx.active_session, 0x03);
    assert_int_equal(g_ctx.security_level, 0x01);
}

/* 2. Test Save on Session Change */
static void test_nvm_save_on_session(void **state) {
    uds_init(&g_ctx, &g_cfg);
    
    /* Request Session 0x03 (Extended) */
    uint8_t req[] = {0x10, 0x03};
    uint8_t expected[] = {0x50, 0x03, 0x00, 0x32, 0x01, 0xF4}; /* Response for extended */
    
    expect_memory(mock_tp_send, data, expected, 6);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    
    uds_input_sdu(&g_ctx, req, sizeof(req));
    
    /* Verify NVM updated */
    assert_int_equal(g_nvm_storage[0], 0x03);
}

/* 3. Test Save on Security Unlock */
static void test_nvm_save_on_security(void **state) {
    uds_init(&g_ctx, &g_cfg);
    
    /* Step 1: Request Seed (0x27 0x01) */
    uint8_t req_seed[] = {0x27, 0x01};
    uint8_t resp_seed[] = {0x67, 0x01, 0xDE, 0xAD, 0xBE, 0xEF};
    
    expect_memory(mock_tp_send, data, resp_seed, 6);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);
    
    uds_input_sdu(&g_ctx, req_seed, sizeof(req_seed));
    
    /* Step 2: Send Key (0x27 0x02 + Key) */
    /* Key is DF AE BF F0 */
    uint8_t req_key[] = {0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0}; 
    uint8_t resp_key[] = {0x67, 0x02};
    
    expect_memory(mock_tp_send, data, resp_key, 2);
    expect_value(mock_tp_send, len, 2);
    will_return(mock_tp_send, 0);
    
    uds_input_sdu(&g_ctx, req_key, sizeof(req_key));
    
    /* Verify NVM updated (Security Unlocked = 1) */
    assert_int_equal(g_nvm_storage[1], 0x01);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_nvm_load_on_init, setup, teardown),
        cmocka_unit_test_setup_teardown(test_nvm_save_on_session, setup, teardown),
        cmocka_unit_test_setup_teardown(test_nvm_save_on_security, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
