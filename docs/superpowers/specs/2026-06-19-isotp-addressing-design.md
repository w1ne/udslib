# ISO-TP Physical vs. Functional Addressing (Issue #42, Part 2 of 2)

**Date:** 2026-06-19
**Issue:** [#42](https://github.com/w1ne/udslib/issues/42)
**Builds on:** Part 1 (full-duplex ISO-TP, PR #46, merged to develop).

## Problem

udslib's ISO-TP layer recognises a single physical `rx_id`
(`uds_isotp_rx_callback`, `id != iso->rx_id` → ignore). It cannot distinguish a
**physically-addressed** request (point-to-point, one ECU) from a
**functionally-addressed** one (broadcast, one-to-many). ISO 14229-1 requires the
distinction: some services answer only physical requests, some both, and a
functionally-addressed request has special rules (Single-Frame only at the
transport layer; suppressed negative responses to avoid bus flooding). Today the
addressing mode is neither detected nor available to the service dispatcher, and
`uds_service_entry_t` has no way to express which addressing a service accepts.

## Design

### 1. ISO-TP layer — functional RX ID and classification

Add a functional (broadcast) receive ID to the channel:

```c
/* uds_isotp_ctx_t */
uint32_t rx_id_func; /* Functional/broadcast RX ID; 0 = functional disabled */
```

```c
/**
 * @brief Set the functional (broadcast) RX ID for this channel.
 * A frame whose CAN ID equals rx_id_func is treated as a functionally
 * addressed request. 0 (default) disables functional reception.
 */
void uds_tp_isotp_set_functional_id(uds_isotp_ctx_t *iso, uint32_t rx_id_func);
```

`uds_isotp_rx_callback` classifies each inbound frame by CAN ID:

- `id == iso->rx_id` → **physical**
- `iso->rx_id_func != 0 && id == iso->rx_id_func` → **functional**
- otherwise → ignore (unchanged)

**Functional addressing is Single-Frame only** (ISO 15765-2: segmented transfer
and flow control are not defined for one-to-many). On the functional path only an
SF is processed; a functionally-addressed FF/CF/FC is ignored. The physical path
is unchanged (SF + full FF/CF/FC reassembly, including the Part-1 full-duplex
behaviour).

The reassembled/SF SDU is delivered to the core **tagged with its addressing
mode** (see §2). The existing `uds->config->rx_buffer` reassembly and FC emission
are physical-only and unaffected.

### 2. Core entry — plumb the addressing mode (source-compatible)

```c
/* uds_core.h */
typedef enum {
    UDS_ADDR_PHYSICAL   = (1u << 0),
    UDS_ADDR_FUNCTIONAL = (1u << 1)
} uds_addr_mode_t;

/* New: SDU entry with explicit addressing. */
void uds_input_sdu_addr(uds_ctx_t *ctx, const uint8_t *data, uint16_t len,
                        uds_addr_mode_t addr);
```

`uds_input_sdu(ctx, data, len)` is **retained as a thin wrapper** that calls
`uds_input_sdu_addr(ctx, data, len, UDS_ADDR_PHYSICAL)`. Every existing caller
(and the physical ISO-TP path) keeps working unchanged — no signature break.

The current request's addressing mode is stored on the context
(`uint8_t req_addr_mode;`, added near `active_session`) for use by the dispatcher
and the negative-response path. It is set at the top of `uds_input_sdu_addr` and
defaults to `UDS_ADDR_PHYSICAL` for any inner dispatch (the 0x84 secured-capture
and 0x86 ROE paths call `handle_request` directly; they must be treated as
physical so functional suppression never affects them).

### 3. Service registry — `address_mode` field

```c
typedef struct
{
    uint8_t sid;
    uint16_t min_len;
    uint8_t session_mask;
    uint16_t security_mask;
    uds_service_handler_t handler;
    const uint8_t *sub_mask;
    uint8_t address_mode; /* NEW, LAST field: UDS_ADDR_* bitmask; 0 = both */
} uds_service_entry_t;
```

The field is added **last** so every existing positional initializer (the core
`core_services[]` table and all user tables) keeps compiling, with `address_mode`
defaulting to `0`. **`0` is treated as `UDS_ADDR_PHYSICAL | UDS_ADDR_FUNCTIONAL`
(both allowed)** — backward compatible and consistent with udslib's
permissive-by-default, opt-in-hardening pattern (`restrict_sessions`). A service
restricts itself by setting `UDS_ADDR_PHYSICAL` or `UDS_ADDR_FUNCTIONAL`. The
core table is left unset (both) for now; integrators opt into restrictions via
`user_services`.

### 4. Dispatch gating + ISO 14229-1 negative-response suppression

In `handle_request`, before executing, compute the service's effective allowed
modes and compare to the request's mode:

```c
uint8_t am = service->address_mode ? service->address_mode
                                   : (UDS_ADDR_PHYSICAL | UDS_ADDR_FUNCTIONAL);
if ((am & ctx->req_addr_mode) == 0) {
    /* Service does not accept this addressing. */
    if (ctx->req_addr_mode == UDS_ADDR_FUNCTIONAL) {
        return; /* broadcast: stay silent */
    }
    uds_send_nrc(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED); /* physical -> 0x11 */
    return;
}
```

**Functional negative-response suppression.** ISO 14229-1 requires that a
functionally-addressed request must not elicit certain negative responses (so a
shared bus is not flooded when many ECUs receive the same broadcast). This is
enforced centrally in `uds_send_nrc`: when `ctx->req_addr_mode ==
UDS_ADDR_FUNCTIONAL` and the NRC is in the suppressable set, the function returns
`UDS_OK` **without transmitting**.

Suppressable set: `0x11` serviceNotSupported, `0x12` subFunctionNotSupported,
`0x7E` subFunctionNotSupportedInActiveSession, `0x7F`
serviceNotSupportedInActiveSession, `0x31` requestOutOfRange. (`0x7E` is added as
a new `UDS_NRC_*` constant for completeness; the stack does not currently emit it,
but it belongs in the ISO suppress set.)

All other NRCs (e.g. `0x22` conditionsNotCorrect, `0x33` securityAccessDenied,
`0x78` responsePending) and all **positive** responses are still sent, on the
physical `tx_id`. The `RESPONSE_PENDING` (0x78) path is explicitly **not**
suppressed.

This keeps suppression in exactly one place; `handle_request` does not special-
case each NRC.

### 5. File touch-list

- `include/uds/uds_isotp.h` — `rx_id_func` field, `uds_tp_isotp_set_functional_id` decl.
- `src/transport/uds_tp_isotp.c` — init `rx_id_func=0`; setter; classify in `rx_callback`; functional SF-only routing.
- `include/uds/uds_core.h` — `uds_addr_mode_t`, `uds_input_sdu_addr` decl, doc on the wrapper.
- `include/uds/uds_config.h` — `address_mode` field on `uds_service_entry_t`; `req_addr_mode` on the context struct.
- `src/core/uds_internal.h` — `UDS_NRC_SUBFUNC_NOT_SUPP_IN_SESS 0x7Eu`; addressing helpers if needed.
- `src/core/uds_core.c` — `uds_input_sdu` → wrapper, new `uds_input_sdu_addr`; set/clear `req_addr_mode` (physical for inner dispatch); gating in `handle_request`; suppression in `uds_send_nrc`.

## Testing

Established cmocka pattern; `mock_can_send` + `check_expected` for emitted frames;
`__wrap_uds_input_sdu` where the ISO-TP→core boundary is asserted; real
`uds_input_sdu`/`uds_input_sdu_addr` for full-stack.

### ISO-TP suite (`test_tp_addressing.c`, wrapped on `uds_input_sdu_addr`)

1. Frame on `rx_id` → core called with `UDS_ADDR_PHYSICAL`.
2. Frame on `rx_id_func` → core called with `UDS_ADDR_FUNCTIONAL`.
3. `rx_id_func == 0` (default): a frame on some other ID is ignored (functional disabled).
4. Functionally-addressed **FF is ignored** (no FC emitted, core not called); functional SF is delivered.
5. Physical multi-frame reassembly still works (regression; Part-1 behaviour intact).

> Note: the wrap target becomes `uds_input_sdu_addr` (the physical wrapper calls
> it too), so existing `--wrap=uds_input_sdu` suites are unaffected; the new
> suite wraps `uds_input_sdu_addr`.

### Core suite (`test_addressing_dispatch.c`)

6. `address_mode==0` service answers BOTH a physical and a functional request.
7. `UDS_ADDR_PHYSICAL`-only service: functional request → **silent** (no frame); physical request → normal response.
8. `UDS_ADDR_FUNCTIONAL`-only service: physical request → NRC `0x11`; functional request → normal response.
9. **NRC suppression:** a functional request that would yield `0x11`/`0x12`/`0x7F`/`0x31` → **no frame emitted**; the identical physical request → the NRC IS emitted.
10. Functional request yielding a non-suppressable NRC (`0x22`, `0x33`) → NRC **is** emitted.
11. Functional request yielding a **positive** response → response **is** emitted.
12. `uds_input_sdu` (legacy, no addr) still dispatches as physical (wrapper regression).
13. Inner dispatch unaffected: a 0x86 ROE / 0x84 secured inner request is treated as physical (no accidental suppression). (Assert via an inner request whose NRC would be suppressed only if functional.)

### Full-stack regression (`test_issue42_functional.c`, real core)

14. Functional `TesterPresent` (`0x3E 0x00`) on `rx_id_func` is serviced (positive response on `tx_id`).
15. Functional request for an unsupported SID → **silence** (suppressed `0x11`), while the same SID physically → NRC `0x11`.

### Build wiring

Register the three new suites in `tests/CMakeLists.txt`; the ISO-TP suite gets
`-Wl,--wrap=uds_input_sdu_addr`.

## Documentation

- `docs/TRANSPORT.md`: functional addressing, `uds_tp_isotp_set_functional_id`, SF-only rule.
- `docs/SERVICE_COMPLIANCE.md` (or TRANSPORT): the `address_mode` field, `0 = both`, and the functional NRC-suppression set.
- `include/uds/uds_core.h`: doc comments on `uds_addr_mode_t` / `uds_input_sdu_addr`.
- `CHANGELOG.md`: one `[Unreleased] → Added` bullet referencing (#42). No version bump.

## Out of scope / non-goals

- No functional **multi-frame** support (ISO disallows it; functional is SF-only).
- No change to how responses are transmitted (always on the physical `tx_id`).
- The core `core_services[]` table is left at `address_mode = 0` (both); per-service
  ISO addressing policy for built-ins can be a later refinement — not required to
  close #42, which asks for the mechanism.
- Closes the remaining half of #42; the issue can be closed when this merges.
