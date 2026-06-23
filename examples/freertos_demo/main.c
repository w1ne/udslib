/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief FreeRTOS two-context reference for the UDS concurrency model.
 *
 * This sketch shows the SUPPORTED two-context model: udslib is driven from two
 * FreeRTOS tasks against one shared uds_ctx_t.
 *
 *   - vCANRxTask: receives CAN frames and feeds them in via uds_input_sdu()
 *     (the RX/dispatch context).
 *   - vUDSTask:   runs uds_process() on a periodic tick (the timer context for
 *     P2-star/responsePending, periodic reads, S3 timeout and deferred post-TX
 *     actions such as ECUReset).
 *
 * Because BOTH contexts are TASKS (never an ISR) a blocking FreeRTOS mutex
 * (xSemaphoreTake(..., portMAX_DELAY)) is a legal and correct OSAL lock here: a
 * task that cannot take the mutex simply blocks and yields, and the stack never
 * touches the lock from interrupt context. This is the model to copy.
 *
 * Do NOT call uds_input_sdu() directly from a CAN RX ISR with this lock wired:
 * a blocking semaphore is illegal in an ISR. For the ISR-driven model, either
 * (a) have the ISR hand the frame to vCANRxTask via a queue (shown below), or
 * (b) replace the OSAL lock with an ISR-safe critical section
 * (taskENTER_CRITICAL/portENTER_CRITICAL or interrupt disable), never a
 * blocking semaphore.
 *
 * This example is a portability sketch (it depends on FreeRTOS and a board CAN
 * driver) and is intentionally not built by CI.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "uds/uds_core.h"

/* --- OSAL Hooks (task-vs-task: a blocking mutex is correct) --- */
static SemaphoreHandle_t uds_mutex;

static void os_lock(void *handle)
{
    /* Legal here: both callers (vUDSTask, vCANRxTask) are tasks, never an ISR. */
    xSemaphoreTake((SemaphoreHandle_t) handle, portMAX_DELAY);
}

static void os_unlock(void *handle)
{
    xSemaphoreGive((SemaphoreHandle_t) handle);
}

static uint32_t os_get_time(void)
{
    return (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* --- UDS Config / shared context --- */
static uds_ctx_t ctx;
static uds_config_t cfg;
static uint8_t rx_buf[1024];
static uint8_t tx_buf[1024];

/* A single CAN frame as received by the driver; >= 8 bytes for a classic CAN
 * payload (CAN-FD frames are larger and would size this up accordingly). */
typedef struct
{
    uint8_t data[64];
    uint16_t len;
} can_frame_t;

/* The ISR hands frames to vCANRxTask through this queue (see CAN_IRQHandler);
 * the task, not the ISR, calls into udslib. */
static QueueHandle_t can_rx_queue;

static int mock_send(struct uds_ctx *uds, const uint8_t *data, uint16_t len)
{
    (void) uds;
    (void) data;
    (void) len;
    /* Hand the framed response to the CAN TX driver here. */
    return 0;
}

/* --- UDS timer task: runs uds_process() (the periodic/timer context) --- */
static void vUDSTask(void *pvParameters)
{
    (void) pvParameters;
    for (;;) {
        uds_process(&ctx);
        vTaskDelay(pdMS_TO_TICKS(10)); /* 10 ms tick */
    }
}

/* --- CAN RX task: feeds frames in via uds_input_sdu() (the dispatch context) --- */
static void vCANRxTask(void *pvParameters)
{
    (void) pvParameters;
    can_frame_t frame;
    for (;;) {
        /* Block until the ISR posts a received frame. Running in a task (not the
         * ISR) is what makes the blocking OSAL mutex above legal. */
        if (xQueueReceive(can_rx_queue, &frame, portMAX_DELAY) == pdTRUE) {
            /* Thread-safe injection: uds_input_sdu() takes the OSAL mutex
             * internally, serialising against vUDSTask's uds_process(). */
            uds_input_sdu(&ctx, frame.data, frame.len);
        }
    }
}

/* --- Example CAN RX ISR: forwards the frame to vCANRxTask, never into udslib --- */
void CAN_IRQHandler(void)
{
    can_frame_t frame;
    BaseType_t higher_priority_task_woken = pdFALSE;

    /* Copy the frame out of the CAN peripheral here; this is illustrative. */
    frame.len = 8u;
    memset(frame.data, 0, sizeof(frame.data));

    /* ISR-safe: post to the queue and never call uds_input_sdu() (which would
     * take a blocking mutex) from interrupt context. */
    xQueueSendFromISR(can_rx_queue, &frame, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

int main(void)
{
    /* 1. Create the OSAL mutex and the ISR->task frame queue. */
    uds_mutex = xSemaphoreCreateMutex();
    can_rx_queue = xQueueCreate(8, sizeof(can_frame_t));
    if ((uds_mutex == NULL) || (can_rx_queue == NULL)) {
        return 1;
    }

    /* 2. Configure udslib. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = os_get_time;
    cfg.fn_tp_send = mock_send;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);

    /* Thread safety: a blocking mutex, correct for the task-vs-task model. */
    cfg.fn_mutex_lock = os_lock;
    cfg.fn_mutex_unlock = os_unlock;
    cfg.mutex_handle = (void *) uds_mutex;

    if (uds_init(&ctx, &cfg) != UDS_OK) {
        return 1;
    }

    /* 3. Start both contexts and the scheduler. */
    xTaskCreate(vUDSTask, "UDS", 1024, NULL, 2, NULL);
    xTaskCreate(vCANRxTask, "CANRx", 1024, NULL, 3, NULL);
    vTaskStartScheduler();

    /* Only reached if the scheduler could not start. */
    return 0;
}
