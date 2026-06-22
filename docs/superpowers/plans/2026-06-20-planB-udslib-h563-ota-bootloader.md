# Plan B — udslib STM32H563 UDS OTA Bootloader Example Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a real STM32H563 UDS service-based OTA bootloader example: a mirrored bootloader receives a new firmware image over CAN-FD via udslib (programming session → AES-CMAC security → erase → download → verify → `SWAP_BANK` activate → reset), validated end-to-end in the labwired H563 sim and cross-checked on real silicon over SWD, with a SocketCAN host flash tool for users with a CAN interface.

**Architecture:** Bootloader mirrored at the base of both 1 MB banks; OTA writes only the *app region* of the inactive bank. The firmware drives real H5 FLASH registers (program by direct write after NSKEYR unlock; erase via NSCR SER+STRT; activate via OPTSR_PRG.SWAP_BANK + OPTCR.OBL_LAUNCH). Those register sequences run identically on real silicon and in the labwired sim (modeled by **Plan A**). UDS is served by udslib's flash service (0x34/35/36/37, 0x31) + security (0x27) + session (0x10/0x11/0x3E), transported over FDCAN loopback through `uds_tp_isotp` with FD framing.

**Tech Stack:** C (freestanding, Cortex-M33), clang `--target=arm-none-eabi`, `rust-lld`; udslib sources compiled from `UDSLIB_DIR`; mbedTLS for AES-CMAC; labwired-cli for the sim smoke test; SocketCAN for the host tool.

**Repos:** firmware/app/host in `/home/andrii/projects/udslib` (branch `feat/h563-uds-ota-bootloader`, off `origin/develop`); sim harness in labwired-core. **Depends on Plan A** (the labwired H5 erase + bank-swap model) being merged first for the sim tasks.

## Global Constraints

- Memory map: bootloader `0x08000000..0x08018000` (96 KB) mirrored in both banks; app region `bank_base+0x18000 .. bank_base+0x100000` (~928 KB). Bank 1 `0x08000000`, bank 2 `0x08100000`.
- App image carries a validity footer: magic `0x4F544131` ("OTA1") + image length (u32) + CRC32 (u32) at the end of the written region; bootloader checks it before jump.
- The bootloader region is **never** written over CAN — reprogramming services only ever target the inactive bank's app region; reject any RequestDownload addressed outside it.
- All reprogramming services are gated: programming session (0x10 02, `restrict_sessions`) AND 0x27 security unlocked.
- CAN-FD: tester ID `0x7E0`, ECU response `0x7E8`, `uds_tp_isotp_set_fd(true)`.
- Build mirrors `examples/h563-uds-ecu/firmware` exactly (toolchain, startup.c, libc stubs, FDCAN driver) — copy and extend, don't reinvent.
- Commits: no AI/assistant references; author `w1ne <14119286+w1ne@users.noreply.github.com>`. PR targets `develop`, links `Closes #64`.
- mbedTLS AES-CMAC is software-only (reuse the approach in `examples/security_access_mbedtls`).

---

### Task 1: Scaffold the example + partitioned linker, buildable ELF

**Files:**
- Create: `examples/h563_uds_bootloader/bootloader/{Makefile,startup.c,bootloader.ld,main.c,libc_stubs.c,fdcan.c,fdcan.h}`
- Create: `examples/h563_uds_bootloader/README.md`
- Reference: `examples/h563-uds-ecu/firmware/{Makefile,startup.c,minimal.ld,main.c}` (copy boilerplate)

**Interfaces:**
- Produces: a `build/h563_uds_bootloader.elf` that boots, inits UART (USART3), prints `BL-START\n`, and spins. Establishes the build that later tasks extend.

- [ ] **Step 1: Copy boilerplate from h563-uds-ecu**

Copy `startup.c` verbatim. Copy `main.c`'s libc stubs (`memcpy`/`memset`/`memcmp`/`__aeabi_*`) into `libc_stubs.c`. Copy the FDCAN driver section of `main.c` (the `fdcan_*`, `can_frame_t`, `write_payload`/`read_payload`, UART helpers) into `fdcan.c`/`fdcan.h`. Copy the `Makefile`, then extend its `UDS_SRCS` to add the security service:
```make
UDS_SRCS := \
  $(UDSLIB_DIR)/src/core/uds_core.c \
  $(UDSLIB_DIR)/src/services/uds_service_session.c \
  $(UDSLIB_DIR)/src/services/uds_service_security.c \
  $(UDSLIB_DIR)/src/services/uds_service_flash.c \
  $(UDSLIB_DIR)/src/services/uds_service_data.c \
  $(UDSLIB_DIR)/src/services/uds_service_maintenance.c \
  $(UDSLIB_DIR)/src/transport/uds_tp_isotp.c
TARGET := h563_uds_bootloader
```

- [ ] **Step 2: Write the partitioned linker script**

`bootloader.ld` — bootloader occupies the first 96 KB of bank 1; app region is a separate section the BL does not link code into (the demo app is built separately, Task 5):
```ld
ENTRY(Reset)
MEMORY {
  BL   (rx)  : ORIGIN = 0x08000000, LENGTH = 96K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 640K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_app_bank1 = 0x08018000;   /* App A: active bank app region   */
_app_bank2 = 0x08118000;   /* App B: inactive bank app region (OTA dst) */
_app_region_size = 0x100000 - 0x18000;  /* 928 KB */
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > BL
  .text       : { *(.text*) *(.rodata*) } > BL
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > BL
  .bss  : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .);
  /DISCARD/ : { *(.ARM.exidx*) *(.note.gnu.build-id*) }
}
```

- [ ] **Step 3: Minimal main that prints BL-START**

In `main.c`: include `fdcan.h`, call `uart_init(); uart_puts("BL-START\n");` then `for(;;){}`.

- [ ] **Step 4: Build**

Run:
```bash
UDSLIB_DIR=/home/andrii/projects/udslib \
  make -C examples/h563_uds_bootloader/bootloader
```
Expected: produces `build/h563_uds_bootloader.elf`, no errors.

- [ ] **Step 5: Commit**

```bash
git add examples/h563_uds_bootloader/
git commit -m "feat(example): scaffold H563 UDS OTA bootloader (partitioned linker, buildable ELF)"
```

---

### Task 2: H5 flash driver (unlock, program, sector-erase, bank-swap)

**Files:**
- Create: `examples/h563_uds_bootloader/bootloader/{flash_h5.c,flash_h5.h}`
- Test: covered by the sim integration smoke (Task 6) and real-HW (Task 8); add a host unit test in Task 7's harness if the driver is compiled host-side.

**Interfaces:**
- Produces (in `flash_h5.h`):
  - `void flash_unlock(void);`
  - `int  flash_erase_sector(uint8_t bank, uint32_t sector);` — NSCR SER+SNB+BKSEL+STRT; returns 0 on success.
  - `int  flash_program(uint32_t addr, const uint8_t *data, uint32_t len);` — quad-word (16-byte) aligned writes to `addr`; returns 0 on success.
  - `void flash_set_swap_and_launch(void);` — OPTKEYR unlock + OPTSR_PRG.SWAP_BANK + OPTCR.OBL_LAUNCH (triggers swap+reset).
  - Register constants must match Plan A Task 0 (`flash_h5_regs`): FLASH base `0x40022000`, NSKEYR `0x04`, OPTKEYR `0x0C`, NSSR `0x20`, NSCR `0x28`, OPTCR `0x1C`, OPTSR_PRG `0x54`; bits SER`(1<<2)`, STRT`(1<<5)`, SNB`<<6`, BKSEL`(1<<31)`, SWAP_BANK`(1<<31)`, OBL_LAUNCH`(1<<27)`.

- [ ] **Step 1: Write `flash_h5.h` with the register map + prototypes** (use the constants above; single-source them with a comment pointing to RM0481 §7).

- [ ] **Step 2: Implement `flash_unlock`** — write `0x45670123` then `0xCDEF89AB` to NSKEYR.

- [ ] **Step 3: Implement `flash_program`** — H5 programs 16 bytes at a time; copy `len` bytes (caller guarantees 16-byte alignment/padding) directly to `addr` as 32-bit writes, polling NSSR.BSY between quad-words:
```c
int flash_program(uint32_t addr, const uint8_t *data, uint32_t len) {
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    const uint32_t *src = (const uint32_t *)data;
    NSCR |= NSCR_PG;
    for (uint32_t i = 0; i < len / 4u; i += 4u) {
        for (uint32_t w = 0; w < 4u; ++w) dst[i + w] = src[i + w];
        while (NSSR & NSSR_BSY) {}
    }
    NSCR &= ~NSCR_PG;
    return 0;
}
```

- [ ] **Step 4: Implement `flash_erase_sector`** — `NSCR = SER | (bank?BKSEL:0) | (sector<<6) | STRT;` then `while (NSSR & NSSR_BSY){}`.

- [ ] **Step 5: Implement `flash_set_swap_and_launch`** — OPTKEYR unlock (`0x08192A3B`, `0x4C5D6E7F`), `OPTSR_PRG |= SWAP_BANK;`, `OPTCR |= OBL_LAUNCH;` (on real HW this resets the MCU; in sim Plan A swaps+resets).

- [ ] **Step 6: Build** `make -C examples/h563_uds_bootloader/bootloader` (link `flash_h5.c`). Expected: PASS.

- [ ] **Step 7: Commit** `feat(example): H5 flash driver (unlock/program/erase/swap)`.

---

### Task 3: Wire the udslib server — session, security (AES-CMAC), flash callbacks

**Files:**
- Modify: `examples/h563_uds_bootloader/bootloader/main.c`
- Create: `examples/h563_uds_bootloader/bootloader/{sec_cmac.c,sec_cmac.h}` (mbedTLS seed/key, mirror `examples/security_access_mbedtls`)
- Modify: `Makefile` (add mbedTLS sources/include per the security_access_mbedtls example's Makefile)

**Interfaces:**
- Consumes: `flash_*` (Task 2); udslib `uds_config_t`, the flash-service download/transfer callbacks, `uds_tp_isotp_*`. Confirm the exact callback field names by reading `examples/pro_flash_tool/main.c` (it wires the same flash-service callbacks) and `include/uds/uds_core.h`.
- Produces: a configured `uds_ctx_t` with:
  - `restrict_sessions = true`; programming session required.
  - 0x27 seed/key via `sec_cmac_*` (AES-CMAC over the seed with a demo key).
  - RequestDownload handler that validates the target is inside the **inactive** bank's app region, erases it (loop `flash_erase_sector`), and arms transfer.
  - TransferData handler that `flash_program`s sequential chunks into the inactive bank.
  - RoutineControl: `0x0202` verify-CRC (CRC32 over written region vs footer), `0xFF01` activate (`flash_set_swap_and_launch`).

- [ ] **Step 1: Read the reference wiring** — `examples/pro_flash_tool/main.c` (flash-service callback signatures, RequestDownload/TransferData/RoutineControl) and `examples/security_access_mbedtls/main.c` (seed/key callbacks + Makefile mbedTLS flags). Mirror their exact udslib API usage.

- [ ] **Step 2: Implement `sec_cmac.c`** — `sec_seed()` returns a fixed/derived seed; `sec_key()` verifies `key == AES_CMAC(secret, seed)` using mbedTLS. Copy structure from `security_access_mbedtls`.

- [ ] **Step 3: Implement the flash-service callbacks in main.c**, computing the inactive bank from the live `SWAP_BANK` bit (read OPTSR_CUR): if bank 1 active, target = bank 2 app region (`0x08118000`), erase its sectors `[0x18000/0x2000 .. 0x100000/0x2000)` on bank 2.

- [ ] **Step 4: Build** with mbedTLS. Run `UDSLIB_DIR=... make -C examples/h563_uds_bootloader/bootloader`. Expected: links clean.

- [ ] **Step 5: Commit** `feat(example): UDS server wiring — session, AES-CMAC 0x27, flash callbacks`.

---

### Task 4: App validity check + jump-to-app

**Files:**
- Modify: `examples/h563_uds_bootloader/bootloader/main.c`
- Create: `examples/h563_uds_bootloader/bootloader/{app_jump.c,app_jump.h}`

**Interfaces:**
- Produces:
  - `int app_is_valid(uint32_t app_base);` — reads the footer (magic/len/CRC32 at `app_base + len`), recomputes CRC32 over `[app_base, app_base+len)`, checks magic + SP plausibility (`*(uint32_t*)app_base` in RAM range). Returns 1 if valid.
  - `void app_jump(uint32_t app_base);` — set `SCB->VTOR = app_base`, load MSP from `app_base[0]`, branch to reset handler `app_base[1]` (thumb). Never returns.

- [ ] **Step 1: Write a host unit test for `app_is_valid`'s CRC/magic logic** (compile `app_jump.c`'s pure-logic parts host-side, feed a crafted buffer). Run via a tiny `cc` test in the host harness (Task 7) — assert valid/invalid footers.
- [ ] **Step 2: Run it; expect FAIL** (not implemented).
- [ ] **Step 3: Implement `app_is_valid` + `app_jump`** (VTOR/MSP/branch inline asm: `__asm volatile("msr msp, %0; bx %1"::"r"(sp),"r"(pc));`).
- [ ] **Step 4: Run it; expect PASS.**
- [ ] **Step 5: Wire boot flow in main.c** — on reset: if `app_is_valid(active_bank_app)` and the A/B confirm flag is OK → `app_jump`; else stay in the UDS server loop (recovery). Print `BL-JUMP\n` before jumping, `BL-RECOVERY\n` otherwise.
- [ ] **Step 6: Build; Commit** `feat(example): app validity check + jump-to-app with VTOR relocation`.

---

### Task 5: Demo app images (App A / App B) with validity footer

**Files:**
- Create: `examples/h563_uds_bootloader/app/{Makefile,app.ld,startup.c,main.c,mkfooter.py}`

**Interfaces:**
- Produces: `app_a.bin` and `app_b.bin` — tiny apps linked at `0x08018000`, each prints a distinct UART banner (`APP-A\n` / `APP-B\n`) and exposes DID `0xF195` returning `"A"`/`"B"`. `mkfooter.py` appends the magic/len/CRC32 footer to the `.bin`.

- [ ] **Step 1: App linker `app.ld`** — `FLASH (rx): ORIGIN = 0x08018000, LENGTH = 928K`, RAM as before; ISR vector at app base.
- [ ] **Step 2: App `main.c`** — UART banner + spin (Task 6 can extend to UDS DID).
- [ ] **Step 3: `mkfooter.py`** — read `.bin`, compute CRC32, append `magic(LE u32)=0x4F544131, len(u32), crc(u32)`, write `*_signed.bin`.
- [ ] **Step 4: Build both variants** — `make APP=A` / `make APP=B` producing `app_a_signed.bin` / `app_b_signed.bin`. Expected: two signed binaries.
- [ ] **Step 5: Commit** `feat(example): demo App A/B images with validity footer`.

---

### Task 6: labwired sim harness — full OTA smoke (depends on Plan A)

**Files:**
- Create (in labwired-core repo): `examples/h563-uds-bootloader/{system.yaml,ota-smoke.yaml,reboot-smoke.yaml,README.md}`
- Modify: `examples/h563_uds_bootloader/bootloader/main.c` (add a `#ifdef SIM_TESTER` virtual-tester that injects the OTA sequence over FDCAN loopback, mirroring h563-uds-ecu's self-injection)

**Interfaces:**
- Consumes: Plan A's erase + swap model; the harness `memory_value` + `uart_contains` assertions; the build pattern (`system.yaml` → `chip: ../../configs/chips/stm32h563.yaml`).
- Produces: two CI smoke tests proving the loop.

- [ ] **Step 1: Add the `SIM_TESTER` virtual tester to the firmware** — under `#ifdef SIM_TESTER`, after `uds_init`, feed App-B (a small embedded image blob) through `0x10 02 → 0x27 → 0x31 erase → 0x34 → 0x36×N → 0x37 → 0x31 verify → 0x31 activate`, pumping `uds_process` + loopback RX between frames (reuse h563-uds-ecu's `pump_one_tester_request` loop). Print `OTA-WRITE-OK`, `OTA-VERIFY-OK`, `OTA-ACTIVATE` at each milestone.

- [ ] **Step 2: Build the SIM_TESTER ELF** — add a Makefile target `make SIM_TESTER=1`. Expected: `build/h563_uds_bootloader_simtester.elf`.

- [ ] **Step 3: Write `system.yaml`** — `chip: "../../configs/chips/stm32h563.yaml"`, empty external_devices/board_io (mirror h563-uds-ecu).

- [ ] **Step 4: Write `ota-smoke.yaml`** — asserts the write+verify+activate path without rebooting:
```yaml
schema_version: "1.0"
inputs:
  system: "./system.yaml"
  firmware: "../../../udslib/examples/h563_uds_bootloader/bootloader/build/h563_uds_bootloader_simtester.elf"
limits: { max_steps: 5000000 }
assertions:
  - uart_contains: "OTA-WRITE-OK"
  - uart_contains: "OTA-VERIFY-OK"
  - uart_contains: "OTA-ACTIVATE"
  # App-B's signature landed in the inactive bank (bank 2 app region)
  - memory_value: { address: 0x08118000, expected_value: 0x20000000, mask: 0xFFFFFFFF, size: 32 }
```
(Use the real expected first word of App-B's image. Adjust the firmware path to however the two repos are checked out in CI; prefer a `UDSLIB_BL_ELF` make/env indirection over a brittle relative path.)

- [ ] **Step 5: Run the smoke** (after Plan A merged):
```bash
cargo run -q -p labwired-cli -- test \
  --script examples/h563-uds-bootloader/ota-smoke.yaml \
  --output-dir out/h563-uds-bootloader --no-uart-stdout
```
Expected: all assertions pass.

- [ ] **Step 6: Write `reboot-smoke.yaml`** — a build where the bootloader actually `OBL_LAUNCH`es; after the model swaps+resets, the bootloader re-runs, finds valid App-B in the now-active bank, jumps, and App-B prints `APP-B`. Assert `uart_contains: "BL-JUMP"` and `uart_contains: "APP-B"`. (If reboot re-runs the SIM_TESTER and loops, gate the tester on an un-erased "already updated" marker so it runs once.)

- [ ] **Step 7: Run reboot smoke; expect PASS.**

- [ ] **Step 8: Commit (labwired-core repo)** `feat(example): H563 UDS OTA bootloader sim harness + smoke tests`. Commit the firmware `SIM_TESTER` changes in udslib separately.

---

### Task 7: SocketCAN host flash tool + vcan CI test

**Files:**
- Create: `examples/h563_uds_bootloader/host/{flash_tool.c,CMakeLists.txt,README.md}`
- Create: `examples/h563_uds_bootloader/host/test_vcan.sh`

**Interfaces:**
- Consumes: udslib client API (mirror `examples/pro_flash_tool/main.c`'s client sequence) + Linux SocketCAN (`AF_CAN`, `can_isotp` or raw CAN-FD frames).
- Produces: `flash_tool <can-iface> <app_signed.bin>` driving `0x10 02 → 0x27 → 0x31 erase → 0x34 → 0x36×N → 0x37 → 0x31 verify → 0x31 activate` over CAN-FD; computes/sends CRC; exits 0 on full success.

- [ ] **Step 1: Implement `flash_tool.c`** — open SocketCAN FD socket on the given iface (tester `0x7E0` / resp `0x7E8`), run the reprogramming sequence reusing pro_flash_tool's client logic; read the `.bin` (already footered by `mkfooter.py`).

- [ ] **Step 2: Write `test_vcan.sh`** — `sudo modprobe vcan; sudo ip link add dev vcan0 type vcan; sudo ip link set up vcan0`, start a host-built bootloader-ECU stub (the udslib server with a RAM-backed "flash" — essentially `pro_flash_tool`'s ECU half) bound to vcan0, run `flash_tool vcan0 app_b_signed.bin`, assert exit 0. Skip gracefully if vcan/perms unavailable (print SKIP).

- [ ] **Step 3: Run** `./examples/h563_uds_bootloader/host/test_vcan.sh`. Expected: PASS (or SKIP if no vcan).

- [ ] **Step 4: Commit** `feat(example): SocketCAN host flash tool + vcan smoke`.

---

### Task 8: Real-silicon cross-check (manual, documented) + CI + README + PR

**Files:**
- Modify: `examples/h563_uds_bootloader/README.md`, udslib CI workflow (`.github/workflows/*.yml`), `examples/README.md` (link the new example)

**Interfaces:**
- Produces: documented SWD verification steps on the connected H563; CI builds the firmware + runs the vcan host test; README with the full OTA flow + PCAN usage notes.

- [ ] **Step 1: Real-HW flash-driver cross-check (manual)** — using the connected board, document a probe-rs/openocd session: erase a sector, program a known pattern, read it back, set `SWAP_BANK` + reset, confirm boot bank changed. Record the exact commands + observed values in the README (this validates Plan A's model against silicon). Probe id: `0483:374e:002100174741500220383733`.

- [ ] **Step 2: Add a CI job** building the bootloader + apps (clang/rust-lld toolchain in the existing CI image — confirm it has clang + rust-lld, matching how h563-uds-ecu is built in CI; if h563-uds-ecu is not yet in CI, gate the new job the same way the other example builds are gated) and running `test_vcan.sh` where vcan is available.

- [ ] **Step 3: Write the README** — architecture (mirrored BL + SWAP_BANK), memory map, the OTA sequence, how to build (firmware/app/host), how to run the sim smoke, and how a user with a PCAN/SocketCAN interface flashes a real board (`flash_tool can0 app_signed.bin`). Note the no-PCAN-locally limitation honestly.

- [ ] **Step 4: Link from `examples/README.md`.**

- [ ] **Step 5: Open the PR**

```bash
git push -u origin feat/h563-uds-ota-bootloader
gh pr create --base develop --title "feat: STM32H563 UDS service-based OTA bootloader example" \
  --body "Implements a CAN-FD UDS OTA bootloader example for the STM32H563 (dual-bank A/B + SWAP_BANK, AES-CMAC 0x27, full 0x10/0x27/0x31/0x34/0x36/0x37 reprogramming). Validated in the labwired H563 sim (requires the Plan A flash-model PR) and cross-checked on real silicon over SWD; ships a SocketCAN host flash tool. Closes #64."
```
(Plan A's labwired-core PR must merge first so the sim smoke is green.)

---

## Self-Review

- **Spec coverage:** mirrored BL + memory map (Task 1), flash driver program/erase/swap (Task 2), session+AES-CMAC+flash callbacks (Task 3), validity+jump+rollback-confirm (Task 4), demo apps (Task 5), full-loop sim incl. swap+jump (Task 6), SocketCAN host tool + vcan (Task 7), real-HW cross-check + CI + README + PR (Task 8). ✔
- **Placeholder scan:** boilerplate is "copy from h563-uds-ecu / pro_flash_tool / security_access_mbedtls" with the exact files named — not vague; the one runtime detail flagged (CI firmware-path indirection, App-B first word) is a concrete value to fill from the built artifact, called out explicitly. ✔
- **Type consistency:** `flash_*` signatures used identically across Tasks 2/3/6; footer magic `0x4F544131`/len/CRC32 consistent Tasks 4/5/7; tester/resp IDs `0x7E0`/`0x7E8` throughout; register constants single-sourced with Plan A Task 0. ✔
- **Dependency:** Tasks 6 (sim) and the green PR depend on Plan A merged. Tasks 1-5, 7 do not and can proceed in parallel with Plan A.
- **Risk:** exact udslib flash-service callback field names are taken from `pro_flash_tool` at implementation time (Task 3 Step 1) rather than guessed here.
