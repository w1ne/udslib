/**
 * @file main.c
 * @brief Host Simulation Example for LibUDS
 * Determines basic connectivity and compilation.
 */

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
uint32_t get_time_ms(void) {
    return GetTickCount();
}
#else
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}
#endif

// Virtual CAN over UDP Packet Structure
#pragma pack(push, 1)
typedef struct {
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
} vcan_packet_t;
#pragma pack(pop)

static int server_fd = -1;
static struct sockaddr_in client_addr;
static socklen_t client_len = sizeof(client_addr);

void log_event(uint8_t level, const char* msg) {
    const char* lvl_str = level == UDS_LOG_ERROR ? "ERR" : (level == UDS_LOG_INFO ? "INF" : "DBG");
    printf("[%u] [%s] %s\n", get_time_ms(), lvl_str, msg);
}

// Mock CAN Send -> UDP
int mock_can_send(uint32_t id, const uint8_t* data, uint8_t len) {
    printf("[CAN TX] ID:%03X Data:", id);
    for(int i=0; i<len; i++) printf(" %02X", data[i]);
    printf("\n");

    if (server_fd != -1) {
        vcan_packet_t pkt;
        pkt.id = id;
        pkt.len = len;
        memcpy(pkt.data, data, len);
        sendto(server_fd, &pkt, sizeof(pkt), 0, (struct sockaddr*)&client_addr, client_len);
    }
    return 0;
}

// ISO-TP Externs now via header

// Buffers
static uint8_t rx_buf[1024];
static uint8_t tx_buf[1024];

int main(int argc, char** argv) {
    int port = 5000;
    if (argc > 1) port = atoi(argv[1]);

    printf("Starting UDS ECU Simulator (ISO-TP over UDP:%d)...\n", port);

    // Setup Socket
    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return -1;
    }

    // Init ISO-TP Layer (TX: 0x7E8, RX: 0x7E0)
    uds_tp_isotp_init(mock_can_send, 0x7E8, 0x7E0);

    uds_config_t cfg = {
        .ecu_address = 0x10,
        .get_time_ms = get_time_ms,
        .fn_log = log_event,
        .fn_tp_send = uds_isotp_send,
        .rx_buffer = rx_buf,
        .rx_buffer_size = sizeof(rx_buf),
        .tx_buffer = tx_buf,
        .tx_buffer_size = sizeof(tx_buf),
        .p2_ms = 50,       /* 50ms P2 timeout */
        .p2_star_ms = 2000 /* 2000ms P2* timeout */
    };

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    printf("Waiting for VCAN packets...\n");

    uint32_t slow_op_start = 0;

    while(1) {
        uint32_t now = get_time_ms();
        uds_process(&ctx);
        uds_tp_isotp_process();

        /* Detect when SID 0x31 becomes pending and start our internal mock "long operation" */
        if (ctx.p2_msg_pending && ctx.pending_sid == 0x31 && slow_op_start == 0) {
            slow_op_start = now;
            printf("[APP] Starting 1.5s operation for SID 0x31...\n");
        }

        /* After 1.5 seconds, send the positive response */
        if (slow_op_start > 0 && (now - slow_op_start) >= 1500) {
            printf("[APP] Operation complete! Sending 0x71 response.\n");
            ctx.config->tx_buffer[0] = 0x71; // 0x31 + 0x40
            ctx.config->tx_buffer[1] = 0x01; // Data
            uds_send_response(&ctx, 2);
            slow_op_start = 0;
        }

        // Non-blocking UDP Recv
        struct timeval tv = {0, 1000}; // 1ms timeout
        setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        vcan_packet_t pkt;
        ssize_t n = recvfrom(server_fd, &pkt, sizeof(pkt), 0, (struct sockaddr*)&client_addr, &client_len);
        if (n > 0) {
            uds_isotp_rx_callback(&ctx, pkt.id, pkt.data, pkt.len);
        }
        
        usleep(100); // Prevent 100% CPU
    }

    return 0;
}
