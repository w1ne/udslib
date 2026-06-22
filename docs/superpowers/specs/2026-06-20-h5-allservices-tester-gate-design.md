# All-services dual-H5 tester gate — design (Sub-project B)

**Date:** 2026-06-20
**Repo:** udslib (firmwares + CI). Runs on the labwired STM32H5 emulator.
**Status:** Approved design, ready for implementation plan.
**Origin:** Closes the real goal behind issue #58 — exercise **all 27 implemented UDS
services** on a virtual ECU driven by a real udslib tester over a virtual CAN bus,
headless, with no hardware. Sub-project A (labwired-core, merged via PR #322 into
`feat/iolink-multichip-station`) delivered the emulator primitives; B is the udslib
half: the two firmwares + the nightly gate.

## 1. Purpose

A real udslib **tester** firmware on one virtual STM32H5 drives a request for every
one of udslib's **27 ISO-14229 services** to a real udslib **ECU** firmware on a
second virtual H5, over a virtual FDCAN bus, verifies each response, and records a
per-service pass bit. A nightly CI job asserts all 27 passed. This is a genuine
**cross-node, all-services** oracle: the tester can only set a service's bit if its
request crossed the bus and the ECU's real response came back.

## 2. What this reuses (it is mostly a port, not green-field)

- **The precondition-correct sweep already exists**: `tests/integration/test_uds.py::test_full_sequence()` walks session → auth → security (seed `DEADBEEF` / key `DFAEBFF0`) → read/write DID → DTC → routine → download/transfer/exit → comm-control → tester-present → reset, with the exact expected response bytes. B reimplements this in C and extends it from ~16 to all 27 services.
- **The full ECU service configuration already exists** in `examples/host_sim/main.c` (`fn_security_seed/key`, `fn_routine_control`, `fn_transfer_data/exit`, `fn_mem_read/write`, `fn_periodic_read`, `fn_auth`, `fn_io_control`, `fn_reset`, DID table, DTC list). B retargets it to STM32H5 + FDCAN, building on the existing `h563-uds-ecu` skeleton (already on current instance-based udslib via Sub-project A).
- **The client API**: `uds_client_request(ctx, sid, data, len, cb)` + `uds_response_cb` — the tester's driver.
- **The emulator primitives** (Sub-project A, labwired-core): the `can_bus` interconnect arm in `World::from_manifest`, `MachineTrait::attach_can_bus`, and `LoadedTestScript::Env` + `run_env_test` (per-node `memory_value` assertions, headless via `labwired test --script`).

## 3. Components (all in udslib)

### B1 — ECU firmware `examples/h5_uds_ecu_full/`
STM32H5 + FDCAN udslib **server** registering all 27 services with the `host_sim`
test configuration (DIDs, routine, security seed/key, transfer buffer, memory
regions, periodic/dynamic DIDs, IO controls, DTC list, ROE, comm-control, link,
timing, secured-transmission stubs). Built on the `h563-uds-ecu` startup/linker/HAL
glue. Driven entirely by inbound CAN requests (no static injector — the tester is the
only requester). FDCAN id scheme: RX `0x7E0`, TX `0x7E8` (matches the Python suite).

### B2 — Tester firmware `examples/h5_uds_tester/`
STM32H5 + FDCAN udslib **client**. Runs a single ordered sweep (one
`uds_client_request` per service, response verified in the callback), recording each
pass as a bit in a 32-bit `g_service_results` at a fixed `.noinit` linker address
(`0x20010000`, the marker convention from A). On completion it also prints a UART
summary `SERVICES <n>/27 PASS`. Reset (`0x11`) is the **last** service tested so the
ECU rebooting afterward cannot disturb earlier results.

**Service order (dependency-correct):** `0x10`(extended) → `0x29` → `0x27`
seed+key → [`0x3E` keep-alive interspersed] → `0x22`,`0x2E`,`0x23`,`0x24`,`0x2C`,
`0x2A`,`0x2F`,`0x3D` → `0x19`,`0x85`,`0x14`,`0x86` → `0x31` → `0x10`(programming) →
`0x34`,`0x36`,`0x37`,`0x35`,`0x38` → `0x28`,`0x83`,`0x84`,`0x87` → `0x11`.

**Crypto-gated services (`0x27`/`0x29`/`0x84`):** deterministic **functional test
stubs** (the `host_sim` values), not real ciphers — this gate proves protocol
envelope + sequencing; real-cipher validation stays with the mbedTLS examples
(`auth_challenge_mbedtls`, `security_access_mbedtls`). *Follow-up:* the nightly may
later add a real-crypto variant.

**Event/timer services (`0x2A`,`0x86`):** verify the **setup positive response** in
this increment; receiving an emitted event is a documented stretch goal.

### B3 — Manifest + env test script
`examples/h5_uds_tester/twonode-env.yaml` (tester + ECU nodes wired by a `can_bus`
interconnect) and `allservices-gate.yaml` (a `LoadedTestScript::Env` script asserting
`memory_value { node: tester, address: 0x20010000, expected_value: 0x07FFFFFF, size: 32 }`
— all 27 bits set). Uses A's `run_env_test`.

### B4 — Nightly CI workflow `.github/workflows/nightly-h5-gate.yml`
A **scheduled-only** job (NOT on PRs — preserves the 1-minute PR gate):
1. build both ARM firmwares (`UDSLIB_DIR=$PWD make -C examples/h5_uds_ecu_full/firmware` and `.../h5_uds_tester/firmware`);
2. obtain the **labwired CLI in Docker** (see §4) and run `labwired test --script examples/h5_uds_tester/allservices-gate.yaml` headless;
3. assert exit 0. Runs on free GitHub Actions runners — no labwired API key, no quota.

## 4. Hard dependency on Sub-project A (prerequisite, explicit)

B's gate needs A's `can_bus` arm + `run_env_test`, which are merged into
labwired-core `feat/iolink-multichip-station` (PR #322) but **not yet on `main` or a
tagged release**. Therefore:
- **Near term:** B's nightly builds the labwired CLI in Docker from a **pinned
  labwired-core ref that includes A** (the #322 merge commit `a0b87476` or its
  branch tip). The firmwares + manifest are inert without it.
- **Once A reaches `main` + a tagged release:** switch the nightly to the pinned
  released labwired binary/image.
- The plan's **Task 0** verifies the gate runs locally against a labwired CLI built
  from that ref BEFORE the firmwares are fully built out — de-risking the
  integration exactly as A's spike did.

## 5. Data flow

```
tester: uds_client_request(SID,…) ─▶ FDCAN TX ─▶ CanBus ─▶ ECU FDCAN RX
                                                              │ real udslib dispatch
tester cb: verify bytes, set bit N ◀─ FDCAN RX ◀─ CanBus ◀───┘ response
   … repeat for all 27 …
g_service_results == 0x07FFFFFF  +  UART "SERVICES 27/27 PASS"
   ▲ read headless by run_env_test memory_value on the tester node ─▶ exit 0
```

## 6. Error handling / honesty

- A service whose response bytes don't match leaves its bit clear → `g_service_results != ALL` → gate exit 1, and the UART summary shows `<n>/27`. The per-bit layout names exactly which service failed.
- `0x11` reset tested last; ECU non-response afterward is expected and irrelevant.
- The gate asserts the **tester** marker (cross-node oracle), never the ECU's own — avoiding A's co-existence-smoke false-pass class.
- No `expected_stop_reason`/`uart_contains` in the env script (A's `run_env_test`
  rejects those); the bitmap memory_value is the deterministic oracle.

## 7. Scope / YAGNI

- **In:** positive-path for all 27 services; the nightly gate; the two firmwares + manifest.
- **Out (documented follow-ups):** the NRC/negative matrix (wrong key → 0x35,
  security-gated-without-unlock → 0x33, etc.); real-cipher crypto variant; emitted-event
  verification for 0x2A/0x86; switching from build-from-ref to a released labwired binary.

## 8. Testing

- The nightly headless run IS the gate (exit 0 ⇔ 27/27).
- Both firmwares compile-check in the CI job.
- Task 0 proves the end-to-end path on a minimal (few-service) sweep before the full
  27 are wired, so a failure is localized early.

## 9. Risks

- **A not on main/release** (§4) — mitigated by build-from-pinned-ref + Task 0.
- **ARM build surface in udslib CI** — two firmwares need `arm-none-eabi-gcc`; the
  nightly installs it (nightly only, so it doesn't touch the 1-min PR gate).
- **Sweep fidelity** — some services (0x2A/0x86/0x84) are partial-depth by design;
  §3 scopes them to setup-response verification, stated plainly so a green gate is not
  read as more than it is.
