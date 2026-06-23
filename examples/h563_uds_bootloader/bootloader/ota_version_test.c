/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file ota_version_test.c
 * @brief Host unit test for the pure anti-rollback decision function.
 *
 * Tests ota_version_allows() (no flash I/O, no MCU deps) from ota_image.h.
 *
 * Build and run (host):
 *   gcc -O2 -g -I. -Wall -Wextra -Werror ota_version_test.c -o otaversion_test
 *   ./otaversion_test
 *
 * The Makefile exposes this as:
 *   make -C bootloader otaversion-test
 */

#include "ota_image.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(cond, msg)                          \
    do {                                          \
        if (!(cond)) {                            \
            fprintf(stderr, "FAIL: %s\n", (msg)); \
            return 1;                             \
        }                                         \
        printf("PASS: %s\n", (msg));              \
    } while (0)

/* Example versions: 0x00010000 = v1.0.0, 0x00020000 = v2.0.0. */
#define V1 0x00010000u
#define V2 0x00020000u

/* Enforcement ON: upgrade allowed. */
static int test_upgrade_allowed_enforced(void)
{
    CHECK(ota_version_allows(V2, V1, 1) == 1, "enforce: upgrade (v2 over v1) allowed");
    return 0;
}

/* Enforcement ON: equal version allowed (reinstall / re-activate). */
static int test_equal_allowed_enforced(void)
{
    CHECK(ota_version_allows(V1, V1, 1) == 1, "enforce: equal version (v1 over v1) allowed");
    return 0;
}

/* Enforcement ON: downgrade rejected. */
static int test_downgrade_rejected_enforced(void)
{
    CHECK(ota_version_allows(V1, V2, 1) == 0, "enforce: downgrade (v1 over v2) rejected");
    return 0;
}

/* Enforcement OFF: downgrade allowed (policy disabled). */
static int test_downgrade_allowed_not_enforced(void)
{
    CHECK(ota_version_allows(V1, V2, 0) == 1, "no-enforce: downgrade (v1 over v2) allowed");
    return 0;
}

/* Enforcement OFF: upgrade still allowed. */
static int test_upgrade_allowed_not_enforced(void)
{
    CHECK(ota_version_allows(V2, V1, 0) == 1, "no-enforce: upgrade (v2 over v1) allowed");
    return 0;
}

/* Floor of 0 (active bank invalid / first flash): any candidate allowed even
 * when enforced — recovery must not be bricked by anti-rollback. */
static int test_zero_floor_allows_any(void)
{
    CHECK(ota_version_allows(V1, 0u, 1) == 1,
          "enforce: candidate over zero floor (recovery) allowed");
    CHECK(ota_version_allows(0u, 0u, 1) == 1, "enforce: zero candidate over zero floor allowed");
    return 0;
}

/* Default policy switch is ON (anti-rollback enforced unless overridden). */
static int test_default_switch_is_on(void)
{
    CHECK(OTA_ANTIROLLBACK_ENFORCE == 1, "default OTA_ANTIROLLBACK_ENFORCE == 1 (enforced)");
    /* With the default switch, a downgrade must be rejected. */
    CHECK(ota_version_allows(V1, V2, OTA_ANTIROLLBACK_ENFORCE) == 0,
          "default policy rejects downgrade");
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_upgrade_allowed_enforced();
    rc |= test_equal_allowed_enforced();
    rc |= test_downgrade_rejected_enforced();
    rc |= test_downgrade_allowed_not_enforced();
    rc |= test_upgrade_allowed_not_enforced();
    rc |= test_zero_floor_allows_any();
    rc |= test_default_switch_is_on();

    if (rc == 0) {
        printf("\nAll otaversion-test cases PASS\n");
    }
    else {
        fprintf(stderr, "\nSome otaversion-test cases FAILED\n");
    }
    return rc;
}
