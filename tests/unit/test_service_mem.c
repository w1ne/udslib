#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include <string.h>
#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* Mock Mocks */
static int mock_can_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    (void)ctx;
    printf("[DEBUG] mock_can_send: len=%d data=", len);
    for(int i=0; i<len; i++) printf("%02X ", data[i]);
    printf("\n");
    
    check_expected(len);
    check_expected_ptr(data);
    return 0;
}

static uint32_t g_time_ms = 0;
static uint32_t get_time_ms(void) { return g_time_ms; }

/* Mock Memory Backend */
static uint8_t g_memory[1024];

static int mock_mem_read(struct uds_ctx *ctx, uint32_t addr, uint32_t size, uint8_t *out_buf) {
    (void)ctx;
    if (addr + size > sizeof(g_memory)) return -0x31; /* RequestOutOfRange */
    memcpy(out_buf, &g_memory[addr], size);
    return 0;
}

static int mock_mem_write(struct uds_ctx *ctx, uint32_t addr, uint32_t size, const uint8_t *data) {
    (void)ctx;
    if (addr + size > sizeof(g_memory)) return -0x31;
    memcpy(&g_memory[addr], data, size);
    return 0;
}

static uds_ctx_t ctx;
static uds_config_t cfg;
static uint8_t rx_buf[256];
static uint8_t tx_buf[256];

static int setup(void **state) {
    (void)state;
    memset(&ctx, 0, sizeof(ctx));
    memset(&cfg, 0, sizeof(cfg));
    memset(g_memory, 0, sizeof(g_memory));

    cfg.fn_tp_send = mock_can_send;
    cfg.get_time_ms = get_time_ms;
    cfg.fn_mem_read = mock_mem_read;
    cfg.fn_mem_write = mock_mem_write;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);
    cfg.p2_ms = 50;

    uds_init(&ctx, &cfg);
    return 0;
}

static void test_read_memory_success(void **state) {
    (void)state;
    /* Preset memory */
    g_memory[0x100] = 0xAA;
    g_memory[0x101] = 0xBB;

    /* 0x23, Format=0x12 (1 byte addr? No, 2 byte addr, 1 byte size?), 
       Wait, spec says Format High nibble = length bytes, Low nibble = addr bytes 
       Let's say 0x12 -> Size=1 byte len, Addr=2 byte len */
    
    uint8_t req[] = {0x23, 0x12, 0x01, 0x00, 0x02}; /* Addr 0x0100, Size 0x02 */
    
    /* Expected Response: 63 + data */
    uint8_t resp[] = {0x63, 0xAA, 0xBB};

    expect_value(mock_can_send, len, 3);
    expect_memory(mock_can_send, data, resp, 3);

    uds_input_sdu(&ctx, req, 5);
}

static void test_read_memory_out_of_range(void **state) {
    (void)state;
    /* Addr=0x1000 (4096) > 1024 -> Fail */
    uint8_t req[] = {0x23, 0x12, 0x10, 0x00, 0x01}; 

    /* Expect NRC 0x31 */
    uint8_t resp[] = {0x7F, 0x23, 0x31};
    expect_value(mock_can_send, len, 3);
    expect_memory(mock_can_send, data, resp, 3);
    
    uds_input_sdu(&ctx, req, 5);
}

static void test_write_memory_success(void **state) {
    (void)state;
    /* 0x3D, Format=0x12, Addr=0x0200, Size=0x02, Data=0xCC,0xDD */
    uint8_t req[] = {0x3D, 0x12, 0x02, 0x00, 0x02, 0xCC, 0xDD};

    /* Expect 0x7D + Format (0x12) + Echoed Addr (02 00) + Size (02) */
    uint8_t resp[] = {0x7D, 0x12, 0x02, 0x00, 0x02};
    
    expect_value(mock_can_send, len, 5);
    expect_memory(mock_can_send, data, resp, 5);

    uds_input_sdu(&ctx, req, 7);

    assert_int_equal(g_memory[0x200], 0xCC);
    assert_int_equal(g_memory[0x201], 0xDD);
}

static void test_write_memory_bad_length(void **state) {
    (void)state;
    /* Claims size 2 but provides only 1 byte */
    uint8_t req[] = {0x3D, 0x12, 0x02, 0x00, 0x02, 0xCC};

    /* Expect NRC 0x13 (Invalid Format) */
    uint8_t resp[] = {0x7F, 0x3D, 0x13};
    expect_value(mock_can_send, len, 3);
    expect_memory(mock_can_send, data, resp, 3);

    uds_input_sdu(&ctx, req, 6);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_read_memory_success, setup),
        cmocka_unit_test_setup(test_read_memory_out_of_range, setup),
        cmocka_unit_test_setup(test_write_memory_success, setup),
        cmocka_unit_test_setup(test_write_memory_bad_length, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
