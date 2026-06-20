/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief STM32H563 UDS Tester firmware for the all-services gate (spike: 2 services).
 *        Drives a udslib CLIENT over FDCAN1 to the ECU node.
 *        TX 0x7E0 / RX 0x7E8, CAN-FD enabled.
 *
 *        Records per-service pass bits in g_service_results at 0x20010000:
 *          bit 0  (0x01) = SID 0x10 DiagnosticSessionControl passed
 *          bit 4  (0x10) = SID 0x22 ReadDataByIdentifier passed
 *        Gate expects 0x11 when both pass.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"

/* ---- freestanding mem helpers ---- */

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n-- > 0u) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    while (n-- > 0u) {
        *d++ = (uint8_t) value;
    }
    return dst;
}

int memcmp(const void *lhs, const void *rhs, size_t n)
{
    const uint8_t *a = (const uint8_t *) lhs;
    const uint8_t *b = (const uint8_t *) rhs;
    while (n-- > 0u) {
        if (*a != *b) {
            return (int) *a - (int) *b;
        }
        ++a;
        ++b;
    }
    return 0;
}

void *__aeabi_memcpy(void *dst, const void *src, size_t n)  { return memcpy(dst, src, n); }
void *__aeabi_memcpy4(void *dst, const void *src, size_t n) { return memcpy(dst, src, n); }
void *__aeabi_memcpy8(void *dst, const void *src, size_t n) { return memcpy(dst, src, n); }
void *__aeabi_memset(void *dst, size_t n, int value)        { return memset(dst, value, n); }
void *__aeabi_memclr(void *dst, size_t n)                   { return memset(dst, 0, n); }
void *__aeabi_memclr4(void *dst, size_t n)                  { return memset(dst, 0, n); }
void *__aeabi_memclr8(void *dst, size_t n)                  { return memset(dst, 0, n); }

/* ---- result marker at fixed address ---- */

volatile uint32_t g_service_results __attribute__((section(".uds_result"), used));

/* ---- register map ---- */

#define REG32(addr) (*(volatile uint32_t *) (addr))

#define USART3_BASE   0x40004800u
#define USART3_CR1    REG32(USART3_BASE + 0x00u)
#define USART3_ISR    REG32(USART3_BASE + 0x1Cu)
#define USART3_TDR    REG32(USART3_BASE + 0x28u)
#define USART_ISR_TXE (1u << 7)
#define USART_CR1_UE  (1u << 0)
#define USART_CR1_TE  (1u << 3)

#define FDCAN1_BASE     0x4000A400u
#define FDCAN_REG_TEST  0x010u
#define FDCAN_REG_CCCR  0x018u
#define FDCAN_REG_IR    0x050u
#define FDCAN_REG_RXF0S 0x090u
#define FDCAN_REG_RXF0A 0x094u
#define FDCAN_REG_TXBAR 0x0CCu

#define FDCAN_RAM_BASE   0x800u
#define FDCAN_RXF0_ELEM0 0x0B0u
#define FDCAN_TXBUF0     0x278u

#define CCCR_INIT (1u << 0)
#define CCCR_CCE  (1u << 1)
#define TX_T1_BRS (1u << 20)
#define TX_T1_FDF (1u << 21)

typedef struct {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[64];
    bool     fd;
} can_frame_t;

/* ---- UART helpers ---- */

static void uart_init(void)
{
    USART3_CR1 = USART_CR1_UE | USART_CR1_TE;
}

static void uart_putc(char c)
{
    while ((USART3_ISR & USART_ISR_TXE) == 0u) {}
    USART3_TDR = (uint32_t)(uint8_t) c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

/* ---- FDCAN helpers ---- */

static uint32_t fdcan_reg(uint32_t off) { return FDCAN1_BASE + off; }
static uint32_t fdcan_ram(uint32_t off) { return FDCAN1_BASE + FDCAN_RAM_BASE + off; }

static uint8_t len_to_dlc(uint8_t len)
{
    if (len <= 8u)  return len;
    if (len <= 12u) return 9u;
    if (len <= 16u) return 10u;
    if (len <= 20u) return 11u;
    if (len <= 24u) return 12u;
    if (len <= 32u) return 13u;
    if (len <= 48u) return 14u;
    return 15u;
}

static uint8_t dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return map[dlc & 0x0Fu];
}

static void write_payload(uint32_t addr, const uint8_t *data, uint8_t len)
{
    for (uint32_t i = 0; i < 16u; ++i) REG32(addr + i * 4u) = 0u;
    for (uint8_t i = 0; i < len; ++i) {
        uint32_t wa = addr + ((uint32_t)i / 4u) * 4u;
        uint32_t sh = ((uint32_t)i % 4u) * 8u;
        REG32(wa) = REG32(wa) | ((uint32_t)data[i] << sh);
    }
}

static void read_payload(uint32_t addr, uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; ++i) {
        uint32_t word = REG32(addr + ((uint32_t)i / 4u) * 4u);
        data[i] = (uint8_t)((word >> (((uint32_t)i % 4u) * 8u)) & 0xFFu);
    }
}

static void fdcan_start(void)
{
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = CCCR_INIT | CCCR_CCE;
    REG32(fdcan_reg(FDCAN_REG_TEST)) = 0u;
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = 0u;
    while ((REG32(fdcan_reg(FDCAN_REG_CCCR)) & CCCR_INIT) != 0u) {}
}

static int fdcan_send_frame(uint32_t id, const uint8_t *data, uint8_t len, bool fd)
{
    if (id > 0x7FFu || len > 64u) return -1;
    uint32_t base = fdcan_ram(FDCAN_TXBUF0);
    REG32(base + 0u) = (id & 0x7FFu) << 18u;
    REG32(base + 4u) = ((uint32_t)len_to_dlc(len) << 16u) | (fd ? (TX_T1_FDF | TX_T1_BRS) : 0u);
    write_payload(base + 8u, data, len);
    REG32(fdcan_reg(FDCAN_REG_TXBAR)) = 1u;
    return 0;
}

static bool fdcan_poll_rx(can_frame_t *frame)
{
    uint32_t rxf0s = REG32(fdcan_reg(FDCAN_REG_RXF0S));
    if ((rxf0s & 0x7Fu) == 0u) return false;
    uint32_t gi   = (rxf0s >> 8u) & 0x3Fu;
    uint32_t base = fdcan_ram(FDCAN_RXF0_ELEM0 + gi * 72u);
    uint32_t r0   = REG32(base + 0u);
    uint32_t r1   = REG32(base + 4u);
    frame->id  = (r0 >> 18u) & 0x7FFu;
    frame->len = dlc_to_len((uint8_t)((r1 >> 16u) & 0x0Fu));
    frame->fd  = (r1 & TX_T1_FDF) != 0u;
    read_payload(base + 8u, frame->data, frame->len);
    REG32(fdcan_reg(FDCAN_REG_RXF0A)) = gi;
    REG32(fdcan_reg(FDCAN_REG_IR))    = REG32(fdcan_reg(FDCAN_REG_IR));
    return true;
}

/* ---- udslib glue ---- */

static volatile uint32_t g_now_ms;

static uds_isotp_ctx_t g_iso;
static uint8_t         g_iso_tx_sdu[256];
static uint8_t         g_rx_buf[256];
static uint8_t         g_tx_buf[256];

static uint32_t get_time_ms(void) { return g_now_ms; }

static int can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    return fdcan_send_frame(id, data, len, len > 8u);
}

static int isotp_send_adapter(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    return uds_isotp_send(&g_iso, data, len);
}

/* ---- response state ---- */

static volatile bool g_resp_0x10_done;
static volatile bool g_resp_0x22_done;

/*
 * Response callback.
 * SID in the response is the *positive* response SID (request SID | 0x40).
 * 0x10 request → 0x50 response
 * 0x22 request → 0x62 response
 *
 * Expected bytes:
 *   0x50 03 00 32 01 F4          (DiagnosticSessionControl extended, timing params)
 *   0x62 F1 90 <VIN bytes...>    (ReadDataByIdentifier)
 */
static void on_response(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    (void)data;
    (void)len;

    if (sid == 0x50u) {
        /* 0x10 extended session positive response */
        uart_puts("TESTER_RESP_50\n");
        /* bit 0 = SID 0x10 pass */
        g_service_results |= (1u << 0);
        g_resp_0x10_done = true;
    } else if (sid == 0x62u) {
        /* 0x22 ReadDataByIdentifier positive response — verify DID bytes */
        if (len >= 3u && data[0] == 0xF1u && data[1] == 0x90u) {
            uart_puts("TESTER_RESP_62_F190\n");
            /* bit 4 = SID 0x22 pass */
            g_service_results |= (1u << 4);
        } else {
            uart_puts("TESTER_RESP_62_BAD\n");
        }
        g_resp_0x22_done = true;
    }
}

/* ---- pump loop helpers ---- */

/*
 * Run the ISO-TP/UDS pump for up to max_ticks iterations, returning when
 * *done is set to true or the tick budget is exhausted.
 */
static void pump_until(uds_ctx_t *ctx, volatile bool *done, uint32_t max_ticks)
{
    for (uint32_t i = 0; i < max_ticks && !(*done); ++i) {
        can_frame_t frame;
        while (fdcan_poll_rx(&frame)) {
            if (frame.id == 0x7E8u) {
                uds_isotp_rx_callback(&g_iso, ctx, frame.id, frame.data, frame.len);
            }
        }
        uds_process(ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }
}

/* ---- main ---- */

int main(void)
{
    g_service_results = 0u;

    uart_init();
    uart_puts("H5-UDS-TESTER\n");

    fdcan_start();

    /* Tester TX 0x7E0 → ECU RX 0x7E0; ECU responds on 0x7E8 → Tester RX 0x7E8 */
    uds_tp_isotp_init(&g_iso, can_send, 0x7E0u, 0x7E8u, g_iso_tx_sdu, sizeof(g_iso_tx_sdu));
    uds_tp_isotp_set_fd(&g_iso, true);

    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms    = get_time_ms;
    cfg.fn_tp_send     = isotp_send_adapter;
    cfg.p2_ms          = 50u;
    cfg.p2_star_ms     = 2000u;
    cfg.rx_buffer      = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer      = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);

    uds_ctx_t ctx;
    if (uds_init(&ctx, &cfg) != UDS_OK) {
        uart_puts("UDS_INIT_FAIL\n");
        for (;;) {}
    }

    /* Give ECU time to start up: advance the virtual clock */
    for (uint32_t i = 0; i < 20u; ++i) {
        uds_process(&ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }

    /* --- Service 1: DiagnosticSessionControl (0x10, extended=0x03) --- */
    uart_puts("TESTER_REQ_10\n");
    {
        uint8_t req[] = {0x10u, 0x03u};
        /* Bypass uds_client_request and call uds_isotp_send directly */
        ctx.client_pending_sid  = 0x10u;
        ctx.client_cb           = (void *)on_response;
        int rc = uds_isotp_send(&g_iso, req, (uint16_t)sizeof(req));
        uart_puts(rc == 0 ? "ISOTP_SEND_OK\n" : "ISOTP_SEND_FAIL\n");
    }
    pump_until(&ctx, &g_resp_0x10_done, 500u);

    if (!g_resp_0x10_done) {
        uart_puts("TESTER_TIMEOUT_10\n");
    }

    /* --- Service 2: ReadDataByIdentifier (0x22, DID F190) --- */
    uart_puts("TESTER_REQ_22\n");
    {
        uint8_t req[] = {0x22u, 0xF1u, 0x90u};
        ctx.client_pending_sid  = 0x22u;
        ctx.client_cb           = (void *)on_response;
        int rc = uds_isotp_send(&g_iso, req, (uint16_t)sizeof(req));
        uart_puts(rc == 0 ? "ISOTP_SEND_OK\n" : "ISOTP_SEND_FAIL\n");
    }
    pump_until(&ctx, &g_resp_0x22_done, 500u);

    if (!g_resp_0x22_done) {
        uart_puts("TESTER_TIMEOUT_22\n");
    }

    /* Report result */
    if (g_service_results == 0x11u) {
        uart_puts("SERVICES 2/2 PASS\n");
    } else {
        uart_puts("SERVICES FAIL\n");
    }

    for (;;) {}
}
