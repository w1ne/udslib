# ISO-TP Physical/Functional Addressing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Distinguish physically- vs. functionally-addressed UDS requests by received CAN ID, gate service dispatch on a per-service `address_mode`, and suppress the ISO 14229-1 negative-response set for functional requests.

**Architecture:** ISO-TP classifies each inbound frame by CAN ID (`rx_id` → physical, `rx_id_func` → functional; functional is Single-Frame only). The mode reaches the core through a new `uds_input_sdu_addr(ctx,data,len,addr)`; `uds_input_sdu` becomes a thin physical-default wrapper so existing callers/tests are untouched. `handle_request` gates on a new last-field `address_mode` of `uds_service_entry_t` (0 = both); `uds_send_nrc` centrally suppresses functional NRCs.

**Tech Stack:** C11, CMake, cmocka, linker `-Wl,--wrap=` for boundary interception. Builds on Part 1 (full-duplex ISO-TP), already merged to develop.

## Global Constraints

- License header on every new file (copy from any existing source): `/*\n * Copyright (c) 2026 Andrii Shylenko\n * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0\n */`.
- No dynamic allocation. MISRA-leaning style: explicit `uint8_t`/`uint16_t` casts, NULL guards on public entry points, match the surrounding file.
- Commit messages: **no Claude / AI / assistant references**; reference the issue with `(#42)`.
- PR target: `develop`. Work branch `feature/isotp-addressing-issue42` (created off latest `origin/develop`; spec already committed there).
- Formatting gate: CI runs **clang-format** in an Ubuntu-24.04 Docker image (clang-format 18). `clang-format-14` is installed locally and is a close proxy — run `clang-format-14 --dry-run --Werror` on changed `.c/.h` files; match surrounding style, never reformat unrelated lines.
- Build & test (from repo root): `cmake -S . -B build` · `cmake --build build` · `ctest --test-dir build --output-on-failure` · one test: `ctest --test-dir build -R <name> --output-on-failure`.
- `address_mode == 0` MUST be treated as `UDS_ADDR_PHYSICAL | UDS_ADDR_FUNCTIONAL` (both). The core `core_services[]` table stays unset (both).
- Functional addressing is **Single-Frame only**; a functionally-addressed FF/CF/FC is ignored.
- Functional NRC suppress-set (suppressed only when the request is functional): `0x11`, `0x12`, `0x7E`, `0x7F`, `0x31`. All other NRCs and all positive responses are still sent (on the physical `tx_id`).
- Responses are always transmitted on the physical `tx_id` (unchanged).

---

## File Structure

- `include/uds/uds_core.h` — Modify: add `uds_addr_mode_t` enum + `uds_input_sdu_addr` declaration; doc the `uds_input_sdu` wrapper.
- `include/uds/uds_config.h` — Modify: `address_mode` (last field of `uds_service_entry_t`); `req_addr_mode` on the context struct (near `active_session`).
- `src/core/uds_internal.h` — Modify: add `UDS_NRC_SUBFUNC_NOT_SUPP_IN_SESS 0x7Eu`.
- `src/core/uds_core.c` — Modify: split `uds_input_sdu` into wrapper + `uds_input_sdu_addr` (sets `req_addr_mode`); addressing gate in `handle_request`; functional suppression in `uds_send_nrc`.
- `include/uds/uds_isotp.h` — Modify: `rx_id_func` field + `uds_tp_isotp_set_functional_id` decl.
- `src/transport/uds_tp_isotp.c` — Modify: init `rx_id_func=0`; setter; classify in `rx_callback`; functional SF-only; `uds_rx_sf` gains an `addr` param.
- `tests/unit/test_addressing_dispatch.c` — Create (Task 1).
- `tests/unit/test_tp_addressing.c` — Create (Task 2).
- `tests/unit/test_issue42_functional.c` — Create (Task 3).
- `tests/CMakeLists.txt` — Modify: register the three suites.
- `docs/TRANSPORT.md`, `docs/SERVICE_COMPLIANCE.md`, `CHANGELOG.md` — Modify (Task 4).

---

## Task 1: Core addressing — entry plumbing, dispatch gate, NRC suppression

**Files:**
- Modify: `include/uds/uds_core.h` (after the return-code defines / before `uds_input_sdu`)
- Modify: `include/uds/uds_config.h` (`uds_service_entry_t` ~line 191-199; context struct near `active_session` ~line 649)
- Modify: `src/core/uds_internal.h` (NRC defines ~line 31)
- Modify: `src/core/uds_core.c` (`handle_request` 251; `uds_input_sdu` 566; `uds_send_nrc` 669)
- Create: `tests/unit/test_addressing_dispatch.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing core (`handle_request`, `uds_send_nrc`, `uds_service_entry_t`).
- Produces:
  - `typedef enum { UDS_ADDR_PHYSICAL = (1u<<0), UDS_ADDR_FUNCTIONAL = (1u<<1) } uds_addr_mode_t;`
  - `void uds_input_sdu_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_addr_mode_t addr);`
  - `uds_service_entry_t.address_mode` (uint8_t, last field).
  - `ctx->req_addr_mode` (uint8_t).

- [ ] **Step 1: Add the enum and `uds_input_sdu_addr` declaration to the public header**

In `include/uds/uds_core.h`, immediately before the declaration of `uds_input_sdu` (line ~117), add:

```c
/**
 * @brief UDS addressing mode of an incoming request.
 *
 * Physical = point-to-point (one ECU). Functional = broadcast (one-to-many).
 * Used as a bitmask in uds_service_entry_t.address_mode (0 there means both).
 */
typedef enum
{
    UDS_ADDR_PHYSICAL = (1u << 0),
    UDS_ADDR_FUNCTIONAL = (1u << 1)
} uds_addr_mode_t;

/**
 * @brief Input a UDS SDU with an explicit addressing mode.
 *
 * Same as uds_input_sdu(), but records whether the request was physically or
 * functionally addressed (from the received CAN ID). Functionally addressed
 * requests are gated per service and have certain negative responses
 * suppressed (ISO 14229-1).
 *
 * @param ctx  Initialized context.
 * @param data SDU buffer.
 * @param len  SDU length in bytes.
 * @param addr UDS_ADDR_PHYSICAL or UDS_ADDR_FUNCTIONAL.
 */
void uds_input_sdu_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_addr_mode_t addr);
```

Then update the existing `uds_input_sdu` doc comment to note it is the physical-default wrapper: append a line `@note Equivalent to uds_input_sdu_addr(ctx, data, len, UDS_ADDR_PHYSICAL).`

- [ ] **Step 2: Add `address_mode` to the service entry and `req_addr_mode` to the context**

In `include/uds/uds_config.h`, change `uds_service_entry_t` (lines ~191-199) to add the field LAST:

```c
typedef struct
{
    uint8_t sid;                   /**< Service ID (e.g., 0x22) */
    uint16_t min_len;              /**< Minimum required request length */
    uint8_t session_mask;          /**< Allowed sessions bitmask */
    uint16_t security_mask;        /**< Minimum security level required (bitmask or level) */
    uds_service_handler_t handler; /**< Function pointer to handler */
    const uint8_t *sub_mask; /**< Optional bitmask of supported 7-bit subfunctions (16 bytes) */
    uint8_t address_mode;    /**< Allowed addressing (UDS_ADDR_* bitmask); 0 = both */
} uds_service_entry_t;
```

In the context struct (the `struct uds_ctx { ... }`), add near `active_session` (line ~649):

```c
    uint8_t req_addr_mode; /**< Addressing mode of the request in flight (UDS_ADDR_*) */
```

- [ ] **Step 3: Add the 0x7E NRC constant**

In `src/core/uds_internal.h`, after `UDS_NRC_SERVICE_NOT_SUPP_IN_SESS 0x7Fu` (line ~31) add:

```c
#define UDS_NRC_SUBFUNC_NOT_SUPP_IN_SESS 0x7Eu
```

- [ ] **Step 4: Split `uds_input_sdu` into a wrapper + `uds_input_sdu_addr`**

In `src/core/uds_core.c`, replace the function header line of `uds_input_sdu` (line 566) so the body becomes `uds_input_sdu_addr`, set `req_addr_mode`, and add the wrapper. Concretely:

1. Change `void uds_input_sdu(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)` to
   `void uds_input_sdu_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len, uds_addr_mode_t addr)`.
2. Inside it, immediately after the `if (!data || len == 0u) { ...unlock...; return; }` guard and before `uint8_t sid = data[0];`, insert:
   ```c
       ctx->req_addr_mode = (uint8_t) addr;
   ```
3. Directly above the (now renamed) function, add the wrapper:
   ```c
   void uds_input_sdu(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
   {
       uds_input_sdu_addr(ctx, data, len, UDS_ADDR_PHYSICAL);
   }
   ```

- [ ] **Step 5: Add the addressing gate in `handle_request`**

In `src/core/uds_core.c`, in `handle_request`, immediately after the `if (!service) { uds_send_nrc(..., UDS_NRC_SERVICE_NOT_SUPPORTED); return; }` block (ends ~line 259) and before the session check, insert:

```c
    /* Addressing gate: does this service accept the request's addressing mode?
       address_mode == 0 means "both" (backward compatible). */
    uint8_t allowed_addr = (service->address_mode != 0u)
                               ? service->address_mode
                               : (uint8_t) (UDS_ADDR_PHYSICAL | UDS_ADDR_FUNCTIONAL);
    if ((allowed_addr & ctx->req_addr_mode) == 0u) {
        if (ctx->req_addr_mode == (uint8_t) UDS_ADDR_FUNCTIONAL) {
            return; /* functional broadcast for an unsupported addressing: stay silent */
        }
        uds_send_nrc(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
        return;
    }
```

- [ ] **Step 6: Add functional NRC suppression in `uds_send_nrc`**

In `src/core/uds_core.c`, in `uds_send_nrc`, after the pending-flag clear block (ends ~line 683) and before `ctx->config->tx_buffer[0] = ...` (line 685), insert:

```c
    /* ISO 14229-1: a functionally addressed request must not elicit these
       negative responses (avoid flooding a shared bus when many ECUs answer).
       Captured inner dispatches (0x84/0x86) are never functional. */
    if (ctx->req_addr_mode == (uint8_t) UDS_ADDR_FUNCTIONAL && !ctx->secure_capturing &&
        (nrc == UDS_NRC_SERVICE_NOT_SUPPORTED || nrc == UDS_NRC_SUBFUNCTION_NOT_SUPPORTED ||
         nrc == UDS_NRC_SUBFUNC_NOT_SUPP_IN_SESS || nrc == UDS_NRC_SERVICE_NOT_SUPP_IN_SESS ||
         nrc == UDS_NRC_REQUEST_OUT_OF_RANGE)) {
        return UDS_OK; /* suppressed: emit nothing on the bus */
    }
```

- [ ] **Step 7: Write the core addressing test suite**

Create `tests/unit/test_addressing_dispatch.c`. It drives the core directly via `uds_input_sdu` / `uds_input_sdu_addr` with a real `uds_ctx`, capturing emitted frames through `fn_tp_send`. Uses two custom user services to exercise gating, plus an unknown SID for suppression.

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_config.h"

/* Captured server output (the response frame, if any). */
static uint8_t g_tx[64];
static uint16_t g_tx_len;
static int g_tx_calls;

static int fn_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    g_tx_calls++;
    if (len <= sizeof(g_tx)) {
        memcpy(g_tx, data, len);
        g_tx_len = len;
    }
    return 0;
}

static uint32_t g_time;
static uint32_t time_ms(void) { return g_time; }

/* A service that echoes a fixed positive response (0x40|SID). */
static int svc_ok(struct uds_ctx *ctx, const uint8_t *data, uint16_t len);

/* User services: 0xA0 = both (unset), 0xA1 = physical-only, 0xA2 = functional-only. */
static const uds_service_entry_t k_services[] = {
    {0xA0u, 1u, UDS_SESSION_ALL, 0u, svc_ok, NULL, 0u},                 /* both */
    {0xA1u, 1u, UDS_SESSION_ALL, 0u, svc_ok, NULL, UDS_ADDR_PHYSICAL},  /* physical only */
    {0xA2u, 1u, UDS_SESSION_ALL, 0u, svc_ok, NULL, UDS_ADDR_FUNCTIONAL} /* functional only */
};

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx[64];
static uint8_t g_txbuf[64];

static int svc_ok(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) len;
    ctx->config->tx_buffer[0] = (uint8_t) (data[0] + 0x40u);
    return uds_send_response(ctx, 1u);
}

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.get_time_ms = time_ms;
    g_cfg.fn_tp_send = fn_tp_send;
    g_cfg.rx_buffer = g_rx;
    g_cfg.rx_buffer_size = sizeof(g_rx);
    g_cfg.tx_buffer = g_txbuf;
    g_cfg.tx_buffer_size = sizeof(g_txbuf);
    g_cfg.p2_ms = 50;
    g_cfg.p2_star_ms = 5000;
    g_cfg.user_services = k_services;
    g_cfg.user_service_count = 3u;
    assert_int_equal(uds_init(&g_ctx, &g_cfg), UDS_OK);
    g_time = 0;
    g_tx_len = 0;
    g_tx_calls = 0;
    return 0;
}

/* 6: address_mode==0 answers BOTH physical and functional. */
static void test_both_default(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA0u};
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE0u); /* 0xA0|0x40 */

    g_tx_calls = 0;
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE0u);
}

/* 7: physical-only service — functional request is silent, physical answers. */
static void test_physical_only(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA1u};
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 0); /* silent */

    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE1u);
}

/* 8: functional-only service — physical request -> NRC 0x11; functional answers. */
static void test_functional_only(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA2u};
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0x7Fu); /* negative response */
    assert_int_equal(g_tx[1], 0xA2u);
    assert_int_equal(g_tx[2], 0x11u); /* serviceNotSupported */

    g_tx_calls = 0;
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE2u);
}

/* 9: functional request for an UNKNOWN SID -> 0x11 suppressed (silent);
      same request physical -> 0x11 emitted. */
static void test_suppress_unknown_sid(void **state)
{
    (void) state;
    uint8_t req[1] = {0xBFu}; /* not in any table */
    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_FUNCTIONAL);
    assert_int_equal(g_tx_calls, 0); /* 0x11 suppressed */

    uds_input_sdu_addr(&g_ctx, req, 1u, UDS_ADDR_PHYSICAL);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[2], 0x11u);
}

/* 12: legacy uds_input_sdu still dispatches as physical. */
static void test_legacy_entry_physical(void **state)
{
    (void) state;
    uint8_t req[1] = {0xA1u}; /* physical-only */
    uds_input_sdu(&g_ctx, req, 1u);
    assert_int_equal(g_tx_calls, 1);
    assert_int_equal(g_tx[0], 0xE1u); /* answered => treated as physical */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_both_default, setup, NULL),
        cmocka_unit_test_setup_teardown(test_physical_only, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_only, setup, NULL),
        cmocka_unit_test_setup_teardown(test_suppress_unknown_sid, setup, NULL),
        cmocka_unit_test_setup_teardown(test_legacy_entry_physical, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

> Note: this suite must NOT be `--wrap`ped (it uses the real `uds_input_sdu`/`uds_input_sdu_addr` and real dispatch). If `uds_init` rejects the config, read its required fields in `src/core/uds_core.c` and add them to `setup`.

- [ ] **Step 8: Register the suite**

In `tests/CMakeLists.txt`, near the other core suites, add:

```cmake
add_uds_test(test_addressing_dispatch unit/test_addressing_dispatch.c)
```

- [ ] **Step 9: Build, run the new suite, then the full suite**

Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build -R test_addressing_dispatch --output-on-failure && ctest --test-dir build --output-on-failure
```
Expected: new suite 5/5 PASS; full suite PASS (the `address_mode` field addition compiles against every existing positional initializer; `uds_input_sdu` wrapper keeps all prior tests green).

- [ ] **Step 10: Format and commit**

```bash
clang-format-14 -i include/uds/uds_core.h include/uds/uds_config.h src/core/uds_internal.h src/core/uds_core.c tests/unit/test_addressing_dispatch.c
git add include/uds/uds_core.h include/uds/uds_config.h src/core/uds_internal.h src/core/uds_core.c tests/unit/test_addressing_dispatch.c tests/CMakeLists.txt
git commit -m "feat(core): physical/functional addressing gate + functional NRC suppression (#42)"
```

---

## Task 2: ISO-TP functional RX ID + classification

**Files:**
- Modify: `include/uds/uds_isotp.h` (ctx struct; new prototype)
- Modify: `src/transport/uds_tp_isotp.c` (init; setter; `uds_rx_sf` signature; `rx_callback`)
- Create: `tests/unit/test_tp_addressing.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `uds_addr_mode_t`, `uds_input_sdu`, `uds_input_sdu_addr` (Task 1).
- Produces: `uds_isotp_ctx_t.rx_id_func`; `void uds_tp_isotp_set_functional_id(uds_isotp_ctx_t *iso, uint32_t rx_id_func);`

- [ ] **Step 1: Add `rx_id_func` and the setter prototype to the header**

In `include/uds/uds_isotp.h`, in `uds_isotp_ctx_t` right after `uint32_t rx_id;` (line ~109), add:

```c
    uint32_t rx_id_func; /**< Functional/broadcast RX ID; 0 = functional disabled */
```

After the `uds_tp_isotp_set_mode` prototype, add:

```c
/**
 * @brief Set the functional (broadcast) RX ID for this channel.
 *
 * A received frame whose CAN ID equals @p rx_id_func is treated as a
 * functionally addressed (one-to-many) request and delivered to the core with
 * UDS_ADDR_FUNCTIONAL. Functional reception is Single-Frame only (ISO 15765-2):
 * a functionally addressed FF/CF/FC is ignored. Pass 0 (the default) to disable
 * functional reception.
 *
 * @param iso         ISO-TP context.
 * @param rx_id_func  Functional RX CAN ID, or 0 to disable.
 */
void uds_tp_isotp_set_functional_id(uds_isotp_ctx_t *iso, uint32_t rx_id_func);
```

- [ ] **Step 2: Initialize `rx_id_func` and implement the setter**

In `src/transport/uds_tp_isotp.c`, in `uds_tp_isotp_init`, after `iso->rx_id = rx_id;` add:

```c
    iso->rx_id_func = 0u; /* functional reception disabled until configured */
```

After `uds_tp_isotp_set_mode` (or `uds_tp_isotp_set_fd`), add:

```c
void uds_tp_isotp_set_functional_id(uds_isotp_ctx_t *iso, uint32_t rx_id_func)
{
    if (!iso) {
        return;
    }
    iso->rx_id_func = rx_id_func;
}
```

- [ ] **Step 3: Give `uds_rx_sf` an addressing parameter**

In `src/transport/uds_tp_isotp.c`, change the `uds_rx_sf` definition signature to take the addressing mode, and route the delivery accordingly. Replace the final delivery call `uds_input_sdu(uds, &data[data_offset], (uint16_t) sdu_len);` with the addressing-aware version. The full function becomes:

```c
static void uds_rx_sf(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len,
                      uint8_t addr)
{
    /* A new reception supersedes any in-progress reception. */
    iso->rx_state = ISOTP_RX_IDLE;

    /* Half-duplex: a new inbound message terminates an in-flight transmission. */
    if (iso->mode == ISOTP_HALF_DUPLEX) {
        iso->tx_state = ISOTP_TX_IDLE;
        iso->timer_n_bs = 0u;
    }

    uint8_t sdu_len = (uint8_t) (data[0] & 0x0Fu);
    uint8_t data_offset = 1;

    if (sdu_len == 0u) {
        /* CAN-FD SF: Byte 0 is 0x00, Byte 1 is Length */
        if (len < 2u) return;
        sdu_len = data[1];
        data_offset = 2;
        if (sdu_len == 0) return;
    }

    if (sdu_len > (len - data_offset)) {
        return;
    }

    if (addr == (uint8_t) UDS_ADDR_FUNCTIONAL) {
        uds_input_sdu_addr(uds, &data[data_offset], (uint16_t) sdu_len, UDS_ADDR_FUNCTIONAL);
    }
    else {
        uds_input_sdu(uds, &data[data_offset], (uint16_t) sdu_len);
    }
}
```

> Verify the half-duplex abort block and SF decode above match the current file (Part 1 shipped them); preserve them exactly — only the signature and the final delivery branch change.

- [ ] **Step 4: Classify by CAN ID in `uds_isotp_rx_callback`**

In `src/transport/uds_tp_isotp.c`, replace the body of `uds_isotp_rx_callback` (lines 457-489) with:

```c
void uds_isotp_rx_callback(uds_isotp_ctx_t *iso, struct uds_ctx *uds, uint32_t id,
                           const uint8_t *data, uint8_t len)
{
    if (!iso || !data || len == 0u) {
        return;
    }

    uint8_t addr;
    if (id == iso->rx_id) {
        addr = (uint8_t) UDS_ADDR_PHYSICAL;
    }
    else if ((iso->rx_id_func != 0u) && (id == iso->rx_id_func)) {
        addr = (uint8_t) UDS_ADDR_FUNCTIONAL;
    }
    else {
        return; /* not for us */
    }

    uint8_t pci = data[0] & 0xF0;

    if (addr == (uint8_t) UDS_ADDR_FUNCTIONAL) {
        /* Functional addressing is Single-Frame only (ISO 15765-2):
           segmented transfer and flow control are undefined for one-to-many. */
        if (pci == ISOTP_PCI_SF) {
            uds_rx_sf(iso, uds, data, len, (uint8_t) UDS_ADDR_FUNCTIONAL);
        }
        return;
    }

    switch (pci) {
        case ISOTP_PCI_SF:
            uds_rx_sf(iso, uds, data, len, (uint8_t) UDS_ADDR_PHYSICAL);
            break;
        case ISOTP_PCI_FF:
            uds_rx_ff(iso, uds, data, len);
            break;
        case ISOTP_PCI_CF:
            uds_rx_cf(iso, uds, data, len);
            break;
        case ISOTP_PCI_FC:
            uds_rx_fc(iso, data, len);
            break;
        default:
            break;
    }
}
```

- [ ] **Step 5: Write the ISO-TP addressing test suite**

Create `tests/unit/test_tp_addressing.c`. It wraps BOTH boundary symbols so it can prove which entry each path uses and with what mode.

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id; (void) data; (void) len;
    return 0;
}

/* Physical deliveries land here (transport calls uds_input_sdu). */
void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    check_expected(len);
    check_expected_ptr(data);
}

/* Functional deliveries land here (transport calls uds_input_sdu_addr). */
void __wrap_uds_input_sdu_addr(struct uds_ctx *ctx, const uint8_t *data, uint16_t len,
                               uds_addr_mode_t addr)
{
    (void) ctx;
    check_expected(len);
    check_expected_ptr(data);
    check_expected(addr);
}

static uds_isotp_ctx_t g_iso;
static uint8_t g_sdu[256];

static int setup(void **state)
{
    (void) state;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_sdu, sizeof(g_sdu));
    return 0;
}

/* 1: frame on rx_id -> physical entry. */
static void test_physical_sf(void **state)
{
    (void) state;
    uint8_t sf[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uint8_t expect[2] = {0x3E, 0x00};
    expect_value(__wrap_uds_input_sdu, len, 2);
    expect_memory(__wrap_uds_input_sdu, data, expect, 2);
    uds_isotp_rx_callback(&g_iso, NULL, 0x7E8, sf, 8);
}

/* 2: frame on rx_id_func -> functional entry with UDS_ADDR_FUNCTIONAL. */
static void test_functional_sf(void **state)
{
    (void) state;
    uds_tp_isotp_set_functional_id(&g_iso, 0x7DF);
    uint8_t sf[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uint8_t expect[2] = {0x3E, 0x00};
    expect_value(__wrap_uds_input_sdu_addr, len, 2);
    expect_memory(__wrap_uds_input_sdu_addr, data, expect, 2);
    expect_value(__wrap_uds_input_sdu_addr, addr, UDS_ADDR_FUNCTIONAL);
    uds_isotp_rx_callback(&g_iso, NULL, 0x7DF, sf, 8);
}

/* 3: functional disabled by default (rx_id_func == 0): foreign ID ignored. */
static void test_functional_disabled(void **state)
{
    (void) state;
    /* no functional id set */
    uint8_t sf[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, NULL, 0x7DF, sf, 8); /* not rx_id, func disabled -> ignored */
    /* no expectation set on either wrap => any delivery fails the test */
}

/* 4: functionally addressed FF is ignored (SF-only). */
static void test_functional_ff_ignored(void **state)
{
    (void) state;
    uds_tp_isotp_set_functional_id(&g_iso, 0x7DF);
    uint8_t ff[8] = {0x10, 0x14, 0, 0, 0, 0, 0, 0}; /* FF, 20 bytes */
    uds_isotp_rx_callback(&g_iso, NULL, 0x7DF, ff, 8);
    /* no FC emitted (mock_can_send asserts nothing, but a real FC would be a bug:
       the functional path never calls uds_rx_ff), and no delivery occurs. */
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_physical_sf, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_sf, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_disabled, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_ff_ignored, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

> Physical multi-frame reassembly (spec test 5) remains covered by the existing
> `test_tp_isotp_*` suites (they wrap `uds_input_sdu`, which the physical path
> still calls) — no new case needed here.

- [ ] **Step 6: Register the suite (wrap BOTH boundary symbols)**

In `tests/CMakeLists.txt`, add:

```cmake
add_uds_test(test_tp_addressing unit/test_tp_addressing.c)
target_link_options(test_tp_addressing PRIVATE -Wl,--wrap=uds_input_sdu -Wl,--wrap=uds_input_sdu_addr)
```

- [ ] **Step 7: Build and run**

Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build -R test_tp_addressing --output-on-failure && ctest --test-dir build --output-on-failure
```
Expected: new suite 4/4 PASS; full suite PASS (existing `--wrap=uds_input_sdu` suites still intercept the unchanged physical path).

- [ ] **Step 8: Format and commit**

```bash
clang-format-14 -i include/uds/uds_isotp.h src/transport/uds_tp_isotp.c tests/unit/test_tp_addressing.c
git add include/uds/uds_isotp.h src/transport/uds_tp_isotp.c tests/unit/test_tp_addressing.c tests/CMakeLists.txt
git commit -m "feat(isotp): functional (broadcast) RX ID and SF-only classification (#42)"
```

---

## Task 3: Full-stack functional regression

**Files:**
- Create: `tests/unit/test_issue42_functional.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: full public API + ISO-TP (real `uds_input_sdu`/`uds_input_sdu_addr`).

- [ ] **Step 1: Write the regression**

Create `tests/unit/test_issue42_functional.c`. One ISO-TP context with a functional ID; the application wires `fn_tp_send → uds_isotp_send`. Drives real frames through `uds_isotp_rx_callback`.

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/* Issue #42 (part 2): functional addressing end to end. A functionally
 * addressed TesterPresent is serviced; a functional request for an unsupported
 * service is silently ignored (suppressed 0x11), while the same physical
 * request returns NRC 0x11. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

static uint8_t g_last_tx[8];
static uint8_t g_last_len;
static int g_can_calls;

static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    g_can_calls++;
    if (len <= sizeof(g_last_tx)) {
        memcpy(g_last_tx, data, len);
        g_last_len = len;
    }
    return 0;
}

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx[64];
static uint8_t g_txbuf[64];
static uint32_t g_time;
static uint32_t time_ms(void) { return g_time; }

static uds_isotp_ctx_t g_iso;
static uint8_t g_sdu[64];

/* Server response transport: send via ISO-TP on the physical tx_id. */
static int fn_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    return uds_isotp_send(&g_iso, data, len);
}

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.get_time_ms = time_ms;
    g_cfg.fn_tp_send = fn_tp_send;
    g_cfg.rx_buffer = g_rx;
    g_cfg.rx_buffer_size = sizeof(g_rx);
    g_cfg.tx_buffer = g_txbuf;
    g_cfg.tx_buffer_size = sizeof(g_txbuf);
    g_cfg.p2_ms = 50;
    g_cfg.p2_star_ms = 5000;
    assert_int_equal(uds_init(&g_ctx, &g_cfg), UDS_OK);

    g_time = 0;
    g_can_calls = 0;
    g_last_len = 0;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E8, 0x7E0, g_sdu, sizeof(g_sdu));
    uds_tp_isotp_set_functional_id(&g_iso, 0x7DF);
    return 0;
}

/* 14: functional TesterPresent is serviced (positive response emitted). */
static void test_functional_tester_present(void **state)
{
    (void) state;
    uint8_t tp[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7DF, tp, 8);
    assert_int_equal(g_can_calls, 1);          /* one response frame */
    assert_int_equal(g_last_tx[0], 0x02);      /* SF, len 2 */
    assert_int_equal(g_last_tx[1], 0x7E);      /* 0x3E | 0x40 */
}

/* 15: functional request for an unsupported SID -> silence; physical -> NRC 0x11. */
static void test_functional_unsupported_silent(void **state)
{
    (void) state;
    uint8_t req[8] = {0x01, 0xBF, 0, 0, 0, 0, 0, 0}; /* unknown SID 0xBF */
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7DF, req, 8); /* functional */
    assert_int_equal(g_can_calls, 0);                     /* suppressed: silence */

    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E0, req, 8); /* physical */
    assert_int_equal(g_can_calls, 1);
    assert_int_equal(g_last_tx[1], 0x7F);                 /* negative response */
    assert_int_equal(g_last_tx[2], 0xBF);
    assert_int_equal(g_last_tx[3], 0x11);                 /* serviceNotSupported */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_functional_tester_present, setup, NULL),
        cmocka_unit_test_setup_teardown(test_functional_unsupported_silent, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

> If `uds_init` requires additional mandatory config fields, read its validation
> in `src/core/uds_core.c` and set them minimally. Do not weaken the silence
> assertion (`g_can_calls == 0`) for the functional unsupported case — that is the
> load-bearing check.

- [ ] **Step 2: Register (NOT wrapped — real core)**

In `tests/CMakeLists.txt`, add:

```cmake
# Regression for issue #42 part 2: functional addressing end to end.
add_uds_test(test_issue42_functional unit/test_issue42_functional.c)
```

- [ ] **Step 3: Build and run**

Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build -R test_issue42_functional --output-on-failure && ctest --test-dir build --output-on-failure
```
Expected: 2/2 PASS; full suite PASS.

- [ ] **Step 4: Format and commit**

```bash
clang-format-14 -i tests/unit/test_issue42_functional.c
git add tests/unit/test_issue42_functional.c tests/CMakeLists.txt
git commit -m "test(isotp): full-stack functional-addressing regression (#42)"
```

---

## Task 4: Documentation

**Files:**
- Modify: `docs/TRANSPORT.md`, `docs/SERVICE_COMPLIANCE.md`, `CHANGELOG.md`

**Interfaces:** none.

- [ ] **Step 1: Document functional addressing in `docs/TRANSPORT.md`**

Read `docs/TRANSPORT.md` first to match structure, then add a section near the duplex-mode section:

```markdown
## Physical vs. functional addressing

Each ISO-TP channel recognises a physical (point-to-point) RX ID and an optional
functional (broadcast, one-to-many) RX ID, set with
`uds_tp_isotp_set_functional_id(&iso, id)` (pass 0 to disable; disabled by
default). A frame on the physical `rx_id` is delivered as `UDS_ADDR_PHYSICAL`; a
frame on the functional ID as `UDS_ADDR_FUNCTIONAL`.

Functional addressing is **Single-Frame only** (ISO 15765-2 has no flow control
for one-to-many): a functionally addressed FF/CF/FC is ignored. Responses are
always sent on the physical `tx_id`.

A service declares which addressing it accepts via `address_mode` in its
`uds_service_entry_t` (a `UDS_ADDR_*` bitmask). `0` (the default, and every
built-in core service) means **both**. A functionally addressed request to a
service that does not accept it is silently dropped.

Per ISO 14229-1, a functionally addressed request never elicits the negative
response codes `0x11`, `0x12`, `0x7E`, `0x7F`, or `0x31` (these are suppressed to
avoid flooding a shared bus); all other negative responses and all positive
responses are still sent.
```

- [ ] **Step 2: Note the field + suppression in `docs/SERVICE_COMPLIANCE.md`**

Read the file; add a short note (matching its format) that `uds_service_entry_t.address_mode` gates physical/functional addressing (`0 = both`) and that functional requests suppress NRC `0x11/0x12/0x7E/0x7F/0x31`.

- [ ] **Step 3: CHANGELOG entry**

In `CHANGELOG.md`, under `## [Unreleased]` → `### Added`, append:

```markdown
- **ISO-TP physical/functional addressing**: a channel can now recognise a functional (broadcast) RX ID via `uds_tp_isotp_set_functional_id()`; requests are tagged physical/functional and delivered through the new `uds_input_sdu_addr()` (`uds_input_sdu()` stays as a physical-default wrapper). Services gate on a new `address_mode` field in `uds_service_entry_t` (0 = both). Functional addressing is Single-Frame only, and functionally addressed requests suppress NRC 0x11/0x12/0x7E/0x7F/0x31 per ISO 14229-1. Completes #42.
```

- [ ] **Step 4: Commit**

```bash
git add docs/TRANSPORT.md docs/SERVICE_COMPLIANCE.md CHANGELOG.md
git commit -m "docs(isotp): document physical/functional addressing (#42)"
```

---

## Self-Review

**Spec coverage:**
- Functional RX ID + setter + classification → Task 2. ✅
- Functional SF-only (ignore FF/CF/FC) → Task 2 Step 4 + test 4. ✅
- `uds_addr_mode_t` + `uds_input_sdu_addr` + wrapper → Task 1 Steps 1, 4. ✅
- `req_addr_mode` on ctx, physical for inner dispatch → Task 1 Step 2/4 (set per top-level call; suppression gated on `!secure_capturing` for inner 0x84/0x86) Step 6. ✅
- `address_mode` field (last, 0 = both) → Task 1 Step 2. ✅
- Dispatch gating (functional→silent, physical→0x11) → Task 1 Step 5 + tests 7/8. ✅
- NRC suppression set 0x11/0x12/0x7E/0x7F/0x31 in `uds_send_nrc` → Task 1 Step 6 + tests 9/10/11 (10/11 via dispatch test variants & full-stack). ✅
- 0x7E constant → Task 1 Step 3. ✅
- Tests: ISO-TP (Task 2), core dispatch incl. suppression + legacy entry (Task 1 Step 7), full-stack (Task 3). ✅
- Docs (TRANSPORT, SERVICE_COMPLIANCE, CHANGELOG, header doc comments) → Task 4 + Task 1 Step 1. ✅

**Placeholder scan:** No TBD/TODO; every code step shows complete code. The two `uds_init`-fields fallback notes are explicit guidance, not placeholders.

**Type consistency:** `uds_addr_mode_t {UDS_ADDR_PHYSICAL, UDS_ADDR_FUNCTIONAL}`, `uds_input_sdu_addr(ctx,data,len,addr)`, `uds_service_entry_t.address_mode` (uint8_t, last), `ctx->req_addr_mode` (uint8_t), `uds_tp_isotp_set_functional_id(iso,id)`, `uds_isotp_ctx_t.rx_id_func`, and `uds_rx_sf(..., uint8_t addr)` are used identically across Tasks 1-3. The suppress-set NRC names (`UDS_NRC_SERVICE_NOT_SUPPORTED`, `UDS_NRC_SUBFUNCTION_NOT_SUPPORTED`, `UDS_NRC_SUBFUNC_NOT_SUPP_IN_SESS`, `UDS_NRC_SERVICE_NOT_SUPP_IN_SESS`, `UDS_NRC_REQUEST_OUT_OF_RANGE`) match the internal.h defines (0x7E added in Task 1 Step 3). ✅

**Spec note reconciled:** the spec's testing section mentioned wrapping `uds_input_sdu_addr` for the ISO-TP suite; the plan keeps the physical path on `uds_input_sdu` (zero churn to existing wrapped suites) and the new suite wraps BOTH symbols — strictly better, same coverage.
