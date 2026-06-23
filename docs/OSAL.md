# OS Abstraction Layer (OSAL)

The OS Abstraction Layer (OSAL) lets the UDS server run safely when its two
entry points are driven from two different execution contexts: a receive path
(`uds_input_sdu()` / `uds_input_sdu_addr()`) and a periodic tick
(`uds_process()`). It is a thin pair of lock/unlock callbacks plus a handle; the
library calls them around the state it shares between those two contexts.

## Supported concurrency model

The server has exactly two re-entrancy points, and they are the only functions
the lock protects:

| Function | Context it is meant to run in |
| --- | --- |
| `uds_input_sdu()` / `uds_input_sdu_addr()` | The bus RX path: a transport task, or an RX interrupt. |
| `uds_process()` | The periodic tick: a super-loop iteration, an RTOS task, or a timer callback. |

When `fn_mutex_lock`/`fn_mutex_unlock` are provided, these two may run
concurrently against one `uds_ctx_t`. The lock makes their critical sections
mutually exclusive, so the shared state (active session, security level, the
S3/P2/P2\* timers, the responsePending state machine, the deferred post-TX
action, and the periodic/ROE schedule) is never observed half-updated. The
cross-context fields are additionally declared `volatile` so a compiler cannot
cache a stale copy across the critical section when the "lock" is a bare
interrupt-disable (see below).

What is **not** supported:

- Two threads both calling `uds_input_sdu()` on the same context at once. UDS is
  one-request-at-a-time; a second concurrent request is the caller's bug, not a
  case the lock is sized for. (A request that arrives while another is still
  pending is rejected with NRC 0x21 busyRepeatRequest — but that is the *serial*
  re-entry case, still under the lock.)
- Calling the server entry points and the **client** role
  (`uds_client_request()`) on the same `tx_buffer` without the same mutex; the
  client takes the lock around building its frame for exactly this reason.

### `fn_tp_send` runs OUTSIDE the lock

Each entry point builds its response into `tx_buffer` under the lock and latches
the length. How the transport send relates to the lock then depends on the frame
size:

- **Frames up to `UDS_TX_FLUSH_SNAPSHOT_MAX` (default 512 bytes)** are copied into
  a private stack snapshot under the lock; the lock is then released and
  `fn_tp_send` is called on the snapshot. The (possibly slow or blocking)
  transport runs **outside** the critical section, so it cannot stall the other
  context or — when the lock is interrupt-disable — extend interrupt latency.
- **Oversized frames (larger than `UDS_TX_FLUSH_SNAPSHOT_MAX`)** are sent **while
  the lock is still held**, then the lock is released. A frame that does not fit
  the stack snapshot cannot be copied out safely, so it is transmitted under the
  lock — exactly as every frame was before this concurrency model — which
  guarantees a concurrent context cannot overwrite `tx_buffer` mid-send.

Raise `UDS_TX_FLUSH_SNAPSHOT_MAX` toward your `tx_buffer_size` to keep more (or
all) frames on the lock-free fast path; the only cost is that much stack on the
flushing context.

Two consequences for the integrator:

1. **`fn_tp_send` may be re-entered relative to the lock.** On the snapshot fast
   path it runs with the lock released, so it must not assume the stack lock is
   held. If your driver needs its own serialization (e.g. a single shared CAN
   mailbox), take a driver-local lock inside `fn_tp_send`.
2. **Do not assume `data` aliases `config.tx_buffer`.** On the snapshot fast path
   the library hands `fn_tp_send` a private snapshot of the frame rather than
   `tx_buffer` itself (on the oversized path it is `tx_buffer`). Treat the
   `data`/`len` arguments as the authoritative frame either way.

### Interrupt mode: the lock must be an ISR-safe critical section

If `uds_input_sdu()` runs from an **interrupt** while `uds_process()` runs from
the main loop, the mutex callbacks must form a critical section that the ISR
honours — typically *disable/enable interrupts* (or raise/lower BASEPRI on
Cortex-M), not an RTOS mutex (an RTOS mutex cannot be taken from an ISR and would
either assert or deadlock):

```c
static void irq_lock(void *h) {
    uint32_t *primask = (uint32_t *) h;
    *primask = __get_PRIMASK();
    __disable_irq();
}
static void irq_unlock(void *h) {
    if (*(uint32_t *) h == 0u) {
        __enable_irq();
    }
}
```

Because the snapshot send is outside the lock, the actual `fn_tp_send` runs with
interrupts enabled — the disable window is only the short state update plus the
snapshot copy, not the transmit. (An oversized response, transmitted under the
lock, does extend the disable window across the transmit; keep
`UDS_TX_FLUSH_SNAPSHOT_MAX` at or above your largest response in interrupt mode.)

## Configuration

Provide `fn_mutex_lock`, `fn_mutex_unlock`, and `mutex_handle` in `uds_config_t`.
All three may be left NULL/zero for a single-threaded super-loop, in which case
the library performs no locking.

### Data structures

The callbacks return `void` (a lock that can fail has no safe recovery inside the
stack; failures must be handled by the OS primitive itself):

```c
typedef void (*uds_mutex_lock_fn)(void *mutex_handle);
typedef void (*uds_mutex_unlock_fn)(void *mutex_handle);

typedef struct {
    /* ... other config ... */
    void *mutex_handle;                  /**< User-defined mutex / lock-state object */
    void (*fn_mutex_lock)(void *handle);   /**< Enter the critical section */
    void (*fn_mutex_unlock)(void *handle); /**< Leave the critical section */
} uds_config_t;
```

## Integration examples

### 1. Zephyr RTOS (task vs task)

```c
#include <zephyr/kernel.h>
#include "uds/uds.h"

static struct k_mutex uds_mutex;

static void my_mutex_lock(void *handle) {
    k_mutex_lock((struct k_mutex *) handle, K_FOREVER);
}

static void my_mutex_unlock(void *handle) {
    k_mutex_unlock((struct k_mutex *) handle);
}

void app_uds_init(void) {
    k_mutex_init(&uds_mutex);

    uds_config_t config = {
        .mutex_handle = &uds_mutex,
        .fn_mutex_lock = my_mutex_lock,
        .fn_mutex_unlock = my_mutex_unlock,
        /* ... */
    };
    uds_init(&ctx, &config);
}
```

### 2. POSIX (pthread)

```c
#include <pthread.h>

static pthread_mutex_t lock;

static void posix_lock(void *h) {
    pthread_mutex_lock((pthread_mutex_t *) h);
}

static void posix_unlock(void *h) {
    pthread_mutex_unlock((pthread_mutex_t *) h);
}
```

## Critical sections

Held only for the state update, never for the transmit, the lock protects:

- **Session state**: transitions (Default/Extended/Programming/Safety) and the
  S3-timeout revert.
- **Security state**: lock/unlock, the failed-attempt delay, and the S3 relock.
- **Timing logic**: the P2/P2\* responsePending state machine.
- **Deferred post-TX action**: arming (ECUReset 0x11, LinkControl 0x87) and the
  `uds_process()` drain.
- **Periodic / ResponseOnEvent schedule**: the 0x2A table and 0x86 slots.
- **Service dispatch**: building the response in `tx_buffer`.

> [!NOTE]
> `uds_input_sdu` is the protected entry point to the core stack. The ISO-TP
> transport layer (`uds_tp_isotp.c`) must handle its own thread safety if it is
> driven from a context other than the one that calls `uds_input_sdu`.
