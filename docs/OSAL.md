# OS Abstraction Layer (OSAL)

LibUDS v1.2.0+ introduces an OS Abstraction Layer (OSAL) to ensure thread-safety when running in Multi-Threaded/RTOS environments (e.g., Zephyr, FreeRTOS, POSIX threads).

## Overview

The UDS stack is designed to be **reentrant** and **thread-safe** provided that the application implements the required mutex callbacks. This allows for:
- Concurrent calls to `uds_process()` (Stack Logic) and `uds_input_sdu()` (Bus Input).
- Safe manipulation of shared state (Session, Security Level, Timing Timers).

## Configuration

To enable thread-safety, you must provide `fn_mutex_lock` and `fn_mutex_unlock` in the `uds_config_t` structure during initialization.

### Data Structures

```c
typedef int (*uds_mutex_op_t)(void *mutex_handle);

typedef struct {
    // ... other config ...
    void *mutex_handle;          /**< Pointer to user-defined mutex object */
    uds_mutex_op_t fn_mutex_lock;   /**< Callback to lock mutex */
    uds_mutex_op_t fn_mutex_unlock; /**< Callback to unlock mutex */
} uds_config_t;
```

## Implementation Guide

### 1. Zephyr RTOS Example

```c
#include <zephyr/kernel.h>
#include "uds/uds.h"

static struct k_mutex uds_mutex;

static int my_mutex_lock(void *handle) {
    return k_mutex_lock((struct k_mutex *)handle, K_FOREVER);
}

static int my_mutex_unlock(void *handle) {
    return k_mutex_unlock((struct k_mutex *)handle);
}

void app_uds_init(void) {
    k_mutex_init(&uds_mutex);

    uds_config_t config = {
        .mutex_handle = &uds_mutex,
        .fn_mutex_lock = my_mutex_lock,
        .fn_mutex_unlock = my_mutex_unlock,
        // ...
    };
    uds_init(&ctx, &config);
}
```

### 2. POSIX Example (pthread)

```c
#include <pthread.h>

static pthread_mutex_t lock;

static int posix_lock(void *h) {
    pthread_mutex_lock((pthread_mutex_t*)h);
    return 0;
}

static int posix_unlock(void *h) {
    pthread_mutex_unlock((pthread_mutex_t*)h);
    return 0;
}
```

## Critical Sections

The stack automatically protects:
- **Session State**: Transitions between Default/Extended/Programming sessions.
- **Security State**: Unlocking/Locking security levels.
- **Timing Logic**: P2 and S3 timer updates.
- **Service Dispatch**: Processing of incoming requests.

**Note**: The transport layer (ISO-TP) is responsible for its own thread safety if handled separately, but `uds_input_sdu` is the entry point into the core stack and is protected.
