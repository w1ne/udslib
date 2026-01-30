# Quickstart: Zephyr OS Integration

This guide gets you up and running with LibUDS on Zephyr OS in less than 5 minutes.

## Prerequisites

- Zephyr SDK installed
- `west` tool installed
- Virtual CAN (`vcan0`) setup on your host

## 1. Setup Virtual CAN

```bash
sudo scripts/setup_vcan.sh
```

## 2. Initialize Zephyr Workspace

If you haven't already, add LibUDS to your `west.yml`:

```yaml
manifest:
  projects:
    - name: libuds
      path: modules/lib/libuds
      url: https://github.com/your-org/libuds
```

Then run:
```bash
west update
```

## 3. Build the Example

We will build the `zephyr_uds_server` for the `native_sim` board, which allows running Zephyr as a Linux process.

```bash
cd modules/lib/libuds/examples/zephyr_uds_server
west build -b native_sim
```

## 4. Run the Server

```bash
# Set up vcan interface for native_sim
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up

# Run the zephyr process
./build/zephyr/zephyr.exe --can-if vcan0
```

## 5. Test with the C Client

In another terminal, run the LibUDS C client demo:

```bash
cd modules/lib/libuds/examples/client_demo
./uds_client_demo vcan0
```

### Expected Output

**Zephyr Server:**
```
Starting LibUDS Zephyr Server Example...
UDS Server ready. Waiting for requests (0x7E0 RX / 0x7E8 TX)...
[INFO] (uds_core.c:120) dispatcher: sid 0x10, len 2
```

**C Client:**
```
[CLIENT] Sending DiagnosticSessionControl (Extended)...
[CLIENT] Response Received: SID=50, Len=6
[CLIENT] Session changed OK
```

## 🔧 Configuration Options

You can tune LibUDS via your `prj.conf`:

| Option | Default | Description |
|:-------|:--------|:------------|
| `CONFIG_LIBUDS` | `y` | Enable the library |
| `CONFIG_LIBUDS_TRANSPORT_NATIVE` | `y` | Use Zephyr SocketCAN ISO-TP |
| `CONFIG_LIBUDS_MAX_SDU_SIZE` | `4095` | Max buffer size |
| `CONFIG_LIBUDS_LOG_LEVEL` | `3` | Info level logging |

## 🧪 Advanced Testing

Run the full test suite (if on Linux):

```bash
bash scripts/run_all_tests.sh
```

This will run:
1. Unit tests (POSIX)
2. Integration tests (POSIX Client -> POSIX Server)
3. Python validation scripts
4. External `iso14229` cross-validation
