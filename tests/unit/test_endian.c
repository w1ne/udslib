/**
 * @file test_endian.c
 * @brief Unit tests for Endianness Independence
 *
 * Verifies that the stack correctly parses Big-Endian UDS data
 * regardless of the host machine's architecture.
 */

#include "test_helpers.h"
#include "uds/uds_core.h"

/* We need to peek into internal helpers or test effects via public API */
/* For this test, we'll verify via the Memory Services (0x23/0x3D)
 * because they explicitly parse 32-bit addresses and sizes.
 */

/* Mock Memory (Not used for generic parsing test) */
static uint32_t last_read_addr = 0;
static uint32_t last_read_size = 0;

static int fn_mem_read(uds_ctx_t *ctx, uint32_t addr, uint32_t size, uint8_t *out_buf)
{
    (void) ctx;
    last_read_addr = addr;
    last_read_size = size;
    /* For parsing verification, we don't care about the real memory access.
     * Just allow it and fill with 0xEE to indicate success.
     */
    if (size > 0) {
        memset(out_buf, 0xEE, size);
    }
    return 0;
}

static void setup_ctx_endian(uds_ctx_t *ctx, uds_config_t *cfg)
{
    setup_ctx(ctx, cfg);
    cfg->fn_mem_read = fn_mem_read;
    /* Enable Memory Services (0x23) in session mask if needed (default is ALL) */
}

static void test_big_endian_parsing(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx_endian(&ctx, &cfg);

    /*
     * Request: 0x23 (ReadMemoryByAddress)
     * Format: 0x44 (4-byte Size, 4-byte Address)
     * Address: 0x12345678 (Big Endian)
     * Size:    0x00000010 (16 decimal)
     */
    uint8_t req[] = {
        0x23, 0x44, 0x12, 0x34, 0x56, 0x78, /* Address: 0x12345678 */
        0x00, 0x00, 0x00, 0x10              /* Size: 0x10 */
    };

    /* Expectations */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    /* Expect positive response (0x63 + Data(16)) = 17 bytes */
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 17);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));

    /* Verify the stack parsed the Big Endian bytes correctly into the native uint32_t */
    assert_int_equal(last_read_addr, 0x12345678);
    assert_int_equal(last_read_size, 0x10);
}

static void test_big_endian_parsing_small(void **state)
{
    (void) state;
    uds_ctx_t ctx;
    uds_config_t cfg;
    setup_ctx_endian(&ctx, &cfg);

    /*
     * Request: 0x23 (ReadMemoryByAddress)
     * Format: 0x12 (1-byte Size, 2-byte Address)
     * Address: 0xAABB (Big Endian)
     * Size:    0x05
     */
    uint8_t req[] = {
        0x23, 0x12, 0xAA, 0xBB, /* Address: 0xAABB */
        0x05                    /* Size: 0x05 */
    };

    /* Expectations */
    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 1 + 5);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, sizeof(req));

    assert_int_equal(last_read_addr, 0xAABB);
    assert_int_equal(last_read_size, 0x05);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_big_endian_parsing),
        cmocka_unit_test(test_big_endian_parsing_small),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
