/**
 * @file main.c
 * @brief UDS Client Demo
 */

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
} vcan_packet_t;
#pragma pack(pop)

static int sock_fd = -1;
static struct sockaddr_in server_addr;

uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

int mock_can_send(uint32_t id, const uint8_t* data, uint8_t len) {
    vcan_packet_t pkt;
    pkt.id = id;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    sendto(sock_fd, &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    return 0;
}

void on_response(uds_ctx_t* ctx, uint8_t sid, const uint8_t* data, uint16_t len) {
    (void)ctx;
    printf("[CLIENT] Response Received: SID=%02X, Len=%d\n", sid, len);
    printf("[CLIENT] Data:");
    for(int i=0; i<len; i++) printf(" %02X", data[i]);
    printf("\n");

    if (sid == 0x50) printf("[CLIENT] Session changed OK\n");
    if (sid == 0x62) printf("[CLIENT] Read Data OK\n");
}

int main(int argc, char** argv) {
    const char* target_ip = "127.0.0.1";
    int port = 5000;
    if (argc > 1) target_ip = argv[1];
    if (argc > 2) port = atoi(argv[2]);

    printf("UDS Client starting (Target: %s:%d)\n", target_ip, port);

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, target_ip, &server_addr.sin_addr);

    // TX: 0x7E0, RX: 0x7E8
    uds_tp_isotp_init(mock_can_send, 0x7E0, 0x7E8);

    uint8_t rx_buf[1024], tx_buf[1024];
    uds_config_t cfg = {
        .get_time_ms = get_time_ms,
        .fn_tp_send = uds_isotp_send,
        .rx_buffer = rx_buf,
        .rx_buffer_size = sizeof(rx_buf),
        .tx_buffer = tx_buf,
        .tx_buffer_size = sizeof(tx_buf),
        .fn_log = NULL
    };

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    // 1. Send Request
    printf("[CLIENT] Sending DiagnosticSessionControl (Extended)...\n");
    uint8_t sub = 0x03;
    uds_client_request(&ctx, 0x10, &sub, 1, on_response);

    // 2. Loop until response or timeout
    uint32_t start = get_time_ms();
    while(get_time_ms() - start < 1000) {
        uds_process(&ctx);
        uds_tp_isotp_process();

        // Non-blocking recv
        struct timeval tv = {0, 1000};
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        vcan_packet_t pkt;
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        if (recvfrom(sock_fd, &pkt, sizeof(pkt), 0, (struct sockaddr*)&from, &flen) > 0) {
            uds_isotp_rx_callback(&ctx, pkt.id, pkt.data, pkt.len);
        }
    }

    // 3. Send Another Request
    printf("\n[CLIENT] Sending ReadDataByIdentifier (VIN)...\n");
    uint8_t did[] = {0xF1, 0x90};
    uds_client_request(&ctx, 0x22, did, 2, on_response);

    start = get_time_ms();
    while(get_time_ms() - start < 1000) {
        uds_process(&ctx);
        uds_tp_isotp_process();
        
        vcan_packet_t pkt;
        if (recv(sock_fd, &pkt, sizeof(pkt), MSG_DONTWAIT) > 0) {
            uds_isotp_rx_callback(&ctx, pkt.id, pkt.data, pkt.len);
        }
        usleep(100);
    }

    return 0;
}
