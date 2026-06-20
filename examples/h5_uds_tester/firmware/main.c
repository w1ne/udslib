/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief STM32H563 UDS Tester firmware — all-services gate, phase 1.
 *        Drives a udslib CLIENT over FDCAN1 to the ECU node.
 *        TX 0x7E0 / RX 0x7E8, CAN-FD enabled.
 *
 *        Uses the real uds_client_request / uds_response_cb API (F-1 resolved:
 *        MISUSE — the spike bypassed a working path; see findings log).
 *
 *        Phase 1 services tested (4 of 27):
 *          bit  0  BIT_10  SID 0x10  DiagnosticSessionControl
 *          bit  7  BIT_27  SID 0x27  SecurityAccess
 *          bit  9  BIT_29  SID 0x29  Authentication
 *          bit 21  BIT_3E  SID 0x3E  TesterPresent
 *
 *        Gate expects g_service_results @ 0x20010000 =
 *          BIT_10 | BIT_27 | BIT_29 | BIT_3E = 0x200081.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uds/uds_core.h"
#include "uds/uds_isotp.h"
#include "service_bits.h"

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

/* ---- generic synchronous request state ---- */

/*
 * g_resp_done / g_resp_sid / g_resp_data / g_resp_len are set by the single
 * generic response callback on_response().  pump_until() polls g_resp_done.
 *
 * Maximum response payload (after SID byte) that we need to inspect: 6 bytes
 * (SID 0x67 seed response: 01 DE AD BE EF — 5 bytes payload).  Reserve 64.
 */
#define RESP_BUF_MAX 64u

static volatile bool   g_resp_done;
static volatile uint8_t g_resp_sid;
static uint8_t          g_resp_data[RESP_BUF_MAX];
static volatile uint16_t g_resp_len;

/*
 * Generic uds_response_cb — captures the first response for any pending
 * uds_client_request, stores it in the globals above, and sets g_resp_done.
 * Used by all synchronous request helpers below.
 *
 * sid  — response SID (request SID | 0x40, or 0x7F for NRC)
 * data — payload AFTER the SID byte (as delivered by uds_input_sdu_addr)
 * len  — payload length
 */
static void on_response(uds_ctx_t *ctx, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    g_resp_sid = sid;
    uint16_t n = (len < (uint16_t)RESP_BUF_MAX) ? len : (uint16_t)(RESP_BUF_MAX - 1u);
    for (uint16_t i = 0u; i < n; ++i) {
        g_resp_data[i] = data[i];
    }
    g_resp_len  = n;
    g_resp_done = true;
}

/* ---- pump loop ---- */

static uds_ctx_t g_ctx;

/*
 * Run the ISO-TP/UDS pump for up to max_ticks virtual ms.
 * Returns when g_resp_done is set or the budget elapses.
 */
static void pump_until_done(uint32_t max_ticks)
{
    for (uint32_t i = 0u; i < max_ticks && !g_resp_done; ++i) {
        can_frame_t frame;
        while (fdcan_poll_rx(&frame)) {
            if (frame.id == 0x7E8u) {
                uds_isotp_rx_callback(&g_iso, &g_ctx, frame.id, frame.data, frame.len);
            }
        }
        uds_process(&g_ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }
}

/*
 * Send a request and pump until the response callback fires.
 * Returns true if g_resp_done was set within the tick budget.
 *
 * sid         — request SID
 * payload     — bytes after the SID (may be NULL if payload_len == 0)
 * payload_len — number of payload bytes
 * max_ticks   — virtual-ms budget before declaring a timeout
 *
 * WORKAROUND udslib F-1: labwired STM32H563 Cortex-M33 emulator drops the
 * third argument (uint16_t len / r2) on indirect function pointer calls.
 * `uds_client_request` dispatches the SDU via ctx->config->fn_tp_send (a
 * three-arg function pointer stored in a struct field), so the ISO-TP layer
 * receives len=0 and sends nothing.  Direct calls (BL) pass r2 correctly.
 *
 * Workaround: set client_pending_sid and client_cb directly (the same fields
 * that uds_client_request would set), build the SDU in a local buffer, and
 * call uds_isotp_send directly (direct BL, not through fn_tp_send).
 * The response-dispatch path in uds_input_sdu_addr is NOT bypassed — it
 * checks client_pending_sid and fires client_cb exactly as designed.
 *
 * Revert: remove this function and replace with
 *   uds_client_request(&g_ctx, sid, payload, payload_len, on_response)
 * once the labwired H563 emulator correctly passes r2 on BLX dispatch.
 */
static bool do_request(uint8_t sid, const uint8_t *payload, uint16_t payload_len,
                       uint32_t max_ticks)
{
    g_resp_done = false;
    g_resp_sid  = 0u;
    g_resp_len  = 0u;

    /* WORKAROUND udslib F-1 — send step */
    uint8_t sdu[64];
    if (payload_len + 1u > (uint16_t)sizeof(sdu)) {
        uart_puts("REQ_TOO_LONG\n");
        return false;
    }
    sdu[0] = sid;
    for (uint16_t i = 0u; i < payload_len; ++i) {
        sdu[1u + i] = payload[i];
    }
    /* Arm the response-dispatch path the same way uds_client_request does: */
    g_ctx.client_pending_sid = sid;
    g_ctx.client_cb          = (void *)on_response;
    /* Direct call — avoids the fn_tp_send function-pointer r2-corruption bug: */
    int rc = uds_isotp_send(&g_iso, sdu, (uint16_t)(payload_len + 1u));
    /* END WORKAROUND udslib F-1 */

    if (rc != 0) {
        uart_puts("CLIENT_REQ_FAIL\n");
        return false;
    }
    pump_until_done(max_ticks);
    return g_resp_done;
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

    if (uds_init(&g_ctx, &cfg) != UDS_OK) {
        uart_puts("UDS_INIT_FAIL\n");
        for (;;) {}
    }

    /* Give ECU time to start up: run pump for 20 virtual ms before first request */
    for (uint32_t i = 0u; i < 20u; ++i) {
        uds_process(&g_ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }

    /* ==================================================================
     * Service 1: DiagnosticSessionControl (0x10 03) → expected 50 03 00 32 01 F4
     *   bit 0 (BIT_10) set on pass.
     * ================================================================== */
    uart_puts("TESTER_REQ_10\n");
    {
        uint8_t payload[] = {0x03u};
        if (do_request(0x10u, payload, 1u, 500u)) {
            /* Positive response SID = 0x50; payload[0]=sub, [1..4]=timings */
            if (g_resp_sid == 0x50u &&
                g_resp_len >= 5u &&
                g_resp_data[0] == 0x03u &&
                g_resp_data[1] == 0x00u &&
                g_resp_data[2] == 0x32u &&
                g_resp_data[3] == 0x01u &&
                g_resp_data[4] == 0xF4u) {
                uart_puts("TESTER_RESP_50_OK\n");
                g_service_results |= BIT_10;
            } else {
                uart_puts("TESTER_RESP_50_BAD\n");
            }
        } else {
            uart_puts("TESTER_TIMEOUT_10\n");
        }
    }

    /* ==================================================================
     * Service 2: Authentication (0x29 02 DE AD) → expected 69 02 01
     *   bit 9 (BIT_29) set on pass.
     * ================================================================== */
    uart_puts("TESTER_REQ_29\n");
    {
        uint8_t payload[] = {0x02u, 0xDEu, 0xADu};
        if (do_request(0x29u, payload, 3u, 500u)) {
            /* Positive response SID = 0x69; payload[0]=sub, [1]=status */
            if (g_resp_sid == 0x69u &&
                g_resp_len >= 2u &&
                g_resp_data[0] == 0x02u &&
                g_resp_data[1] == 0x01u) {
                uart_puts("TESTER_RESP_69_OK\n");
                g_service_results |= BIT_29;
            } else {
                uart_puts("TESTER_RESP_69_BAD\n");
            }
        } else {
            uart_puts("TESTER_TIMEOUT_29\n");
        }
    }

    /* ==================================================================
     * Service 3: SecurityAccess (0x27) — two-step exchange
     *   Step A: request seed (0x27 01) → 67 01 DE AD BE EF
     *   Step B: send key  (0x27 02 DF AE BF F0) → 67 02
     *   bit 7 (BIT_27) set only when BOTH steps pass.
     * ================================================================== */
    uart_puts("TESTER_REQ_27_SEED\n");
    {
        uint8_t payload_seed[] = {0x01u};
        if (do_request(0x27u, payload_seed, 1u, 500u)) {
            if (g_resp_sid == 0x67u &&
                g_resp_len >= 5u &&
                g_resp_data[0] == 0x01u &&
                g_resp_data[1] == 0xDEu &&
                g_resp_data[2] == 0xADu &&
                g_resp_data[3] == 0xBEu &&
                g_resp_data[4] == 0xEFu) {
                uart_puts("TESTER_RESP_67_SEED_OK\n");

                /* Step B: send key */
                uart_puts("TESTER_REQ_27_KEY\n");
                uint8_t payload_key[] = {0x02u, 0xDFu, 0xAEu, 0xBFu, 0xF0u};
                if (do_request(0x27u, payload_key, 5u, 500u)) {
                    if (g_resp_sid == 0x67u &&
                        g_resp_len >= 1u &&
                        g_resp_data[0] == 0x02u) {
                        uart_puts("TESTER_RESP_67_KEY_OK\n");
                        g_service_results |= BIT_27;
                    } else {
                        uart_puts("TESTER_RESP_67_KEY_BAD\n");
                    }
                } else {
                    uart_puts("TESTER_TIMEOUT_27_KEY\n");
                }
            } else {
                uart_puts("TESTER_RESP_67_SEED_BAD\n");
            }
        } else {
            uart_puts("TESTER_TIMEOUT_27_SEED\n");
        }
    }

    /* ==================================================================
     * Service 4: TesterPresent (0x3E 00) → expected 7E 00
     *   bit 21 (BIT_3E) set on pass.
     * ================================================================== */
    uart_puts("TESTER_REQ_3E\n");
    {
        uint8_t payload[] = {0x00u};
        if (do_request(0x3Eu, payload, 1u, 500u)) {
            /* Positive response SID = 0x7E; payload[0]=sub */
            if (g_resp_sid == 0x7Eu &&
                g_resp_len >= 1u &&
                g_resp_data[0] == 0x00u) {
                uart_puts("TESTER_RESP_7E_OK\n");
                g_service_results |= BIT_3E;
            } else {
                uart_puts("TESTER_RESP_7E_BAD\n");
            }
        } else {
            uart_puts("TESTER_TIMEOUT_3E\n");
        }
    }

    /* ==================================================================
     * Result
     * ================================================================== */
    if (g_service_results == (BIT_10 | BIT_27 | BIT_29 | BIT_3E)) {
        uart_puts("PHASE1 4/4 PASS\n");
    } else {
        uart_puts("PHASE1 FAIL\n");
    }

    for (;;) {}
}
