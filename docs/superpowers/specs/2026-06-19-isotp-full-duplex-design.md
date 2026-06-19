# ISO-TP Full-Duplex Support (Issue #42, Part 1 of 2)

**Date:** 2026-06-19
**Issue:** [#42](https://github.com/w1ne/udslib/issues/42)
**Scope:** This spec covers **only** full-duplex ISO-TP. The second half of issue
#42 — physical vs. functional addressing — is deliberately deferred to a
separate spec and PR. Both PRs reference #42.

## Problem

ISO 15765-2 allows a node to transmit and receive segmented messages
simultaneously on the same N_AI (full-duplex). udslib's ISO-TP engine is
half-duplex only:

- `uds_isotp_ctx_t` has a **single** `state` field and **one** shared set of
  segmentation counters (`msg_len`, `bytes_processed`, `sn`, `bs_counter`)
  serving both directions (`include/uds/uds_isotp.h:101-122`).
- `uds_rx_sf()` / `uds_rx_ff()` set `state = ISOTP_IDLE` on any incoming SF/FF
  (`src/transport/uds_tp_isotp.c:285,309`).

Concretely: if the ECU is streaming a multi-frame **response** (FF + CFs) and the
tester sends *any* inbound frame — even a single-frame `TesterPresent` — the
incoming SF lands in `uds_rx_sf()`, which resets the shared `state` and **aborts
the in-flight response mid-transfer**. The tester never receives the rest of its
data. This is the half-duplex limitation described in the issue (and is closer
to a defect than a design choice).

Note: the RX and TX **buffers** are already independent — RX reassembles into
`config->rx_buffer`, TX caches into the caller-provided `tx_sdu_buf`. Only the
*state fields* are shared. So this is a state-field split, not a buffer change.

## Industry alignment

ISO 15765-2 supports both modes and does not mandate one; Table 23 ("Handling of
unexpected arrival of N_PDU with same N_AI as currently being processed") exists
precisely to define half-duplex conflict behavior. AUTOSAR CanTp — the de-facto
production TP layer — models this as a **per-channel configuration parameter**,
`CanTpChannelMode` ∈ {`CANTP_HALF_DUPLEX`, `CANTP_FULL_DUPLEX`}. Lightweight
embedded server stacks (which is what udslib is today) commonly default to
half-duplex because a UDS server serializes diagnostics request→response (the
core already rejects concurrent requests with NRC 0x21).

**Decision:** expose duplex as a configurable flag (mirroring `CanTpChannelMode`)
with the conservative **default = half-duplex**. Full-duplex is one opt-in call.
This preserves current behavior for all existing users and keeps existing tests
green.

## Design

### 1. State split

Replace the single `state` + shared counters with two independent sub-machines
in `uds_isotp_ctx_t`:

```c
typedef enum { ISOTP_RX_IDLE = 0, ISOTP_RX_WAIT_CF } uds_isotp_rx_state_t;
typedef enum { ISOTP_TX_IDLE = 0, ISOTP_TX_WAIT_FC, ISOTP_TX_SENDING_CF }
        uds_isotp_tx_state_t;

typedef enum { ISOTP_HALF_DUPLEX = 0, ISOTP_FULL_DUPLEX } uds_isotp_duplex_t;
```

Context fields, grouped by direction:

| RX machine | TX machine |
|---|---|
| `rx_state` | `tx_state` |
| `rx_msg_len` | `tx_msg_len` |
| `rx_bytes_processed` | `tx_bytes_processed` |
| `rx_sn` | `tx_sn` |
| `rx_bs_counter` | `tx_bs_counter` |
| `timer_n_cr` | `timer_n_bs`, `timer_st` |
| (`config->rx_buffer`) | `tx_sdu_buf`, `tx_sdu_len` (unchanged) |

Plus `uds_isotp_duplex_t mode;` (init defaults to `ISOTP_HALF_DUPLEX`).

The legacy `uds_isotp_state_t` enum and the shared `state`/`msg_len`/
`bytes_processed`/`sn`/`bs_counter` fields are removed. No application/library
code outside the TP implementation touches them, and the library is statically
linked, so there is no ABI concern for **consumers**.

However, four **white-box test files** assert directly on these internal fields
and must be migrated as part of this PR (mechanical, behavior-preserving):

- `test_tp_timeout.c` — `.state` (RX and TX) → `.rx_state` / `.tx_state`.
- `test_tp_isolation.c` — `.state` → `.tx_state` (the asserted values are TX).
- `test_tp_isotp_escape_fc.c` — `.state`, `.msg_len`, `.bytes_processed`, mapped
  to the `rx_*` or `tx_*` variant per the scenario at each assertion site.
- `test_fuzz_core.c` — `valid_isotp_state()` and the `uds_isotp_state_t` /
  `.bytes_processed` / `.state` references reworked to validate both
  `rx_state` and `tx_state` invariants.

Migrating these is a required, in-scope task — not a separate cleanup. (Tests
that assert only on emitted frames — `test_tp_flow_control`,
`test_tp_isotp_integration`, `test_tp_isotp_canfd`, `test_transport` — contain
only comments mentioning state and need no edits.)

### 2. Frame routing + Table 23 conflict handling

With split state, routing is direction-clean:

| Incoming frame | Routed to | Behavior |
|---|---|---|
| **FC** | TX machine only | Consumed when `tx_state == WAIT_FC`; otherwise ignored. Never touches RX. |
| **CF** | RX machine only | Consumed when `rx_state == WAIT_CF`; wrong SN → abort RX (back to RX_IDLE); if RX idle → ignore. |
| **SF** | RX machine | Completes immediately and delivers the SDU. If a segmented RX was in progress → terminate it first, then process the SF (Table 23, RX-vs-RX, both modes). |
| **FF** | RX machine | Start a new reception, send FC. If RX already in progress → terminate old, start new. |

The half- vs. full-duplex difference is **exactly one rule**: what an incoming
**SF/FF** (start of a reception) does to an **in-flight TX**:

- **Full-duplex:** nothing. The reception runs in parallel; the outgoing
  response continues streaming. This is the fix for the defect above.
- **Half-duplex:** if a segmented TX is active (`tx_state != IDLE`), it is
  terminated (only one connection per N_AI). This reproduces today's behavior
  exactly — `uds_rx_sf/ff` currently reset the shared `state`, which *is* the TX
  state.

Symmetric rule for TX initiation:

- **Half-duplex:** `uds_isotp_send()` while a segmented RX is in progress aborts
  that RX (reproduces today's clobber semantics).
- **Full-duplex:** `uds_isotp_send()` does not disturb an active RX.

### 3. `process()` ticks both machines

`uds_tp_isotp_process()` ticks the RX machine (N_Cr timeout → `rx_state =
RX_IDLE`) and the TX machine (N_Bs in WAIT_FC; STmin/BS in SENDING_CF) **independently**.
In half-duplex at most one machine is ever non-idle, so timing behavior is
unchanged; in full-duplex both run. The early-`return`s in the current
`process()` (which assume a single active state) are restructured so RX and TX
ticks do not short-circuit each other.

### 4. Public API (additive, non-breaking)

```c
/* uds_isotp.h */
typedef enum { ISOTP_HALF_DUPLEX = 0, ISOTP_FULL_DUPLEX } uds_isotp_duplex_t;

/**
 * @brief Select half- or full-duplex operation for this channel.
 * Default after init is ISOTP_HALF_DUPLEX (conservative, preserves prior
 * behavior). Full-duplex lets a segmented reception and a segmented
 * transmission proceed simultaneously on the same N_AI.
 */
void uds_tp_isotp_set_mode(uds_isotp_ctx_t *iso, uds_isotp_duplex_t mode);
```

`uds_tp_isotp_init()` sets `mode = ISOTP_HALF_DUPLEX`. No existing signature
changes.

### 5. MISRA / style

Match the existing file: explicit `uint8_t`/`uint16_t` casts, widen before
shift, no dynamic allocation, `if (!iso) return;` guards. Update the file's
header comment where it describes a single state machine.

## Testing

Testing is first-class. All new tests use the established cmocka pattern:
`mock_can_send` (with `check_expected`) verifies emitted frames, and
`__wrap_uds_input_sdu` (linker `-Wl,--wrap=uds_input_sdu`) intercepts RX
completion — so a single test can assert *both* directions concurrently.

### Regression (behavior must stay identical)

Default half-duplex ⇒ identical runtime behavior. Two categories:

- **Pass with no edits** (black-box, assert on emitted frames / delivered SDUs):
  `test_tp_flow_control`, `test_tp_isotp_integration`, `test_tp_isotp_canfd`,
  `test_transport`, `test_issue29_multiframe_request`.
- **Mechanical field-rename migration only** (white-box, per the list in
  §1): `test_tp_timeout`, `test_tp_isolation`, `test_tp_isotp_escape_fc`,
  `test_fuzz_core`. Assertions map old shared fields to the new `rx_*`/`tx_*`
  fields; **asserted values do not change**, only field names.

### New suite: `test_tp_full_duplex.c` (wrapped on `uds_input_sdu`)

1. **Half-duplex incoming SF aborts active TX (behavior lock).** Start a
   multi-frame TX (FF emitted, awaiting FC). Inject an inbound SF. Assert the SF
   is delivered via `uds_input_sdu` **and** that a subsequent FC + `process()`
   produces **no** further CFs (TX was terminated). Locks today's contract.

2. **Full-duplex incoming SF does NOT abort active TX.** Same setup with
   `ISOTP_FULL_DUPLEX`. Inject inbound SF → delivered via `uds_input_sdu`; then
   inject FC and `process()` → remaining CFs of the original response **are**
   emitted in full and correct order. This is the core new capability.

3. **Full-duplex simultaneous segmented RX + segmented TX.** Start a multi-frame
   TX; interleave an inbound FF + CFs for a separate multi-frame reception.
   Assert: (a) the FC for the inbound FF is emitted, (b) the reassembled inbound
   SDU is delivered byte-exact via `uds_input_sdu`, and (c) the outbound CFs are
   all emitted byte-exact. Drive both via `process()` and `rx_callback`
   interleaving.

4. **Independent timers in full-duplex.** With both machines active, advance time
   so N_Cr (RX) would expire while the TX is mid-stream; assert RX resets to idle
   **without** disturbing TX progress, and vice-versa (N_Bs expiry on TX leaves a
   healthy RX untouched).

5. **FC never affects RX; CF never affects TX.** Inject an FC while only an RX is
   active → ignored, RX unaffected. Inject a CF while only a TX is active →
   ignored, TX unaffected.

6. **Half-duplex `send()` aborts active RX (behavior lock).** Begin a reception
   (FF in, awaiting CFs), then call `uds_isotp_send()`; assert RX is abandoned
   (late CFs ignored) — reproduces current clobber semantics.

7. **Wrong-SN during full-duplex RX aborts only RX.** With a TX streaming, feed a
   CF with a bad sequence number; assert RX aborts and TX continues.

### Full-stack regression (real `uds_input_sdu`, NOT wrapped)

8. **`test_issue42_full_duplex_response.c`** — mirrors the issue scenario end to
   end against a real `uds_ctx`: server begins emitting a multi-frame response;
   an inbound functionally-irrelevant SF (`TesterPresent`) arrives mid-stream;
   in full-duplex the response completes intact and the `TesterPresent` is also
   serviced. Registered in `tests/CMakeLists.txt` without `--wrap` (matching the
   issue-#29 regression precedent).

### Build wiring

Add both new test executables to `tests/CMakeLists.txt`; the wrapped one gets
`target_link_options(... -Wl,--wrap=uds_input_sdu)`.

## Documentation

- `docs/TRANSPORT.md`: document the duplex modes, `uds_tp_isotp_set_mode()`, the
  default, and the Table 23 conflict rules.
- `CHANGELOG.md`: short entry under a new minor version — "ISO-TP: optional
  full-duplex mode (`uds_tp_isotp_set_mode`); default half-duplex."
- `docs/SERVICE_COMPLIANCE.md` if it tracks ISO 15765-2 transport conformance.

## Out of scope (deferred to PR 2)

Physical vs. functional addressing: functional (broadcast) RX ID, addressing
mode plumbed to the core, `address_mode` field on `uds_service_entry_t`, and
NRC-suppression rules for functionally-addressed requests. Separate spec + PR,
also referencing #42.
