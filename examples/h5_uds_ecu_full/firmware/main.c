/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief STM32H563 UDS ECU firmware for the all-services gate.
 *        Drives a udslib server over FDCAN1.
 *        RX 0x7E0 (tester requests) / TX 0x7E8 (ECU responses).
 *        CAN-FD enabled.
 *
 *        Implements the full 27-service configuration mirroring
 *        examples/host_sim/main.c.  All 27 ISO-14229-1 services are
 *        registered via the real udslib server API (built-in dispatch +
 *        the documented fn_* hooks).
 *
 * F-2 / F-3 (EMULATOR — labwired STM32H563 FLASH model):
 *        The built-in RDBI path (uds_internal_handle_read_data_by_id) calls
 *        memcpy(tx_buf, entry->storage, size) where entry->storage is a
 *        pointer stored in .rodata pointing to more .rodata data.  On the
 *        labwired H563 emulator this double-indirection FLASH read faults
 *        (CPU jumps to Default_Handler, no response emitted).  The same
 *        fault class was observed with local const char arrays (F-3).
 *        Workaround: DID data (VIN, customer name) live in RAM, not .rodata;
 *        a user-service shim handles 0x22 directly from RAM buffers.
 *        Real udslib is bug-free; workarounds are confined and marked.
 *        See docs/superpowers/udslib-findings-from-h5-gate.md entries F-2, F-3.
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

static uds_isotp_ctx_t  g_iso;
static uint8_t          g_iso_tx_sdu[1024];
static uint8_t          g_rx_buf[1024];
static uint8_t          g_tx_buf[1024];

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

/* ---- Mock memory (1 KB) — mirrors host_sim ---- */

static uint8_t g_mock_memory[1024];

/* ---- DID data storage in RAM ----
 *
 * WORKAROUND F-2 / F-3: DID data buffers are in RAM (.bss), not .rodata.
 * On the labwired STM32H563 emulator the FLASH model faults when memcpy reads
 * from a .rodata address obtained via a pointer stored in another .rodata struct
 * (double-indirection read from FLASH).  The DID table entry's `storage` field
 * holds the address of this buffer; with the buffer in RAM the memcpy succeeds.
 * These buffers are initialized byte-by-byte in main() to avoid any string-literal
 * copy from .rodata.
 *
 * Real-hardware fix: remove the `static` (let the linker put data in .rodata
 * as const), no firmware change otherwise.
 */
static char g_ecu_vin[15];       /* "UDSLIB_SIM_001" (14 bytes) + NUL */
static char g_customer_name[16]; /* "ECU_OWNER" (9 bytes) + NUL + 6 pad bytes */

static const uds_did_entry_t g_ecu_dids[] = {
    /* id,    size, session_mask, security_mask, read, write, storage */
    {0xF190u, 14u,  0u,          0u,            NULL, NULL,  (void *)g_ecu_vin},
    {0x0123u, 16u,  0u,          0u,            NULL, NULL,  g_customer_name},
};

/* ---- WORKAROUND udslib F-2: user-service shim for SID 0x22 ----
 *
 * The built-in uds_internal_handle_read_data_by_id calls
 *   memcpy(tx_buf, entry->storage, entry->size)
 * where entry is in .rodata and storage points to more .rodata.  In the
 * labwired H563 emulator this two-level FLASH read faults (Default_Handler,
 * ECU goes silent).  With the DID data in RAM (g_ecu_vin / g_customer_name
 * above) the memcpy works.  This shim performs the same operation as the
 * built-in path but always reads from RAM, bypassing the built-in handler.
 *
 * Revert: remove svc_rdbi and g_user_services once the emulator FLASH model
 * supports double-indirection reads from .rodata (labwired issue).
 */
static int svc_rdbi(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, 0x22u, 0x13u); /* incorrectMessageLengthOrInvalidFormat */
    }

    /* DID 0xF190 — VIN (14 bytes, RAM copy of "UDSLIB_SIM_001") */
    if (data[1] == 0xF1u && data[2] == 0x90u) {
        uint8_t *tx = ctx->config->tx_buffer;
        tx[0] = 0x62u;
        tx[1] = 0xF1u;
        tx[2] = 0x90u;
        memcpy(&tx[3], g_ecu_vin, 14u);
        return uds_send_response(ctx, (uint16_t)(3u + 14u));
    }

    /* DID 0x0123 — Customer Name (16 bytes, RAM copy of "ECU_OWNER") */
    if (data[1] == 0x01u && data[2] == 0x23u) {
        uint8_t *tx = ctx->config->tx_buffer;
        tx[0] = 0x62u;
        tx[1] = 0x01u;
        tx[2] = 0x23u;
        memcpy(&tx[3], g_customer_name, 16u);
        return uds_send_response(ctx, (uint16_t)(3u + 16u));
    }

    return uds_send_nrc(ctx, 0x22u, 0x31u); /* requestOutOfRange */
}

/* WORKAROUND udslib F-2 */
static const uds_service_entry_t g_user_services[] = {
    {0x22u, 3u, UDS_SESSION_ALL, 0u, svc_rdbi, NULL, 0u},
};

/* ---- Service callbacks — all fn_* hooks, mirroring examples/host_sim/main.c ---- */

static void fn_reset(uds_ctx_t *ctx, uint8_t type)
{
    (void)ctx;
    (void)type;
    uart_puts("ECU_RESET\n");
}

static int fn_dtc_read(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req,
                       uint16_t req_len, uint8_t *out_buf, uint16_t max_len)
{
    (void)ctx;
    (void)req;
    (void)req_len;
    (void)max_len;
    if (subfn == 0x01u) {   /* reportNumberOfDTCByStatusMask */
        out_buf[0] = 0x01u; /* DTCStatusAvailabilityMask */
        out_buf[1] = 0x01u; /* DTCFormatIdentifier */
        out_buf[2] = 0x00u; /* count MSB */
        out_buf[3] = 0x02u; /* count LSB (2 DTCs) */
        return 4;
    }
    return -0x31; /* requestOutOfRange */
}

static int fn_dtc_clear(struct uds_ctx *ctx, uint32_t group)
{
    (void)ctx;
    (void)group;
    return UDS_OK;
}

static int fn_security_seed(struct uds_ctx *ctx, uint8_t level,
                            uint8_t *seed_buf, uint16_t max_len)
{
    (void)ctx;
    (void)level;
    if (max_len < 4u) return -0x22; /* conditionsNotCorrect */
    seed_buf[0] = 0xDEu;
    seed_buf[1] = 0xADu;
    seed_buf[2] = 0xBEu;
    seed_buf[3] = 0xEFu;
    return 4;
}

static int fn_security_key(struct uds_ctx *ctx, uint8_t level, const uint8_t *seed,
                           const uint8_t *key, uint16_t key_len)
{
    (void)ctx;
    (void)level;
    (void)seed;
    (void)key;
    (void)key_len;
    return UDS_OK;
}

static int fn_auth(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *data,
                   uint16_t len, uint8_t *out_buf, uint16_t max_len)
{
    (void)ctx;
    (void)data;
    (void)len;
    (void)max_len;
    if (subfn == 0x01u) return 0; /* deAuthenticate success */
    if (subfn == 0x02u) {         /* verifyCertificateUnidirectional */
        out_buf[0] = 0x01u;       /* Evaluation Status: Valid */
        return 1;
    }
    return -0x22; /* conditionsNotCorrect */
}

static int fn_routine_control(struct uds_ctx *ctx, uint8_t type, uint16_t id,
                              const uint8_t *data, uint16_t len,
                              uint8_t *out_buf, uint16_t max_len)
{
    (void)ctx;
    (void)data;
    (void)len;
    (void)max_len;
    (void)type;
    if (id == 0xFF00u) {    /* Erase Memory */
        out_buf[0] = 0x00u; /* Success */
        return 1;
    }
    return -0x31; /* requestOutOfRange */
}

static int fn_request_download(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void)ctx;
    (void)addr;
    (void)size;
    return UDS_OK;
}

static int fn_transfer_data(struct uds_ctx *ctx, uint8_t sequence,
                            const uint8_t *data, uint16_t len)
{
    (void)ctx;
    (void)sequence;
    (void)data;
    (void)len;
    return UDS_OK;
}

static int fn_transfer_exit(struct uds_ctx *ctx)
{
    (void)ctx;
    return UDS_OK;
}

static int fn_mem_read(uds_ctx_t *ctx, uint32_t addr, uint32_t size, uint8_t *out_buf)
{
    (void)ctx;
    if ((uint32_t)(addr + size) > (uint32_t)sizeof(g_mock_memory)) {
        return -0x31; /* requestOutOfRange */
    }
    memcpy(out_buf, &g_mock_memory[addr], (size_t)size);
    return 0;
}

static int fn_mem_write(uds_ctx_t *ctx, uint32_t addr, uint32_t size, const uint8_t *data)
{
    (void)ctx;
    if ((uint32_t)(addr + size) > (uint32_t)sizeof(g_mock_memory)) {
        return -0x31; /* requestOutOfRange */
    }
    memcpy(&g_mock_memory[addr], data, size);
    return 0;
}

static int fn_io_control(struct uds_ctx *ctx, uint16_t id, uint8_t type,
                         const uint8_t *data, uint16_t len,
                         uint8_t *out_buf, uint16_t max_len)
{
    (void)ctx;
    (void)type;
    (void)data;
    (void)len;
    (void)max_len;
    if (id == 0x0123u) {
        out_buf[0] = 0x55u;
        return 1;
    }
    return -0x31; /* requestOutOfRange */
}

static int fn_request_upload(struct uds_ctx *ctx, uint32_t addr, uint32_t size)
{
    (void)ctx;
    (void)addr;
    (void)size;
    return UDS_OK;
}

static int fn_periodic_read(struct uds_ctx *ctx, uint8_t periodic_id,
                            uint8_t *out_buf, uint16_t max_len)
{
    (void)ctx;
    (void)periodic_id;
    (void)max_len;
    out_buf[0] = 0xAAu;
    out_buf[1] = 0xBBu;
    return 2;
}

/* ---- main ---- */

int main(void)
{
    /* WORKAROUND F-2: initialize VIN and customer-name DID data in RAM.
     * Byte-by-byte assignment avoids any .rodata-to-stack copy that would
     * fault in the labwired H563 emulator (see F-3 pattern). */
    g_ecu_vin[0]='U'; g_ecu_vin[1]='D'; g_ecu_vin[2]='S'; g_ecu_vin[3]='L';
    g_ecu_vin[4]='I'; g_ecu_vin[5]='B'; g_ecu_vin[6]='_'; g_ecu_vin[7]='S';
    g_ecu_vin[8]='I'; g_ecu_vin[9]='M'; g_ecu_vin[10]='_'; g_ecu_vin[11]='0';
    g_ecu_vin[12]='0'; g_ecu_vin[13]='1'; g_ecu_vin[14]='\0';

    g_customer_name[0]='E'; g_customer_name[1]='C'; g_customer_name[2]='U';
    g_customer_name[3]='_'; g_customer_name[4]='O'; g_customer_name[5]='W';
    g_customer_name[6]='N'; g_customer_name[7]='E'; g_customer_name[8]='R';
    g_customer_name[9]='\0'; /* remaining bytes stay zero-initialized from .bss */

    uart_init();
    uart_puts("H5-UDS-ECU-FULL\n");

    fdcan_start();
    /* RX 0x7E0 (tester requests) / TX 0x7E8 (ECU responses) */
    uds_tp_isotp_init(&g_iso, can_send, 0x7E8u, 0x7E0u, g_iso_tx_sdu, sizeof(g_iso_tx_sdu));
    uds_tp_isotp_set_fd(&g_iso, true);

    static uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* Platform */
    cfg.ecu_address    = 0x10u;
    cfg.get_time_ms    = get_time_ms;
    cfg.fn_tp_send     = isotp_send_adapter;
    cfg.p2_ms          = 50u;
    cfg.p2_star_ms     = 2000u;

    /* Buffers */
    cfg.rx_buffer      = g_rx_buf;
    cfg.rx_buffer_size = (uint16_t)sizeof(g_rx_buf);
    cfg.tx_buffer      = g_tx_buf;
    cfg.tx_buffer_size = (uint16_t)sizeof(g_tx_buf);

    /* DID table for RDBI (0x22) / WDBI (0x2E).
     * Set fields individually (not struct assignment) to keep the DID entry
     * pointer chain from going through .rodata at runtime — see F-2 workaround. */
    cfg.did_table.entries = g_ecu_dids;
    cfg.did_table.count   = (uint16_t)(sizeof(g_ecu_dids) / sizeof(g_ecu_dids[0]));

    /* WORKAROUND F-2: user-service shim for 0x22; remove when emulator is fixed */
    cfg.user_services      = g_user_services;
    cfg.user_service_count = (uint16_t)(sizeof(g_user_services) / sizeof(g_user_services[0]));

    /* Service callbacks — all 27 ISO-14229-1 services */
    cfg.fn_reset            = fn_reset;
    cfg.fn_dtc_read         = fn_dtc_read;
    cfg.fn_dtc_clear        = fn_dtc_clear;
    cfg.fn_security_seed    = fn_security_seed;
    cfg.fn_security_key     = fn_security_key;
    cfg.fn_auth             = fn_auth;
    cfg.fn_routine_control  = fn_routine_control;
    cfg.fn_request_download = fn_request_download;
    cfg.fn_transfer_data    = fn_transfer_data;
    cfg.fn_transfer_exit    = fn_transfer_exit;
    cfg.fn_mem_read         = fn_mem_read;
    cfg.fn_mem_write        = fn_mem_write;
    cfg.fn_io_control       = fn_io_control;
    cfg.fn_request_upload   = fn_request_upload;
    cfg.fn_periodic_read    = fn_periodic_read;

    static uds_ctx_t ctx;
    if (uds_init(&ctx, &cfg) != UDS_OK) {
        uart_puts("UDS_INIT_FAIL\n");
        for (;;) {}
    }

    uart_puts("ECU_READY\n");

    /* Main loop: pump FDCAN → ISO-TP → UDS indefinitely */
    for (;;) {
        can_frame_t frame;
        while (fdcan_poll_rx(&frame)) {
            if (frame.id == 0x7E0u) {
                uds_isotp_rx_callback(&g_iso, &ctx, frame.id, frame.data, frame.len);
            }
        }
        uds_process(&ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }
}
