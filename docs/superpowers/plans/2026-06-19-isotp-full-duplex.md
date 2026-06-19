# ISO-TP Full-Duplex Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let an ISO-TP channel reassemble an incoming multi-frame message while transmitting one, via a per-channel half/full-duplex flag (default half-duplex), fixing the defect where any inbound frame aborts an in-flight multi-frame response.

**Architecture:** Split the single shared ISO-TP state machine (`state` + shared `msg_len`/`bytes_processed`/`sn`/`bs_counter`/`block_size`/`st_min`) in `uds_isotp_ctx_t` into independent RX and TX sub-machines. Add `uds_isotp_duplex_t mode` (default `ISOTP_HALF_DUPLEX`). The half/full difference reduces to one rule: in half-duplex an incoming SF/FF aborts an active TX (and `send()` aborts an active RX); in full-duplex the two directions never disturb each other.

**Tech Stack:** C11, CMake, cmocka, linker `-Wl,--wrap=uds_input_sdu` for RX-completion interception. Pure transport-layer change in `src/transport/uds_tp_isotp.c` + `include/uds/uds_isotp.h`.

## Global Constraints

- License header on every new file: `/*\n * Copyright (c) 2026 Andrii Shylenko\n * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0\n */` (copy from any existing source file).
- No dynamic allocation (zero-malloc library). All buffers caller-provided.
- MISRA-leaning style as in the existing file: explicit `uint8_t`/`uint16_t`/`uint32_t` casts, widen before shift, `if (!iso) return;` guards on public entry points.
- Commit messages: **no Claude / AI / assistant references**; reference the issue with `(#42)`.
- PR target branch: `develop`. Work branch: `feature/isotp-full-duplex-issue42` (already created off `origin/develop`; the spec is already committed there).
- Format gate: `clang-format` is enforced in CI by **clang-format-14** (a locally newer clang-format may report different results — trust CI). Run `clang-format` per the repo's `.clang-format` before committing.
- Build & test commands (from repo root):
  - Configure: `cmake -S . -B build`
  - Build: `cmake --build build`
  - Run all tests: `ctest --test-dir build --output-on-failure`
  - Run one test: `ctest --test-dir build -R <test_name> --output-on-failure`
- Default mode after `uds_tp_isotp_init()` MUST be `ISOTP_HALF_DUPLEX` (preserve existing behavior).

---

## File Structure

- `include/uds/uds_isotp.h` — public types/API. Modified: replace `uds_isotp_state_t` with two state enums + duplex enum; restructure `uds_isotp_ctx_t` into RX/TX groups; add `uds_tp_isotp_set_mode()`.
- `src/transport/uds_tp_isotp.c` — engine. Modified: route frames per direction, gate cross-direction aborts on `mode`, tick both machines in `process()`, split `block_size`/`st_min` into advertised (RX) vs honored (TX).
- `tests/unit/test_tp_timeout.c`, `test_tp_isolation.c`, `test_tp_isotp_escape_fc.c`, `test_fuzz_core.c` — Modified: migrate white-box field references to new names (Task 1).
- `tests/unit/test_tp_full_duplex.c` — Create: new behavior suite (Task 3).
- `tests/unit/test_issue42_full_duplex_response.c` — Create: full-stack regression (Task 4).
- `tests/CMakeLists.txt` — Modified: register the two new tests (Tasks 3, 4).
- `docs/TRANSPORT.md`, `CHANGELOG.md` — Modified: document the feature (Task 5).

---

## Task 1: Behavior-preserving state split

Split the shared state machine into independent RX/TX fields **without changing runtime behavior** (half-duplex semantics identical to today). The full suite passing — after migrating four white-box tests to the new field names — is the gate.

**Files:**
- Modify: `include/uds/uds_isotp.h` (lines 52-122: enum + struct)
- Modify: `src/transport/uds_tp_isotp.c` (all of it)
- Modify: `tests/unit/test_tp_timeout.c`, `tests/unit/test_tp_isolation.c`, `tests/unit/test_tp_isotp_escape_fc.c`, `tests/unit/test_fuzz_core.c`

**Interfaces:**
- Consumes: nothing new.
- Produces (relied on by later tasks):
  - `typedef enum { ISOTP_RX_IDLE = 0, ISOTP_RX_WAIT_CF } uds_isotp_rx_state_t;`
  - `typedef enum { ISOTP_TX_IDLE = 0, ISOTP_TX_WAIT_FC, ISOTP_TX_SENDING_CF } uds_isotp_tx_state_t;`
  - Context fields: `rx_state, rx_msg_len, rx_bytes_processed, rx_sn, timer_n_cr` (RX); `tx_state, tx_msg_len, tx_bytes_processed, tx_sn, tx_bs_counter, tx_block_size, tx_st_min, timer_n_bs, timer_st, tx_sdu_buf, tx_sdu_size, tx_sdu_len` (TX); `block_size, st_min` retained as **our advertised receiver** BS/STmin.

- [ ] **Step 1: Replace the state enum and restructure the context struct in the header**

In `include/uds/uds_isotp.h`, replace the `uds_isotp_state_t` enum (lines 52-65) with:

```c
/**
 * @brief ISO-TP reception state (independent of transmission).
 */
typedef enum
{
    ISOTP_RX_IDLE = 0,
    ISOTP_RX_WAIT_CF /**< Received FF, sent FC, waiting for CFs */
} uds_isotp_rx_state_t;

/**
 * @brief ISO-TP transmission state (independent of reception).
 */
typedef enum
{
    ISOTP_TX_IDLE = 0,
    ISOTP_TX_WAIT_FC,   /**< Sent FF, waiting for FC */
    ISOTP_TX_SENDING_CF /**< Received CTS, sending CFs */
} uds_isotp_tx_state_t;

/**
 * @brief ISO-TP duplex mode (mirrors AUTOSAR CanTpChannelMode).
 */
typedef enum
{
    ISOTP_HALF_DUPLEX = 0, /**< One transfer per N_AI at a time (default) */
    ISOTP_FULL_DUPLEX      /**< Simultaneous RX and TX on the same N_AI */
} uds_isotp_duplex_t;
```

Then replace the `uds_isotp_ctx_t` struct (lines 90-122) with:

```c
typedef struct
{
    uds_can_send_fn can_send; /**< Output function for CAN frames */

    /* --- Configuration --- */
    uint32_t tx_id;          /**< CAN ID to transmit on (Source) */
    uint32_t rx_id;          /**< CAN ID to listen for (Target) */
    uint8_t block_size;      /**< BS we advertise as receiver (sent in our FC) */
    uint8_t st_min;          /**< STmin we advertise as receiver (sent in our FC) */
    uint8_t use_can_fd;      /**< Flag: Enable CAN-FD support (0=Standard, 1=FD) */
    uint8_t tx_dl;           /**< Transmit Data Length (Max frame size: 8 or 64) */
    uds_isotp_duplex_t mode; /**< Half- (default) or full-duplex operation */

    /* --- RX machine (reassembly of an inbound segmented message) --- */
    uds_isotp_rx_state_t rx_state; /**< Current reception state */
    uint16_t rx_msg_len;           /**< Total SDU length being received */
    uint16_t rx_bytes_processed;   /**< SDU bytes received so far */
    uint8_t rx_sn;                 /**< Expected next sequence number (0-15) */
    uint32_t timer_n_cr;           /**< Timestamp of last RX progress (N_Cr base) */

    /* --- TX machine (segmentation of an outbound message) --- */
    uds_isotp_tx_state_t tx_state; /**< Current transmission state */
    uint16_t tx_msg_len;           /**< Total SDU length being sent */
    uint16_t tx_bytes_processed;   /**< SDU bytes sent so far */
    uint8_t tx_sn;                 /**< Next sequence number to send (0-15) */
    uint8_t tx_bs_counter;         /**< CFs sent in the current block */
    uint8_t tx_block_size;         /**< BS the receiver told us to honor (from FC) */
    uint8_t tx_st_min;             /**< STmin the receiver told us to honor (from FC) */
    uint32_t timer_n_bs;           /**< Timestamp FF was sent (N_Bs base, 0 = unarmed) */
    uint32_t timer_st;             /**< Separation Time timer (STmin) */
    uint8_t *tx_sdu_buf;           /**< Caller buffer caching the SDU during TX */
    uint16_t tx_sdu_size;          /**< Capacity of tx_sdu_buf in bytes */
    uint16_t tx_sdu_len;           /**< Length of the SDU currently being transmitted */

    /* --- Timeout limits (ms); defaulted at init, overridable by the caller --- */
    uint32_t n_cr_ms; /**< Max wait for a consecutive frame during reception */
    uint32_t n_bs_ms; /**< Max wait for flow control after sending a First Frame */
} uds_isotp_ctx_t;
```

- [ ] **Step 2: Add the `set_mode` prototype to the header**

After the `uds_tp_isotp_set_fd` prototype (around line 149) add:

```c
/**
 * @brief Select half- or full-duplex operation for this channel.
 *
 * Default after init is ISOTP_HALF_DUPLEX (preserves prior behavior: an
 * inbound SF/FF aborts an in-flight transmission). In ISOTP_FULL_DUPLEX a
 * segmented reception and a segmented transmission proceed simultaneously
 * on the same N_AI without disturbing each other.
 *
 * @param iso  Pointer to the ISO-TP context.
 * @param mode ISOTP_HALF_DUPLEX or ISOTP_FULL_DUPLEX.
 */
void uds_tp_isotp_set_mode(uds_isotp_ctx_t *iso, uds_isotp_duplex_t mode);
```

> Note: the implementation of `set_mode` and the full-duplex *behavior* land in Task 2. This task only declares the type/field so the struct compiles; `mode` defaults to `ISOTP_HALF_DUPLEX` and nothing reads it yet, so behavior is unchanged.

- [ ] **Step 3: Rewrite `uds_tp_isotp_init` to initialize the split fields**

In `src/transport/uds_tp_isotp.c`, replace the body of `uds_tp_isotp_init` (lines 56-75) with:

```c
void uds_tp_isotp_init(uds_isotp_ctx_t *iso, uds_can_send_fn can_send, uint32_t tx_id,
                       uint32_t rx_id, uint8_t *tx_sdu_buf, uint16_t tx_sdu_size)
{
    if (!iso) {
        return;
    }
    memset(iso, 0, sizeof(*iso));
    iso->can_send = can_send;
    iso->tx_id = tx_id;
    iso->rx_id = rx_id;
    iso->block_size = 8;           /* BS we advertise as receiver */
    iso->st_min = 0;               /* STmin we advertise as receiver */
    iso->use_can_fd = 0;           /* Default: Classic CAN */
    iso->tx_dl = ISOTP_MAX_DL_CAN; /* Default: 8 bytes */
    iso->mode = ISOTP_HALF_DUPLEX; /* Default: conservative, prior behavior */
    iso->rx_state = ISOTP_RX_IDLE;
    iso->tx_state = ISOTP_TX_IDLE;
    iso->tx_sdu_buf = tx_sdu_buf;
    iso->tx_sdu_size = tx_sdu_size;
    iso->tx_sdu_len = 0;
    iso->n_cr_ms = ISOTP_N_CR_DEFAULT_MS;
    iso->n_bs_ms = ISOTP_N_BS_DEFAULT_MS;
}
```

- [ ] **Step 4: Rewrite `uds_send_mf` to use TX fields**

Replace the state-setting block in `uds_send_mf` (lines 126-130) so it uses TX fields:

```c
    iso->tx_msg_len = len;
    iso->tx_bytes_processed = 0;
    iso->tx_bs_counter = 0; /* fresh block accounting for this transfer */
    iso->tx_state = ISOTP_TX_WAIT_FC;
    iso->timer_n_bs = 0u; /* armed on the first process() tick in WAIT_FC */
```

And later in the same function replace `iso->bytes_processed = to_copy;` (line 162) with `iso->tx_bytes_processed = to_copy;` and `iso->sn = 1u;` (line 163) with `iso->tx_sn = 1u;`.

- [ ] **Step 5: Rewrite `uds_tp_isotp_process` to tick RX and TX independently**

Replace the entire `uds_tp_isotp_process` function (lines 195-280) with:

```c
void uds_tp_isotp_process(uds_isotp_ctx_t *iso, uint32_t time_ms)
{
    if (!iso) {
        return;
    }

    /* --- RX tick: N_Cr (reception stalls if a CF never arrives) --- */
    if (iso->rx_state == ISOTP_RX_WAIT_CF) {
        if ((time_ms - iso->timer_n_cr) >= iso->n_cr_ms) {
            iso->rx_state = ISOTP_RX_IDLE;
        }
    }

    /* --- TX tick: N_Bs (waiting for FC) --- */
    if (iso->tx_state == ISOTP_TX_WAIT_FC) {
        if (iso->timer_n_bs == 0u) {
            /* Arm on first observation; avoid 0 which means "unarmed". */
            iso->timer_n_bs = (time_ms == 0u) ? 1u : time_ms;
        }
        else if ((time_ms - iso->timer_n_bs) >= iso->n_bs_ms) {
            iso->tx_state = ISOTP_TX_IDLE;
            iso->timer_n_bs = 0u;
        }
        return;
    }

    /* --- TX tick: sending consecutive frames --- */
    if (iso->tx_state == ISOTP_TX_SENDING_CF) {
        uint16_t remaining = iso->tx_msg_len - iso->tx_bytes_processed;
        if (remaining == 0) {
            iso->tx_state = ISOTP_TX_IDLE;
            return;
        }

        /* Check STmin (Separation Time) using the receiver-honored value. */
        uint32_t elapsed = time_ms - iso->timer_st;
        uint32_t required_st = iso->tx_st_min;

        /* Decode ISO-TP STmin:
           0x00 - 0x7F: 0ms - 127ms
           0xF1 - 0xF9: 100us - 900us (treated as 1ms at ms resolution) */
        if (required_st >= 0xF1 && required_st <= 0xF9) {
            required_st = 1;
        }
        else if (required_st > 0x7F) {
            required_st = 0; /* Reserved or invalid */
        }

        if (elapsed < required_st) {
            return; /* Wait for STmin */
        }

        /* Check Block Size (BS) the receiver told us to honor. */
        if (iso->tx_block_size > 0 && iso->tx_bs_counter >= iso->tx_block_size) {
            iso->tx_state = ISOTP_TX_WAIT_FC;
            iso->tx_bs_counter = 0;
            iso->timer_n_bs = 0u; /* re-arm N_Bs while waiting for the next FC */
            return;
        }

        /* Calculate max payload per CF (header is 1 byte). */
        uint8_t max_cf_payload =
            (iso->use_can_fd) ? (ISOTP_MAX_DL_CANFD - 1) : (ISOTP_MAX_DL_CAN - 1);

        uint8_t to_copy = (remaining > max_cf_payload) ? max_cf_payload : (uint8_t) remaining;
        uint8_t frame[ISOTP_MAX_DL_CANFD] = {0};
        frame[0] = (uint8_t) (ISOTP_PCI_CF | iso->tx_sn);
        memcpy(&frame[1], &iso->tx_sdu_buf[iso->tx_bytes_processed], to_copy);

        uint8_t dl = ISOTP_MAX_DL_CAN;
        if (iso->use_can_fd) {
            dl = uds_dlc_align(1 + to_copy);
        }

        if (uds_internal_tp_send_frame(iso, frame, dl) == 0) {
            iso->tx_bytes_processed += to_copy;
            iso->tx_sn = (iso->tx_sn + 1) & 0x0F;
            iso->tx_bs_counter++;
            iso->timer_st = time_ms; /* Reset ST timer */

            if (iso->tx_bytes_processed >= iso->tx_msg_len) {
                iso->tx_state = ISOTP_TX_IDLE;
            }
        }
    }
}
```

> Behavior note: the BS-exhausted branch now also resets `timer_n_bs = 0u` so N_Bs re-arms when we return to WAIT_FC mid-transfer. This matches ISO 15765-2 (N_Bs applies to every FC wait) and the existing `uds_rx_fc` WAIT handling; it does not change any existing test outcome (the BS test in `test_tp_flow_control.c` injects the next FC before any timeout).

- [ ] **Step 6: Rewrite `uds_rx_sf` (direction-aware, half-duplex preserved)**

Replace `uds_rx_sf` (lines 282-304) with:

```c
static void uds_rx_sf(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
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
        if (len < 2u) return; /* Not enough data for the FD length byte */
        sdu_len = data[1];
        data_offset = 2;
        if (sdu_len == 0) return; /* Invalid */
    }

    if (sdu_len > (len - data_offset)) {
        /* Not enough data in frame */
        return;
    }

    uds_input_sdu(uds, &data[data_offset], (uint16_t) sdu_len);
}
```

- [ ] **Step 7: Rewrite `uds_rx_ff` (direction-aware, uses RX fields + advertised BS/STmin)**

Replace `uds_rx_ff` (lines 306-364) with:

```c
static void uds_rx_ff(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
{
    /* A new reception supersedes any in-progress reception. */
    iso->rx_state = ISOTP_RX_IDLE;

    /* Half-duplex: a new inbound message terminates an in-flight transmission. */
    if (iso->mode == ISOTP_HALF_DUPLEX) {
        iso->tx_state = ISOTP_TX_IDLE;
        iso->timer_n_bs = 0u;
    }

    if (len < 2u) {
        return; /* FF requires at least PCI + length byte */
    }

    uint32_t sdu_len;
    uint8_t header_len;

    if ((data[0] & 0x0Fu) == 0u && data[1] == 0u) {
        /* Escape FF (ISO 15765-2): 32-bit FF_DL in bytes 2..5 (MSB first). */
        if (len < 6u) {
            return; /* Not enough bytes for the escape length field */
        }
        sdu_len = ((uint32_t) data[2] << 24u) | ((uint32_t) data[3] << 16u) |
                  ((uint32_t) data[4] << 8u) | (uint32_t) data[5];
        if (sdu_len <= ISOTP_MAX_SDU_LEN_STD) {
            return; /* Escape sequence with FF_DL <= 4095 is invalid; ignore (9.6.3.2). */
        }
        header_len = 6u;
    }
    else {
        sdu_len = ((uint32_t) (data[0] & 0x0Fu) << 8u) | (uint32_t) data[1];
        if (sdu_len < 8u) {
            return; /* Multi-frame must be > 7 bytes (Standard) or handled by SF */
        }
        header_len = 2u;
    }

    /* FF_DL exceeding the receive buffer: cancel and notify the sender (9.6.3.2). */
    if (sdu_len > uds->config->rx_buffer_size) {
        iso->rx_state = ISOTP_RX_IDLE;
        uint8_t fc_ov[8] = {0};
        fc_ov[0] = (uint8_t) (ISOTP_PCI_FC | ISOTP_FC_OVA);
        uds_internal_tp_send_frame(iso, fc_ov, 8);
        return;
    }

    iso->rx_msg_len = (uint16_t) sdu_len;

    uint8_t data_in_ff = (uint8_t) (len - header_len);

    iso->rx_bytes_processed = data_in_ff;
    iso->rx_sn = 1;
    iso->rx_state = ISOTP_RX_WAIT_CF;
    iso->timer_n_cr = uds->config->get_time_ms ? uds->config->get_time_ms() : 0u;

    memcpy(uds->config->rx_buffer, &data[header_len], data_in_ff);

    /* Send Flow Control (CTS) advertising OUR receiver BS/STmin. */
    uint8_t fc[8] = {0};
    fc[0] = (uint8_t) (ISOTP_PCI_FC | ISOTP_FC_CTS);
    fc[1] = iso->block_size;
    fc[2] = iso->st_min;
    uds_internal_tp_send_frame(iso, fc, 8);
}
```

- [ ] **Step 8: Rewrite `uds_rx_cf` to use RX fields**

Replace `uds_rx_cf` (lines 366-394) with:

```c
static void uds_rx_cf(uds_isotp_ctx_t *iso, struct uds_ctx *uds, const uint8_t *data, uint8_t len)
{
    if (iso->rx_state != ISOTP_RX_WAIT_CF) {
        return;
    }

    uint8_t sn = data[0] & 0x0F;
    if (sn != iso->rx_sn) {
        iso->rx_state = ISOTP_RX_IDLE;
        return;
    }
    iso->rx_sn = (iso->rx_sn + 1) & 0x0F;

    uint16_t remaining = iso->rx_msg_len - iso->rx_bytes_processed;

    uint8_t data_capacity = len - 1; /* Byte 0 is PCI+SN */
    uint8_t to_copy = (remaining > data_capacity) ? data_capacity : (uint8_t) remaining;

    memcpy(&uds->config->rx_buffer[iso->rx_bytes_processed], &data[1], to_copy);
    iso->rx_bytes_processed += to_copy;
    iso->timer_n_cr = uds->config->get_time_ms ? uds->config->get_time_ms() : 0u;

    if (iso->rx_bytes_processed >= iso->rx_msg_len) {
        iso->rx_state = ISOTP_RX_IDLE;
        uds_input_sdu(uds, uds->config->rx_buffer, iso->rx_msg_len);
    }
}
```

- [ ] **Step 9: Rewrite `uds_rx_fc` to use TX fields + honored BS/STmin**

Replace `uds_rx_fc` (lines 396-431) with:

```c
static void uds_rx_fc(uds_isotp_ctx_t *iso, const uint8_t *data, uint8_t len)
{
    if (iso->tx_state != ISOTP_TX_WAIT_FC) {
        return;
    }
    if (len < 3u) {
        return; /* FC requires flow status, block size and STmin */
    }

    uint8_t fs = data[0] & 0x0F;
    switch (fs) {
        case ISOTP_FC_CTS:
            /* ClearToSend: latch the receiver's BS/STmin and resume CFs. */
            iso->tx_state = ISOTP_TX_SENDING_CF;
            iso->tx_block_size = data[1];
            iso->tx_st_min = data[2];
            iso->tx_bs_counter = 0u;
            iso->timer_n_bs = 0u; /* FC arrived: disarm N_Bs */
            break;

        case ISOTP_FC_WAIT:
            /* Wait: keep waiting for a further FC and restart N_Bs. */
            iso->tx_state = ISOTP_TX_WAIT_FC;
            iso->timer_n_bs = 0u; /* re-armed on the next process() tick */
            break;

        case ISOTP_FC_OVA:
        default:
            /* Overflow or reserved/invalid FS: cancel the transmission. */
            iso->tx_state = ISOTP_TX_IDLE;
            iso->timer_n_bs = 0u;
            break;
    }
}
```

- [ ] **Step 10: Migrate `test_tp_timeout.c` field references**

In `tests/unit/test_tp_timeout.c`, change the RX-context assertions (around lines 67, 71, 75) and TX-context assertions (around lines 90, 94, 98):
- `iso.state == ISOTP_RX_WAIT_CF` → `iso.rx_state == ISOTP_RX_WAIT_CF`
- the RX-context `iso.state == ISOTP_IDLE` (line 75, after the N_Cr path) → `iso.rx_state == ISOTP_RX_IDLE`
- `iso.state == ISOTP_TX_WAIT_FC` → `iso.tx_state == ISOTP_TX_WAIT_FC`
- the TX-context `iso.state == ISOTP_IDLE` (line 98, after the N_Bs path) → `iso.tx_state == ISOTP_TX_IDLE`

(Read each assertion's surrounding scenario to confirm whether it is an RX or TX check before mapping. Asserted *values* are unchanged.)

- [ ] **Step 11: Migrate `test_tp_isolation.c` field references**

In `tests/unit/test_tp_isolation.c` (lines 49-50), both asserted values are TX states:
- `iso_a.state == ISOTP_TX_WAIT_FC` → `iso_a.tx_state == ISOTP_TX_WAIT_FC`
- `iso_b.state == ISOTP_IDLE` → `iso_b.tx_state == ISOTP_TX_IDLE`

- [ ] **Step 12: Migrate `test_tp_isotp_escape_fc.c` field references**

In `tests/unit/test_tp_isotp_escape_fc.c`, map per scenario:
- Line 86 (after `uds_isotp_send`): `g_iso.state == ISOTP_TX_WAIT_FC` → `g_iso.tx_state == ISOTP_TX_WAIT_FC`
- Line 87: `g_iso.msg_len` → `g_iso.tx_msg_len`
- Line 88: `g_iso.bytes_processed` → `g_iso.tx_bytes_processed`
- Line 116 (after FF rx): `g_iso.state == ISOTP_RX_WAIT_CF` → `g_iso.rx_state == ISOTP_RX_WAIT_CF`
- Line 117: `g_iso.msg_len` → `g_iso.rx_msg_len`
- Line 138 (after RX reassembly completes): `g_iso.state == ISOTP_IDLE` → `g_iso.rx_state == ISOTP_RX_IDLE`
- Line 162 (after RX overflow FC): `g_iso.state == ISOTP_IDLE` → `g_iso.rx_state == ISOTP_RX_IDLE`
- Lines 177, 189, 193 (`start_mf_tx` + FC.WAIT scenario, TX context): `g_iso.state == ISOTP_TX_WAIT_FC` → `g_iso.tx_state == ISOTP_TX_WAIT_FC`
- Line 198 (after CTS): `g_iso.state == ISOTP_TX_SENDING_CF` → `g_iso.tx_state == ISOTP_TX_SENDING_CF`
- Lines 215, 219, 230, 233 (after FC.OVFLW / invalid FS abort, TX context): `g_iso.state == ISOTP_IDLE` → `g_iso.tx_state == ISOTP_TX_IDLE`

(Confirm each by its scenario; this maps every occurrence in the file.)

- [ ] **Step 13: Migrate `test_fuzz_core.c` invariant check**

In `tests/unit/test_fuzz_core.c`, replace the `valid_isotp_state` helper (lines 93-96) and its use (line 135), plus the `bytes_processed` assertion (line 134):

```c
static bool valid_isotp_rx_state(uds_isotp_rx_state_t s)
{
    return s == ISOTP_RX_IDLE || s == ISOTP_RX_WAIT_CF;
}

static bool valid_isotp_tx_state(uds_isotp_tx_state_t s)
{
    return s == ISOTP_TX_IDLE || s == ISOTP_TX_WAIT_FC || s == ISOTP_TX_SENDING_CF;
}
```

Then at the assertion site:
- `assert_true(iso.bytes_processed <= cfg.rx_buffer_size);` → `assert_true(iso.rx_bytes_processed <= cfg.rx_buffer_size);`
- `assert_true(valid_isotp_state(iso.state));` → two asserts:
  ```c
  assert_true(valid_isotp_rx_state(iso.rx_state));
  assert_true(valid_isotp_tx_state(iso.tx_state));
  ```

- [ ] **Step 14: Build and run the full suite (regression gate)**

Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: PASS — all existing tests green. This proves the split is behavior-preserving in the default (half-duplex) mode.

- [ ] **Step 15: Format and commit**

```bash
clang-format -i include/uds/uds_isotp.h src/transport/uds_tp_isotp.c \
  tests/unit/test_tp_timeout.c tests/unit/test_tp_isolation.c \
  tests/unit/test_tp_isotp_escape_fc.c tests/unit/test_fuzz_core.c
git add include/uds/uds_isotp.h src/transport/uds_tp_isotp.c \
  tests/unit/test_tp_timeout.c tests/unit/test_tp_isolation.c \
  tests/unit/test_tp_isotp_escape_fc.c tests/unit/test_fuzz_core.c
git commit -m "refactor(isotp): split shared state into independent RX/TX machines (#42)"
```

---

## Task 2: Full-duplex mode (API + gated behavior)

Implement `uds_tp_isotp_set_mode()` and make full-duplex actually independent. The only code that differs by mode is already isolated in Task 1 (the `if (iso->mode == ISOTP_HALF_DUPLEX)` blocks in `uds_rx_sf`/`uds_rx_ff`, and the symmetric rule in `uds_isotp_send`). This task adds the setter, the `send()`-aborts-RX half-duplex rule, and a minimal mode test. The full behavior suite is Task 3.

**Files:**
- Modify: `src/transport/uds_tp_isotp.c` (add `uds_tp_isotp_set_mode`; add half-duplex rule in `uds_isotp_send`)
- Modify: `tests/unit/test_tp_full_duplex.c` does not exist yet — the minimal mode assertion goes in Task 3's new file. For this task, add a tiny check to an existing suite instead (see Step 3).

**Interfaces:**
- Consumes: `uds_isotp_duplex_t`, `mode` field, `rx_state`/`tx_state` (Task 1).
- Produces: `void uds_tp_isotp_set_mode(uds_isotp_ctx_t *iso, uds_isotp_duplex_t mode);`

- [ ] **Step 1: Implement `uds_tp_isotp_set_mode`**

In `src/transport/uds_tp_isotp.c`, immediately after `uds_tp_isotp_set_fd` (ends at line 84), add:

```c
void uds_tp_isotp_set_mode(uds_isotp_ctx_t *iso, uds_isotp_duplex_t mode)
{
    if (!iso) {
        return;
    }
    iso->mode = mode;
}
```

- [ ] **Step 2: Add the half-duplex `send()`-aborts-RX rule**

In `uds_isotp_send` (lines 179-193), after the `if (!iso) return -1;` guard, add the symmetric half-duplex rule so initiating a TX while reassembling abandons the RX (reproducing today's single-connection clobber):

```c
int uds_isotp_send(uds_isotp_ctx_t *iso, const uint8_t *data, uint16_t len)
{
    if (!iso) {
        return -1;
    }

    /* Half-duplex: starting a transmission terminates an in-flight reception. */
    if (iso->mode == ISOTP_HALF_DUPLEX) {
        iso->rx_state = ISOTP_RX_IDLE;
    }

    /* Check if we can use Single Frame */
    uint8_t max_sf_len = (iso->use_can_fd) ? ISOTP_SF_MAX_DL_CANFD : ISOTP_SF_MAX_DL_CAN;

    if (len <= max_sf_len) {
        return uds_send_sf(iso, data, len);
    }

    return uds_send_mf(iso, data, len);
}
```

- [ ] **Step 3: Smoke-test the setter and default in `test_tp_flow_control.c`**

Add this test function to `tests/unit/test_tp_flow_control.c` before `main`, and register it in the `tests[]` array:

```c
/* 3. Verify duplex mode default and setter */
static void test_tp_duplex_mode_default_and_set(void **state)
{
    (void) state;
    /* g_iso is freshly initialized by setup() */
    assert_int_equal(g_iso.mode, ISOTP_HALF_DUPLEX); /* default */

    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    assert_int_equal(g_iso.mode, ISOTP_FULL_DUPLEX);

    uds_tp_isotp_set_mode(&g_iso, ISOTP_HALF_DUPLEX);
    assert_int_equal(g_iso.mode, ISOTP_HALF_DUPLEX);

    uds_tp_isotp_set_mode(NULL, ISOTP_FULL_DUPLEX); /* must not crash */
}
```

Register it:
```c
        cmocka_unit_test_setup_teardown(test_tp_duplex_mode_default_and_set, setup, teardown),
```

- [ ] **Step 4: Build and run**

Run: `cmake --build build && ctest --test-dir build -R test_tp_flow_control --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i src/transport/uds_tp_isotp.c tests/unit/test_tp_flow_control.c
git add src/transport/uds_tp_isotp.c tests/unit/test_tp_flow_control.c
git commit -m "feat(isotp): add full-duplex mode and uds_tp_isotp_set_mode (#42)"
```

---

## Task 3: Full-duplex behavior test suite

New cmocka suite proving full-duplex independence and locking half-duplex behavior. Wrapped on `uds_input_sdu` to assert RX completion alongside TX frame emission.

**Files:**
- Create: `tests/unit/test_tp_full_duplex.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `uds_tp_isotp_set_mode`, split fields, all public TP API.
- Produces: nothing (leaf test).

- [ ] **Step 1: Write the test suite file**

Create `tests/unit/test_tp_full_duplex.c`:

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
    check_expected(id);
    check_expected(len);
    check_expected_ptr(data);
    return (int) mock();
}

/* RX completion interception (linker --wrap). */
void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    check_expected(len);
    check_expected_ptr(data);
}

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx_buffer[4096];
static uint32_t g_time;
static uint32_t time_ms(void) { return g_time; }

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_sdu[1024];

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.rx_buffer = g_rx_buffer;
    g_cfg.rx_buffer_size = sizeof(g_rx_buffer);
    g_cfg.get_time_ms = time_ms;
    g_ctx.config = &g_cfg;
    g_time = 0;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_iso_sdu, sizeof(g_iso_sdu));
    return 0;
}

/* Start a 30-byte classic-CAN multi-frame TX; consume the emitted FF. */
static void start_tx_30(void)
{
    static uint8_t data[30];
    for (int i = 0; i < 30; i++) data[i] = (uint8_t) (0xA0 + i);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    assert_int_equal(uds_isotp_send(&g_iso, data, 30), 0);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);
}

/* --- 1: half-duplex incoming SF aborts active TX (behavior lock) --- */
static void test_half_duplex_sf_aborts_tx(void **state)
{
    (void) state;
    /* default mode is half-duplex */
    start_tx_30();

    /* Inbound SF (3 data bytes) must be delivered AND must abort the TX. */
    uint8_t sf[8] = {0x03, 0x11, 0x22, 0x33, 0, 0, 0, 0};
    uint8_t expected_sf[3] = {0x11, 0x22, 0x33};
    expect_value(__wrap_uds_input_sdu, len, 3);
    expect_memory(__wrap_uds_input_sdu, data, expected_sf, 3);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, sf, 8);

    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);

    /* A subsequent CTS + process() must emit NO consecutive frame. */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    uds_tp_isotp_process(&g_iso, 10); /* no mock expectation -> any send fails the test */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- 2: full-duplex incoming SF does NOT abort active TX --- */
static void test_full_duplex_sf_keeps_tx(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    /* Inbound SF delivered; TX must remain in WAIT_FC. */
    uint8_t sf[8] = {0x03, 0x44, 0x55, 0x66, 0, 0, 0, 0};
    uint8_t expected_sf[3] = {0x44, 0x55, 0x66};
    expect_value(__wrap_uds_input_sdu, len, 3);
    expect_memory(__wrap_uds_input_sdu, data, expected_sf, 3);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, sf, 8);

    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* CTS resumes the original response: remaining 24 bytes => CF1(7)+CF2(7)+CF3(7)+CF4(3). */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_SENDING_CF);

    for (int i = 0; i < 4; i++) {
        expect_value(mock_can_send, id, 0x7E0);
        expect_value(mock_can_send, len, 8);
        expect_any(mock_can_send, data);
        will_return(mock_can_send, 0);
        uds_tp_isotp_process(&g_iso, (uint32_t) (10 + i));
    }
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- 3: full-duplex simultaneous segmented RX and TX both complete --- */
static void test_full_duplex_simultaneous_rx_tx(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    /* Inbound FF for a 14-byte reception: FF carries 6 bytes, then 2 CFs. */
    static uint8_t in_payload[14];
    for (int i = 0; i < 14; i++) in_payload[i] = (uint8_t) (0x10 + i);

    uint8_t ff[8] = {0x10, 0x0E, 0,0,0,0,0,0}; /* FF_DL=14 */
    memcpy(&ff[2], in_payload, 6);
    /* FF must produce our FC.CTS (advertised BS=8, STmin=0). */
    uint8_t expected_fc[8] = {0x30, 0x08, 0x00, 0,0,0,0,0};
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_memory(mock_can_send, data, expected_fc, 8);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);

    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC); /* TX untouched */

    /* CF1 (7 bytes) */
    uint8_t cf1[8] = {0x21, 0,0,0,0,0,0,0};
    memcpy(&cf1[1], &in_payload[6], 7);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, cf1, 8);

    /* CF2 (final, 1 byte) completes reassembly -> delivered. */
    uint8_t cf2[8] = {0x22, 0,0,0,0,0,0,0};
    memcpy(&cf2[1], &in_payload[13], 1);
    expect_value(__wrap_uds_input_sdu, len, 14);
    expect_memory(__wrap_uds_input_sdu, data, in_payload, 14);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, cf2, 8);

    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC); /* TX still pending its FC */

    /* Now drive the TX to completion. */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0,0,0,0,0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    for (int i = 0; i < 4; i++) {
        expect_value(mock_can_send, id, 0x7E0);
        expect_value(mock_can_send, len, 8);
        expect_any(mock_can_send, data);
        will_return(mock_can_send, 0);
        uds_tp_isotp_process(&g_iso, (uint32_t) (20 + i));
    }
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
}

/* --- 4: independent timers — N_Cr expiry resets RX, leaves TX --- */
static void test_full_duplex_independent_timers(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    /* Start a reception so rx_state == WAIT_CF, timer_n_cr seeded at g_time. */
    static uint8_t in_payload[14];
    for (int i = 0; i < 14; i++) in_payload[i] = (uint8_t) i;
    uint8_t ff[8] = {0x10, 0x0E, 0,0,0,0,0,0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    g_time = 100;
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF);

    /* Advance beyond N_Cr (default 1000ms). RX must reset; TX must remain WAIT_FC.
       (TX timer_n_bs arms here but N_Bs default is 1000ms; 100+1100 - arm(=1101) < 1000,
       so TX has NOT yet timed out at this tick.) */
    uds_tp_isotp_process(&g_iso, 1101); /* arms N_Bs at 1101; N_Cr: 1101-100 >= 1000 -> reset RX */
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);
}

/* --- 5: FC ignored when only RX active; CF ignored when only TX active --- */
static void test_cross_direction_frames_ignored(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);

    /* Only a TX is active: an inbound CF must be ignored (no crash, TX intact). */
    start_tx_30();
    uint8_t cf[8] = {0x21, 1,2,3,4,5,6,7};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, cf, 8);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* Only an RX is active (fresh ctx): an inbound FC must be ignored. */
    setup(state);
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    static uint8_t in_payload[14];
    uint8_t ff[8] = {0x10, 0x0E, 0,0,0,0,0,0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);
    uint8_t fc[8] = {0x30, 0x00, 0x00, 0,0,0,0,0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF); /* unaffected */
}

/* --- 6: half-duplex send() aborts active RX (behavior lock) --- */
static void test_half_duplex_send_aborts_rx(void **state)
{
    (void) state;
    /* default half-duplex */
    static uint8_t in_payload[14];
    uint8_t ff[8] = {0x10, 0x0E, 0,0,0,0,0,0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_WAIT_CF);

    /* Start a TX: RX must be abandoned. */
    start_tx_30();
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
}

/* --- 7: wrong-SN during full-duplex RX aborts only RX, TX continues --- */
static void test_full_duplex_wrong_sn_isolated(void **state)
{
    (void) state;
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    start_tx_30();

    static uint8_t in_payload[14];
    uint8_t ff[8] = {0x10, 0x0E, 0,0,0,0,0,0};
    memcpy(&ff[2], in_payload, 6);
    expect_value(mock_can_send, id, 0x7E0);
    expect_value(mock_can_send, len, 8);
    expect_any(mock_can_send, data);
    will_return(mock_can_send, 0);
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, ff, 8);

    /* CF with wrong SN (expected 1, send 5) aborts RX. */
    uint8_t bad_cf[8] = {0x25, 1,2,3,4,5,6,7};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, bad_cf, 8);
    assert_int_equal(g_iso.rx_state, ISOTP_RX_IDLE);
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC); /* TX untouched */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_half_duplex_sf_aborts_tx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_sf_keeps_tx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_simultaneous_rx_tx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_independent_timers, setup, NULL),
        cmocka_unit_test_setup_teardown(test_cross_direction_frames_ignored, setup, NULL),
        cmocka_unit_test_setup_teardown(test_half_duplex_send_aborts_rx, setup, NULL),
        cmocka_unit_test_setup_teardown(test_full_duplex_wrong_sn_isolated, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

- [ ] **Step 2: Register the test (wrapped) in CMake**

In `tests/CMakeLists.txt`, after the `test_tp_isotp_escape_fc` block (line 45), add:

```cmake
add_uds_test(test_tp_full_duplex unit/test_tp_full_duplex.c)
target_link_options(test_tp_full_duplex PRIVATE -Wl,--wrap=uds_input_sdu)
```

- [ ] **Step 3: Run the new suite and verify it fails first, then passes**

The behavior is already implemented (Tasks 1-2), so this suite should pass on first build. Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build -R test_tp_full_duplex --output-on-failure
```
Expected: PASS. If any case fails, fix the engine — not the test — unless the test's frame arithmetic is wrong (verify CF payload sizing: classic CAN CF carries 7 bytes).

- [ ] **Step 4: Run the entire suite (no regressions)**

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i tests/unit/test_tp_full_duplex.c
git add tests/unit/test_tp_full_duplex.c tests/CMakeLists.txt
git commit -m "test(isotp): full-duplex behavior suite + half-duplex behavior locks (#42)"
```

---

## Task 4: Full-stack regression (the issue scenario)

End-to-end test against a real `uds_ctx` (NOT wrapped): the server streams a multi-frame response while an inbound `TesterPresent` arrives mid-stream; in full-duplex the response completes intact and TesterPresent is serviced. Mirrors the issue-#29 regression precedent.

**Files:**
- Create: `tests/unit/test_issue42_full_duplex_response.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: full public API (`uds_init`, `uds_input_sdu`, ISO-TP). Real `uds_input_sdu`.
- Produces: nothing (leaf test).

- [ ] **Step 1: Write the regression test**

Create `tests/unit/test_issue42_full_duplex_response.c`. Design: one ISO-TP context in `ISOTP_FULL_DUPLEX`. We act as the **server transmitter**: `uds_isotp_send()` a 30-byte response (e.g. a large positive response payload). After the FF, inject an inbound single-frame `TesterPresent` request (`0x3E 0x00`) **before** sending the FC, then provide the FC and `process()` the CFs. Capture all emitted frames in an array via `mock_can_send` and assert: (a) the inbound `TesterPresent` SDU reached the core (observe via a registered response or simply that no CF was dropped), and (b) all CFs of the 30-byte response are emitted in order and the reassembled bytes match.

```c
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/* Issue #42 regression: in full-duplex, an inbound frame arriving while the
 * server is streaming a multi-frame response must NOT abort that response. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "uds/uds_config.h"

/* Captured TX frames (concatenated CF payloads reassembled here). */
static uint8_t g_reassembled[64];
static uint16_t g_reassembled_len;
static int g_cf_count;

static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void) id;
    uint8_t pci = data[0] & 0xF0;
    if (pci == 0x10) { /* FF: 2-byte header, rest is payload */
        memcpy(&g_reassembled[g_reassembled_len], &data[2], (size_t) (len - 2));
        g_reassembled_len += (uint16_t) (len - 2);
    }
    else if (pci == 0x20) { /* CF: 1-byte header */
        g_cf_count++;
        memcpy(&g_reassembled[g_reassembled_len], &data[1], (size_t) (len - 1));
        g_reassembled_len += (uint16_t) (len - 1);
    }
    /* FC (0x30) from a transmitter context never occurs here. */
    return 0;
}

static struct uds_ctx g_ctx;
static uds_config_t g_cfg;
static uint8_t g_rx_buffer[256];
static uint8_t g_tx_buffer[256];
static uint32_t g_time;
static uint32_t time_ms(void) { return g_time; }
static int g_tp_send_calls;
static int fn_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx; (void) data; (void) len;
    g_tp_send_calls++; /* TesterPresent positive response path */
    return 0;
}

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_sdu[256];

static int setup(void **state)
{
    (void) state;
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.rx_buffer = g_rx_buffer;
    g_cfg.rx_buffer_size = sizeof(g_rx_buffer);
    g_cfg.tx_buffer = g_tx_buffer;
    g_cfg.tx_buffer_size = sizeof(g_tx_buffer);
    g_cfg.get_time_ms = time_ms;
    g_cfg.fn_tp_send = fn_tp_send;
    g_cfg.p2_ms = 50;
    g_cfg.p2_star_ms = 5000;
    assert_int_equal(uds_init(&g_ctx, &g_cfg), UDS_OK);

    g_time = 0;
    g_reassembled_len = 0;
    g_cf_count = 0;
    g_tp_send_calls = 0;
    uds_tp_isotp_init(&g_iso, mock_can_send, 0x7E0, 0x7E8, g_iso_sdu, sizeof(g_iso_sdu));
    uds_tp_isotp_set_mode(&g_iso, ISOTP_FULL_DUPLEX);
    return 0;
}

static void test_inbound_sf_does_not_abort_response(void **state)
{
    (void) state;

    /* 1. Server starts a 30-byte multi-frame response. */
    uint8_t resp[30];
    for (int i = 0; i < 30; i++) resp[i] = (uint8_t) i;
    assert_int_equal(uds_isotp_send(&g_iso, resp, 30), 0); /* emits FF (6 bytes) */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* 2. Inbound TesterPresent SF arrives mid-stream (functionally irrelevant). */
    uint8_t tp[8] = {0x02, 0x3E, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, tp, 8);
    /* Core serviced it (TesterPresent positive response sent via fn_tp_send). */
    assert_true(g_tp_send_calls >= 1);
    /* TX response untouched. */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_WAIT_FC);

    /* 3. Receiver grants flow control; server streams remaining 24 bytes. */
    uint8_t fc_cts[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};
    uds_isotp_rx_callback(&g_iso, &g_ctx, 0x7E8, fc_cts, 8);
    for (int i = 0; i < 4; i++) {
        g_time += 1;
        uds_tp_isotp_process(&g_iso, g_time);
    }

    /* 4. The full 30-byte response was transmitted intact and in order. */
    assert_int_equal(g_iso.tx_state, ISOTP_TX_IDLE);
    assert_int_equal(g_reassembled_len, 30);
    assert_memory_equal(g_reassembled, resp, 30);
    assert_int_equal(g_cf_count, 4); /* 24 bytes / 7 per CF = 4 frames */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_inbound_sf_does_not_abort_response, setup, NULL),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

> If `uds_init` requires additional mandatory config fields (check `uds_init` in `src/core/uds_core.c` for required non-NULL members and add them to `setup`), populate them minimally. The TesterPresent positive response is emitted through `fn_tp_send`; if the core suppresses it for some reason, assert instead that `uds_input_sdu` did not disturb the TP TX state (the load-bearing assertion is the intact 30-byte response).

- [ ] **Step 2: Register the test (NOT wrapped) in CMake**

In `tests/CMakeLists.txt`, near the issue-#29 regression registration (line 78), add:

```cmake
# Regression for issue #42: full-duplex inbound frame must not abort an
# in-flight multi-frame response (full stack, real uds_input_sdu).
add_uds_test(test_issue42_full_duplex_response unit/test_issue42_full_duplex_response.c)
```

- [ ] **Step 3: Build and run**

Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build -R test_issue42 --output-on-failure
```
Expected: PASS. If `uds_init` rejects the config, read its validation in `src/core/uds_core.c` and add the missing required fields to `setup`.

- [ ] **Step 4: Run the whole suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i tests/unit/test_issue42_full_duplex_response.c
git add tests/unit/test_issue42_full_duplex_response.c tests/CMakeLists.txt
git commit -m "test(isotp): full-stack regression for issue #42 full-duplex response"
```

---

## Task 5: Documentation

**Files:**
- Modify: `docs/TRANSPORT.md`
- Modify: `CHANGELOG.md`

**Interfaces:** none.

- [ ] **Step 1: Document duplex modes in `docs/TRANSPORT.md`**

Read `docs/TRANSPORT.md` first to match its structure, then add a section near the ISO-TP description:

```markdown
## Duplex mode (half / full)

Each ISO-TP channel operates in one of two modes, selected with
`uds_tp_isotp_set_mode(&iso, mode)`:

- `ISOTP_HALF_DUPLEX` (default): one transfer per N_AI at a time. An inbound
  Single/First Frame terminates an in-flight transmission, and starting a
  transmission abandons an in-flight reception — matching ISO 15765-2 Table 23
  for half-duplex nodes. This is the default to preserve compatibility.
- `ISOTP_FULL_DUPLEX`: a segmented reception and a segmented transmission run
  simultaneously and independently on the same N_AI. Inbound frames never abort
  an outgoing response, and vice versa. Use this when the node must accept new
  requests while still streaming a long response (e.g. gateways, or a server
  that must not drop its response when the tester sends TesterPresent).

The mode is per channel (mirrors AUTOSAR `CanTpChannelMode`). RX and TX use
independent state, sequence numbers, block-size counters, and N_Cr/N_Bs timers;
the receiver-advertised BS/STmin (sent in our FlowControl) are kept separate
from the sender-honored BS/STmin (received in the peer's FlowControl).
```

- [ ] **Step 2: Add the CHANGELOG entry**

In `CHANGELOG.md`, under `## [Unreleased]` → `### Added`, append:

```markdown
- **ISO-TP full-duplex mode**: `uds_tp_isotp_set_mode(iso, ISOTP_FULL_DUPLEX)` lets a segmented reception and a segmented transmission proceed simultaneously on the same N_AI; an inbound frame no longer aborts an in-flight multi-frame response. Default remains half-duplex (prior behavior). The ISO-TP context now keeps independent RX/TX state. (#42)
```

- [ ] **Step 3: Commit**

```bash
git add docs/TRANSPORT.md CHANGELOG.md
git commit -m "docs(isotp): document full-duplex mode and Table 23 behavior (#42)"
```

---

## Final: open the PR

- [ ] **Step 1: Push and open the PR against `develop`**

```bash
git push -u origin feature/isotp-full-duplex-issue42
gh pr create --base develop --title "ISO-TP full-duplex mode (#42, part 1/2)" \
  --body "$(cat <<'EOF'
Implements part 1 of #42: optional full-duplex ISO-TP.

- Splits the shared ISO-TP state machine into independent RX/TX sub-machines.
- Adds a per-channel duplex flag (mirrors AUTOSAR CanTpChannelMode), default
  half-duplex (behavior-preserving). `uds_tp_isotp_set_mode()` opts in.
- Full-duplex: an inbound frame no longer aborts an in-flight multi-frame
  response; RX and TX have independent SN/BS/timers and separate
  advertised-vs-honored BS/STmin.
- Tests: behavior suite (`test_tp_full_duplex`), full-stack regression
  (`test_issue42_full_duplex_response`), and migrated white-box TP tests.

Physical vs. functional addressing (the other half of #42) follows in a
separate PR.

Refs #42
EOF
)"
```
Expected: PR created targeting `develop`, CI (clang-format-14 gate + ctest) green.

---

## Self-Review

**Spec coverage:**
- State split → Task 1. ✅
- Duplex flag + `set_mode` + default half-duplex → Tasks 1 (decl/default) + 2 (impl/behavior). ✅
- Table 23 one-rule (SF/FF vs TX; send vs RX) → Task 1 (rx_sf/rx_ff) + Task 2 (send). ✅
- Independent `process()` ticks → Task 1 Step 5. ✅
- Frame routing (FC→TX, CF→RX isolation) → covered by Task 1 rewrites; tested Task 3 case 5. ✅
- Advertised-vs-honored BS/STmin split → Task 1 (struct + rx_ff + rx_fc + process). ✅
- White-box test migration (4 files) → Task 1 Steps 10-13. ✅
- New behavior suite (7 cases mapping to spec's test list 1-7) → Task 3. ✅
- Full-stack regression (spec test 8) → Task 4. ✅
- Build wiring (`--wrap` on the new wrapped test) → Task 3 Step 2; non-wrapped reg → Task 4 Step 2. ✅
- Docs (TRANSPORT.md, CHANGELOG) → Task 5. ✅ (No version bump: releases are a separate workflow.)

**Placeholder scan:** No TBD/TODO; every code step shows complete code. Task 4 Step 1 notes a conditional fallback (if `uds_init` needs more fields / suppresses the TP response) — this is explicit guidance with a stated load-bearing assertion, not a placeholder.

**Type consistency:** Field names (`rx_state`, `tx_state`, `rx_msg_len`, `tx_msg_len`, `rx_bytes_processed`, `tx_bytes_processed`, `rx_sn`, `tx_sn`, `tx_bs_counter`, `tx_block_size`, `tx_st_min`, `block_size`, `st_min`, `mode`) and enum constants (`ISOTP_RX_IDLE/ISOTP_RX_WAIT_CF`, `ISOTP_TX_IDLE/ISOTP_TX_WAIT_FC/ISOTP_TX_SENDING_CF`, `ISOTP_HALF_DUPLEX/ISOTP_FULL_DUPLEX`) are used identically across Tasks 1-4. `uds_tp_isotp_set_mode` signature consistent in header (Task 1), impl (Task 2), and tests (Tasks 2-4). ✅
