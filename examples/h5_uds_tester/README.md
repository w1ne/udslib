# Dual-H5 all-services UDS gate

A real udslib **tester** firmware on one virtual STM32H5 drives a request for every one
of udslib's **27 ISO-14229 services** to a real udslib **ECU** firmware on a second
virtual H5, over a **virtual FDCAN bus**, verifies each response, and records a per-service
pass bit. The gate asserts all 27 bits — headless, with **no CAN hardware**.

This is a genuine cross-node, all-services oracle: a service's bit is set only if its
request crossed the bus *and* the ECU's real response came back and matched.

## Layout

| File | Role |
|------|------|
| `firmware/` | tester firmware — udslib **client**, FDCAN1, TX `0x7E0` / RX `0x7E8` |
| `../h5_uds_ecu_full/firmware/` | ECU firmware — udslib **server**, all 27 services, RX `0x7E0` / TX `0x7E8` |
| `system.yaml` | single STM32H563 node system manifest |
| `twonode-env.yaml` | both nodes wired by a `can_bus` interconnect on `fdcan1` |
| `allservices-gate.yaml` | env-test script: asserts the tester's results bitmap |

## Run it

```sh
# 1. Build both firmwares (host-side udslib sources compiled for STM32H5):
UDSLIB_DIR=$PWD make -C examples/h5_uds_ecu_full/firmware
UDSLIB_DIR=$PWD make -C examples/h5_uds_tester/firmware

# 2. Run the gate headless with LabWired v0.19.2 or newer
#    (exit 0 == all 27 services passed):
labwired test --script examples/h5_uds_tester/allservices-gate.yaml
echo "exit=$?"
```

The nightly workflow does the firmware build and then invokes the public,
release-backed `labwired-test` action pinned to LabWired v0.19.2. It supplies
only this YAML contract to the runner; the action downloads the released CLI,
writes JUnit plus `summary.md` and `report.html`, and uploads the complete
evidence bundle automatically.

The tester also prints a UART summary `SERVICES 27/27 PASS`.

## The results bitmap (`g_service_results` @ `0x20010000`)

The tester accumulates a 27-bit mask; the gate asserts it equals `0x07FFFFFF`. Bit layout
(see `firmware/service_bits.h`):

| bit | SID | service | bit | SID | service |
|----:|-----|---------|----:|-----|---------|
| 0 | 0x10 | DiagnosticSessionControl | 14 | 0x31 | RoutineControl |
| 1 | 0x11 | EcuReset (last) | 15 | 0x34 | RequestDownload |
| 2 | 0x14 | ClearDiagnosticInformation | 16 | 0x35 | RequestUpload |
| 3 | 0x19 | ReadDTCInformation | 17 | 0x36 | TransferData |
| 4 | 0x22 | ReadDataByIdentifier | 18 | 0x37 | RequestTransferExit |
| 5 | 0x23 | ReadMemoryByAddress | 19 | 0x38 | RequestFileTransfer |
| 6 | 0x24 | ReadScalingDataByIdentifier | 20 | 0x3D | WriteMemoryByAddress |
| 7 | 0x27 | SecurityAccess | 21 | 0x3E | TesterPresent |
| 8 | 0x28 | CommunicationControl | 22 | 0x83 | AccessTimingParameter |
| 9 | 0x29 | Authentication | 23 | 0x84 | SecuredDataTransmission |
| 10 | 0x2A | ReadDataByPeriodicIdentifier | 24 | 0x85 | ControlDTCSetting |
| 11 | 0x2C | DynamicallyDefineDataIdentifier | 25 | 0x86 | ResponseOnEvent |
| 12 | 0x2E | WriteDataByIdentifier | 26 | 0x87 | LinkControl |
| 13 | 0x2F | InputOutputControlByIdentifier | | | |

`0x11` ECUReset is tested **last** so the ECU rebooting afterward cannot disturb earlier
results. The crypto-gated services (`0x27`/`0x29`/`0x84`) use deterministic functional
stubs — this gate proves the protocol envelope + sequencing; real-cipher validation lives
in the mbedTLS examples (`auth_challenge_mbedtls`, `security_access_mbedtls`).

## labwired requirement

The gate requires LabWired **v0.19.2 or newer** for the released multi-node
`inputs.env` runner, virtual `can_bus` interconnect, and durable
assertion-completion contract. No LabWired source checkout or local Core build
is needed for CI.

## Why nightly, not on every PR

The gate is heavy — two ARM firmware builds and two fully emulated Cortex-M33
MCUs running all 27 service exchanges. It runs on a **schedule** (and
`workflow_dispatch`), never on `pull_request`, to keep the PR gate fast.
