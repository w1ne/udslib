/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file test_concurrency_stress.c
 * @brief Host pthreads stress test for the UDS concurrency model.
 *
 * One thread hammers uds_input_sdu() (the RX/dispatch context) while another
 * runs uds_process() (the periodic tick) against a single shared uds_ctx_t,
 * with the OSAL mutex hooks wired to a real pthread mutex. It asserts the stack
 * never crashes or trips an internal assertion and that its shared state ends in
 * a consistent, in-range configuration over many iterations.
 *
 * This is NOT part of the core ctest gate (it is registered only when a POSIX
 * threads implementation is detected, and the value of the test is its dynamic
 * analysis). Build the tree with -DCMAKE_C_FLAGS=-fsanitize=thread (or run
 * ./build under ThreadSanitizer) and execute test_concurrency_stress to look for
 * data races on the cross-context ctx fields. A clean run is the pass criterion;
 * under TSan, zero reports is the stronger one.
 */

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uds/uds_config.h"
#include "uds/uds_core.h"

#define STRESS_ITERATIONS 200000

/* The transport is the natural serialization point against tx_buffer: emulate a
 * real driver by taking the same lock the stack holds for state. The send is now
 * invoked OUTSIDE the stack lock, so this models the realistic case where the
 * driver has its own internal synchronization. */
static pthread_mutex_t g_tx_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile unsigned long g_tx_count = 0u;

static int stress_tp_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    /* Touch the bytes so a use-after-free / torn buffer would be caught by the
     * sanitizer, and confirm the framed length is always sane. */
    assert(len >= 1u);
    volatile uint8_t sink = 0u;
    for (uint16_t i = 0u; i < len; i++) {
        sink = (uint8_t) (sink ^ data[i]);
    }
    (void) sink;
    pthread_mutex_lock(&g_tx_lock);
    g_tx_count++;
    pthread_mutex_unlock(&g_tx_lock);
    return 0;
}

/* Monotonic-ish clock shared by both threads. */
static pthread_mutex_t g_time_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_now = 0u;

static uint32_t stress_get_time(void)
{
    uint32_t t;
    pthread_mutex_lock(&g_time_lock);
    t = ++g_now;
    pthread_mutex_unlock(&g_time_lock);
    return t;
}

/* The stack OSAL hooks: a recursive pthread mutex so re-entrancy through the
 * stack cannot self-deadlock (matches a typical RTOS recursive mutex). */
static pthread_mutex_t g_uds_lock;

static void stress_lock(void *h)
{
    pthread_mutex_lock((pthread_mutex_t *) h);
}

static void stress_unlock(void *h)
{
    pthread_mutex_unlock((pthread_mutex_t *) h);
}

/* A periodic-read provider so the 0x2A scheduler in uds_process() actually
 * transmits (exercising the out-of-lock periodic flush path). */
static int stress_periodic_read(struct uds_ctx *ctx, uint8_t pid, uint8_t *out, uint16_t max_len)
{
    (void) ctx;
    (void) pid;
    if (max_len < 2u) {
        return -1;
    }
    out[0] = 0xAAu;
    out[1] = 0x55u;
    return 2;
}

/* A large DID so an RDBI (0x22) response exceeds UDS_TX_FLUSH_SNAPSHOT_MAX (512)
 * and exercises the oversize-under-lock flush path concurrently with the
 * periodic out-of-lock flush. The 0x62 response is 3 + BIG_DID_SIZE bytes. */
#define STRESS_BIG_DID_SIZE 600u

static int stress_big_did_read(struct uds_ctx *ctx, uint16_t did, uint8_t *buf, uint16_t max_len)
{
    (void) ctx;
    if ((did != 0x9000u) || (max_len < STRESS_BIG_DID_SIZE)) {
        return -1;
    }
    for (uint16_t i = 0u; i < STRESS_BIG_DID_SIZE; i++) {
        buf[i] = (uint8_t) ((i * 7u + 3u) & 0xFFu);
    }
    return 0;
}

static const uds_did_entry_t g_stress_dids[] = {
    {0x9000u, STRESS_BIG_DID_SIZE, UDS_SESSION_ALL, 0u, stress_big_did_read, NULL, NULL},
};

/* tx_buffer must exceed UDS_TX_FLUSH_SNAPSHOT_MAX (512) so the big RDBI response
 * lands on the oversize-under-lock path rather than being rejected as too long. */
static uint8_t g_rx[1024];
static uint8_t g_tx[1024];
static uds_ctx_t g_ctx;
static uds_config_t g_cfg;
static volatile bool g_stop = false;

static void *input_thread(void *arg)
{
    (void) arg;
    /* A mix of requests: TesterPresent (0x3E), a session change (0x10), a
     * periodic-read setup (0x2A), and a malformed frame. */
    static const uint8_t tp[] = {0x3E, 0x00};
    static const uint8_t session_ext[] = {0x10, 0x03};
    static const uint8_t session_def[] = {0x10, 0x01};
    static const uint8_t periodic[] = {0x2A, 0x01, 0xE3}; /* fast rate, pid 0xE3 */
    static const uint8_t big_rdbi[] = {0x22, 0x90, 0x00}; /* oversize 0x62 response */
    static const uint8_t bad[] = {0x10};                  /* too short -> NRC 0x13 */

    for (long i = 0; i < STRESS_ITERATIONS && !g_stop; i++) {
        switch (i & 0x7) {
            case 0:
                uds_input_sdu(&g_ctx, tp, sizeof(tp));
                break;
            case 1:
                uds_input_sdu(&g_ctx, (i & 0x4) ? session_ext : session_def, sizeof(session_ext));
                break;
            case 2:
                uds_input_sdu(&g_ctx, periodic, sizeof(periodic));
                break;
            case 4:
                /* Oversize RDBI: its 603-byte response is sent under the lock,
                 * concurrently with the periodic out-of-lock flush in uds_process. */
                uds_input_sdu(&g_ctx, big_rdbi, sizeof(big_rdbi));
                break;
            default:
                uds_input_sdu(&g_ctx, bad, sizeof(bad));
                break;
        }
    }
    return NULL;
}

static void *process_thread(void *arg)
{
    (void) arg;
    for (long i = 0; i < STRESS_ITERATIONS && !g_stop; i++) {
        uds_process(&g_ctx);
    }
    return NULL;
}

int main(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_uds_lock, &attr);

    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.get_time_ms = stress_get_time;
    g_cfg.fn_tp_send = stress_tp_send;
    g_cfg.fn_periodic_read = stress_periodic_read;
    g_cfg.rx_buffer = g_rx;
    g_cfg.rx_buffer_size = sizeof(g_rx);
    g_cfg.tx_buffer = g_tx;
    g_cfg.tx_buffer_size = sizeof(g_tx);
    g_cfg.did_table.entries = g_stress_dids;
    g_cfg.did_table.count = (uint16_t) (sizeof(g_stress_dids) / sizeof(g_stress_dids[0]));
    g_cfg.p2_ms = 5;
    g_cfg.p2_star_ms = 20;
    g_cfg.s3_ms = 10;
    g_cfg.mutex_handle = &g_uds_lock;
    g_cfg.fn_mutex_lock = stress_lock;
    g_cfg.fn_mutex_unlock = stress_unlock;

    if (uds_init(&g_ctx, &g_cfg) != UDS_OK) {
        fprintf(stderr, "uds_init failed\n");
        return 1;
    }

    pthread_t t_in, t_proc;
    if (pthread_create(&t_in, NULL, input_thread, NULL) != 0 ||
        pthread_create(&t_proc, NULL, process_thread, NULL) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        return 1;
    }

    pthread_join(t_in, NULL);
    pthread_join(t_proc, NULL);

    /* Final-state consistency: the stack must be left in a valid configuration,
     * not a torn one. The active session is one of the four ISO values; the
     * periodic table count never exceeds its capacity; the deferred-TX flag is
     * not left dangling with a zero length while marked pending. */
    uint8_t s = g_ctx.session.active;
    if (s != 0x01u && s != 0x02u && s != 0x03u && s != 0x04u) {
        fprintf(stderr, "inconsistent session.active = 0x%02X\n", s);
        return 1;
    }
    if (g_ctx.server.periodic_count > 8u) {
        fprintf(stderr, "periodic_count out of range = %u\n", g_ctx.server.periodic_count);
        return 1;
    }
    if (g_ctx.server.tx_pending && g_ctx.server.tx_pending_len == 0u) {
        fprintf(stderr, "tx_pending set with zero length\n");
        return 1;
    }

    printf("OK: %ld iterations/thread, %lu frames transmitted, final session=0x%02X\n",
           (long) STRESS_ITERATIONS, g_tx_count, s);
    pthread_mutex_destroy(&g_uds_lock);
    return 0;
}
