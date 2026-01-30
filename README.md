# LibUDS: Commercial ISO-14229 Protocol Stack

**The "Universally Portable" UDS Stack for Embedded Systems.**

## 1. Product Overview (The "Why")

### The Problem
Traditional UDS stacks are inextricably linked to specific hardware drivers, RTOS threads, or proprietary ISO-TP implementations. Migrating a stack from a prototype (STM32 Bare Metal) to a product (Zephyr/Linux) typically requires a complete rewrite.

### The Solution: LibUDS
LibUDS is an **Application Layer Pure-Play**. It is architected to be:
*   **Target Agnostic**: Runs on 8-bit MCUs, 32-bit Bare Metal, FreeRTOS, Zephyr, Linux, or Windows.
*   **Zero Dependencies**: No `libc` required (no `printf`, `malloc`, `memcpy` dependency in core).
*   **Future Proof**: Designed specifically to wrap native RTOS socket interfaces (like Zephyr `isotp`) while providing a robust fallback for bare metal.

### Commercial License
*   **Single Developer**: $1,499 (Royalty Free)
*   **Consultancy Pack**: $4,999 (Up to 5 Seats)
*   **Evaluation**: GPLv3 (Source Available)

---

## 2. Technical Architecture (The "How")

### 🚀 Status: v1.1.0 (Maintenance & Hardening)

LibUDS v1.1.0 introduces full support for **ECU Reset (0x11)**, **Communication Control (0x28)**, and a scalable **Table-Driven DID Registry** for Data Identifiers (0x22/0x2E). It remains 100% compliant with its rigorous unit test suite.

### 🛠️ Quick Start (Simulated)

```bash
# 1. Build host simulator & tests
mkdir build && cd build
cmake ..
make

# 2. Run unit tests
ctest --output-on-failure

# 3. Run ECU Simulator
./examples/host_sim/uds_host_sim
```

### Dependency Injection
LibUDS does not own the hardware. It asks you to provide the hardware via `uds_config_t`.

```c
typedef struct {
    // 1. Time Source (Tickless Operation)
    uds_get_time_fn get_time_ms;
    
    // 2. Transport Output (SDU Level)
    uds_tp_send_fn  fn_tp_send;
    
    // 3. Memory (Caller Owned buffers)
    uint8_t* rx_buffer;
    uint16_t rx_buffer_size;
    // ...
} uds_config_t;
```

### The "Spliced Layer" Transport Strategy
Most stacks fail because they hardcode ISO-TP segmentation. 
**LibUDS operates strictly on SDUs (Service Data Units)**—fully assembled messages.

*   **Scenario A: Zephyr / Linux**
    *   The OS handles segmentation (ISO 15765-2).
    *   LibUDS receives the full packet from the socket.
    *   LibUDS writes the full response to the socket.
    *   *Core ISO-TP logic is bypassed entirely.*

*   **Scenario B: Bare Metal / Simple RTOS**
    *   The OS has no ISO-TP stack.
    *   LibUDS provides a lightweight, **Zero-Copy ISO-TP Fallback layer**.
    *   You feed CAN frames into `uds_isotp_rx_callback`.
    *   The fallback layer reassembles the SDU and calls the Core.

---

## 3. Quick Start

### Directory Structure
*   `include/uds/` - Public API Headers.
*   `src/core/` - The protocol logic (State machines, Services).
*   `src/transport/` - The ISO-TP fallback layer.
*   `examples/` - Host simulation and integration templates.

### Example: Host Simulation
LibUDS includes a PC-based simulation to verify logic without hardware.

```bash
cd examples/host_sim
make
./uds_host_sim
```

**Output:**
```
Starting UDS Host Simulation (ISO-TP Mode)...
[INF] UDS Stack Initialized
--- Simulating CAN Rx: SF [10 03] ---
[DBG] Processing SDU...
[CAN TX] ID:100 Data: 06 50 03 00 32 01 F4
[INF] Session changed to Extended
```

---

## 4. Porting Guide

### Step 1: Configuration
Allocate buffers and define your time source.

```c
uint32_t my_time_ms(void) { return HAL_GetTick(); }
uint8_t rx_buf[1024]; 
uint8_t tx_buf[1024];

uds_config_t cfg = {
    .get_time_ms = my_time_ms,
    .rx_buffer = rx_buf,
    .rx_buffer_size = sizeof(rx_buf),
    .tx_buffer = tx_buf,
    .tx_buffer_size = sizeof(tx_buf),
    // ...
};
```

### Step 2: Wire the Transport

**Option A: Bare Metal (Use Fallback)**
```c
// 1. Init Fallback
uds_tp_isotp_init(my_can_send_func, 0x7E8, 0x7E0);

// 2. Link Config
cfg.fn_tp_send = uds_isotp_send;

// 3. Feed CAN Frames
void CAN_Rx_IRQHandler() {
   uds_isotp_rx_callback(&ctx, rx_header.StdId, rx_data, rx_len);
}
```

**Option B: Zephyr (Native Sockets)**
```c
// 1. Link Config to Socket Wrapper
int zephyr_tp_send(uds_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    return send(isotp_socket, data, len, 0);
}
cfg.fn_tp_send = zephyr_tp_send;

// 2. Feed SDUs directly
void zephyr_rx_thread() {
   recv(isotp_socket, buf, sizeof(buf), 0);
   uds_input_sdu(&ctx, buf, len);
}
```

### Step 3: Run the Idle Loop
Call `uds_process(&ctx)` periodically (e.g., every 1ms or in the idle task) to handle session timeouts and P2 timers.

---

## 5. Documentation

For deeper dives into the project's design and future, please refer to the following documents:

*   [**Architecture**](docs/ARCHITECTURE.md) - Design philosophy, component diagrams, and transport strategy.
*   [**Roadmap**](docs/ROADMAP.md) - Project phases, upcoming features, and long-term vision.
*   [**Vision**](docs/VISION.md) - Product mission, market position, and design principles.
