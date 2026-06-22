# Plan A — labwired-core STM32H5 FLASH erase + bank-swap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend labwired-core's STM32H5 FLASH peripheral model with sector erase and hardware bank-swap (`SWAP_BANK` + `OBL_LAUNCH`) so a UDS bootloader can erase the inactive bank, swap, reset, and boot the new bank entirely in simulation.

**Architecture:** Flash *programming* already works (the bus write path routes CPU stores at `0x08000000` into the writable `flash: LinearMemory`). This plan adds the two register-triggered operations that are not yet modeled: (1) NSCR sector erase → fill the sector with `0xFF`; (2) `OPTSR_PRG.SWAP_BANK` + `OPTCR.OBL_LAUNCH` → swap the two 1 MB halves of the flash buffer and reset the CPU. Both follow the existing ESP32 `drain_rtc_cntl_reset_request` pattern: the FLASH peripheral records a pending op behind interior mutability, and `Machine::step()` drains it between instructions and applies it to the bus + CPU.

**Tech Stack:** Rust (labwired-core workspace), `cargo test`, clang/rust-lld for the integration-test firmware.

**Repo:** `/home/andrii/projects/labwired-core-h563` (a labwired-core checkout). Confirm the correct integration branch before starting — labwired-core integrates on `origin/main` (no `develop`); branch from latest `origin/main`.

## Global Constraints

- Touching the H5 branch must NOT change L4 or F1 reset values or behaviour (the file is explicitly isolated per-family — see `flash.rs:8-12`).
- H563 flash: base `0x08000000`, total `2 MB` (2 × 1 MB banks), `8 KB` (0x2000) sectors, 128 sectors/bank.
- Erase granularity = one 8 KB sector; erased state = all `0xFF`.
- Bank swap is hardware `SWAP_BANK`: after swap + reset, bank 2's content is what lives at `0x08000000`.
- Register bitfield values are authoritative from **RM0481 §7 (FLASH)**; Task 0 pins and silicon-verifies them.
- Run `cargo fmt` and `cargo clippy -p labwired-core --all-targets` clean before each commit; CI may gate on clippy.
- Unit tests live in a `#[cfg(test)]` module in the modified `src/` file; integration tests in `crates/core/tests/`.

---

### Task 0: Pin & silicon-verify the H5 FLASH register bitfields

**Files:**
- Create: `crates/core/src/peripherals/flash_h5_regs.rs` (constants module)
- Modify: `crates/core/src/peripherals/flash.rs:1` (add `mod`/`use`)

**Interfaces:**
- Produces: `pub const` register offsets + bitfield masks used by all later tasks:
  `NSCR_OFF=0x28`, `NSSR_OFF=0x20`, `OPTSR_PRG_OFF=0x54`, `OPTSR_CUR_OFF=0x50`, `OPTCR_OFF=0x1C`,
  `NSCR_LOCK=1<<0`, `NSCR_PG=1<<1`, `NSCR_SER=1<<2`, `NSCR_BER=1<<3`, `NSCR_STRT=1<<5`,
  `NSCR_SNB_SHIFT=6`, `NSCR_SNB_MASK=0x7F<<6`, `NSCR_BKSEL=1<<31`,
  `OPTSR_SWAP_BANK=1<<31`, `OPTCR_OBL_LAUNCH=1<<27`, `NSSR_BSY=1<<0`,
  `BANK_SIZE=0x100000`, `SECTOR_SIZE=0x2000`, `FLASH_BASE=0x08000000`.

These candidate values are from RM0481 §7; **verify each against the manual and the connected board before relying on them.**

- [ ] **Step 1: Read the real FLASH registers off the connected H563 over SWD**

Run:
```bash
probe-rs read --chip STM32H563ZITx --probe 0483:374e:002100174741500220383733 \
  b32 0x40022000 24
```
Expected: a 24-word dump of the FLASH register block. Record `OPTSR_CUR` (offset 0x50) and `OPTCR` (0x1C) reset values; they must match the model's reset constants (`OPTSR_CUR` already pinned to `0x2D30_EDF8` in `flash.rs:126`). Note the live `SWAP_BANK` bit (OPTSR_CUR bit 31) — it tells you which bank is currently active.

- [ ] **Step 2: Cross-check bitfield positions against RM0481 §7**

Confirm against the reference manual: `FLASH_NSCR` (LOCK/PG/SER/BER/STRT/SNB/BKSEL bit positions), `FLASH_OPTSR_PRG` SWAP_BANK bit, `FLASH_OPTCR` OBL_LAUNCH bit. Correct any constant below that disagrees with the RM.

- [ ] **Step 3: Write the constants module**

```rust
// crates/core/src/peripherals/flash_h5_regs.rs
//! STM32H5 FLASH register offsets + bitfields (RM0481 §7).
//! Cross-checked against NUCLEO-H563ZI silicon via SWD (2026-06-20).

pub const NSCR_OFF: u64 = 0x28;
pub const NSSR_OFF: u64 = 0x20;
pub const OPTCR_OFF: u64 = 0x1C;
pub const OPTSR_CUR_OFF: u64 = 0x50;
pub const OPTSR_PRG_OFF: u64 = 0x54;

pub const NSCR_LOCK: u32 = 1 << 0;
pub const NSCR_PG: u32 = 1 << 1;
pub const NSCR_SER: u32 = 1 << 2;
pub const NSCR_BER: u32 = 1 << 3;
pub const NSCR_STRT: u32 = 1 << 5;
pub const NSCR_SNB_SHIFT: u32 = 6;
pub const NSCR_SNB_MASK: u32 = 0x7F << NSCR_SNB_SHIFT;
pub const NSCR_BKSEL: u32 = 1 << 31;

pub const NSSR_BSY: u32 = 1 << 0;
pub const OPTSR_SWAP_BANK: u32 = 1 << 31;
pub const OPTCR_OBL_LAUNCH: u32 = 1 << 27;

pub const FLASH_BASE: u64 = 0x0800_0000;
pub const BANK_SIZE: u64 = 0x10_0000; // 1 MB
pub const SECTOR_SIZE: u64 = 0x2000; // 8 KB
```

- [ ] **Step 4: Wire the module into flash.rs**

Add near the top of `crates/core/src/peripherals/flash.rs` (after the existing `use`):
```rust
#[path = "flash_h5_regs.rs"]
mod h5;
```

- [ ] **Step 5: Build to confirm it compiles**

Run: `cargo build -p labwired-core`
Expected: compiles (the `mod h5` is currently unused — allow with `#[allow(dead_code)]` on the module if clippy warns; it is consumed in Task 2).

- [ ] **Step 6: Commit**

```bash
git add crates/core/src/peripherals/flash_h5_regs.rs crates/core/src/peripherals/flash.rs
git commit -m "feat(flash): pin silicon-verified STM32H5 FLASH register bitfields"
```

---

### Task 1: Add `fill` + `swap_banks` to `LinearMemory`

**Files:**
- Modify: `crates/core/src/memory/mod.rs` (add methods to `LinearMemory`, ~after line 132)
- Test: same file, `#[cfg(test)]` module

**Interfaces:**
- Consumes: `LinearMemory { data: Vec<u8>, base_addr: u64 }` (existing).
- Produces:
  - `pub fn fill(&mut self, offset: u64, len: u64, byte: u8) -> bool` — fills `[offset, offset+len)` (offset relative to `base_addr`); returns false if out of range.
  - `pub fn swap_banks(&mut self, bank_size: u64) -> bool` — swaps the two `bank_size` halves in place; returns false if `data.len() != 2*bank_size`.

- [ ] **Step 1: Write the failing tests**

```rust
#[cfg(test)]
mod bank_tests {
    use super::LinearMemory;

    #[test]
    fn fill_sets_range_relative_to_base() {
        let mut m = LinearMemory::new(0x4000, 0x0800_0000);
        assert!(m.fill(0x2000, 0x2000, 0xFF));
        assert_eq!(m.read_u8(0x0800_1FFF).unwrap(), 0x00);
        assert_eq!(m.read_u8(0x0800_2000).unwrap(), 0xFF);
        assert_eq!(m.read_u8(0x0800_3FFF).unwrap(), 0xFF);
    }

    #[test]
    fn fill_rejects_out_of_range() {
        let mut m = LinearMemory::new(0x2000, 0x0800_0000);
        assert!(!m.fill(0x1000, 0x2000, 0xFF));
    }

    #[test]
    fn swap_banks_exchanges_halves() {
        let mut m = LinearMemory::new(0x4, 0x0800_0000); // tiny 2-byte banks
        m.write_u8(0x0800_0000, 0xA1);
        m.write_u8(0x0800_0001, 0xA2);
        m.write_u8(0x0800_0002, 0xB1);
        m.write_u8(0x0800_0003, 0xB2);
        assert!(m.swap_banks(0x2));
        assert_eq!(m.read_u8(0x0800_0000).unwrap(), 0xB1);
        assert_eq!(m.read_u8(0x0800_0001).unwrap(), 0xB2);
        assert_eq!(m.read_u8(0x0800_0002).unwrap(), 0xA1);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cargo test -p labwired-core --lib memory::bank_tests`
Expected: FAIL — `no method named fill`/`swap_banks`.

- [ ] **Step 3: Implement the methods**

Add to `impl LinearMemory`:
```rust
/// Fill [offset, offset+len) (offset relative to base_addr) with `byte`.
/// Returns false if the range is outside the backing buffer.
pub fn fill(&mut self, offset: u64, len: u64, byte: u8) -> bool {
    let start = offset as usize;
    let end = match offset.checked_add(len) {
        Some(e) => e as usize,
        None => return false,
    };
    if end > self.data.len() {
        return false;
    }
    self.data[start..end].iter_mut().for_each(|b| *b = byte);
    true
}

/// Swap the two `bank_size` halves of the buffer in place (models H5
/// hardware SWAP_BANK). Returns false unless the buffer is exactly two banks.
pub fn swap_banks(&mut self, bank_size: u64) -> bool {
    let bank = bank_size as usize;
    if self.data.len() != bank * 2 {
        return false;
    }
    let (lo, hi) = self.data.split_at_mut(bank);
    lo.swap_with_slice(hi);
    true
}
```
(Note: `fill` takes an offset relative to `base_addr`, matching how `read_u8`/`write_u8` translate — verify the existing translation convention in this file and keep `fill` consistent with it.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `cargo test -p labwired-core --lib memory::bank_tests`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
cargo fmt && cargo clippy -p labwired-core --all-targets
git add crates/core/src/memory/mod.rs
git commit -m "feat(memory): add LinearMemory::fill and swap_banks"
```

---

### Task 2: Model NSCR sector erase + OPTSR/OBL_LAUNCH in the FLASH peripheral

**Files:**
- Modify: `crates/core/src/peripherals/flash.rs` (H5 read/write branches + struct + `as_any`)
- Test: same file, `#[cfg(test)]` module

**Interfaces:**
- Consumes: `h5::*` constants (Task 0); the existing `Flash` struct, `KeyUnlockState`, the `Peripheral` trait.
- Produces:
  - `pub enum FlashOp { EraseSector { bank: u8, sector: u32 }, SwapAndReset }`
  - `pub fn drain_pending_op(&self) -> Option<FlashOp>` on `Flash` (clears the pending op via interior mutability).
  - `Flash` gains: `optsr_prg: u32`, `pending_op: std::cell::Cell<Option<FlashOp>>` (use `Cell` since `FlashOp` is `Copy`).
  - `Flash` implements `as_any(&self) -> Option<&dyn std::any::Any>` (matching how the trait/`rtc_cntl` exposes it — confirm the exact `Peripheral::as_any` signature in `lib.rs:352` and mirror it).

- [ ] **Step 1: Make `FlashOp` `Copy` and add fields**

In `flash.rs`, add above `struct Flash`:
```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum FlashOp {
    EraseSector { bank: u8, sector: u32 },
    SwapAndReset,
}
```
Add to `struct Flash`:
```rust
    optsr_prg: u32,
    #[serde(skip)]
    pending_op: std::cell::Cell<Option<FlashOp>>,
```
Initialise both in `new_with_layout` (and `_new_l4_legacy`): `optsr_prg: optr_reset` (mirror OPTSR_CUR), `pending_op: std::cell::Cell::new(None)`.

- [ ] **Step 2: Write the failing tests**

```rust
#[cfg(test)]
mod h5_erase_swap_tests {
    use super::{Flash, FlashOp, FlashRegisterLayout};
    use crate::Peripheral;
    use super::h5;

    fn unlock(f: &mut Flash) {
        f.write_u32(0x08, 0x4567_0123).unwrap(); // NSKEYR
        f.write_u32(0x08, 0xCDEF_89AB).unwrap();
    }

    #[test]
    fn ser_strt_records_erase_of_selected_sector() {
        let mut f = Flash::new_with_layout(FlashRegisterLayout::Stm32H5);
        unlock(&mut f);
        // SER + SNB=7 (bank-1) + STRT
        let nscr = h5::NSCR_SER | (7 << h5::NSCR_SNB_SHIFT) | h5::NSCR_STRT;
        f.write_u32(h5::NSCR_OFF, nscr).unwrap();
        assert_eq!(
            f.drain_pending_op(),
            Some(FlashOp::EraseSector { bank: 0, sector: 7 })
        );
        // op drains exactly once
        assert_eq!(f.drain_pending_op(), None);
    }

    #[test]
    fn ser_with_bksel_targets_bank2() {
        let mut f = Flash::new_with_layout(FlashRegisterLayout::Stm32H5);
        unlock(&mut f);
        let nscr = h5::NSCR_SER | h5::NSCR_BKSEL | (3 << h5::NSCR_SNB_SHIFT) | h5::NSCR_STRT;
        f.write_u32(h5::NSCR_OFF, nscr).unwrap();
        assert_eq!(
            f.drain_pending_op(),
            Some(FlashOp::EraseSector { bank: 1, sector: 3 })
        );
    }

    #[test]
    fn swap_bank_plus_obl_launch_records_swap_and_reset() {
        let mut f = Flash::new_with_layout(FlashRegisterLayout::Stm32H5);
        unlock(&mut f);
        f.write_u32(h5::OPTSR_PRG_OFF, h5::OPTSR_SWAP_BANK).unwrap();
        f.write_u32(h5::OPTCR_OFF, h5::OPTCR_OBL_LAUNCH).unwrap();
        assert_eq!(f.drain_pending_op(), Some(FlashOp::SwapAndReset));
    }

    #[test]
    fn erase_ignored_while_locked() {
        let mut f = Flash::new_with_layout(FlashRegisterLayout::Stm32H5);
        let nscr = h5::NSCR_SER | (7 << h5::NSCR_SNB_SHIFT) | h5::NSCR_STRT;
        f.write_u32(h5::NSCR_OFF, nscr).unwrap();
        assert_eq!(f.drain_pending_op(), None);
    }
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cargo test -p labwired-core --lib h5_erase_swap_tests`
Expected: FAIL — `drain_pending_op` not found / op never recorded.

- [ ] **Step 4: Implement the H5 write-branch logic + drain**

In the H5 branch of `write_reg` (currently only handles `0x00` ACR), add handling. Replace the H5 block with:
```rust
if matches!(self.layout, FlashRegisterLayout::Stm32H5) {
    let unlocked = matches!(self.key_state, KeyUnlockState::Unlocked);
    match offset {
        0x00 => self.acr = value & 0x0000_013F,
        h5::NSCR_OFF => {
            self.cr = value;
            if unlocked && (value & h5::NSCR_SER) != 0 && (value & h5::NSCR_STRT) != 0 {
                let sector = (value & h5::NSCR_SNB_MASK) >> h5::NSCR_SNB_SHIFT;
                let bank = if value & h5::NSCR_BKSEL != 0 { 1 } else { 0 };
                self.pending_op.set(Some(FlashOp::EraseSector { bank, sector }));
            }
        }
        h5::OPTSR_PRG_OFF => self.optsr_prg = value,
        h5::OPTCR_OFF => {
            if unlocked
                && (value & h5::OPTCR_OBL_LAUNCH) != 0
                && (self.optsr_prg & h5::OPTSR_SWAP_BANK) != 0
            {
                self.pending_op.set(Some(FlashOp::SwapAndReset));
            }
        }
        _ => {}
    }
    return;
}
```
Add `drain_pending_op` and `as_any` in `impl Flash` / the trait impl:
```rust
impl Flash {
    pub fn drain_pending_op(&self) -> Option<FlashOp> {
        self.pending_op.take()
    }
}
```
In `impl crate::Peripheral for Flash`, add (match the trait's exact signature from `lib.rs:352`):
```rust
    fn as_any(&self) -> Option<&dyn std::any::Any> {
        Some(self)
    }
```
Also extend the H5 `read_reg` branch so `OPTSR_PRG` reads back: add `h5::OPTSR_PRG_OFF => self.optsr_prg,`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cargo test -p labwired-core --lib h5_erase_swap_tests`
Expected: PASS (4 tests). Also run `cargo test -p labwired-core --lib flash` to confirm L4/F1 paths are untouched.

- [ ] **Step 6: Commit**

```bash
cargo fmt && cargo clippy -p labwired-core --all-targets
git add crates/core/src/peripherals/flash.rs
git commit -m "feat(flash): model H5 sector erase + SWAP_BANK/OBL_LAUNCH pending ops"
```

---

### Task 3: Drain flash ops in `Machine::step` (apply erase + swap+reset)

**Files:**
- Modify: `crates/core/src/lib.rs` — add `flash_index` field, set it at construction, drain in `step()`.
- Test: `crates/core/tests/flash_h5_ops.rs` (new integration test)

**Interfaces:**
- Consumes: `Flash::drain_pending_op` + `FlashOp` (Task 2), `LinearMemory::fill`/`swap_banks` (Task 1), `Machine::reset` (`lib.rs:840`), the `flash: LinearMemory` bus field, `h5::{BANK_SIZE, SECTOR_SIZE}`.
- Produces: applied side effects — erase fills the sector with `0xFF` in `bus.flash`; swap exchanges the banks and resets the CPU (re-reads SP/PC from the new `0x08000000`).

- [ ] **Step 1: Add `flash_index` to `Machine` and set it where `rtc_cntl_index` is set**

Find the field + assignment: `grep -n "rtc_cntl_index" crates/core/src/lib.rs`. Add a sibling `flash_index: Option<usize>` to the `Machine` struct, and where peripherals are indexed at construction, set it to the index of the peripheral whose `as_any()` downcasts to `peripherals::flash::Flash` with H5 layout (mirror the `rtc_cntl_index` discovery loop). Default `None`.

- [ ] **Step 2: Write the failing integration test**

```rust
// crates/core/tests/flash_h5_ops.rs
// Drives the FLASH model directly through the bus + Machine to prove erase
// fills 0xFF and SWAP_BANK+OBL_LAUNCH swaps banks and resets the CPU.
use labwired_core::peripherals::flash::h5; // re-export needed (see Step 4)

// Helper builds a Machine from the H563 chip config with a 2 MB flash whose
// two banks carry distinguishable vector tables. See repo test helpers for the
// canonical Machine builder (mirror an existing crates/core/tests/*.rs setup).

#[test]
fn erase_fills_sector_with_ff() {
    let mut m = test_support::h563_machine();
    // program a byte, then erase its sector via NSCR
    m.bus.flash.write_u8(0x0800_4000, 0x00);
    m.bus.write_u32(0x4002_2008, 0x4567_0123).unwrap(); // NSKEYR
    m.bus.write_u32(0x4002_2008, 0xCDEF_89AB).unwrap();
    let sector = (0x4000u32 / 0x2000) << h5::NSCR_SNB_SHIFT; // sector 2
    m.bus
        .write_u32(0x4002_2000 + h5::NSCR_OFF, h5::NSCR_SER | sector | h5::NSCR_STRT)
        .unwrap();
    m.step().unwrap();
    assert_eq!(m.bus.flash.read_u8(0x0800_4000).unwrap(), 0xFF);
}

#[test]
fn swap_bank_reboots_into_bank2() {
    let mut m = test_support::h563_machine();
    // bank1 reset vector → PC 0x08000101; bank2 → PC 0x08100201
    m.bus.flash.write_u32(0x0800_0000, 0x2000_0000); // SP
    m.bus.flash.write_u32(0x0800_0004, 0x0800_0101); // bank1 PC
    m.bus.flash.write_u32(0x0810_0000, 0x2000_0000); // SP
    m.bus.flash.write_u32(0x0810_0004, 0x0810_0201); // bank2 PC (pre-swap addr)
    m.bus.write_u32(0x4002_2008, 0x4567_0123).unwrap();
    m.bus.write_u32(0x4002_2008, 0xCDEF_89AB).unwrap();
    m.bus.write_u32(0x4002_2000 + h5::OPTSR_PRG_OFF, h5::OPTSR_SWAP_BANK).unwrap();
    m.bus.write_u32(0x4002_2000 + h5::OPTCR_OFF, h5::OPTCR_OBL_LAUNCH).unwrap();
    m.step().unwrap();
    // after swap, what was bank2 now lives at 0x08000000, and CPU reset read it
    assert_eq!(m.bus.flash.read_u32(0x0800_0004).unwrap(), 0x0810_0201);
    assert_eq!(m.cpu_pc(), 0x0810_0200); // thumb bit cleared
}
```
(If the repo lacks a `test_support::h563_machine()` / `cpu_pc()` helper, add a small one in the test file mirroring the construction used by `crates/core/tests/brom_boot_smoke.rs`; reuse, don't reinvent, the existing Machine builder.)

- [ ] **Step 3: Run the test to verify it fails**

Run: `cargo test -p labwired-core --test flash_h5_ops`
Expected: FAIL — ops not applied (erase byte still 0x00; no swap).

- [ ] **Step 4: Implement the drain in `step()` + re-export `h5`**

In `Machine::step()`, immediately after the existing `drain_rtc_cntl_reset_request` block (`lib.rs:911-915`), add:
```rust
if let Some(op) = self.drain_flash_op() {
    use crate::peripherals::flash::{h5, FlashOp};
    match op {
        FlashOp::EraseSector { bank, sector } => {
            let off = bank as u64 * h5::BANK_SIZE + sector as u64 * h5::SECTOR_SIZE;
            self.bus.flash.fill(off, h5::SECTOR_SIZE, 0xFF);
        }
        FlashOp::SwapAndReset => {
            self.bus.flash.swap_banks(h5::BANK_SIZE);
            self.reset()?;
        }
    }
}
```
Add the helper near `drain_rtc_cntl_reset_request` (`lib.rs:1059`):
```rust
fn drain_flash_op(&self) -> Option<crate::peripherals::flash::FlashOp> {
    let idx = self.flash_index?;
    let p = self.bus.peripherals.get(idx)?;
    p.dev
        .as_any()
        .and_then(|a| a.downcast_ref::<crate::peripherals::flash::Flash>())
        .and_then(|f| f.drain_pending_op())
}
```
Make `h5` reachable from tests: in `crates/core/src/peripherals/flash.rs` change `mod h5;` to `pub mod h5;`, and confirm `pub use` of `Flash`/`FlashOp` from the crate root (`peripherals::flash::*`).

- [ ] **Step 5: Run the test to verify it passes**

Run: `cargo test -p labwired-core --test flash_h5_ops`
Expected: PASS (2 tests).

- [ ] **Step 6: Run the full default-members test suite (no regressions)**

Run: `cargo test`
Expected: PASS — existing suites green, L4/F1 flash behaviour unchanged.

- [ ] **Step 7: Commit**

```bash
cargo fmt && cargo clippy -p labwired-core --all-targets
git add crates/core/src/lib.rs crates/core/src/peripherals/flash.rs
git commit -m "feat(flash): apply H5 erase + bank-swap/reset in Machine::step"
```

---

### Task 4: (Optional) Hook AIRCR.SYSRESETREQ for UDS ECUReset (0x11)

**Files:**
- Modify: `crates/core/src/peripherals/scb.rs:150` (AIRCR write), `crates/core/src/lib.rs` (drain + reset)
- Test: `crates/core/tests/flash_h5_ops.rs` (add a case)

**Interfaces:**
- Consumes: `Machine::reset`, the SCB peripheral index pattern.
- Produces: a plain CPU reset (no bank swap) when firmware writes `AIRCR` with `SYSRESETREQ` (bit 2) and the `VECTKEY` (0x05FA) in the top half.

Only implement this if Plan B's bootloader uses `0x11` ECUReset to reboot *instead of* relying on `OBL_LAUNCH` for activation. If activation is solely via `OBL_LAUNCH` (Task 3 covers it), skip this task and note it in the PR.

- [ ] **Step 1: Write the failing test** — write `0x05FA_0004` to AIRCR (`SCB_BASE+0x0C`), `m.step()`, assert CPU PC reloaded from the vector table.
- [ ] **Step 2: Run it; expect FAIL.**
- [ ] **Step 3:** In `scb.rs` AIRCR write, when `(value >> 16) == 0x05FA && value & (1<<2) != 0`, set an interior-mutability `reset_requested` flag; add `drain_reset_request(&self) -> bool`. In `lib.rs::step`, add `if self.drain_scb_reset_request() { self.reset()?; }` using the same `as_any` downcast pattern.
- [ ] **Step 4: Run it; expect PASS.**
- [ ] **Step 5: Commit** `feat(scb): honor AIRCR.SYSRESETREQ as a system reset`.

---

### Task 5: Update the H563 chip yaml comment (program/erase now modeled)

**Files:**
- Modify: `configs/chips/stm32h563.yaml:289-296` and `crates/core/src/peripherals/flash.rs:50-56` (doc comments).

- [ ] **Step 1:** Update the yaml comment from "program/erase is not modeled" to note erase + bank-swap are now modeled; programming is via direct flash writes.
- [ ] **Step 2:** Update the `Stm32H5` doc comment in `flash.rs` likewise.
- [ ] **Step 3: Build + test** `cargo test -p labwired-core --lib flash`. Expected: PASS.
- [ ] **Step 4: Commit** `docs(flash): note H5 erase + bank-swap now modeled`.

---

## Self-Review

- **Spec coverage:** erase (Task 2+3), bank-swap+reset (Task 2+3), program (pre-existing, noted Task 5), reset for 0x11 (Task 4, optional), unit + integration tests (each task), memory-content verification available to Plan B via the harness `memory_value` assertion. ✔
- **Type consistency:** `FlashOp` (Copy) used identically in Task 2/3; `drain_pending_op`/`drain_flash_op` names consistent; `h5::*` constants single-sourced in Task 0. ✔
- **Placeholder scan:** the only deferred values are the register bitfields, resolved by Task 0's RM + silicon verification (a real step, not a TBD). ✔
- **Risk:** the `as_any` signature and the `rtc_cntl_index` discovery loop are mirrored from existing code — confirm exact signatures at implementation time (`lib.rs:352`, `lib.rs:1059`). The test-support Machine builder must be reused from an existing `crates/core/tests/*.rs`.
