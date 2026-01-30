/**
 * @file main.c
 * @brief FreeRTOS Example (Task Based)
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "uds/uds_core.h"

/* --- OSAL Hooks --- */
static SemaphoreHandle_t uds_mutex;

static void os_lock(void *handle) {
    xSemaphoreTake((SemaphoreHandle_t)handle, portMAX_DELAY);
}

static void os_unlock(void *handle) {
    xSemaphoreGive((SemaphoreHandle_t)handle);
}

static uint32_t os_get_time(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* --- UDS Config --- */
static uds_ctx_t ctx;
static uds_config_t cfg;
static uint8_t rx_buf[1024];
static uint8_t tx_buf[1024];

static int mock_send(struct uds_ctx *ctx, const uint8_t *data, uint16_t len) {
    return 0;
}

/* --- UDS Task --- */
void vUDSTask(void *pvParameters) {
    /* 1. Init Mutex */
    uds_mutex = xSemaphoreCreateMutex();

    /* 2. Configure */
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = os_get_time;
    cfg.fn_tp_send = mock_send;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = sizeof(rx_buf);
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = sizeof(tx_buf);
    
    /* Thread Safety */
    cfg.fn_mutex_lock = os_lock;
    cfg.fn_mutex_unlock = os_unlock;
    cfg.mutex_handle = (void*)uds_mutex;

    uds_init(&ctx, &cfg);

    /* 3. Event Loop */
    for (;;) {
        uds_process(&ctx);
        vTaskDelay(pdMS_TO_TICKS(10)); /* 10ms Tick */
    }
}

/* --- Rx Task (Separate Context) --- */
void vCANRxTask(void *pvParameters) {
    for (;;) {
        /* Wait for CAN frame... */
        /* uint8_t data[] = { ... }; */
        
        /* Thread-safe injection */
        /* uds_input_sdu handles locking internally if configured? 
           Check Core: Yes, uds_input_sdu calls fn_mutex_lock! */
        uds_input_sdu(&ctx, NULL, 0); 
    }
}

int main(void) {
    xTaskCreate(vUDSTask, "UDS", 1024, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
