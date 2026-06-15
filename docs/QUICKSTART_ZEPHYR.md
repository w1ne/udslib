# Quickstart: Zephyr OS Integration

This guide explains how to run UDSLib on Zephyr OS in under 5 minutes.

## Prerequisites

- Zephyr SDK installed.
- `west` tool installed.
- Virtual CAN (`vcan0`) configured on the host.

## 1. Setup Virtual CAN

```bash
sudo scripts/setup_vcan.sh
```

## 2. Initialize Zephyr Workspace

Add UDSLib to your `west.yml`:

```yaml
manifest:
  projects:
    - name: udslib
      path: modules/lib/udslib
      url: https://github.com/your-org/udslib
```

Update dependencies:
```bash
west update
```

## 3. Build the Example

Build the `zephyr_uds_server` for `native_sim` (Zephyr as a Linux process):

```bash
cd modules/lib/udslib/examples/zephyr_uds_server
west build -b native_sim
```

> [!NOTE]
> CI builds this example against Zephyr **v4.4.1**. Use that version for a
> known-good build.

The example uses the internal ISO-TP fallback transport
(`CONFIG_UDSLIB_TRANSPORT_FALLBACK`). The transport instance is owned by
`zephyr/uds_zephyr_tp_fallback.c`: it wires `uds_zephyr_tp_fallback_send` as
`config.fn_tp_send` and drives timers/consecutive frames from the main loop via
`uds_zephyr_tp_fallback_process(time_ms)`.

## 4. Run the Server

```bash
# Ensure vcan0 is up
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up

# Run the native executable
./build/zephyr/zephyr.exe --can-if vcan0
```

## 5. Test with the C Client

In a separate terminal:

```bash
cd modules/lib/udslib/examples/client_demo
./uds_client_demo vcan0
```

### Expected Output

**Zephyr Server:**
```
Starting LibUDS Zephyr Server Example (Fallback Mode)...
UDS Server ready. Waiting for requests (0x7E0 RX / 0x7E8 TX)...
[INFO] (uds_core.c:120) dispatcher: sid 0x10, len 2
```

**C Client:**
```
[CLIENT] Sending DiagnosticSessionControl (Extended)...
[CLIENT] Response Received: SID=50, Len=6
[CLIENT] Session changed OK
```

## Configuration

Tune UDSLib in `prj.conf`:

| Option | Description |
|:-------|:------------|
| `CONFIG_UDSLIB` | Enable the library |
| `CONFIG_UDSLIB_TRANSPORT_FALLBACK` | Use the internal ISO-TP over classic CAN (this example) |
| `CONFIG_UDSLIB_TRANSPORT_NATIVE` | Use the Zephyr native ISO-TP shim instead |
| `CONFIG_UDSLIB_MAX_SDU_SIZE` | Max SDU buffer size |
| `CONFIG_UDSLIB_LOG_LEVEL` | Logging verbosity |

This example's `prj.conf` selects `CONFIG_UDSLIB_TRANSPORT_FALLBACK`.

## Advanced Testing

Run the full test suite (Linux only):

```bash
bash scripts/run_all_tests.sh
```

This executes:
1. Unit tests (POSIX).
2. Integration tests (POSIX Client -> POSIX Server).
3. Python validation scripts.
4. External `iso14229` cross-validation.
