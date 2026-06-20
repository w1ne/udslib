# All-services dual-H5 tester gate — Implementation Plan (Sub-project B)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A real udslib tester firmware on a virtual STM32H5 drives a request for all 27 UDS services to a real udslib ECU firmware on a second virtual H5 over a virtual FDCAN bus, verifies each response, and a nightly CI job asserts all 27 passed.

**Architecture:** Port two existing, working udslib artifacts onto STM32H5+FDCAN: the ECU = `examples/host_sim/main.c`'s full service config on the `h563-uds-ecu` firmware skeleton; the tester = `tests/integration/test_uds.py::test_full_sequence()` reimplemented as a udslib C client, extended to all 27 services, recording a per-service pass bit. The gate asserts the tester's bitmap headless via Sub-project A's `run_env_test`.

**Tech Stack:** C (udslib, `arm-none-eabi-gcc`), STM32H5 FDCAN register glue (from `h563-uds-ecu`), the labwired CLI (built from Sub-project A's ref), GitHub Actions (nightly).

## Global Constraints

- Repo **udslib**, PolyForm-Noncommercial; license header on every new `.c`/`.h` (copy from `examples/host_sim/main.c:1-4`). No Claude/AI references in commits or files. Commit identity = `w1ne <14119286+w1ne@users.noreply.github.com>`. PRs target **`develop`**.
- **CAN id scheme:** tester TX / ECU RX = `0x7E0`; ECU TX / tester RX = `0x7E8` (matches `tests/integration/test_uds.py:19-20`). CAN-FD enabled (`uds_tp_isotp_set_fd(&iso, true)`), like `h563-uds-ecu`.
- **Security test values (reuse verbatim from `host_sim`):** seed `DE AD BE EF`, key `DF AE BF F0`. Auth (0x29) and secured-transmission (0x84) use the `host_sim` deterministic stubs — NOT real crypto.
- **Result marker:** `volatile uint32_t g_service_results __attribute__((section(".uds_result"),used));` at fixed address `0x20010000` (the `.noinit` linker convention from `h563-uds-ecu/firmware/minimal.ld`). On the **tester** node only.
- **Bitmap layout (service → bit), 27 bits, full-pass mask `0x07FFFFFF`:**
  bit0=0x10, 1=0x11, 2=0x14, 3=0x19, 4=0x22, 5=0x23, 6=0x24, 7=0x27, 8=0x28, 9=0x29, 10=0x2A, 11=0x2C, 12=0x2E, 13=0x2F, 14=0x31, 15=0x34, 16=0x35, 17=0x36, 18=0x37, 19=0x38, 20=0x3D, 21=0x3E, 22=0x83, 23=0x84, 24=0x85, 25=0x86, 26=0x87. Define these as `#define BIT_xx (1u<<n)` in a shared `examples/h5_uds_tester/firmware/service_bits.h`.
- **Service order (dependency-correct):** 0x10(extended)→0x29→0x27 seed+key→0x22→0x2E→0x23→0x24→0x2C→0x2A→0x2F→0x3D→0x19→0x85→0x14→0x86→0x31→0x10(programming)→0x34→0x36→0x37→0x35→0x38→0x28→0x83→0x84→0x87→0x3E(throughout)→0x11(LAST). Reset is last; the ECU rebooting afterward must not change earlier bits.
- **Sub-project A dependency:** the labwired CLI used to run the gate must include A's `can_bus` arm + `run_env_test`. Build it in Docker from labwired-core ref `a0b87476` (PR #322 merge on `feat/iolink-multichip-station`) until A reaches `main`+release. The local worktree `/home/andrii/projects/labwired/.worktrees/core-fdcan-multinode` already has this code for local runs (`cargo run -p labwired-cli -- test …`).
- The gate asserts the **tester** marker only (cross-node oracle) — never the ECU's own (avoids A's co-existence-smoke false-pass class).

---

### Task 0: Spike — minimal 2-service cross-node sweep proves the whole path (go/no-go)

**Exploratory, not TDD.** De-risk before building 27 services: prove a real udslib tester firmware drives a request to a real udslib ECU firmware over the virtual bus and the headless gate reads the tester's bitmap.

**Files (spike, promoted in later tasks):**
- `examples/h5_uds_tester/firmware/` (tester: session 0x10 + read-DID 0x22 only)
- `examples/h5_uds_ecu_full/firmware/` (ECU: minimal config — session + one DID), copied from `examples/h563-uds-ecu/firmware/`
- `examples/h5_uds_tester/twonode-env.yaml`, `examples/h5_uds_tester/allservices-gate.yaml`

- [ ] **Step 1:** Copy `examples/h563-uds-ecu/firmware/` → both firmware dirs as the startup/linker/HAL/Makefile skeleton. The ECU keeps its 0x22/F190 handler; REMOVE its `can-diagnostic-tester` from the ECU's `system.yaml` (the tester firmware is now the requester).
- [ ] **Step 2:** Tester `main.c`: init udslib **client** (instance-based ISO-TP, FD, TX `0x7E0`/RX `0x7E8`), and drive `uds_client_request(&ctx, 0x10, {0x03}, 1, cb)` then `uds_client_request(&ctx, 0x22, {0xF1,0x90}, 2, cb)`. In the response callback, verify `0x50` then `0x62 F1 90`; set `g_service_results` bit0 (0x10) and bit4 (0x22). Pump FDCAN RX→`uds_isotp_rx_callback` and `uds_tp_isotp_process` in the main loop (mirror `examples/client_demo/main.c` for the client request/callback shape and `h563-uds-ecu` for the FDCAN register glue).
- [ ] **Step 3:** `twonode-env.yaml`: two nodes `tester` + `ecu`, both `configs/chips/stm32h563.yaml` system, wired by `can_bus` interconnect (peripheral `fdcan1`). `allservices-gate.yaml`: `inputs.env: ./twonode-env.yaml`, `memory_value { node: tester, address: 0x20010000, expected_value: 0x11, size: 32 }` (bit0|bit4 = 0x11) — a minimal mask for the spike.
- [ ] **Step 4:** Build both: `UDSLIB_DIR=$PWD make -C examples/h5_uds_tester/firmware && UDSLIB_DIR=$PWD make -C examples/h5_uds_ecu_full/firmware`. Run headless with the A-built CLI:
  `cd /home/andrii/projects/labwired/.worktrees/core-fdcan-multinode && cargo run -q -p labwired-cli -- test --script /home/andrii/projects/udslib-h5-gate/examples/h5_uds_tester/allservices-gate.yaml`
- [ ] **Step 5 (go/no-go):** GO if exit 0 and the tester marker shows `0x11` (both services crossed the bus and verified). NO-GO → STOP and report the chain break (tester didn't send / didn't cross / ECU didn't respond / marker not read). Commit the spike.

```bash
git add examples/h5_uds_tester examples/h5_uds_ecu_full
git commit -m "spike: minimal 2-service dual-H5 cross-node sweep over virtual CAN"
```

---

### Task 1: ECU firmware — full 27-service configuration

**Files:**
- Modify: `examples/h5_uds_ecu_full/firmware/main.c` (extend the spike ECU)
- Reference (read, port): `examples/host_sim/main.c` (the full `uds_config_t` + all `fn_*` handlers + DID table + DTC list + security seed/key + routine/transfer/memory/periodic/IO config)

**Interfaces:**
- Produces: an ECU that, given the inbound requests in the Global-Constraints service order, returns the positive responses the tester expects (see Tasks 2-6 for the exact expected bytes per service, mirrored from `tests/integration/test_uds.py::test_full_sequence`).

- [ ] **Step 1:** Port `host_sim`'s `uds_config_t` and every `fn_*` handler into the ECU `main.c`, retaining the FDCAN register glue from the spike. Register all 27 services (the core stack dispatches them; supply the app callbacks `host_sim` uses: `fn_security_seed/key`, `fn_routine_control`, `fn_request_download/upload`, `fn_transfer_data/exit`, `fn_mem_read/write`, `fn_periodic_read`, `fn_io_control`, `fn_auth`, `fn_reset`, `fn_dtc_read/clear`, DID table incl. F190, ROE config). Keep the `0x20010000` marker OUT of the ECU (tester owns it).
- [ ] **Step 2:** Build: `UDSLIB_DIR=$PWD make -C examples/h5_uds_ecu_full/firmware` — must compile/link clean against current udslib.
- [ ] **Step 3:** Smoke it against the existing tester (Task 0's 2-service tester) headless — confirm 0x10+0x22 still pass (no regression from the config expansion). Exit 0 with marker `0x11`.
- [ ] **Step 4: Commit** `feat(example): h5_uds_ecu_full — full 27-service ECU config on STM32H5/FDCAN`.

---

### Task 2: Tester — session/auth/security/tester-present phase

**Files:** `examples/h5_uds_tester/firmware/main.c`, `examples/h5_uds_tester/firmware/service_bits.h`
**Reference:** `tests/integration/test_uds.py:115-127` (exact request/response bytes).

**Interfaces:**
- Consumes: the ECU (Task 1). Produces: `set_bit(BIT)` + `verify(resp, expected)` helpers reused by Tasks 3-6; bits 0x10, 0x29, 0x27, 0x3E set on pass.

- [ ] **Step 1:** Add `service_bits.h` with the 27 `#define BIT_xx (1u<<n)` from Global Constraints + `#define ALL_SERVICES_MASK 0x07FFFFFFu`.
- [ ] **Step 2:** Implement a small synchronous request helper over the existing client callback (send SID+payload, pump RX/process until the callback fires or a step budget elapses, return the response bytes). Then drive, asserting the verbatim expected bytes: `0x10 03`→`50 03 00 32 01 F4` (BIT 0x10); `0x29 02 DE AD`→`69 02 01` (BIT 0x29); `0x27 01`→`67 01 DE AD BE EF` then `0x27 02 DF AE BF F0`→`67 02` (BIT 0x27); `0x3E 00`→`7E 00` (BIT 0x3E).
- [ ] **Step 3:** Build + run headless; set `allservices-gate.yaml` mask to `(BIT0x10|BIT0x29|BIT0x27|BIT0x3E)` and confirm exit 0.
- [ ] **Step 4: Commit** `feat(example): tester phase 1 — session/auth/security/tester-present`.

---

### Task 3: Tester — data services phase

**Files:** `examples/h5_uds_tester/firmware/main.c`
**Reference:** `test_uds.py:129-136` for 0x22/0x2E; `docs/SERVICE_COMPLIANCE.md` rows for 0x23/0x24/0x2A/0x2C/0x2F/0x3D request shapes; the ECU config (Task 1) for the exact DIDs/addresses it accepts.

- [ ] **Step 1:** After the Task-2 sequence, drive and verify positive responses for: `0x22 F1 90`→`62 F1 90 …` (BIT 0x22); `0x2E 01 23 <data>`→`6E 01 23` (BIT 0x2E); `0x23` ReadMemoryByAddress (BIT 0x23); `0x24` ReadScalingDataByIdentifier (BIT 0x24); `0x2C` DynamicallyDefineDID (BIT 0x2C); `0x2A` ReadDataByPeriodicIdentifier — verify the SETUP positive response only (BIT 0x2A); `0x2F` IOControlByIdentifier (BIT 0x2F); `0x3D` WriteMemoryByAddress (BIT 0x3D). Use request payloads matching what the ECU config accepts (read Task 1's config).
- [ ] **Step 2:** Build + run; extend the gate mask to include these bits; exit 0.
- [ ] **Step 3: Commit** `feat(example): tester phase 2 — data services`.

---

### Task 4: Tester — DTC services phase

**Files:** `examples/h5_uds_tester/firmware/main.c`
**Reference:** `test_uds.py:138-142` (0x19/0x85/0x14).

- [ ] **Step 1:** Drive and verify: `0x19 01 FF`→`59 01 …` (BIT 0x19); `0x85 01`→`C5 01` (BIT 0x85); `0x14 FF FF FF`→`54` (BIT 0x14); `0x86` ResponseOnEvent — verify the SETUP positive response (e.g. `0x86 00 …` start/stop or a definition sub-function the ECU config supports) (BIT 0x86).
- [ ] **Step 2:** Build + run; extend mask; exit 0.
- [ ] **Step 3: Commit** `feat(example): tester phase 3 — DTC services`.

---

### Task 5: Tester — routine + transfer phase (programming session)

**Files:** `examples/h5_uds_tester/firmware/main.c`
**Reference:** `test_uds.py:144-153` (0x31/0x34/0x36/0x37).

- [ ] **Step 1:** Drive and verify: `0x31 01 FF 00`→`71 01 FF 00 00` (BIT 0x31); switch to programming session `0x10 02`; `0x34 …`→`74 …` (BIT 0x34); `0x36 01 …`→`76 01` (BIT 0x36); `0x37`→`77` (BIT 0x37); `0x35` RequestUpload symmetric to 0x34 (BIT 0x35); `0x38` RequestFileTransfer with a valid `modeOfOperation` (BIT 0x38). Match the ECU's accepted ALFID/size config (Task 1).
- [ ] **Step 2:** Build + run; extend mask; exit 0.
- [ ] **Step 3: Commit** `feat(example): tester phase 4 — routine + transfer`.

---

### Task 6: Tester — misc phase + reset-last + full mask

**Files:** `examples/h5_uds_tester/firmware/main.c`
**Reference:** `test_uds.py:155-162` (0x28/0x3E/0x11); SERVICE_COMPLIANCE rows for 0x83/0x84/0x87.

- [ ] **Step 1:** Drive and verify: `0x28 00 01`→`68 00` (BIT 0x28); `0x83` AccessTimingParameter read→positive (BIT 0x83); `0x84` SecuredDataTransmission with the stub envelope→positive (BIT 0x84); `0x87` LinkControl verify→transition→positive (BIT 0x87). THEN, last: `0x11 01`→`51 01` (BIT 0x11). After the reset response, write `g_service_results` once with the accumulated mask (don't depend on post-reset ECU).
- [ ] **Step 2:** Print UART `SERVICES <popcount>/27 PASS`. Set `allservices-gate.yaml` to the full `expected_value: 0x07FFFFFF`. Build + run headless → exit 0, all 27 bits.
- [ ] **Step 3: NEGATIVE check:** temporarily break one service's expected bytes in the tester, rebuild, rerun → exit 1 (mask mismatch); revert → exit 0.
- [ ] **Step 4: Commit** `feat(example): tester phase 5 — misc + reset; full 27/27 gate green`.

---

### Task 7: Manifest + README + env-test finalization

**Files:** `examples/h5_uds_tester/twonode-env.yaml`, `examples/h5_uds_tester/allservices-gate.yaml`, `examples/h5_uds_tester/README.md`

- [ ] **Step 1:** Finalize `twonode-env.yaml` (tester+ecu over can_bus) and `allservices-gate.yaml` (full mask, honest header comment: this asserts the TESTER's cross-node bitmap — a real all-services cross-node oracle). README documents the command, the bitmap layout, the 27/27 meaning, and the labwired-CLI-from-A-ref requirement.
- [ ] **Step 2:** Final headless run green; commit `docs(example): h5 all-services gate manifest + README`.

---

### Task 8: Nightly CI workflow

**Files:** `.github/workflows/nightly-h5-gate.yml`
**Reference:** `.github/workflows/ci.yml` (the post-#62 structure); Global-Constraints A-dependency.

- [ ] **Step 1:** A `schedule`-only workflow (`cron: '0 4 * * *'`) + `workflow_dispatch` (NOT on `pull_request` — preserves the 1-min PR gate). Steps: checkout; `apt install gcc-arm-none-eabi`; build both firmwares (`UDSLIB_DIR=$PWD make -C …`); build/obtain the labwired CLI in Docker from labwired-core ref `a0b87476` (clone + `cargo build -p labwired-cli`, or a prebuilt image pinned to that ref); run `labwired test --script examples/h5_uds_tester/allservices-gate.yaml`; assert exit 0. No `LABWIRED_API_KEY` (free tier).
- [ ] **Step 2:** Validate YAML (`python3 -c "import yaml;yaml.safe_load(open('.github/workflows/nightly-h5-gate.yml'))"`). Document in the README that this is nightly-only and why (heavy: 2 ARM builds + labwired build + 2 emulated MCUs × all services).
- [ ] **Step 3: Commit** `ci: nightly all-services dual-H5 gate (scheduled, non-blocking)`.

---

## Self-Review

**Spec coverage:** §3 B1→Task 1; B2→Tasks 2-6 (+service_bits.h); B3→Task 7; B4→Task 8. §4 A-dependency→Global Constraints + Task 0 + Task 8. §5 data flow→Tasks 2-6. §2 reuse (host_sim/test_full_sequence)→referenced per task. Task 0 spike (de-risk) front-loaded. The 27-bit mask `0x07FFFFFF` and per-service bit layout are pinned in Global Constraints and consumed identically by Tasks 2-6 and the gate YAML.

**Placeholder scan:** no TBD/TODO; each task pins exact request/response bytes (from `test_uds.py`) or points to the exact reference (`host_sim` config, SERVICE_COMPLIANCE rows) for shapes the Python suite doesn't cover; firmware bulk is ported from named reference files, not invented.

**Type consistency:** `g_service_results` @ `0x20010000`, `ALL_SERVICES_MASK 0x07FFFFFF`, `BIT_xx` defines, CAN ids `0x7E0`/`0x7E8`, seed/key `DEADBEEF`/`DFAEBFF0` — used identically across Tasks 0-8 and the gate YAML.

**Known follow-ups (out of scope, per spec §7):** NRC/negative matrix; real-crypto variant; 0x2A/0x86 emitted-event verification; switch from build-from-ref to released labwired binary once A lands on `main`.
