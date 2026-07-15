/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief STM32H563 UDS Tester firmware — all-services gate.
 *        Drives a udslib CLIENT over FDCAN1 to the ECU node.
 *        TX 0x7E0 / RX 0x7E8, CAN-FD enabled.
 *
 *        udslib is used unmodified: do_request() calls the public
 *        uds_client_request() and pumps the ISO-TP/UDS engine until the response
 *        callback fires. No firmware workarounds remain — the earlier ones
 *        (recorded as F-1/F-8) were mis-attributions of three labwired STM32H563
 *        Cortex-M33 *decoder* bugs (LDRB.W/LDRH.W signedness = F-9, missing
 *        UXTH.W = F-11, missing UXTAH = F-10), all fixed in labwired-core and
 *        released in v0.17.3. See docs/superpowers/udslib-findings-from-h5-gate.md.
 *
 *        Phase 1 services tested (4 of 27):
 *          bit  0  BIT_10  SID 0x10  DiagnosticSessionControl
 *          bit  7  BIT_27  SID 0x27  SecurityAccess
 *          bit  9  BIT_29  SID 0x29  Authentication
 *          bit 21  BIT_3E  SID 0x3E  TesterPresent
 *
 *        Phase 2 services tested (8 of 27):
 *          bit  4  BIT_22  SID 0x22  ReadDataByIdentifier       (DID 0xF190)
 *          bit  5  BIT_23  SID 0x23  ReadMemoryByAddress        (ALFID 0x12)
 *          bit  6  BIT_24  SID 0x24  ReadScalingDataByIdentifier (DID 0xF190)
 *          bit 10  BIT_2A  SID 0x2A  ReadDataByPeriodicIdentifier (setup only)
 *          bit 11  BIT_2C  SID 0x2C  DynamicallyDefineDataIdentifier
 *          bit 12  BIT_2E  SID 0x2E  WriteDataByIdentifier      (DID 0x0123)
 *          bit 13  BIT_2F  SID 0x2F  InputOutputControlByIdentifier (DID 0x0123)
 *          bit 20  BIT_3D  SID 0x3D  WriteMemoryByAddress       (ALFID 0x12)
 *
 *        Phase 3 services tested (4 of 27 — DTC subset):
 *          bit  3  BIT_19  SID 0x19  ReadDTCInformation
 *          bit 24  BIT_85  SID 0x85  ControlDTCSetting       (F-9 fix)
 *          bit  2  BIT_14  SID 0x14  ClearDiagnosticInformation
 *          bit 25  BIT_86  SID 0x86  ResponseOnEvent (setup) (F-9 fix)
 *
 *        Phase 4 services tested (6 of 27 — transfer subset):
 *          bit 14  BIT_31  SID 0x31  RoutineControl          (F-11 fix)
 *          bit 15  BIT_34  SID 0x34  RequestDownload
 *          bit 17  BIT_36  SID 0x36  TransferData
 *          bit 18  BIT_37  SID 0x37  RequestTransferExit
 *          bit 16  BIT_35  SID 0x35  RequestUpload
 *          bit 19  BIT_38  SID 0x38  RequestFileTransfer     (F-10 fix)
 *
 *        Phase 5 services tested (5 of 27):
 *          bit  8  BIT_28  SID 0x28  CommunicationControl
 *          bit 22  BIT_83  SID 0x83  AccessTimingParameter
 *          bit 26  BIT_87  SID 0x87  LinkControl
 *          bit 23  BIT_84  SID 0x84  SecuredDataTransmission (identity crypto)
 *          bit  1  BIT_11  SID 0x11  EcuReset (tested LAST)
 *
 *        Gate expects g_service_results @ 0x20010000 = 0x07FFFFFF  (27/27).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uds/uds_core.h"
#include "uds/uds_client.h"
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

void *__aeabi_memcpy(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}
void *__aeabi_memcpy4(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}
void *__aeabi_memcpy8(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}
void *__aeabi_memset(void *dst, size_t n, int value)
{
    return memset(dst, value, n);
}
void *__aeabi_memclr(void *dst, size_t n)
{
    return memset(dst, 0, n);
}
void *__aeabi_memclr4(void *dst, size_t n)
{
    return memset(dst, 0, n);
}
void *__aeabi_memclr8(void *dst, size_t n)
{
    return memset(dst, 0, n);
}

/* ---- result marker at fixed address ---- */

volatile uint32_t g_service_results __attribute__((section(".uds_result"), used));

/* ---- register map ---- */

#define REG32(addr) (*(volatile uint32_t *) (addr))

#define USART3_BASE 0x40004800u
#define USART3_CR1 REG32(USART3_BASE + 0x00u)
#define USART3_ISR REG32(USART3_BASE + 0x1Cu)
#define USART3_TDR REG32(USART3_BASE + 0x28u)
#define USART_ISR_TXE (1u << 7)
#define USART_CR1_UE (1u << 0)
#define USART_CR1_TE (1u << 3)

#define RCC_BASE 0x44020C00u
#define RCC_APB1HENR REG32(RCC_BASE + 0x0A0u)
#define RCC_APB1HENR_FDCAN1EN (1u << 9)

#define FDCAN1_BASE 0x4000A400u
#define FDCAN_REG_TEST 0x010u
#define FDCAN_REG_CCCR 0x018u
#define FDCAN_REG_IR 0x050u
#define FDCAN_REG_RXF0S 0x090u
#define FDCAN_REG_RXF0A 0x094u
#define FDCAN_REG_TXBAR 0x0CCu

#define FDCAN_RAM_BASE 0x800u
#define FDCAN_RXF0_ELEM0 0x0B0u
#define FDCAN_TXBUF0 0x278u

#define CCCR_INIT (1u << 0)
#define CCCR_CCE (1u << 1)
#define TX_T1_BRS (1u << 20)
#define TX_T1_FDF (1u << 21)

typedef struct
{
    uint32_t id;
    uint8_t len;
    uint8_t data[64];
    bool fd;
} can_frame_t;

/* ---- UART helpers ---- */

static void uart_init(void)
{
    USART3_CR1 = USART_CR1_UE | USART_CR1_TE;
}

static void uart_putc(char c)
{
    while ((USART3_ISR & USART_ISR_TXE) == 0u) {
    }
    USART3_TDR = (uint32_t) (uint8_t) c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

/* Hex nibble table (.rodata const). */
static const char g_hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                               '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

static void uart_puthex8(uint8_t v)
{
    uart_putc(g_hex[(v >> 4u) & 0x0Fu]);
    uart_putc(g_hex[v & 0x0Fu]);
}

/* ---- FDCAN helpers ---- */

static uint32_t fdcan_reg(uint32_t off)
{
    return FDCAN1_BASE + off;
}
static uint32_t fdcan_ram(uint32_t off)
{
    return FDCAN1_BASE + FDCAN_RAM_BASE + off;
}

static uint8_t len_to_dlc(uint8_t len)
{
    if (len <= 8u) return len;
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
    static const uint8_t map[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    return map[dlc & 0x0Fu];
}

static void write_payload(uint32_t addr, const uint8_t *data, uint8_t len)
{
    for (uint32_t i = 0; i < 16u; ++i) REG32(addr + i * 4u) = 0u;
    for (uint8_t i = 0; i < len; ++i) {
        uint32_t wa = addr + ((uint32_t) i / 4u) * 4u;
        uint32_t sh = ((uint32_t) i % 4u) * 8u;
        REG32(wa) = REG32(wa) | ((uint32_t) data[i] << sh);
    }
}

static void read_payload(uint32_t addr, uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; ++i) {
        uint32_t word = REG32(addr + ((uint32_t) i / 4u) * 4u);
        data[i] = (uint8_t) ((word >> (((uint32_t) i % 4u) * 8u)) & 0xFFu);
    }
}

static void fdcan_start(void)
{
    RCC_APB1HENR |= RCC_APB1HENR_FDCAN1EN;
    (void) RCC_APB1HENR;

    REG32(fdcan_reg(FDCAN_REG_CCCR)) = CCCR_INIT | CCCR_CCE;
    REG32(fdcan_reg(FDCAN_REG_TEST)) = 0u;
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = 0u;
    while ((REG32(fdcan_reg(FDCAN_REG_CCCR)) & CCCR_INIT) != 0u) {
    }
}

static int fdcan_send_frame(uint32_t id, const uint8_t *data, uint8_t len, bool fd)
{
    if (id > 0x7FFu || len > 64u) return -1;
    uint32_t base = fdcan_ram(FDCAN_TXBUF0);
    REG32(base + 0u) = (id & 0x7FFu) << 18u;
    REG32(base + 4u) = ((uint32_t) len_to_dlc(len) << 16u) | (fd ? (TX_T1_FDF | TX_T1_BRS) : 0u);
    write_payload(base + 8u, data, len);
    REG32(fdcan_reg(FDCAN_REG_TXBAR)) = 1u;
    return 0;
}

static bool fdcan_poll_rx(can_frame_t *frame)
{
    uint32_t rxf0s = REG32(fdcan_reg(FDCAN_REG_RXF0S));
    if ((rxf0s & 0x7Fu) == 0u) return false;
    uint32_t gi = (rxf0s >> 8u) & 0x3Fu;
    uint32_t base = fdcan_ram(FDCAN_RXF0_ELEM0 + gi * 72u);
    uint32_t r0 = REG32(base + 0u);
    uint32_t r1 = REG32(base + 4u);
    frame->id = (r0 >> 18u) & 0x7FFu;
    frame->len = dlc_to_len((uint8_t) ((r1 >> 16u) & 0x0Fu));
    frame->fd = (r1 & TX_T1_FDF) != 0u;
    read_payload(base + 8u, frame->data, frame->len);
    REG32(fdcan_reg(FDCAN_REG_RXF0A)) = gi;
    REG32(fdcan_reg(FDCAN_REG_IR)) = REG32(fdcan_reg(FDCAN_REG_IR));
    return true;
}

/* ---- udslib glue ---- */

static volatile uint32_t g_now_ms;

static uds_isotp_ctx_t g_iso;
static uint8_t g_iso_tx_sdu[256];
static uint8_t g_rx_buf[256];
static uint8_t g_tx_buf[256];

static uint32_t get_time_ms(void)
{
    return g_now_ms;
}

static int can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    return fdcan_send_frame(id, data, len, len > 8u);
}

static int isotp_send_adapter(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
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

static volatile bool g_resp_done;
static volatile uint8_t g_resp_sid;
static uint8_t g_resp_data[RESP_BUF_MAX];
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
static void on_response(uds_client_ctx_t *c, uint8_t sid, const uint8_t *data, uint16_t len)
{
    (void) c;
    uart_putc('{');
    uart_puthex8(sid); /* DIAG: on_response entered */
    if (data == NULL) {
        uart_putc('N'); /* DIAG: response carried no payload */
    }
    else {
        uart_puthex8(data[0]); /* DIAG: data[0] */
    }
    uart_putc('}');
    g_resp_sid = sid;
    uint16_t n = (data != NULL)
                     ? ((len < (uint16_t) RESP_BUF_MAX) ? len : (uint16_t) (RESP_BUF_MAX - 1u))
                     : 0u;
    for (uint16_t i = 0u; i < n; ++i) {
        g_resp_data[i] = data[i];
    }
    g_resp_len = n;
    g_resp_done = true;
}

/* ---- pump loop ---- */

static uds_ctx_t g_ctx;           /* holds the transport config for ISO-TP reassembly */
static uds_client_ctx_t g_client; /* client role: pending request + callback */

/* ISO-TP delivers a reassembled response SDU here; route it to the client. */
static void on_sdu_to_client(void *cookie, const uint8_t *sdu, uint16_t len, uint8_t addr)
{
    (void) addr;
    if (len == 0u) {
        return;
    }
    (void) uds_client_handle_response((uds_client_ctx_t *) cookie, sdu[0], sdu, len);
}

/*
 * Run the ISO-TP/UDS pump for up to max_ticks virtual ms.
 * Returns when g_resp_done is set or the budget elapses.
 */
static volatile uint8_t g_diag_pump; /* DIAG: set to 1 to enable RX prints */

static void pump_until_done(uint32_t max_ticks)
{
    uint32_t rx_count = 0u;
    for (uint32_t i = 0u; i < max_ticks && !g_resp_done; ++i) {
        can_frame_t frame;
        while (fdcan_poll_rx(&frame)) {
            if (frame.id == 0x7E8u) {
                if (g_diag_pump != 0u && rx_count < 4u) {
                    uart_putc('<'); /* DIAG: RX frame from ECU */
                    uart_puthex8(frame.data[0]);
                    uart_puthex8(frame.len);
                    uart_putc('>');
                    uart_putc('p'); /* DIAG: client pending_sid before callback */
                    uart_puthex8(g_client.pending_sid);
                    uart_putc('c'); /* DIAG: client cb != 0 */
                    uart_putc((g_client.cb != NULL) ? '1' : '0');
                }
                ++rx_count;
                uds_isotp_rx_callback(&g_iso, &g_ctx, frame.id, frame.data, frame.len);
                if (g_diag_pump != 0u && rx_count <= 4u) {
                    uart_putc('q'); /* DIAG: client pending_sid AFTER callback */
                    uart_puthex8(g_client.pending_sid);
                    if (g_resp_done) {
                        uart_putc('D'); /* DIAG: resp_done set */
                    }
                }
            }
        }
        uds_process(&g_ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }
    if (g_diag_pump != 0u) {
        uart_putc('R');
        uart_puthex8((uint8_t) rx_count); /* DIAG: total RX count */
        g_diag_pump = 0u;
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
 * Uses the public uds_client_request() unmodified, then pumps until the generic
 * on_response() callback sets g_resp_done.
 */
static bool do_request(uint8_t sid, const uint8_t *payload, uint16_t payload_len,
                       uint32_t max_ticks)
{
    g_resp_done = false;
    g_resp_sid = 0u;
    g_resp_len = 0u;

    int rc = uds_client_request(&g_client, sid, payload, payload_len, on_response);
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
    cfg.get_time_ms = get_time_ms;
    cfg.fn_tp_send = isotp_send_adapter;
    cfg.p2_ms = 50u;
    cfg.p2_star_ms = 2000u;
    cfg.rx_buffer = g_rx_buf;
    cfg.rx_buffer_size = sizeof(g_rx_buf);
    cfg.tx_buffer = g_tx_buf;
    cfg.tx_buffer_size = sizeof(g_tx_buf);

    if (uds_init(&g_ctx, &cfg) != UDS_OK) {
        uart_puts("UDS_INIT_FAIL\n");
        for (;;) {
        }
    }

    /* Client role shares the transport config; ISO-TP routes responses to it. */
    g_client.config = &cfg;
    g_client.pending_sid = 0u;
    g_client.cb = NULL;
    uds_isotp_set_sdu_handler(&g_iso, on_sdu_to_client, &g_client);

    /* Give ECU time to start up: run pump for 20 virtual ms before first request */
    for (uint32_t i = 0u; i < 20u; ++i) {
        uds_process(&g_ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }

    /* ==================================================================
     * Service 1: DiagnosticSessionControl (0x10 03) → expected 50 03 00 32 00 C8
     *   bit 0 (BIT_10) set on pass.
     * ================================================================== */
    uart_puts("TESTER_REQ_10\n");
    {
        uint8_t payload[] = {0x03u};
        if (do_request(0x10u, payload, 1u, 500u)) {
            /* Positive response SID = 0x50; payload[0]=sub, [1..4]=timings */
            if (g_resp_sid == 0x50u && g_resp_len >= 5u && g_resp_data[0] == 0x03u &&
                g_resp_data[1] == 0x00u && g_resp_data[2] == 0x32u && g_resp_data[3] == 0x00u &&
                g_resp_data[4] == 0xC8u) {
                uart_puts("TESTER_RESP_50_OK\n");
                g_service_results |= BIT_10;
            }
            else {
                uart_puts("TESTER_RESP_50_BAD\n");
            }
        }
        else {
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
            if (g_resp_sid == 0x69u && g_resp_len >= 2u && g_resp_data[0] == 0x02u &&
                g_resp_data[1] == 0x01u) {
                uart_puts("TESTER_RESP_69_OK\n");
                g_service_results |= BIT_29;
            }
            else {
                uart_puts("TESTER_RESP_69_BAD\n");
            }
        }
        else {
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
            if (g_resp_sid == 0x67u && g_resp_len >= 5u && g_resp_data[0] == 0x01u &&
                g_resp_data[1] == 0xDEu && g_resp_data[2] == 0xADu && g_resp_data[3] == 0xBEu &&
                g_resp_data[4] == 0xEFu) {
                uart_puts("TESTER_RESP_67_SEED_OK\n");

                /* Step B: send key */
                uart_puts("TESTER_REQ_27_KEY\n");
                uint8_t payload_key[] = {0x02u, 0xDFu, 0xAEu, 0xBFu, 0xF0u};
                if (do_request(0x27u, payload_key, 5u, 500u)) {
                    if (g_resp_sid == 0x67u && g_resp_len >= 1u && g_resp_data[0] == 0x02u) {
                        uart_puts("TESTER_RESP_67_KEY_OK\n");
                        g_service_results |= BIT_27;
                    }
                    else {
                        uart_puts("TESTER_RESP_67_KEY_BAD\n");
                    }
                }
                else {
                    uart_puts("TESTER_TIMEOUT_27_KEY\n");
                }
            }
            else {
                uart_puts("TESTER_RESP_67_SEED_BAD\n");
            }
        }
        else {
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
            if (g_resp_sid == 0x7Eu && g_resp_len >= 1u && g_resp_data[0] == 0x00u) {
                uart_puts("TESTER_RESP_7E_OK\n");
                g_service_results |= BIT_3E;
            }
            else {
                uart_puts("TESTER_RESP_7E_BAD\n");
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_3E\n");
        }
    }

    /* ==================================================================
     * Phase 1 result
     * ================================================================== */
    if (g_service_results == (BIT_10 | BIT_27 | BIT_29 | BIT_3E)) {
        uart_puts("PHASE1 4/4 PASS\n");
    }
    else {
        uart_puts("PHASE1 PARTIAL\n");
    }

    /* ==================================================================
     * PHASE 2 — Data services
     *
     * Extended session (0x10 03) was entered in Phase 1.  Keep the session
     * alive with a TesterPresent before each group of requests if the tick
     * budget is large.  Each do_request() also resets the S3 timer on the
     * ECU side (any valid request does), so explicit 0x3E is only needed
     * when many ticks may elapse without a request.
     * ================================================================== */

    /* Service 5: ReadDataByIdentifier (0x22 F1 90) → 62 F1 90 <VIN 14B>
     *   BIT_22 (1<<4) set on pass. */
    uart_puts("TESTER_REQ_22\n");
    {
        uint8_t payload[] = {0xF1u, 0x90u};
        if (do_request(0x22u, payload, 2u, 500u)) {
            /* Positive response SID = 0x62; data[0..1] = DID echo, data[2..15] = VIN */
            if (g_resp_sid == 0x62u && g_resp_len >= 3u && g_resp_data[0] == 0xF1u &&
                g_resp_data[1] == 0x90u) {
                uart_puts("TESTER_RESP_62_OK\n");
                g_service_results |= BIT_22;
            }
            else {
                uart_puts("TESTER_RESP_62_BAD\n");
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_22\n");
        }
    }

    /* Service 6: WriteDataByIdentifier (0x2E 01 23 <16 bytes>) → 6E 01 23
     *   DID 0x0123 size=16; ECU writes to g_customer_name RAM buffer.
     *   BIT_2E (1<<12) set on pass. */
    uart_puts("TESTER_REQ_2E\n");
    {
        /* 3 bytes header + 16 bytes data = 19 total payload (after SID) */
        uint8_t payload[18];
        payload[0] = 0x01u;
        payload[1] = 0x23u;
        /* 16 bytes of write data: "TEST_CLIENT_001" + NUL padded */
        payload[2] = 'T';
        payload[3] = 'E';
        payload[4] = 'S';
        payload[5] = 'T';
        payload[6] = '_';
        payload[7] = 'C';
        payload[8] = 'L';
        payload[9] = 'I';
        payload[10] = 'E';
        payload[11] = 'N';
        payload[12] = 'T';
        payload[13] = '_';
        payload[14] = '0';
        payload[15] = '0';
        payload[16] = '1';
        payload[17] = '\0';
        if (do_request(0x2Eu, payload, 18u, 500u)) {
            /* Positive response SID = 0x6E; data[0..1] = DID echo */
            if (g_resp_sid == 0x6Eu && g_resp_len >= 2u && g_resp_data[0] == 0x01u &&
                g_resp_data[1] == 0x23u) {
                uart_puts("TESTER_RESP_6E_OK\n");
                g_service_results |= BIT_2E;
            }
            else {
                uart_puts("TESTER_RESP_6E_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d0=");
                uart_puthex8((g_resp_len > 0u) ? g_resp_data[0] : 0u);
                uart_puts(" d1=");
                uart_puthex8((g_resp_len > 1u) ? g_resp_data[1] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_2E\n");
        }
    }

    /* Service 7: WriteMemoryByAddress (0x3D) — write before read so 0x23 has
     *   a known value.  ALFID 0x12: size_nibble=1 (1 byte size), addr_nibble=2
     *   (2 bytes addr).  Write 0xAB to g_mock_memory[0x0010].
     *   Positive response: 7D 12 00 10 01.
     *   BIT_3D (1<<20) set on pass. */
    uart_puts("TESTER_REQ_3D\n");
    {
        uint8_t payload[] = {0x12u, 0x00u, 0x10u, 0x01u, 0xABu};
        if (do_request(0x3Du, payload, 5u, 500u)) {
            /* Positive response SID = 0x7D; data = [format, addr_bytes..., size_bytes...] */
            if (g_resp_sid == 0x7Du && g_resp_len >= 4u && g_resp_data[0] == 0x12u &&
                g_resp_data[1] == 0x00u && g_resp_data[2] == 0x10u && g_resp_data[3] == 0x01u) {
                uart_puts("TESTER_RESP_7D_OK\n");
                g_service_results |= BIT_3D;
            }
            else {
                uart_puts("TESTER_RESP_7D_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d0=");
                uart_puthex8((g_resp_len > 0u) ? g_resp_data[0] : 0u);
                uart_puts(" d1=");
                uart_puthex8((g_resp_len > 1u) ? g_resp_data[1] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_3D\n");
        }
    }

    /* Service 8: ReadMemoryByAddress (0x23) — read the byte written above.
     *   ALFID 0x12: addr=2 bytes=0x0010, size=1 byte → read 1 byte from 0x0010.
     *   Positive response: 63 AB.
     *   BIT_23 (1<<5) set on pass. */
    uart_puts("TESTER_REQ_23\n");
    {
        uint8_t payload[] = {0x12u, 0x00u, 0x10u, 0x01u};
        if (do_request(0x23u, payload, 4u, 500u)) {
            /* Positive response SID = 0x63; data[0] = byte read */
            if (g_resp_sid == 0x63u && g_resp_len >= 1u && g_resp_data[0] == 0xABu) {
                uart_puts("TESTER_RESP_63_OK\n");
                g_service_results |= BIT_23;
            }
            else {
                uart_puts("TESTER_RESP_63_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d0=");
                uart_puthex8((g_resp_len > 0u) ? g_resp_data[0] : 0u);
                uart_puts(" d1=");
                uart_puthex8((g_resp_len > 1u) ? g_resp_data[1] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_23\n");
        }
    }

    /* Service 9: ReadScalingDataByIdentifier (0x24 F1 90) → 64 F1 90 <scaling>
     *   ECU fn_read_scaling returns 1 byte (0x01) for DID 0xF190.
     *   BIT_24 (1<<6) set on pass. */
    uart_puts("TESTER_REQ_24\n");
    {
        uint8_t payload[] = {0xF1u, 0x90u};
        if (do_request(0x24u, payload, 2u, 500u)) {
            /* Positive response SID = 0x64; data[0..1] = DID echo */
            if (g_resp_sid == 0x64u && g_resp_len >= 2u && g_resp_data[0] == 0xF1u &&
                g_resp_data[1] == 0x90u) {
                uart_puts("TESTER_RESP_64_OK\n");
                g_service_results |= BIT_24;
            }
            else {
                uart_puts("TESTER_RESP_64_BAD\n");
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_24\n");
        }
    }

    /* Service 10: DynamicallyDefineDataIdentifier (0x2C) — defineByIdentifier
     *   sub-function 0x01, define DID 0xF234 from source DID 0xF190.
     *   Request: 2C 01 F2 34 F1 90 01 01
     *     sub=0x01, defined_DID=0xF234, source_DID=0xF190, posn=0x01, size=0x01
     *   Positive response: 6C 01 F2 34.
     *   BIT_2C (1<<11) set on pass. */
    uart_puts("TESTER_REQ_2C\n");
    {
        uint8_t payload[] = {0x01u, 0xF2u, 0x34u, 0xF1u, 0x90u, 0x01u, 0x01u};
        if (do_request(0x2Cu, payload, 7u, 500u)) {
            /* Positive response SID = 0x6C; data[0]=sub, data[1..2]=defined DID */
            if (g_resp_sid == 0x6Cu && g_resp_len >= 3u && g_resp_data[0] == 0x01u &&
                g_resp_data[1] == 0xF2u && g_resp_data[2] == 0x34u) {
                uart_puts("TESTER_RESP_6C_OK\n");
                g_service_results |= BIT_2C;
            }
            else {
                uart_puts("TESTER_RESP_6C_BAD\n");
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_2C\n");
        }
    }

    /* Service 11: ReadDataByPeriodicIdentifier (0x2A) — setup only.
     *   sub=0x01 (Fast rate), periodic ID = 0xF1.
     *   Positive SETUP response: 6A  (1 byte, no payload).
     *   BIT_2A (1<<10) set on positive setup response. */
    uart_puts("TESTER_REQ_2A\n");
    {
        uint8_t payload[] = {0x01u, 0xF1u};
        if (do_request(0x2Au, payload, 2u, 500u)) {
            /* Positive response SID = 0x6A; no further payload required */
            if (g_resp_sid == 0x6Au) {
                uart_puts("TESTER_RESP_6A_OK\n");
                g_service_results |= BIT_2A;
            }
            else {
                uart_puts("TESTER_RESP_6A_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d0=");
                uart_puthex8((g_resp_len > 0u) ? g_resp_data[0] : 0u);
                uart_puts(" d1=");
                uart_puthex8((g_resp_len > 1u) ? g_resp_data[1] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_2A\n");
        }
    }

    /* Service 12: InputOutputControlByIdentifier (0x2F 01 23 03) → 6F 01 23 55
     *   DID 0x0123, controlOptionRecord = 0x03 (shortTermAdjustment).
     *   ECU fn_io_control returns 1 byte (0x55) for DID 0x0123.
     *   BIT_2F (1<<13) set on pass. */
    uart_puts("TESTER_REQ_2F\n");
    {
        uint8_t payload[] = {0x01u, 0x23u, 0x03u};
        if (do_request(0x2Fu, payload, 3u, 500u)) {
            /* Positive response SID = 0x6F; data[0..1]=DID echo, data[2]=output */
            if (g_resp_sid == 0x6Fu && g_resp_len >= 3u && g_resp_data[0] == 0x01u &&
                g_resp_data[1] == 0x23u && g_resp_data[2] == 0x55u) {
                uart_puts("TESTER_RESP_6F_OK\n");
                g_service_results |= BIT_2F;
            }
            else {
                uart_puts("TESTER_RESP_6F_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d0=");
                uart_puthex8((g_resp_len > 0u) ? g_resp_data[0] : 0u);
                uart_puts(" d1=");
                uart_puthex8((g_resp_len > 1u) ? g_resp_data[1] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_2F\n");
        }
    }

    /* ==================================================================
     * Phase 2 result
     * ================================================================== */
    {
        uint32_t phase2_bits =
            BIT_22 | BIT_23 | BIT_24 | BIT_2A | BIT_2C | BIT_2E | BIT_2F | BIT_3D;
        uint32_t phase2_got = g_service_results & phase2_bits;
        if (phase2_got == phase2_bits) {
            uart_puts("PHASE2 8/8 PASS\n");
        }
        else {
            uart_puts("PHASE2 FAIL\n");
        }
    }

    /* ==================================================================
     * PHASE 3 — DTC services
     *
     * Extended session (0x10 03) is still active from Phase 1.
     * ================================================================== */

    /* Service 13: ReadDTCInformation (0x19 01 FF)
     *   subfn=0x01 (reportNumberOfDTCByStatusMask), statusMask=0xFF.
     *   ECU fn_dtc_read returns: [0x01, 0x01, 0x00, 0x02] (mask, fmt, MSB, LSB).
     *   Positive response: 59 01 01 01 00 02.
     *   BIT_19 (1<<3) set on pass. */
    uart_puts("TESTER_REQ_19\n");
    {
        uint8_t payload[] = {0x01u, 0xFFu};
        if (do_request(0x19u, payload, 2u, 500u)) {
            /* Positive response SID = 0x59; data[0]=subfn, [1..4]=DTC info */
            if (g_resp_sid == 0x59u && g_resp_len >= 5u && g_resp_data[0] == 0x01u &&
                g_resp_data[1] == 0x01u && g_resp_data[2] == 0x01u && g_resp_data[3] == 0x00u &&
                g_resp_data[4] == 0x02u) {
                uart_puts("TESTER_RESP_59_OK\n");
                g_service_results |= BIT_19;
            }
            else {
                uart_puts("TESTER_RESP_59_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d0=");
                uart_puthex8((g_resp_len > 0u) ? g_resp_data[0] : 0u);
                uart_puts(" d1=");
                uart_puthex8((g_resp_len > 1u) ? g_resp_data[1] : 0u);
                uart_puts(" d2=");
                uart_puthex8((g_resp_len > 2u) ? g_resp_data[2] : 0u);
                uart_puts(" len=");
                uart_puthex8((uint8_t) g_resp_len);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_19\n");
        }
    }

    /* Service 14: ControlDTCSetting (0x85 01) -> C5 01.  Re-enabled after the
     * F-9 root cause (labwired LDRB.W sign-extension) was fixed; the 0xC5
     * response SID now arrives intact and matches. */
    uart_puts("TESTER_REQ_85\n");
    {
        uint8_t payload[] = {0x01u};
        if (do_request(0x85u, payload, 1u, 5000u)) {
            if (g_resp_sid == 0xC5u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_C5_OK\n");
                g_service_results |= BIT_85;
            }
            else {
                uart_puts("TESTER_RESP_C5_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_85\n");
        }
    }

    /* Service 15: ClearDiagnosticInformation (0x14 FF FF FF)
     *   group=0xFFFFFF (all DTCs).
     *   ECU fn_dtc_clear returns UDS_OK.
     *   Positive response: 54 (SID byte only, no payload).
     *   BIT_14 (1<<2) set on pass. */
    uart_puts("TESTER_REQ_14\n");
    {
        uint8_t payload[] = {0xFFu, 0xFFu, 0xFFu};
        if (do_request(0x14u, payload, 3u, 500u)) {
            /* Positive response SID = 0x54; no payload */
            if (g_resp_sid == 0x54u) {
                uart_puts("TESTER_RESP_54_OK\n");
                g_service_results |= BIT_14;
            }
            else {
                uart_puts("TESTER_RESP_54_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d0=");
                uart_puthex8((g_resp_len > 0u) ? g_resp_data[0] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_14\n");
        }
    }

    /* Service 16: ResponseOnEvent (0x86) — SETUP response only.
     * Request 86 01 02 FF 19 01 FF -> C6 01 ...  Re-enabled after the F-9 fix. */
    uart_puts("TESTER_REQ_86\n");
    {
        uint8_t payload[] = {0x01u, 0x02u, 0xFFu, 0x19u, 0x01u, 0xFFu};
        if (do_request(0x86u, payload, 6u, 5000u)) {
            if (g_resp_sid == 0xC6u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_C6_OK\n");
                g_service_results |= BIT_86;
            }
            else {
                uart_puts("TESTER_RESP_C6_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_86\n");
        }
    }

    /* ==================================================================
     * Phase 3 result
     * ================================================================== */
    {
        /* All four DTC services now pass after the F-9 fix. */
        uint32_t phase3_bits = BIT_19 | BIT_85 | BIT_14 | BIT_86;
        uint32_t phase3_got = g_service_results & phase3_bits;
        if (phase3_got == phase3_bits) {
            uart_puts("PHASE3 4/4 PASS\n");
        }
        else {
            uart_puts("PHASE3 FAIL\n");
        }
    }

    /* ==================================================================
     * Phase 4 — programming / transfer services (6 of 27)
     *   0x31 RoutineControl, 0x34 RequestDownload, 0x36 TransferData,
     *   0x37 RequestTransferExit, 0x35 RequestUpload, 0x38 RequestFileTransfer.
     * Request/response bytes mirror tests/integration/test_uds.py
     * (test_full_sequence "Flash Engine" + upload steps).  All SIDs < 0x80,
     * so unaffected by F-9.
     * ================================================================== */

    /* 0x31 RoutineControl: 31 01 FF00 (erase) -> 71 01 FF 00 00.
     * Re-enabled after the F-11 fix: the library extracts the routine id with
     * `uxth.w r2, ip` (a wide T2 register-extend) that labwired previously did
     * not decode, leaving id=0; now decoded, the id arrives intact. */
    uart_puts("TESTER_REQ_31\n");
    {
        uint8_t payload[] = {0x01u, 0xFFu, 0x00u};
        if (do_request(0x31u, payload, 3u, 1000u)) {
            if (g_resp_sid == 0x71u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_71_OK\n");
                g_service_results |= BIT_31;
            }
            else {
                uart_puts("TESTER_RESP_71_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_31\n");
        }
    }

    /* 0x34 RequestDownload: 34 00 44 <addr32> <size32> -> 74 20 00 00 04 00 */
    uart_puts("TESTER_REQ_34\n");
    {
        uint8_t payload[] = {0x00u, 0x44u, 0x11u, 0x22u, 0x33u, 0x44u, 0x00u, 0x00u, 0x10u, 0x00u};
        if (do_request(0x34u, payload, 10u, 1000u)) {
            if (g_resp_sid == 0x74u) {
                uart_puts("TESTER_RESP_74_OK\n");
                g_service_results |= BIT_34;
            }
            else {
                uart_puts("TESTER_RESP_74_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_34\n");
        }
    }

    /* 0x36 TransferData: 36 01 AA BB -> 76 01 */
    uart_puts("TESTER_REQ_36\n");
    {
        uint8_t payload[] = {0x01u, 0xAAu, 0xBBu};
        if (do_request(0x36u, payload, 3u, 1000u)) {
            if (g_resp_sid == 0x76u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_76_OK\n");
                g_service_results |= BIT_36;
            }
            else {
                uart_puts("TESTER_RESP_76_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_36\n");
        }
    }

    /* 0x37 RequestTransferExit: 37 -> 77 */
    uart_puts("TESTER_REQ_37\n");
    {
        uint8_t payload[] = {0x00u}; /* unused; payload_len 0 */
        if (do_request(0x37u, payload, 0u, 1000u)) {
            if (g_resp_sid == 0x77u) {
                uart_puts("TESTER_RESP_77_OK\n");
                g_service_results |= BIT_37;
            }
            else {
                uart_puts("TESTER_RESP_77_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_37\n");
        }
    }

    /* 0x35 RequestUpload: 35 00 44 <addr32> <size32> -> 75 20 00 00 04 00 */
    uart_puts("TESTER_REQ_35\n");
    {
        uint8_t payload[] = {0x00u, 0x44u, 0x11u, 0x22u, 0x33u, 0x44u, 0x00u, 0x00u, 0x10u, 0x00u};
        if (do_request(0x35u, payload, 10u, 1000u)) {
            if (g_resp_sid == 0x75u) {
                uart_puts("TESTER_RESP_75_OK\n");
                g_service_results |= BIT_35;
            }
            else {
                uart_puts("TESTER_RESP_75_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_35\n");
        }
    }

    /* 0x38 RequestFileTransfer: 38 01 0004 "test" -> 78 01.
     * Re-enabled after the F-9/F-11 fixes (the prior NRC 0x13 was a symptom of
     * the wide-instruction decode bugs corrupting the parsed length/path_len). */
    uart_puts("TESTER_REQ_38\n");
    {
        /* 38 01 0002 'ab' -> 78 01 (mode=AddFile, filePathLen=2, path="ab"). */
        uint8_t payload[] = {0x01u, 0x00u, 0x02u, 'a', 'b'};
        if (do_request(0x38u, payload, 5u, 1000u)) {
            if (g_resp_sid == 0x78u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_78_OK\n");
                g_service_results |= BIT_38;
            }
            else {
                uart_puts("TESTER_RESP_78_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" nrc=");
                uart_puthex8((g_resp_len > 1u) ? g_resp_data[1] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_38\n");
        }
    }

    /* ==================================================================
     * Phase 4 result (transfer subset, 6/6)
     * ================================================================== */
    {
        uint32_t phase4_bits = BIT_31 | BIT_34 | BIT_36 | BIT_37 | BIT_35 | BIT_38;
        uint32_t phase4_got = g_service_results & phase4_bits;
        if (phase4_got == phase4_bits) {
            uart_puts("PHASE4 6/6 PASS\n");
        }
        else {
            uart_puts("PHASE4 FAIL\n");
        }
    }

    /* ==================================================================
     * Phase 5 — remaining services (0x28, 0x83, 0x87, 0x84) + reset (0x11 last).
     * ================================================================== */

    /* 0x28 CommunicationControl: 28 00 01 -> 68 00 */
    uart_puts("TESTER_REQ_28\n");
    {
        uint8_t payload[] = {0x00u, 0x01u};
        if (do_request(0x28u, payload, 2u, 1000u)) {
            if (g_resp_sid == 0x68u) {
                uart_puts("TESTER_RESP_68_OK\n");
                g_service_results |= BIT_28;
            }
            else {
                uart_puts("TESTER_RESP_68_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_28\n");
        }
    }

    /* 0x83 AccessTimingParameter: 83 01 (readExtended) -> C3 01 P2 P2* */
    uart_puts("TESTER_REQ_83\n");
    {
        uint8_t payload[] = {0x01u};
        if (do_request(0x83u, payload, 1u, 1000u)) {
            if (g_resp_sid == 0xC3u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_C3_OK\n");
                g_service_results |= BIT_83;
            }
            else {
                uart_puts("TESTER_RESP_C3_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_83\n");
        }
    }

    /* 0x87 LinkControl: 87 01 01 (verifyModeFixed) -> C7 01 */
    uart_puts("TESTER_REQ_87\n");
    {
        uint8_t payload[] = {0x01u, 0x01u};
        if (do_request(0x87u, payload, 2u, 1000u)) {
            if (g_resp_sid == 0xC7u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_C7_OK\n");
                g_service_results |= BIT_87;
            }
            else {
                uart_puts("TESTER_RESP_C7_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_87\n");
        }
    }

    /* 0x84 SecuredDataTransmission: wrap an inner TesterPresent.
     *   84 0001 [3E 00]  -> C4 0001 [7E 00]  (identity "crypto"). */
    uart_puts("TESTER_REQ_84\n");
    {
        uint8_t payload[] = {0x00u, 0x01u, 0x3Eu, 0x00u};
        if (do_request(0x84u, payload, 4u, 1000u)) {
            /* response after SID: apar(2) + inner response.  Expect C4 then
             * the wrapped inner positive response 7E. */
            if (g_resp_sid == 0xC4u && g_resp_len >= 3u && g_resp_data[2] == 0x7Eu) {
                uart_puts("TESTER_RESP_C4_OK\n");
                g_service_results |= BIT_84;
            }
            else {
                uart_puts("TESTER_RESP_C4_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_puts(" d2=");
                uart_puthex8((g_resp_len > 2u) ? g_resp_data[2] : 0u);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_84\n");
        }
    }

    /* 0x11 ECUReset: 11 01 -> 51 01.  TESTED LAST — the ECU reboots after, so
     * it cannot disturb any earlier result. */
    uart_puts("TESTER_REQ_11\n");
    {
        uint8_t payload[] = {0x01u};
        if (do_request(0x11u, payload, 1u, 1000u)) {
            if (g_resp_sid == 0x51u && g_resp_len >= 1u && g_resp_data[0] == 0x01u) {
                uart_puts("TESTER_RESP_51_OK\n");
                g_service_results |= BIT_11;
            }
            else {
                uart_puts("TESTER_RESP_51_BAD sid=");
                uart_puthex8(g_resp_sid);
                uart_putc('\n');
            }
        }
        else {
            uart_puts("TESTER_TIMEOUT_11\n");
        }
    }

    /* ==================================================================
     * Phase 5 result + final summary
     * ================================================================== */
    {
        uint32_t phase5_bits = BIT_28 | BIT_83 | BIT_87 | BIT_84 | BIT_11;
        uint32_t phase5_got = g_service_results & phase5_bits;
        if (phase5_got == phase5_bits) {
            uart_puts("PHASE5 5/5 PASS\n");
        }
        else {
            uart_puts("PHASE5 FAIL\n");
        }
        if (g_service_results == ALL_SERVICES_MASK) {
            uart_puts("SERVICES 27/27 PASS\n");
        }
        else {
            uart_puts("SERVICES INCOMPLETE\n");
        }
    }

    for (;;) {
    }
}
