# OS Integration Guide

UDSLib is designed to be OS-agnostic. It can run on Bare Metal, RTOS, or POSIX systems.

## 1. Bare Metal (Super Loop)

In a bare metal environment, `udslib` typically runs inside the main `while(1)` loop.

- **Example**: [examples/bare_metal/main.c](../examples/bare_metal/main.c)
- **Key Concepts**:
  - **Polling**: Call `uds_process()` every loop iteration.
  - **Timing**: Provide a `get_time_ms()` function based on `SysTick` or a hardware timer.
  - **Concurrency**: Disable interrupts during critical sections if `uds_input_sdu` is called from an ISR.
  
```c
while (1) {
    /* 1. Feed the Stack */
    uds_process(&ctx);
    
    /* 2. Check HW */
    if (can_msg_received) {
        uds_input_sdu(&ctx, data, len);
    }
}
```

## 2. FreeRTOS (Task-Based)

In an RTOS, `udslib` usually runs in its own low-priority task.

- **Example**: [examples/freertos_demo/main.c](../examples/freertos_demo/main.c)
- **Key Concepts**:
  - **Thread Safety**: Use `uds_config_t.fn_mutex_lock` logic. The library locks the mutex before modifying internal state and releases it before calling `fn_tp_send` (the transmit runs outside the lock). See [docs/OSAL.md](OSAL.md) for the full concurrency model, including interrupt-mode (disable-IRQ) locking.
  - **Blocking**: The UDS task can `vTaskDelay` to yield CPU.
  - **Communication**: Use Queues or Direct Notifications to pass CAN frames to the UDS task (or call `uds_input_sdu` from the CAN task if Mutex is recursive/safe).

```c
/* Thread-safe config */
cfg.fn_mutex_lock = os_lock; /* xSemaphoreTake */
cfg.fn_mutex_unlock = os_unlock;

/* Task */
void vUDSTask(void *pv) {
    uds_init(&ctx, &cfg);
    while(1) {
        uds_process(&ctx);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
```

## 3. Zephyr RTOS

Zephyr support is built-in as a module.

- **Guide**: [ZEPHYR_INTEGRATION.md](ZEPHYR_INTEGRATION.md)
- **Example**: [examples/zephyr_uds_server](../examples/zephyr_uds_server)
- **Features**:
  - Uses native Zephyr ISO-TP stack (`CONFIG_ISOTP=y`).
  - Integrated with Kconfig (`CONFIG_UDS=y`).
  - Automatic thread spawning (optional).

## 4. POSIX / Linux

Used for simulation and testing.

- **Example**: [examples/host_sim](../examples/host_sim)
- **Key Concepts**:
  - Uses `clock_gettime` for millisecond precision.
  - Uses UDP sockets to simulate CAN bus.

## 5. End-to-End Reprogramming Reference

A complete flash/reprogramming flow tying the privileged services together.

- **Example**: [examples/pro_flash_tool](../examples/pro_flash_tool)
- **Flow**: SessionControl (0x10) → SecurityAccess (0x27) → LinkControl/AccessTimingParameter (0x87 / 0x83) → RequestDownload (0x34) → TransferData (0x36) → RequestTransferExit (0x37) → checksum/RoutineControl.
- **Key Concepts**:
  - Set `config.restrict_sessions` to confine reprogramming services to the programming session.
  - Use `fn_link_control` (0x87) for the baud-rate transition and AccessTimingParameter (0x83) to negotiate P2/P2* at runtime.
