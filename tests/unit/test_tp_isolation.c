/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_tp_isolation.c
 * @brief Verifies that two ISO-TP instances keep fully independent state.
 *
 * This is only possible once the transport stops using file-global state:
 * an in-flight multi-frame transfer on one instance must not disturb another.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_isotp.h"

static int silent_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    (void) data;
    (void) len;
    return 0;
}

static void test_two_instances_have_independent_state(void **state)
{
    (void) state;

    uds_isotp_ctx_t iso_a;
    uds_isotp_ctx_t iso_b;
    uint8_t sdu_a[64];
    uint8_t sdu_b[64];

    uds_tp_isotp_init(&iso_a, silent_can_send, 0x100, 0x101, sdu_a, sizeof(sdu_a));
    uds_tp_isotp_init(&iso_b, silent_can_send, 0x200, 0x201, sdu_b, sizeof(sdu_b));

    /* Start a multi-frame send on A only: 10 bytes > 7 -> FF, then WAIT_FC. */
    uint8_t data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int ret = uds_isotp_send(&iso_a, data, sizeof(data));
    assert_int_equal(ret, 0);

    /* A is mid-transfer; B must be untouched. */
    assert_int_equal(iso_a.tx_state, ISOTP_TX_WAIT_FC);
    assert_int_equal(iso_b.tx_state, ISOTP_TX_IDLE);

    /* Configuration stays per-instance. */
    assert_int_equal(iso_a.tx_id, 0x100);
    assert_int_equal(iso_b.tx_id, 0x200);

    /* The TX cache lives in the caller-provided buffer, not a shared global. */
    assert_int_equal(iso_a.tx_sdu_len, sizeof(data));
    assert_ptr_equal(iso_a.tx_sdu_buf, sdu_a);
    assert_ptr_equal(iso_b.tx_sdu_buf, sdu_b);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_two_instances_have_independent_state),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
