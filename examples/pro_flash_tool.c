/**
 * @file pro_flash_tool.c
 * @brief Enterprise Example: Orchestrating an OTA Update (0x34, 0x36, 0x37)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "uds/uds_core.h"

static void flash_log(uint8_t level, const char *msg) {
    printf("[UDS-PRO] %s\n", msg);
}

static uint32_t get_time(void) { return 0; } // Mock time

static int tp_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len) {
    printf(">> TP TX: ");
    for(int i=0; i<len; i++) printf("%02X ", data[i]);
    printf("\n");
    return 0;
}

int main() {
    uds_ctx_t ctx;
    uds_config_t cfg = {0};
    uint8_t rx_buf[1024], tx_buf[1024];

    cfg.get_time_ms = get_time;
    cfg.fn_tp_send = tp_send;
    cfg.fn_log = flash_log;
    cfg.rx_buffer = rx_buf;
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer = tx_buf;
    cfg.tx_buffer_size = 1024;
    cfg.log_level = UDS_LOG_INFO;
    cfg.strict_compliance = true;

    if (uds_init(&ctx, &cfg) != UDS_OK) return 1;

    printf("--- UDSLib Pro: Starting Flash Sequence ---\n");

    /* 1. Request Download (0x34) */
    printf("\nStep 1: Requesting Download (Address=0x08000000, Size=0x1000)\n");
    uint8_t req_34[] = { 
        0x44,          /* DFIs: AddressLen=4, SizeLen=4 */
        0x08, 0x00, 0x00, 0x00, /* Address */
        0x00, 0x00, 0x10, 0x00  /* Size */
    };
    uds_client_request(&ctx, 0x34, req_34, sizeof(req_34), NULL);

    /* 2. Transfer Data (0x36) */
    printf("\nStep 2: Transferring Data Block 0x01\n");
    uint8_t payload[256];
    memset(payload, 0xAA, 256);
    uint8_t req_36[257];
    req_36[0] = 0x01; /* Sequence */
    memcpy(&req_36[1], payload, 256);
    uds_client_request(&ctx, 0x36, req_36, sizeof(req_36), NULL);

    /* 3. Request Transfer Exit (0x37) */
    printf("\nStep 3: Exiting Transfer\n");
    uds_client_request(&ctx, 0x37, NULL, 0, NULL);

    printf("\n--- Sequence Sent Successfully ---\n");
    return 0;
}
