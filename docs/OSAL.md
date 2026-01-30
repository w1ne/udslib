# OS Abstraction Layer (OSAL)

The OS Abstraction Layer (OSAL) ensures thread safety when running in Multi-Threaded or RTOS environments (Zephyr, FreeRTOS, POSIX).

## Overview

The UDS stack is **reentrant** and **thread-safe** if the application implements the required mutex callbacks. This allows for:
- Concurrent calls to `uds_process()` (Logic) and `uds_input_sdu()` (Bus Input).
- Safe modification of shared state (Session, Security, Timers).

## Configuration

To enable thread safety, provide `fn_mutex_lock` and `fn_mutex_unlock` in `uds_config_t`.

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

## Integration Examples

### 1. Zephyr RTOS

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

### 2. POSIX (pthread)

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
- **Session State**: Transitions (Default/Extended/Programming).
- **Security State**: Lock/Unlock operations.
- **Timing Logic**: P2 and S3 timer updates.
- **Service Dispatch**: Request processing.

> [!NOTE]
> `uds_input_sdu` is the protected entry point to the core stack. The ISO-TP transport layer must handle its own thread safety if managed separately.
