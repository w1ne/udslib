# DTC Enrichment + 0x19 Expansion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enrich the DTC model (severity, fault-detection counter, aging counter, functional unit, functional group), add ReadDTCInformation subfunctions 0x07/0x08/0x09/0x14/0x42/0x55, a stateless P/C/B/U classifier, and an opt-in reference DTC store — without touching udslib's "library owns wire layout, app owns state" core philosophy.

**Architecture:** Core changes are additive struct fields + wire-formatting that reuse the existing `fn_dtc_list` hook (no new enumeration callbacks). Diagnostic policy (counters/aging/self-heal) lives only in an opt-in `uds_dtc_store` helper over an application-provided array. Classifier + named bit macros are header-only.

**Tech Stack:** C99, CMake, cmocka, clang-format (Google base, IndentWidth 4, ColumnLimit 100).

## Global Constraints

- Language: C99; compile clean under `-Wall -Wextra`.
- Format gate: clang-format-14 (`.clang-format`: BasedOnStyle Google, IndentWidth 4, ColumnLimit 100, AllowShortFunctionsOnASingleLine None).
- Tests: cmocka; register every new test executable in `tests/CMakeLists.txt` via `add_uds_test`.
- Naming: types `uds_*_t`; public funcs `uds_*`; internal funcs `uds_internal_*`; SID macros `UDS_SID_*`; NRC macros `UDS_NRC_*`.
- Copyright header on every new file (copy from `tests/unit/test_service_dtc.c` lines 1-4): `Copyright (c) 2026 Andrii Shylenko` / `SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`.
- All commits: author `w1ne <14119286+w1ne@users.noreply.github.com>`; **no AI/Claude references** in commit messages.
- Branch: `feature/dtc-enrichment-issue39` (already created off `develop`).
- Non-breaking: existing `uds_dtc_record_t` initializers and the 0x01/0x02/0x0A wire path must keep working unchanged.

---

### Task 1: Extended DTC record + stateless helpers header

**Files:**
- Modify: `include/uds/uds_config.h:135-139` (widen `uds_dtc_record_t`)
- Create: `include/uds/uds_dtc.h`
- Create: `tests/unit/test_dtc_helpers.c`
- Modify: `tests/CMakeLists.txt` (register test)

**Interfaces:**
- Produces: extended `uds_dtc_record_t { uint32_t dtc; uint8_t status; uint8_t severity; uint8_t functional_unit; int8_t fault_detection_counter; uint8_t aging_counter; uint8_t functional_group; }`
- Produces: `typedef enum { UDS_DTC_POWERTRAIN=0, UDS_DTC_CHASSIS=1, UDS_DTC_BODY=2, UDS_DTC_NETWORK=3 } uds_dtc_category_t;`
- Produces: `static inline uds_dtc_category_t uds_dtc_category(uint32_t dtc)`
- Produces: `UDS_DTC_STATUS_*`, `UDS_DTC_SEVERITY_*`, `UDS_DTC_FGID_*` macros

- [ ] **Step 1: Widen the record struct**

In `include/uds/uds_config.h`, replace the struct body at lines 135-139:

```c
typedef struct
{
    uint32_t dtc;                     /**< 3-byte DTC, right-aligned (high byte ignored) */
    uint8_t status;                   /**< statusOfDTC byte (ISO 14229-1 Annex D) */
    uint8_t severity;                 /**< DTCSeverity bits (0x08/0x09/0x42) */
    uint8_t functional_unit;          /**< DTCFunctionalUnit (0x08/0x09) */
    int8_t fault_detection_counter;   /**< signed -128..127 (0x14) */
    uint8_t aging_counter;            /**< operation cycles since last fault */
    uint8_t functional_group;         /**< WWH-OBD functional group (0x33/0xD0/0xFE) */
} uds_dtc_record_t;
```

- [ ] **Step 2: Create the helpers header**

Create `include/uds/uds_dtc.h`:

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef UDS_DTC_H
#define UDS_DTC_H

#include <stdint.h>

/* statusOfDTC bits (ISO 14229-1 Annex D) */
#define UDS_DTC_STATUS_TEST_FAILED 0x01u
#define UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE 0x02u
#define UDS_DTC_STATUS_PENDING 0x04u
#define UDS_DTC_STATUS_CONFIRMED 0x08u
#define UDS_DTC_STATUS_TEST_NOT_COMPLETED_SINCE_CLEAR 0x10u
#define UDS_DTC_STATUS_TEST_FAILED_SINCE_CLEAR 0x20u
#define UDS_DTC_STATUS_TEST_NOT_COMPLETED_THIS_OP_CYCLE 0x40u
#define UDS_DTC_STATUS_WARNING_INDICATOR_REQUESTED 0x80u

/* DTCSeverity bits (ISO 14229-1) */
#define UDS_DTC_SEVERITY_MAINTENANCE_ONLY 0x20u
#define UDS_DTC_SEVERITY_CHECK_AT_NEXT_HALT 0x40u
#define UDS_DTC_SEVERITY_CHECK_IMMEDIATELY 0x80u

/* FunctionalGroupIdentifier values (ISO 14229-1) */
#define UDS_DTC_FGID_EMISSIONS 0x33u
#define UDS_DTC_FGID_SAFETY 0xD0u
#define UDS_DTC_FGID_VOBD 0xFEu

/** DTC category from the top two bits of the high DTC byte. */
typedef enum
{
    UDS_DTC_POWERTRAIN = 0, /**< P, bit pattern 00 */
    UDS_DTC_CHASSIS = 1,    /**< C, bit pattern 01 */
    UDS_DTC_BODY = 2,       /**< B, bit pattern 10 */
    UDS_DTC_NETWORK = 3     /**< U, bit pattern 11 */
} uds_dtc_category_t;

/**
 * @brief Classify a 3-byte DTC as Powertrain/Chassis/Body/Network.
 * @param dtc 3-byte DTC, right-aligned.
 * @return Category from bits 23..22 of the DTC.
 */
static inline uds_dtc_category_t uds_dtc_category(uint32_t dtc)
{
    return (uds_dtc_category_t) ((dtc >> 22) & 0x3u);
}

#endif /* UDS_DTC_H */
```

- [ ] **Step 3: Write the failing helper test**

Create `tests/unit/test_dtc_helpers.c`:

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "uds/uds_dtc.h"

static void test_dtc_category_examples(void **state)
{
    (void) state;
    assert_int_equal(uds_dtc_category(0x012345u), UDS_DTC_POWERTRAIN);
    assert_int_equal(uds_dtc_category(0xDCBA98u), UDS_DTC_NETWORK);
    assert_int_equal(uds_dtc_category(0xFFFFFFu), UDS_DTC_NETWORK);
}

static void test_dtc_category_boundaries(void **state)
{
    (void) state;
    assert_int_equal(uds_dtc_category(0x400000u), UDS_DTC_CHASSIS); /* 01.. */
    assert_int_equal(uds_dtc_category(0x800000u), UDS_DTC_BODY);    /* 10.. */
    assert_int_equal(uds_dtc_category(0xC00000u), UDS_DTC_NETWORK); /* 11.. */
}

static void test_dtc_status_bits(void **state)
{
    (void) state;
    assert_int_equal(UDS_DTC_STATUS_TEST_FAILED, 0x01u);
    assert_int_equal(UDS_DTC_STATUS_CONFIRMED, 0x08u);
    assert_int_equal(UDS_DTC_STATUS_WARNING_INDICATOR_REQUESTED, 0x80u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dtc_category_examples),
        cmocka_unit_test(test_dtc_category_boundaries),
        cmocka_unit_test(test_dtc_status_bits),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

- [ ] **Step 4: Register the test**

In `tests/CMakeLists.txt`, after the `add_uds_test(test_service_dtc ...)` line (line 22), add:

```cmake
add_uds_test(test_dtc_helpers unit/test_dtc_helpers.c)
```

- [ ] **Step 5: Build and run — verify pass**

Run:
```bash
cd ~/projects/udslib && cmake -S . -B build >/dev/null && cmake --build build --target test_dtc_helpers test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R 'test_dtc_helpers|test_service_dtc' --output-on-failure
```
Expected: both tests PASS (the widened struct keeps existing DTC tests green).

- [ ] **Step 6: Format check & commit**

```bash
cd ~/projects/udslib && clang-format --dry-run --Werror include/uds/uds_dtc.h tests/unit/test_dtc_helpers.c include/uds/uds_config.h
git add include/uds/uds_dtc.h include/uds/uds_config.h tests/unit/test_dtc_helpers.c tests/CMakeLists.txt
git -c user.email="14119286+w1ne@users.noreply.github.com" -c user.name="w1ne" commit -m "feat(dtc): extend DTC record and add classification helpers (#39)"
```

---

### Task 2: Severity subfunctions 0x07/0x08/0x09

**Files:**
- Modify: `src/core/uds_internal.h:98-101` (widen `UDS_MASK_SUB_19`)
- Modify: `include/uds/uds_config.h` (add `dtc_severity_availability_mask` near line 382, after `dtc_format_id`)
- Modify: `src/services/uds_service_maintenance.c` (add helpers + dispatch)
- Modify: `tests/unit/test_service_dtc.c` (add tests)

**Interfaces:**
- Consumes: `uds_dtc_record_t` (Task 1), `cfg.fn_dtc_list`, `cfg.dtc_status_availability_mask`, `cfg.dtc_format_id`.
- Produces: `cfg.dtc_severity_availability_mask` config field; handled subfunctions 0x07/0x08/0x09.

**Wire layouts (ISO 14229-1):**
- 0x07 req `[19 07 sevMask statMask]` (len 4) → resp `[59 07 statAvail format cntHi cntLo]` (6 B).
- 0x08 req `[19 08 sevMask statMask]` (len 4) → resp `[59 08 statAvail {sev funcUnit DTC(3) status}...]` (3 B header + 6 B/rec).
- 0x09 req `[19 09 DTC(3)]` (len 5) → resp `[59 09 statAvail sev funcUnit DTC(3) status]` (9 B if found, 3 B if not).

- [ ] **Step 1: Widen the subfunction mask**

In `src/core/uds_internal.h`, replace `UDS_MASK_SUB_19` (lines 99-101). Byte0 adds bit7 (0x07)→`0xD7`; byte1 adds bits0,1 (0x08,0x09)→`0x07`:

```c
/* Allowed 0x19 subfunctions:
 * byte0 0xD7 = 0x00/0x01/0x02/0x04/0x06/0x07; byte1 0x07 = 0x08/0x09/0x0A;
 * byte2 0x10 = 0x14; byte8 0x04 = 0x42; byte10 0x20 = 0x55. */
#define UDS_MASK_SUB_19                                                          \
    {                                                                            \
        0xD7u, 0x07u, 0x10u, 0, 0, 0, 0, 0, 0x04u, 0, 0x20u, 0, 0, 0, 0, 0       \
    }
```
(Bytes 2/8/10 are pre-set here for Tasks 3 & 4 so the mask is edited once.)

- [ ] **Step 2: Add the config field**

In `include/uds/uds_config.h`, immediately after the `dtc_format_id` field (line ~382), add:

```c
    /** DTCSeverityAvailabilityMask reported in 0x42/0x55 responses. */
    uint8_t dtc_severity_availability_mask;
```

- [ ] **Step 3: Write the failing tests**

In `tests/unit/test_service_dtc.c`, extend `k_dtcs` (lines 17-21) to carry severity/functional_unit (other fields zero):

```c
static const uds_dtc_record_t k_dtcs[] = {
    {0x123456u, 0x09u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u, 0, 0, 0},
    {0x123457u, 0x08u, UDS_DTC_SEVERITY_CHECK_AT_NEXT_HALT, 0x11u, 0, 0, 0},
    {0xABCDEFu, 0x01u, UDS_DTC_SEVERITY_MAINTENANCE_ONLY, 0x12u, 0, 0, 0},
};
```
Add `#include "uds/uds_dtc.h"` near the top of the file (after `#include "test_helpers.h"`).

Add these test functions before `main()`:

```c
static void test_read_dtc_info_0x08_by_severity(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* sevMask=0x80 (checkImmediately) matches only 0x123456; statMask=0xFF. */
    uint8_t req[] = {0x19, 0x08, 0x80, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 08 <avail> + 1 * (sev funcUnit DTC[3] status) = 3 + 6 = 9 */
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x08);
    assert_int_equal(g_tx_buf[2], 0x7F);
    assert_int_equal(g_tx_buf[3], 0x80); /* severity */
    assert_int_equal(g_tx_buf[4], 0x10); /* functional unit */
    assert_int_equal(g_tx_buf[5], 0x12); /* DTC hi */
    assert_int_equal(g_tx_buf[6], 0x34);
    assert_int_equal(g_tx_buf[7], 0x56);
    assert_int_equal(g_tx_buf[8], 0x09); /* status */
}

static void test_read_dtc_info_0x07_number_by_severity(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x01u;

    /* sevMask=0xE0 matches all three; statMask=0xFF. */
    uint8_t req[] = {0x19, 0x07, 0xE0, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 4);

    assert_int_equal(g_tx_buf[1], 0x07);
    assert_int_equal(g_tx_buf[3], 0x01); /* format */
    assert_int_equal(g_tx_buf[4], 0x00); /* count hi */
    assert_int_equal(g_tx_buf[5], 0x03); /* count lo */
}

static void test_read_dtc_info_0x09_severity_info(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list;
    cfg.dtc_status_availability_mask = 0x7Fu;

    /* 0x09 <DTC=12 34 57> -> single record for 0x123457. */
    uint8_t req[] = {0x19, 0x09, 0x12, 0x34, 0x57};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);

    assert_int_equal(g_tx_buf[1], 0x09);
    assert_int_equal(g_tx_buf[2], 0x7F);            /* status avail */
    assert_int_equal(g_tx_buf[3], 0x40);            /* severity (checkAtNextHalt) */
    assert_int_equal(g_tx_buf[4], 0x11);            /* functional unit */
    assert_int_equal(g_tx_buf[5], 0x12);            /* DTC hi */
    assert_int_equal(g_tx_buf[7], 0x57);            /* DTC lo */
    assert_int_equal(g_tx_buf[8], 0x08);            /* status */
}
```
Register them in the `tests[]` array in `main()`.

- [ ] **Step 4: Run tests — verify they fail**

Run:
```bash
cd ~/projects/udslib && cmake --build build --target test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R test_service_dtc --output-on-failure
```
Expected: the three new cases FAIL (0x07/0x08/0x09 fall through to legacy path → wrong length/bytes).

- [ ] **Step 5: Implement the handlers**

In `src/services/uds_service_maintenance.c`, add this include near the top (with the other includes):

```c
#include "uds/uds_dtc.h"
```

Add these static helpers after `uds_internal_dtc_record` (after line 230):

```c
/* Format reportNumberOfDTCBySeverityMaskRecord (0x07) and
 * reportDTCBySeverityMaskRecord (0x08). Request: SID, sub, sevMask, statMask. */
static int uds_internal_dtc_by_severity(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                        uint16_t len, bool suppress_pos_resp)
{
    if (len < 4u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint8_t sev_mask = data[2];
    uint8_t status_mask = data[3];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, status_mask, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = ctx->config->dtc_status_availability_mask;

    uint16_t n = (uint16_t) total;

    if (sub == 0x07u) {
        uint16_t count = 0u;
        for (uint16_t i = 0u; i < n; i++) {
            if ((sev_mask == 0u) || ((recs[i].severity & sev_mask) != 0u)) {
                count++;
            }
        }
        if (suppress_pos_resp) {
            return UDS_OK;
        }
        tx[3] = ctx->config->dtc_format_id;
        tx[4] = (uint8_t) ((count >> 8) & 0xFFu);
        tx[5] = (uint8_t) (count & 0xFFu);
        return uds_send_response(ctx, 6u);
    }

    /* 0x08: [severity functionalUnit DTC(3) status] per matching record. */
    uint16_t pos = 3u;
    for (uint16_t i = 0u; i < n; i++) {
        if ((sev_mask != 0u) && ((recs[i].severity & sev_mask) == 0u)) {
            continue;
        }
        if ((uint16_t) (pos + 6u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        tx[pos] = recs[i].severity;
        tx[pos + 1u] = recs[i].functional_unit;
        tx[pos + 2u] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 3u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 4u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 5u] = recs[i].status;
        pos = (uint16_t) (pos + 6u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportSeverityInformationOfDTC (0x09). Request: SID, sub, DTC(3). */
static int uds_internal_dtc_severity_info(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                          uint16_t len, bool suppress_pos_resp)
{
    if (len < 5u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint32_t want = (uint32_t) ((uint32_t) data[2] << 16) | (uint32_t) ((uint32_t) data[3] << 8) |
                    (uint32_t) data[4];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = ctx->config->dtc_status_availability_mask;

    uint16_t pos = 3u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if (recs[i].dtc == want) {
            tx[pos] = recs[i].severity;
            tx[pos + 1u] = recs[i].functional_unit;
            tx[pos + 2u] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
            tx[pos + 3u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
            tx[pos + 4u] = (uint8_t) (recs[i].dtc & 0xFFu);
            tx[pos + 5u] = recs[i].status;
            pos = (uint16_t) (pos + 6u);
            break;
        }
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}
```

Then wire dispatch in `uds_internal_handle_read_dtc_info`, after the 0x06 branch (after line 264), before the `if (!ctx->config->fn_dtc_read)` check:

```c
    if (((sub == 0x07u) || (sub == 0x08u)) && (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_by_severity(ctx, sub, data, len, suppress_pos_resp);
    }

    if ((sub == 0x09u) && (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_severity_info(ctx, sub, data, len, suppress_pos_resp);
    }
```

- [ ] **Step 6: Run tests — verify pass**

Run:
```bash
cd ~/projects/udslib && cmake --build build --target test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R test_service_dtc --output-on-failure
```
Expected: all DTC tests PASS.

- [ ] **Step 7: Format check & commit**

```bash
cd ~/projects/udslib && clang-format --dry-run --Werror src/services/uds_service_maintenance.c src/core/uds_internal.h include/uds/uds_config.h tests/unit/test_service_dtc.c
git add src/services/uds_service_maintenance.c src/core/uds_internal.h include/uds/uds_config.h tests/unit/test_service_dtc.c
git -c user.email="14119286+w1ne@users.noreply.github.com" -c user.name="w1ne" commit -m "feat(dtc): add 0x19 severity subfunctions 0x07/0x08/0x09 (#39)"
```

---

### Task 3: Fault detection counter 0x14

**Files:**
- Modify: `src/services/uds_service_maintenance.c` (add helper + dispatch)
- Modify: `tests/unit/test_service_dtc.c` (add test)

**Interfaces:**
- Consumes: `cfg.fn_dtc_list`, extended record's `fault_detection_counter`.
- Produces: handled subfunction 0x14.

**Wire layout:** 0x14 req `[19 14]` (len 2) → resp `[59 14 {DTC(3) FDC(1)}...]`; only records with `fault_detection_counter` in 1..0x7E (in-progress).

- [ ] **Step 1: Write the failing test**

In `tests/unit/test_service_dtc.c`, add a dedicated record set + test (place above `main()`):

```c
static const uds_dtc_record_t k_fdc_dtcs[] = {
    {0x111111u, 0x04u, 0, 0, 0x20, 0, 0},  /* FDC 32 -> in progress, reported */
    {0x222222u, 0x08u, 0, 0, 0x7F, 0, 0},  /* FDC 127 -> confirmed, not reported */
    {0x333333u, 0x00u, 0, 0, 0x00, 0, 0},  /* FDC 0 -> not reported */
};

static int mock_dtc_list_fdc(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                             uint16_t max)
{
    (void) ctx;
    (void) status_mask;
    for (uint16_t i = 0u; i < 3u; i++) {
        if ((out != NULL) && (i < max)) {
            out[i] = k_fdc_dtcs[i];
        }
    }
    return 3;
}

static void test_read_dtc_info_0x14_fault_counter(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list_fdc;

    uint8_t req[] = {0x19, 0x14};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 14 + 1 * (DTC[3] FDC[1]) = 2 + 4 = 6 (only 0x111111 in range) */
    expect_value(mock_tp_send, len, 6);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 2);

    assert_int_equal(g_tx_buf[0], 0x59);
    assert_int_equal(g_tx_buf[1], 0x14);
    assert_int_equal(g_tx_buf[2], 0x11); /* DTC hi */
    assert_int_equal(g_tx_buf[3], 0x11);
    assert_int_equal(g_tx_buf[4], 0x11);
    assert_int_equal(g_tx_buf[5], 0x20); /* FDC = 32 */
}
```
Register `test_read_dtc_info_0x14_fault_counter` in `main()`.

- [ ] **Step 2: Run test — verify it fails**

Run:
```bash
cd ~/projects/udslib && cmake --build build --target test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R test_service_dtc --output-on-failure
```
Expected: new case FAILS.

- [ ] **Step 3: Implement the handler**

In `src/services/uds_service_maintenance.c`, add after `uds_internal_dtc_severity_info`:

```c
/* Format reportDTCFaultDetectionCounter (0x14). Request: SID, sub.
 * Reports DTCs whose fault-detection counter is in progress (1..0x7E). */
static int uds_internal_dtc_fault_counter(uds_ctx_t *ctx, uint8_t sub, bool suppress_pos_resp)
{
    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;

    uint16_t pos = 2u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        int8_t fdc = recs[i].fault_detection_counter;
        if ((fdc >= 1) && (fdc <= 0x7E)) {
            if ((uint16_t) (pos + 4u) > ctx->config->tx_buffer_size) {
                return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
            }
            tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
            tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
            tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
            tx[pos + 3u] = (uint8_t) fdc;
            pos = (uint16_t) (pos + 4u);
        }
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}
```

Wire dispatch in `uds_internal_handle_read_dtc_info`, after the 0x09 branch:

```c
    if ((sub == 0x14u) && (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_fault_counter(ctx, sub, suppress_pos_resp);
    }
```

- [ ] **Step 4: Run test — verify pass**

Run:
```bash
cd ~/projects/udslib && cmake --build build --target test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R test_service_dtc --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Format check & commit**

```bash
cd ~/projects/udslib && clang-format --dry-run --Werror src/services/uds_service_maintenance.c tests/unit/test_service_dtc.c
git add src/services/uds_service_maintenance.c tests/unit/test_service_dtc.c
git -c user.email="14119286+w1ne@users.noreply.github.com" -c user.name="w1ne" commit -m "feat(dtc): add 0x19 reportDTCFaultDetectionCounter 0x14 (#39)"
```

---

### Task 4: WWH-OBD subfunctions 0x42 / 0x55

**Files:**
- Modify: `src/services/uds_service_maintenance.c` (add helpers + dispatch)
- Modify: `tests/unit/test_service_dtc.c` (add tests)

**Interfaces:**
- Consumes: `cfg.fn_dtc_list`, `cfg.dtc_status_availability_mask`, `cfg.dtc_severity_availability_mask`, `cfg.dtc_format_id`, record's `functional_group`/`severity`.
- Produces: handled subfunctions 0x42/0x55.

**Wire layouts (ISO 14229-1):**
- 0x42 req `[19 42 FGID statMask sevMask]` (len 5) → resp `[59 42 FGID statAvail sevAvail format {sev DTC(3) status}...]` (6 B header + 5 B/rec). Records match `functional_group == FGID` AND status mask AND severity mask.
- 0x55 req `[19 55 FGID]` (len 3) → resp `[59 55 FGID statAvail format {DTC(3) status}...]` (5 B header + 4 B/rec). Records match `functional_group == FGID` AND `status & confirmedDTC` (permanent).

- [ ] **Step 1: Write the failing tests**

In `tests/unit/test_service_dtc.c`, add a WWH-OBD record set + tests (above `main()`):

```c
static const uds_dtc_record_t k_wwh_dtcs[] = {
    /* dtc, status, severity, funcUnit, fdc, aging, functional_group */
    {0xA00001u, 0x08u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0, 0, 0, UDS_DTC_FGID_EMISSIONS},
    {0xA00002u, 0x01u, UDS_DTC_SEVERITY_MAINTENANCE_ONLY, 0, 0, 0, UDS_DTC_FGID_SAFETY},
};

static int mock_dtc_list_wwh(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                             uint16_t max)
{
    (void) ctx;
    uint16_t n = 0u;
    for (uint16_t i = 0u; i < 2u; i++) {
        bool match = (status_mask == 0u) || ((k_wwh_dtcs[i].status & status_mask) != 0u);
        if (match) {
            if ((out != NULL) && (n < max)) {
                out[n] = k_wwh_dtcs[i];
            }
            n++;
        }
    }
    return (int) n;
}

static void test_read_dtc_info_0x42_wwhobd(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list_wwh;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_severity_availability_mask = 0xE0u;
    cfg.dtc_format_id = 0x04u;

    /* FGID=0x33 emissions, statMask=0xFF, sevMask=0xFF -> only 0xA00001. */
    uint8_t req[] = {0x19, 0x42, 0x33, 0xFF, 0xFF};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 42 FGID statAvail sevAvail format + 1*(sev DTC[3] status) = 6 + 5 = 11 */
    expect_value(mock_tp_send, len, 11);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 5);

    assert_int_equal(g_tx_buf[1], 0x42);
    assert_int_equal(g_tx_buf[2], 0x33); /* FGID echoed */
    assert_int_equal(g_tx_buf[3], 0x7F); /* status avail */
    assert_int_equal(g_tx_buf[4], 0xE0); /* severity avail */
    assert_int_equal(g_tx_buf[5], 0x04); /* format */
    assert_int_equal(g_tx_buf[6], 0x80); /* severity */
    assert_int_equal(g_tx_buf[7], 0xA0); /* DTC hi */
    assert_int_equal(g_tx_buf[10], 0x08); /* status */
}

static void test_read_dtc_info_0x55_permanent(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);
    cfg.fn_dtc_list = mock_dtc_list_wwh;
    cfg.dtc_status_availability_mask = 0x7Fu;
    cfg.dtc_format_id = 0x04u;

    /* FGID=0x33; 0xA00001 is confirmed (status 0x08) -> permanent. */
    uint8_t req[] = {0x19, 0x55, 0x33};

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 55 FGID statAvail format + 1*(DTC[3] status) = 5 + 4 = 9 */
    expect_value(mock_tp_send, len, 9);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[1], 0x55);
    assert_int_equal(g_tx_buf[2], 0x33); /* FGID */
    assert_int_equal(g_tx_buf[3], 0x7F); /* status avail */
    assert_int_equal(g_tx_buf[4], 0x04); /* format */
    assert_int_equal(g_tx_buf[5], 0xA0); /* DTC hi */
    assert_int_equal(g_tx_buf[8], 0x08); /* status */
}
```
Register both in `main()`.

- [ ] **Step 2: Run tests — verify they fail**

Run:
```bash
cd ~/projects/udslib && cmake --build build --target test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R test_service_dtc --output-on-failure
```
Expected: both new cases FAIL.

- [ ] **Step 3: Implement the handlers**

In `src/services/uds_service_maintenance.c`, add after `uds_internal_dtc_fault_counter`:

```c
/* Format reportWWHOBDDTCByMaskRecord (0x42).
 * Request: SID, sub, FGID, statusMask, severityMask. */
static int uds_internal_dtc_wwhobd(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data, uint16_t len,
                                   bool suppress_pos_resp)
{
    if (len < 5u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint8_t fgid = data[2];
    uint8_t status_mask = data[3];
    uint8_t sev_mask = data[4];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, status_mask, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = fgid;
    tx[3] = ctx->config->dtc_status_availability_mask;
    tx[4] = ctx->config->dtc_severity_availability_mask;
    tx[5] = ctx->config->dtc_format_id;

    uint16_t pos = 6u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if (recs[i].functional_group != fgid) {
            continue;
        }
        if ((sev_mask != 0u) && ((recs[i].severity & sev_mask) == 0u)) {
            continue;
        }
        if ((uint16_t) (pos + 5u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        tx[pos] = recs[i].severity;
        tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 2u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 3u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 4u] = recs[i].status;
        pos = (uint16_t) (pos + 5u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}

/* Format reportWWHOBDDTCWithPermanentStatus (0x55). Request: SID, sub, FGID.
 * Permanent = confirmed DTCs in the functional group. */
static int uds_internal_dtc_wwhobd_permanent(uds_ctx_t *ctx, uint8_t sub, const uint8_t *data,
                                             uint16_t len, bool suppress_pos_resp)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_LENGTH);
    }
    uint8_t fgid = data[2];

    uds_dtc_record_t recs[UDS_DTC_LIST_BATCH];
    int total = ctx->config->fn_dtc_list(ctx, 0u, recs, (uint16_t) UDS_DTC_LIST_BATCH);
    if (total < 0) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, (uint8_t) - (int32_t) total);
    }
    if ((uint16_t) total > (uint16_t) UDS_DTC_LIST_BATCH) {
        return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
    }

    if (suppress_pos_resp) {
        ctx->suppress_pos_resp = true;
    }

    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t) (UDS_SID_READ_DTC_INFO + UDS_RESPONSE_OFFSET);
    tx[1] = sub;
    tx[2] = fgid;
    tx[3] = ctx->config->dtc_status_availability_mask;
    tx[4] = ctx->config->dtc_format_id;

    uint16_t pos = 5u;
    for (uint16_t i = 0u; i < (uint16_t) total; i++) {
        if (recs[i].functional_group != fgid) {
            continue;
        }
        if ((recs[i].status & UDS_DTC_STATUS_CONFIRMED) == 0u) {
            continue;
        }
        if ((uint16_t) (pos + 4u) > ctx->config->tx_buffer_size) {
            return uds_send_nrc(ctx, UDS_SID_READ_DTC_INFO, UDS_NRC_RESPONSE_TOO_LONG);
        }
        tx[pos] = (uint8_t) ((recs[i].dtc >> 16) & 0xFFu);
        tx[pos + 1u] = (uint8_t) ((recs[i].dtc >> 8) & 0xFFu);
        tx[pos + 2u] = (uint8_t) (recs[i].dtc & 0xFFu);
        tx[pos + 3u] = recs[i].status;
        pos = (uint16_t) (pos + 4u);
    }
    if (suppress_pos_resp) {
        return UDS_OK;
    }
    return uds_send_response(ctx, pos);
}
```

Wire dispatch in `uds_internal_handle_read_dtc_info`, after the 0x14 branch:

```c
    if ((sub == 0x42u) && (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_wwhobd(ctx, sub, data, len, suppress_pos_resp);
    }

    if ((sub == 0x55u) && (ctx->config->fn_dtc_list != NULL)) {
        return uds_internal_dtc_wwhobd_permanent(ctx, sub, data, len, suppress_pos_resp);
    }
```

- [ ] **Step 4: Run tests — verify pass**

Run:
```bash
cd ~/projects/udslib && cmake --build build --target test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R test_service_dtc --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Format check & commit**

```bash
cd ~/projects/udslib && clang-format --dry-run --Werror src/services/uds_service_maintenance.c tests/unit/test_service_dtc.c
git add src/services/uds_service_maintenance.c tests/unit/test_service_dtc.c
git -c user.email="14119286+w1ne@users.noreply.github.com" -c user.name="w1ne" commit -m "feat(dtc): add WWH-OBD 0x19 subfunctions 0x42/0x55 (#39)"
```

---

### Task 5: Optional reference DTC store

**Files:**
- Create: `include/uds/uds_dtc_store.h`
- Create: `src/services/uds_dtc_store.c`
- Modify: `include/uds/uds_config.h` (add `void *app_data` field)
- Modify: `CMakeLists.txt:21-32` (add source)
- Create: `tests/unit/test_dtc_store.c`
- Modify: `tests/CMakeLists.txt` (register test)

**Interfaces:**
- Consumes: `uds_dtc_record_t` (Task 1), `uds_dtc.h` macros, `struct uds_ctx`, `cfg.app_data`.
- Produces: `uds_dtc_store_t`, `uds_dtc_store_init/register/get/report_test/operation_cycle/clear`, and callbacks `uds_dtc_store_list_cb`, `uds_dtc_store_extdata_cb`, `uds_dtc_store_clear_cb`.

- [ ] **Step 1: Add the `app_data` config field**

In `include/uds/uds_config.h`, add to the `uds_config_t` struct, right after the `dtc_severity_availability_mask` field added in Task 2:

```c
    /** Opaque application handle, recoverable inside callbacks via
     *  ctx->config->app_data (e.g. a uds_dtc_store_t* for the reference store). */
    void *app_data;
```

- [ ] **Step 2: Create the store header**

Create `include/uds/uds_dtc_store.h`:

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#ifndef UDS_DTC_STORE_H
#define UDS_DTC_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "uds/uds_config.h"

/**
 * @brief Optional reference DTC store (opt-in; not used by the core).
 *
 * The application supplies the backing array (no allocation). The store
 * implements the ReadDTCInformation callbacks and owns the diagnostic
 * policy the protocol core deliberately avoids (fault-detection counter,
 * aging, self-heal). Wire it up by setting cfg.app_data = &store and
 * cfg.fn_dtc_list = uds_dtc_store_list_cb (plus extdata/clear as needed).
 */
typedef struct
{
    uds_dtc_record_t *entries; /**< Application-provided backing array. */
    uint16_t capacity;         /**< Number of slots in @ref entries. */
    uint16_t count;            /**< Registered DTCs. */
    uint8_t aging_threshold;   /**< Operation cycles to self-heal (e.g. 40). */
} uds_dtc_store_t;

/** Initialise a store over an application-provided backing array. */
void uds_dtc_store_init(uds_dtc_store_t *s, uds_dtc_record_t *backing, uint16_t capacity,
                        uint8_t aging_threshold);

/**
 * @brief Register (or update) a DTC. Status/counters start at zero.
 * @return Index (>=0) on success, or -1 if the store is full.
 */
int uds_dtc_store_register(uds_dtc_store_t *s, uint32_t dtc, uint8_t severity,
                           uint8_t functional_unit, uint8_t functional_group);

/** Find a registered DTC, or NULL. */
uds_dtc_record_t *uds_dtc_store_get(uds_dtc_store_t *s, uint32_t dtc);

/**
 * @brief Report a self-test result for a DTC.
 *
 * failed=true: fault-detection counter increments (saturates at +127),
 * testFailed/testFailedThisOperationCycle set; at +127 the DTC is confirmed.
 * failed=false: counter decrements (floors at -128), testFailed cleared.
 */
void uds_dtc_store_report_test(uds_dtc_store_t *s, uint32_t dtc, bool failed);

/**
 * @brief Advance one operation cycle: age DTCs not failed this cycle; when a
 * DTC's aging counter reaches the threshold it self-heals (status cleared).
 * Per-cycle status bits and the fault-detection counter are reset.
 */
void uds_dtc_store_operation_cycle(uds_dtc_store_t *s);

/** Clear DTC(s): group 0xFFFFFF clears all, else clears the matching DTC. */
void uds_dtc_store_clear(uds_dtc_store_t *s, uint32_t group);

/* --- Ready-made uds_config_t callbacks (store reached via ctx->config->app_data) --- */
int uds_dtc_store_list_cb(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                          uint16_t max);
int uds_dtc_store_extdata_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len);
int uds_dtc_store_clear_cb(struct uds_ctx *ctx, uint32_t group);

#endif /* UDS_DTC_STORE_H */
```

- [ ] **Step 3: Create the store implementation**

Create `src/services/uds_dtc_store.c`:

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "uds/uds_dtc_store.h"

#include "uds/uds_core.h"
#include "uds/uds_dtc.h"

void uds_dtc_store_init(uds_dtc_store_t *s, uds_dtc_record_t *backing, uint16_t capacity,
                        uint8_t aging_threshold)
{
    s->entries = backing;
    s->capacity = capacity;
    s->count = 0u;
    s->aging_threshold = aging_threshold;
}

int uds_dtc_store_register(uds_dtc_store_t *s, uint32_t dtc, uint8_t severity,
                           uint8_t functional_unit, uint8_t functional_group)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if (s->entries[i].dtc == dtc) {
            s->entries[i].severity = severity;
            s->entries[i].functional_unit = functional_unit;
            s->entries[i].functional_group = functional_group;
            return (int) i;
        }
    }
    if (s->count >= s->capacity) {
        return -1;
    }
    uds_dtc_record_t *r = &s->entries[s->count];
    r->dtc = dtc;
    r->status = 0u;
    r->severity = severity;
    r->functional_unit = functional_unit;
    r->fault_detection_counter = 0;
    r->aging_counter = 0u;
    r->functional_group = functional_group;
    s->count++;
    return (int) (s->count - 1u);
}

uds_dtc_record_t *uds_dtc_store_get(uds_dtc_store_t *s, uint32_t dtc)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if (s->entries[i].dtc == dtc) {
            return &s->entries[i];
        }
    }
    return NULL;
}

void uds_dtc_store_report_test(uds_dtc_store_t *s, uint32_t dtc, bool failed)
{
    uds_dtc_record_t *r = uds_dtc_store_get(s, dtc);
    if (r == NULL) {
        return;
    }
    if (failed) {
        if (r->fault_detection_counter < 0x7F) {
            r->fault_detection_counter++;
        }
        r->status |= (uint8_t) (UDS_DTC_STATUS_TEST_FAILED |
                                UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE |
                                UDS_DTC_STATUS_TEST_FAILED_SINCE_CLEAR);
        if (r->fault_detection_counter >= 0x7F) {
            r->status |= (uint8_t) (UDS_DTC_STATUS_CONFIRMED | UDS_DTC_STATUS_PENDING);
            r->aging_counter = 0u;
        }
    }
    else {
        if (r->fault_detection_counter > -128) {
            r->fault_detection_counter--;
        }
        r->status &= (uint8_t) ~UDS_DTC_STATUS_TEST_FAILED;
    }
}

void uds_dtc_store_operation_cycle(uds_dtc_store_t *s)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        uds_dtc_record_t *r = &s->entries[i];
        bool failed_this_cycle = (r->status & UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE) != 0u;
        if (!failed_this_cycle && ((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u)) {
            if (r->aging_counter < 0xFFu) {
                r->aging_counter++;
            }
            if (r->aging_counter >= s->aging_threshold) {
                r->status = 0u;
                r->aging_counter = 0u;
            }
        }
        r->status &= (uint8_t) ~UDS_DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE;
        r->fault_detection_counter = 0;
    }
}

void uds_dtc_store_clear(uds_dtc_store_t *s, uint32_t group)
{
    for (uint16_t i = 0u; i < s->count; i++) {
        if ((group == 0xFFFFFFu) || (s->entries[i].dtc == group)) {
            s->entries[i].status = 0u;
            s->entries[i].fault_detection_counter = 0;
            s->entries[i].aging_counter = 0u;
        }
    }
}

int uds_dtc_store_list_cb(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                          uint16_t max)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uint16_t n = 0u;
    for (uint16_t i = 0u; i < s->count; i++) {
        bool match = (status_mask == 0u) || ((s->entries[i].status & status_mask) != 0u);
        if (match) {
            if ((out != NULL) && (n < max)) {
                out[n] = s->entries[i];
            }
            n++;
        }
    }
    return (int) n;
}

int uds_dtc_store_extdata_cb(struct uds_ctx *ctx, uint32_t dtc, uint8_t record_num,
                             uint8_t *out_buf, uint16_t max_len)
{
    (void) record_num;
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uds_dtc_record_t *r = uds_dtc_store_get(s, dtc);
    if (r == NULL) {
        return 0;
    }
    if (max_len < 4u) {
        return -(int) UDS_NRC_RESPONSE_TOO_LONG;
    }
    out_buf[0] = r->status;
    out_buf[1] = record_num;
    out_buf[2] = r->aging_counter;
    out_buf[3] = (uint8_t) r->fault_detection_counter;
    return 4;
}

int uds_dtc_store_clear_cb(struct uds_ctx *ctx, uint32_t group)
{
    uds_dtc_store_t *s = (uds_dtc_store_t *) ctx->config->app_data;
    uds_dtc_store_clear(s, group);
    return UDS_OK;
}
```

- [ ] **Step 4: Add to library sources**

In `CMakeLists.txt`, add to the `LIBUDS_SOURCES` list (after line 31, `src/services/uds_service_roe.c`):

```cmake
    src/services/uds_dtc_store.c
```

- [ ] **Step 5: Write the failing store test**

Create `tests/unit/test_dtc_store.c`:

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "uds/uds_dtc.h"
#include "uds/uds_dtc_store.h"

static void test_store_register_and_get(void **state)
{
    (void) state;
    uds_dtc_record_t backing[4];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 4u, 40u);

    assert_int_equal(uds_dtc_store_register(&s, 0x012345u, 0x80u, 0x10u, UDS_DTC_FGID_EMISSIONS), 0);
    assert_int_equal(s.count, 1u);

    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x012345u);
    assert_non_null(r);
    assert_int_equal(r->severity, 0x80u);
    assert_int_equal(r->functional_group, UDS_DTC_FGID_EMISSIONS);
    assert_null(uds_dtc_store_get(&s, 0x999999u));
}

static void test_store_register_full(void **state)
{
    (void) state;
    uds_dtc_record_t backing[1];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 1u, 40u);
    assert_int_equal(uds_dtc_store_register(&s, 0x111111u, 0, 0, 0), 0);
    assert_int_equal(uds_dtc_store_register(&s, 0x222222u, 0, 0, 0), -1);
}

static void test_store_fault_counter_confirms(void **state)
{
    (void) state;
    uds_dtc_record_t backing[2];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 2u, 3u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);

    uds_dtc_store_report_test(&s, 0x111111u, true);
    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    assert_int_equal(r->fault_detection_counter, 1);
    assert_true((r->status & UDS_DTC_STATUS_TEST_FAILED) != 0u);
    assert_false((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);

    for (int i = 0; i < 200; i++) {
        uds_dtc_store_report_test(&s, 0x111111u, true);
    }
    assert_int_equal(r->fault_detection_counter, 0x7F);
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);
}

static void test_store_aging_self_heal(void **state)
{
    (void) state;
    uds_dtc_record_t backing[2];
    uds_dtc_store_t s;
    uds_dtc_store_init(&s, backing, 2u, 3u);
    uds_dtc_store_register(&s, 0x111111u, 0, 0, 0);

    /* Drive to confirmed. */
    for (int i = 0; i < 200; i++) {
        uds_dtc_store_report_test(&s, 0x111111u, true);
    }
    uds_dtc_record_t *r = uds_dtc_store_get(&s, 0x111111u);
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);

    /* 3 clean operation cycles -> aging threshold -> self-heal. */
    uds_dtc_store_operation_cycle(&s);
    uds_dtc_store_operation_cycle(&s);
    assert_true((r->status & UDS_DTC_STATUS_CONFIRMED) != 0u);
    uds_dtc_store_operation_cycle(&s);
    assert_int_equal(r->status, 0u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_store_register_and_get),
        cmocka_unit_test(test_store_register_full),
        cmocka_unit_test(test_store_fault_counter_confirms),
        cmocka_unit_test(test_store_aging_self_heal),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

- [ ] **Step 6: Register the test**

In `tests/CMakeLists.txt`, after the `add_uds_test(test_dtc_helpers ...)` line from Task 1, add:

```cmake
add_uds_test(test_dtc_store unit/test_dtc_store.c)
```

- [ ] **Step 7: Build and run — verify pass**

Run:
```bash
cd ~/projects/udslib && cmake -S . -B build >/dev/null && cmake --build build --target test_dtc_store 2>&1 | tail -5 && ctest --test-dir build -R test_dtc_store --output-on-failure
```
Expected: 4 store tests PASS.

- [ ] **Step 8: Format check & commit**

```bash
cd ~/projects/udslib && clang-format --dry-run --Werror include/uds/uds_dtc_store.h src/services/uds_dtc_store.c tests/unit/test_dtc_store.c include/uds/uds_config.h
git add include/uds/uds_dtc_store.h src/services/uds_dtc_store.c tests/unit/test_dtc_store.c include/uds/uds_config.h CMakeLists.txt tests/CMakeLists.txt
git -c user.email="14119286+w1ne@users.noreply.github.com" -c user.name="w1ne" commit -m "feat(dtc): add optional reference DTC store helper (#39)"
```

---

### Task 6: End-to-end store test, example, docs

**Files:**
- Modify: `tests/unit/test_service_dtc.c` (store-backed 0x19 integration test)
- Create: `examples/dtc_store/main.c` (+ minimal build wiring matching a sibling example)
- Modify: `CHANGELOG.md` (Unreleased / Added)
- Modify: `docs/SERVICE_COMPLIANCE.md` (0x19 coverage)

**Interfaces:**
- Consumes: everything from Tasks 1-5.

- [ ] **Step 1: Write a store-backed integration test**

In `tests/unit/test_service_dtc.c`, add a test that wires the store to the stack and exercises 0x19 0x02 end-to-end, then register it in `main()`:

```c
static void test_store_backed_read_dtc_0x02(void **state)
{
    (void) state;
    BEGIN_UDS_TEST(ctx, cfg);

    static uds_dtc_record_t backing[4];
    static uds_dtc_store_t store;
    uds_dtc_store_init(&store, backing, 4u, 40u);
    uds_dtc_store_register(&store, 0x012345u, UDS_DTC_SEVERITY_CHECK_IMMEDIATELY, 0x10u,
                           UDS_DTC_FGID_EMISSIONS);
    uds_dtc_store_report_test(&store, 0x012345u, true); /* sets testFailed (0x01) */

    cfg.app_data = &store;
    cfg.fn_dtc_list = uds_dtc_store_list_cb;
    cfg.fn_dtc_clear = uds_dtc_store_clear_cb;
    cfg.dtc_status_availability_mask = 0x7Fu;

    uint8_t req[] = {0x19, 0x02, 0x01}; /* status mask testFailed */

    will_return(mock_get_time, 1000);
    will_return(mock_get_time, 1000);
    expect_any(mock_tp_send, data);
    /* 59 02 <avail> + 1 * (DTC[3] status[1]) = 3 + 4 = 7 */
    expect_value(mock_tp_send, len, 7);
    will_return(mock_tp_send, 0);

    uds_input_sdu(&ctx, req, 3);

    assert_int_equal(g_tx_buf[1], 0x02);
    assert_int_equal(g_tx_buf[3], 0x01); /* DTC hi */
    assert_int_equal(g_tx_buf[4], 0x23);
    assert_int_equal(g_tx_buf[5], 0x45);
    assert_true((g_tx_buf[6] & UDS_DTC_STATUS_TEST_FAILED) != 0u);
}
```
Add `#include "uds/uds_dtc_store.h"` near the top of the file.

- [ ] **Step 2: Run it — verify pass**

Run:
```bash
cd ~/projects/udslib && cmake --build build --target test_service_dtc 2>&1 | tail -5 && ctest --test-dir build -R test_service_dtc --output-on-failure
```
Expected: PASS.

- [ ] **Step 3: Inspect a sibling example for the build pattern**

Run:
```bash
cd ~/projects/udslib && ls examples && sed -n '1,40p' examples/host_sim/main.c && cat examples/CMakeLists.txt 2>/dev/null | head -40
```
Note the include style, `uds_init`/`uds_input_sdu` usage, and how examples are registered. Mirror the smallest sibling example's structure for `examples/dtc_store`.

- [ ] **Step 4: Create the example**

Create `examples/dtc_store/main.c` — register the issue's three DTCs (`0x012345`, `0xDCBA98`, `0xFFFFFF`) into a store, wire the store callbacks, print each DTC's `uds_dtc_category()`, and feed a `{0x19,0x02,0xFF}` request through `uds_input_sdu`, printing the response. Use the copyright header and the include pattern observed in Step 3. Register it in the example build the same way the sibling example is registered (e.g. an `add_subdirectory`/`add_executable` entry in `examples/CMakeLists.txt`).

- [ ] **Step 5: Build the example**

Run:
```bash
cd ~/projects/udslib && cmake -S . -B build >/dev/null && cmake --build build 2>&1 | tail -15
```
Expected: clean build (no warnings). Run the example binary if the project builds example executables and confirm it prints the three DTCs with P/U/U categories.

- [ ] **Step 6: Update CHANGELOG**

In `CHANGELOG.md`, under `## [Unreleased]` → `### Added`, add:

```markdown
- **ReadDTCInformation (0x19) — severity, fault-detection counter, and WWH-OBD subfunctions**: added reportNumberOfDTCBySeverityMaskRecord (0x07), reportDTCBySeverityMaskRecord (0x08), reportSeverityInformationOfDTC (0x09), reportDTCFaultDetectionCounter (0x14), reportWWHOBDDTCByMaskRecord (0x42), and reportWWHOBDDTCWithPermanentStatus (0x55). The DTC record now carries severity, functional unit, fault-detection counter, aging counter, and functional group; all new subfunctions are formatted by the library from records supplied via the existing `fn_dtc_list` hook. New config field `dtc_severity_availability_mask`. (#39)
- **DTC classification helpers** (`uds/uds_dtc.h`): `uds_dtc_category()` decodes Powertrain/Chassis/Body/Network from a DTC, plus named `UDS_DTC_STATUS_*`, `UDS_DTC_SEVERITY_*`, and `UDS_DTC_FGID_*` constants. (#39)
- **Optional reference DTC store** (`uds/uds_dtc_store.h`): opt-in, application-provides-storage helper that manages DTC instances (register/get/clear), tracks the fault-detection counter and aging (with self-heal), and supplies ready-made `fn_dtc_list`/`fn_dtc_extdata`/`fn_dtc_clear` callbacks. The protocol core does not depend on it. (#39)
```

- [ ] **Step 7: Update the compliance doc**

In `docs/SERVICE_COMPLIANCE.md`, find the ReadDTCInformation (0x19) section and add 0x07/0x08/0x09/0x14/0x42/0x55 to the list of supported subfunctions (match the existing formatting; if the file enumerates subfunctions in a table, add one row each).

- [ ] **Step 8: Full suite + format, then commit**

Run:
```bash
cd ~/projects/udslib && cmake --build build 2>&1 | tail -5 && ctest --test-dir build --output-on-failure 2>&1 | tail -20
clang-format --dry-run --Werror tests/unit/test_service_dtc.c examples/dtc_store/main.c
```
Expected: entire suite PASS; format clean.

```bash
cd ~/projects/udslib && git add tests/unit/test_service_dtc.c examples/ CHANGELOG.md docs/SERVICE_COMPLIANCE.md
git -c user.email="14119286+w1ne@users.noreply.github.com" -c user.name="w1ne" commit -m "docs(dtc): example, changelog, and compliance for DTC enrichment (#39)"
```

---

## Self-Review

**Spec coverage:**
- Extended `uds_dtc_record_t` → Task 1. ✓
- Classification decoder + named macros → Task 1. ✓
- Subfunctions 0x07/0x08/0x09 → Task 2; 0x14 → Task 3; 0x42/0x55 → Task 4. ✓
- `dtc_severity_availability_mask` → Task 2; `app_data` → Task 5. ✓
- Widened `UDS_MASK_SUB_19` → Task 2 (all bits set at once). ✓
- Optional DTC store (register/get/report_test/operation_cycle/clear + callbacks) → Task 5. ✓
- Tests (per-subfunction wire, classifier, store counter/aging, store-backed e2e) → Tasks 1-6. ✓
- Example with the issue's three DTCs → Task 6. ✓
- CHANGELOG + compliance doc → Task 6. ✓

**Type consistency:** `uds_dtc_record_t` field names (`severity`, `functional_unit`, `fault_detection_counter`, `aging_counter`, `functional_group`) used identically across Tasks 1-6. Store API names match between `uds_dtc_store.h` (Task 5) and tests/example. Callback signatures match `uds_config_t`'s existing `fn_dtc_list`/`fn_dtc_clear` and the snapshot/extdata prototypes.

**Note on ISO byte layouts:** The exact response framing for 0x07/0x08/0x09/0x14/0x42/0x55 follows ISO 14229-1 as transcribed in the spec. If a discrepancy with the standard surfaces during implementation, adjust the wire-building code and the corresponding test's expected bytes together — never loosen a test to pass.
