/**
 * @file main.c
 * @brief Host Simulation Example for LibUDS.
 *
 * Implements a virtual ECU using ISO-TP over UDP.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

#ifdef _WIN32
#include <windows.h>
/**
 * @brief Get monotonic time in milliseconds (Windows).
 * @return uint32_t milliseconds.
 */
static uint32_t get_time_ms(void)
{
    return GetTickCount();
}
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Get monotonic time in milliseconds (POSIX).
 * @return uint32_t milliseconds.
 */
static uint32_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}
#endif

/* --- Virtual CAN over UDP --- */

#pragma pack(push, 1)
/**
 * @brief Virtual CAN packet structure.
 */
typedef struct {
    uint32_t id;      /**< CAN ID */
    uint8_t data[8];  /**< Payload */
    uint8_t len;      /**< Length */
} vcan_packet_t;
#pragma pack(pop)

/** Server socket descriptor */
static int g_server_fd = -1;
/** Client address for UDP response */
static struct sockaddr_in g_client_addr;
/** Length of the client address */
static socklen_t g_client_len = sizeof(g_client_addr);

/**
 * @brief Example event logger callback.
 *
 * @param level Log severity level.
 * @param msg   Null-terminated message string.
 */
static void log_event(uint8_t level, const char *msg)
{
    const char *lvl_str = (level == UDS_LOG_ERROR) ? "ERR" : ((level == UDS_LOG_INFO) ? "INF" : "DBG");
    printf("[%u] [%s] %s\n", get_time_ms(), lvl_str, msg);
}

/**
 * @brief Mock CAN Send function (Transport: UDP).
 *
 * @param id   CAN Identifier.
 * @param data Pointer to frame data.
 * @param len  Length of data.
 * @return     0 on success.
 */
static int mock_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    printf("[CAN TX] ID:%03X Data:", id);
    for (int i = 0; i < len; i++) {
        printf(" %02X", data[i]);
    }
    printf("\n");

    if (g_server_fd != -1) {
        vcan_packet_t pkt;
        pkt.id = id;
        pkt.len = len;
        memcpy(pkt.data, data, len);
        sendto(g_server_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&g_client_addr, g_client_len);
    }
    return 0;
}

/* --- Simulation State --- */

/** Receive buffer (1KB) */
static uint8_t g_rx_buf[1024];
/** Transmit buffer (1KB) */
static uint8_t g_tx_buf[1024];

static char g_ecu_vin[] = "LIBUDS_SIM_001";
static char g_customer_name[16] = "ECU_OWNER";

/**
 * @brief Example DID table setup.
 */
static const uds_did_entry_t g_ecu_dids[] = {
    {0xF190, 14, NULL, NULL, g_ecu_vin},          /* VIN (Direct storage) */
    {0x0123, 16, NULL, NULL, g_customer_name},    /* Customer Name (Read/Write) */
};

static const uds_did_table_t g_ecu_did_table = {.entries = g_ecu_dids, .count = 2};

/**
 * @brief Mock ECU Reset implementation.
 */
static void mock_reset(uds_ctx_t *ctx, uint8_t type)
{
    (void)ctx;
    const char *type_str = (type == UDS_RESET_HARD) ? "HARD" : "SOFT";
    printf("[APP] ECU RESET TRIGGERED: Type %s (0x%02X)\n", type_str, type);
    printf("[APP] Simulated reboot in 3... 2... 1...\n");
}

/**
 * @brief Main Entry Point for ECU Simulator.
 */
int main(int argc, char **argv)
{
    int port = 5000;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("Starting UDS ECU Simulator (ISO-TP over UDP:%d)...\n", port);

    /* Setup UDP Socket */
    g_server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(g_server_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return -1;
    }

    /* Init Transport Layer (TX: 0x7E8, RX: 0x7E0) */
    uds_tp_isotp_init(mock_can_send, 0x7E8, 0x7E0);

    /* Configure UDS Stack */
    uds_config_t cfg = {.ecu_address = 0x10,
                        .get_time_ms = get_time_ms,
                        .fn_log = log_event,
                        .fn_tp_send = uds_isotp_send,
                        .fn_reset = mock_reset,
                        .did_table = g_ecu_did_table,
                        .rx_buffer = g_rx_buf,
                        .rx_buffer_size = sizeof(g_rx_buf),
                        .tx_buffer = g_tx_buf,
                        .tx_buffer_size = sizeof(g_tx_buf),
                        .p2_ms = 50,
                        .p2_star_ms = 2000};

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    printf("Waiting for VCAN packets...\n");

    uint32_t slow_op_start = 0;

    while (1) {
        uint32_t now = get_time_ms();
        uds_process(&ctx);
        uds_tp_isotp_process();

        /* Mock "Long Running" Async Operation for SID 0x31 */
        if (ctx.p2_msg_pending && ctx.pending_sid == 0x31 && slow_op_start == 0) {
            slow_op_start = now;
            printf("[APP] Starting 1.5s operation for SID 0x31...\n");
        }

        if (slow_op_start > 0 && (now - slow_op_start) >= 1500) {
            printf("[APP] Operation complete! Sending 0x71 response.\n");
            ctx.config->tx_buffer[0] = 0x71; /* Positive response to 0x31 */
            ctx.config->tx_buffer[1] = 0x01;
            uds_send_response(&ctx, 2);
            slow_op_start = 0;
        }

        /* Non-blocking UDP Receive */
        struct timeval tv = {0, 1000}; /* 1ms timeout */
        setsockopt(g_server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        vcan_packet_t pkt;
        ssize_t n = recvfrom(g_server_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&g_client_addr,
                             &g_client_len);
        if (n > 0) {
            uds_isotp_rx_callback(&ctx, pkt.id, pkt.data, pkt.len);
        }

        usleep(100);
    }

    return 0;
}
