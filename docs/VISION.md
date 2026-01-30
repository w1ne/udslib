# LibUDS Vision

## Mission
To provide a robust, hardware-agnostic, and **safety-first** UDS stack that eases the transition to Software-Defined Vehicles (SDV).

## Industry Challenges
- **Security**: Moving from Seed/Key to Certificate-based Authentication (ISO 21434).
- **Safety**: Preventing resets or writes during active machine operation.
- **Concurrency**: Integrating with RTOS (Zephyr, FreeRTOS) without blocking main loops or introducing race conditions.
- **Throughput**: Handling high-speed flashing over DoIP without stalling via P2 limits.

## Approach

### 1. Hardware Independence
Core logic is identical whether running on an 8-bit MCU or a Linux Gateway. Abstracting Transport and Time layers reduces porting time significantly.

### 2. Host-First Design
Validation happens on PC/Cloud before targeting hardware. This removes hardware dependencies from the critical path.

### 3. Reliability
- **Safety Gates**: Critical services (Reset, Write, Download) require application checks before execution.
- **Concurrency**: Architecture separates Ingress from Processing for safe RTOS integration.
- **Atomic Operations**: State recovery is the default.

## Market Position
LibUDS targets the mid-market. We bridge the gap between "DIY" repositories and expensive Tier-1 commercial stacks. We offer commercial stability with open-source agility.
