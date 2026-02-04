/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief Host Simulation Example for UDSLib.
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
    return (uint32_t) ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}
#endif

/* --- Virtual CAN over UDP --- */

#pragma pack(push, 1)
/**
 * @brief Virtual CAN packet structure.
 */
typedef struct
{
    uint32_t id;      /**< CAN ID */
    uint8_t data[64]; /**< Payload (Max 64 for CAN-FD) */
    uint8_t len;      /**< Length */
} vcan_packet_t;
#pragma pack(pop)

/** Server socket descriptor */
static int g_server_fd = -1;
/** Client address for UDP response */
static struct sockaddr_in g_client_addr;
/** Length of the client address */
static socklen_t g_client_len = sizeof(g_client_addr);

/* Mock Memory (1KB) */
static uint8_t mock_memory[1024];

static int fn_mem_read(uds_ctx_t *ctx, uint32_t addr, uint32_t size, uint8_t *out_buf)
{
    (void) ctx;
    if (addr + size > sizeof(mock_memory)) {
        return -0x31; /* RequestOutOfRange */
    }
    memcpy(out_buf, &mock_memory[addr], size);
    return 0;
}

static int fn_mem_write(uds_ctx_t *ctx, uint32_t addr, uint32_t size, const uint8_t *data)
{
    (void) ctx;
    if (addr + size > sizeof(mock_memory)) {
        return -0x31; /* RequestOutOfRange */
    }
    memcpy(&mock_memory[addr], data, size);
    return 0;
}

/**
 * @brief Example event logger callback.
 *
 * @param level Log severity level.
 * @param msg   Null-terminated message string.
 */
static void log_event(uint8_t level, const char *msg)
{
    const char *lvl_str =
        (level == UDS_LOG_ERROR) ? "ERR" : ((level == UDS_LOG_INFO) ? "INF" : "DBG");
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
        sendto(g_server_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *) &g_client_addr, g_client_len);
    }
    return 0;
}

/* --- Simulation State --- */

/** Receive buffer (1KB) */
static uint8_t g_rx_buf[1024];
/** Transmit buffer (1KB) */
static uint8_t g_tx_buf[1024];

static char g_ecu_vin[] = "UDSLIB_SIM_001";
static char g_customer_name[16] = "ECU_OWNER";

/**
 * @brief Example DID table setup.
 */
static const uds_did_entry_t g_ecu_dids[] = {
    {0xF190, 14, 0, 0, NULL, NULL, g_ecu_vin},       /* VIN (Direct storage) */
    {0x0123, 16, 0, 0, NULL, NULL, g_customer_name}, /* Customer Name (Read/Write) */
};

static const uds_did_table_t g_ecu_did_table = {.entries = g_ecu_dids, .count = 2};

/**
 * @brief Mock ECU Reset implementation.
 */
static void mock_reset(uds_ctx_t *ctx, uint8_t type)
{
    (void) ctx;
    const char *type_str = (type == UDS_RESET_HARD) ? "HARD" : "SOFT";
    printf("[APP] ECU RESET TRIGGERED: Type %s (0x%02X)\n", type_str, type);
}

static int mock_dtc_read(struct uds_ctx *ctx, uint8_t subfn, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    printf("[APP] DTC READ: Subfunction 0x%02X\n", subfn);
    if (subfn == 0x01) {   /* count of DTCs matching status mask */
        out_buf[0] = 0x01; /* availability mask */
        out_buf[1] = 0x01; /* status availability */
        out_buf[2] = 0x00; /* count MSB */
        out_buf[3] = 0x02; /* count LSB (2 DTCs) */
        return 4;
    }
    return -0x31;
}

static int mock_dtc_clear(struct uds_ctx *ctx, uint32_t group)
{
    (void) ctx;
    printf("[APP] DTC CLEAR: Group 0x%06X\n", group);
    return UDS_OK;
}

static int mock_security_seed(struct uds_ctx *ctx, uint8_t level, uint8_t *seed_buf,
                              uint16_t max_len)
{
    (void) ctx;
    (void) level;
    if (max_len < 4u) return -0x22; /* ConditionsNotCorrect */
    printf("[APP] SECURITY SEED (level %u)\n", level);
    seed_buf[0] = 0xDE;
    seed_buf[1] = 0xAD;
    seed_buf[2] = 0xBE;
    seed_buf[3] = 0xEF;
    return 4;
}

static int mock_security_key(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed,
                             const uint8_t *key, uint16_t key_len)
{
    (void) ctx;
    (void) level;
    (void) seed;
    (void) key;
    (void) key_len;
    printf("[APP] SECURITY KEY ACCEPTED (level %u)\n", level);
    return UDS_OK;
}

static int mock_auth(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *data, uint16_t len,
                     uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) data;
    (void) len;
    (void) max_len;
    printf("[APP] AUTH: Subfunction 0x%02X\n", subfn);
    if (subfn == 0x01) return 0; /* deAuthenticate success */
    if (subfn == 0x02) {         /* verifyCertificateUnidirectional */
        out_buf[0] = 0x01;       /* Evaluation Status: Valid */
        return 1;
    }
    return -0x22;
}

static int mock_routine_control(struct uds_ctx *ctx, uint8_t type, uint16_t id, const uint8_t *data,
                                uint16_t len, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) data;
    (void) len;
    (void) max_len;
    printf("[APP] ROUTINE CONTROL: Type 0x%02X ID 0x%04X\n", type, id);
    if (id == 0xFF00) {    /* Erase Memory */
        out_buf[0] = 0x00; /* Success */
        return 1;
    }
    return -0x31;
}

static int mock_request_download(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    printf("[APP] REQUEST DOWNLOAD: Addr 0x%08X Size 0x%08X\n", addr, size);
    return UDS_OK;
}

static int mock_transfer_data(struct uds_ctx *ctx, uint8_t sequence, const uint8_t *data,
                              uint16_t len)
{
    (void) ctx;
    (void) data;
    (void) len;
    printf("[APP] TRANSFER DATA: Seq 0x%02X Len %u\n", sequence, len);
    return UDS_OK;
}

static int mock_transfer_exit(struct uds_ctx *ctx)
{
    (void) ctx;
    printf("[APP] TRANSFER EXIT\n");
    return UDS_OK;
}

static int mock_io_control(struct uds_ctx *ctx, uint16_t id, uint8_t type, const uint8_t *data,
                           uint16_t len, uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) data;
    (void) len;
    (void) max_len;
    printf("[APP] IO CONTROL: ID 0x%04X Type 0x%02X\n", id, type);
    if (id == 0x0123) {
        /* Success, echo some state */
        out_buf[0] = 0x55;
        return 1;
    }
    return -0x31;
}

static int mock_request_upload(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void) ctx;
    printf("[APP] REQUEST UPLOAD: Addr 0x%08X Size 0x%08X\n", addr, size);
    return UDS_OK;
}

static int mock_periodic_read(struct uds_ctx *ctx, uint8_t periodic_id, uint8_t *out_buf,
                              uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    printf("[APP] PERIODIC READ: ID 0x%02X\n", periodic_id);
    out_buf[0] = 0xAA;
    out_buf[1] = 0xBB;
    return 2;
}

/**
 * @brief Main Entry Point for ECU Simulator.
 */
int main(int argc, char **argv)
{
    int port = 5000;
    int enable_fd = 1; /* Default to CAN-FD */

    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (argc > 2) {
        enable_fd = atoi(argv[2]);
    }

    printf("Starting UDS ECU Simulator (ISO-TP over UDP:%d) [CAN-FD: %d]...\n", port, enable_fd);

    /* Setup UDP Socket */
    g_server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(g_server_fd, (const struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return -1;
    }

    /* Init Transport Layer (TX: 0x7E8, RX: 0x7E0) */
    uds_tp_isotp_init(mock_can_send, 0x7E8, 0x7E0);
    uds_tp_isotp_set_fd(enable_fd != 0);

    /* Configure UDS Stack */
    uds_config_t cfg = {.ecu_address = 0x10,
                        .get_time_ms = get_time_ms,
                        .fn_log = log_event,
                        .fn_tp_send = uds_isotp_send,
                        .fn_reset = mock_reset,
                        .fn_dtc_read = mock_dtc_read,
                        .fn_dtc_clear = mock_dtc_clear,
                        .fn_security_seed = mock_security_seed,
                        .fn_security_key = mock_security_key,
                        .fn_auth = mock_auth,
                        .fn_routine_control = mock_routine_control,
                        .fn_request_download = mock_request_download,
                        .fn_transfer_data = mock_transfer_data,
                        .fn_transfer_exit = mock_transfer_exit,
                        .fn_mem_read = fn_mem_read,
                        .fn_mem_write = fn_mem_write,
                        .fn_io_control = mock_io_control,
                        .fn_request_upload = mock_request_upload,
                        .fn_periodic_read = mock_periodic_read,

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
        uds_tp_isotp_process(now);

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
        ssize_t n = recvfrom(g_server_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *) &g_client_addr,
                             &g_client_len);
        if (n > 0) {
            uds_isotp_rx_callback(&ctx, pkt.id, pkt.data, pkt.len);
        }

        usleep(100);
    }

    return 0;
}
