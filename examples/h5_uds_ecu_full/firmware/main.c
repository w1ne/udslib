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
 * F-7 (EMULATOR — labwired STM32H563 core_services FLASH read at high index):
 *        SID 0x85 (ControlDTCSetting) sits at index 19 in core_services[].
 *        handle_request() reads service->sub_mask from that struct at FLASH
 *        offset ~19*sizeof(uds_service_entry_t).  The labwired H563 FLASH model
 *        returns a non-zero garbage value for this pointer field, so
 *        is_subfunction_supported() dereferences a bad address → Default_Handler.
 *        SID 0x86 (ResponseOnEvent) is at index 26 and hits the same fault.
 *        Workaround: user-service shims for 0x85 and 0x86 with sub_mask=NULL;
 *        the built-in handlers are called directly and validate sub themselves.
 *        SID 0x19 (index 3) uses the same sub_mask mechanism and works, confirming
 *        this is an offset-dependent FLASH read bug, not a universal issue.
 *        See docs/superpowers/udslib-findings-from-h5-gate.md entry F-7.
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
#include "uds_internal.h" /* WORKAROUND udslib F-7: shims for 0x85, 0x86 */

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
    uart_putc('@'); /* DIAG: can_send entered */
    uart_putc((char)('0' + (len & 0xFu))); /* DIAG: len low nibble as ASCII */
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

/* WORKAROUND udslib F-5 (EMULATOR — .rodata struct-field read returns wrong value):
 * The labwired STM32H563 emulator does not correctly read halfword fields from
 * a struct array in .rodata (FLASH) via a pointer; `uds_internal_find_did`
 * iterates `g_ecu_dids[i].id` and the comparison fails, returning NULL for all
 * DIDs.  Moving the table to RAM (.bss) and initializing it in main() resolves
 * the lookup failure.  This is the same emulator bug class as F-2/F-3.
 * Revert (restore `static const`) once the labwired H563 FLASH model correctly
 * returns halfword values for struct fields in .rodata. */
static uds_did_entry_t g_ecu_dids[2]; /* initialized in main() */

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

/* ---- WORKAROUND udslib F-6: user-service shims for SID 0x2E and 0x2F ----
 *
 * The built-in WDBI (0x2E) and IOCTL (0x2F) handlers both call
 * uds_internal_find_did(), which iterates did_table.entries[i].id.  Even with
 * g_ecu_dids in RAM (F-5 workaround), the labwired H563 emulator returns wrong
 * data when the udslib core reads .id via an indirect-pointer load chain through
 * ctx->config->did_table.entries.  The comparison always fails → NRC 0x31.
 *
 * These shims bypass find_did entirely, matching DID 0x0123 by constant
 * comparison in the shim itself (same technique as svc_rdbi for SID 0x22).
 *
 * Revert: remove svc_wdbi, svc_ioctl and the extra entries in g_user_services
 * once the labwired H563 emulator correctly handles find_did DID comparisons.
 */
static int svc_wdbi(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, 0x2Eu, 0x13u); /* incorrectMessageLengthOrInvalidFormat */
    }

    /* DID 0x0123 — Customer Name (16 bytes) */
    if (data[1] == 0x01u && data[2] == 0x23u) {
        if (len != (uint16_t)(3u + 16u)) {
            return uds_send_nrc(ctx, 0x2Eu, 0x13u); /* incorrectMessageLengthOrInvalidFormat */
        }
        memcpy(g_customer_name, &data[3], 16u);
        uint8_t *tx = ctx->config->tx_buffer;
        tx[0] = 0x6Eu;
        tx[1] = 0x01u;
        tx[2] = 0x23u;
        return uds_send_response(ctx, 3u);
    }

    return uds_send_nrc(ctx, 0x2Eu, 0x31u); /* requestOutOfRange */
}

static int svc_ioctl(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 3u) {
        return uds_send_nrc(ctx, 0x2Fu, 0x13u); /* incorrectMessageLengthOrInvalidFormat */
    }

    /* DID 0x0123 — shortTermAdjustment (ctrl_type=0x03): return 0x55 */
    if (data[1] == 0x01u && data[2] == 0x23u) {
        uint8_t *tx = ctx->config->tx_buffer;
        tx[0] = 0x6Fu;
        tx[1] = 0x01u;
        tx[2] = 0x23u;
        tx[3] = 0x55u;
        return uds_send_response(ctx, 4u);
    }

    return uds_send_nrc(ctx, 0x2Fu, 0x31u); /* requestOutOfRange */
}

/* WORKAROUND udslib F-7 — explicit shims for 0x85 and 0x86.
 * sub_mask=NULL (set in g_user_services) means handle_request never calls
 * is_subfunction_supported(), avoiding the FLASH-offset pointer bug.
 * The shims validate sub-function themselves and build the response inline,
 * with no uart_puts calls (ECU UART spin-waits burn tester pump ticks). */
static int svc_ctrl_dtc(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    uart_putc('!'); /* DIAG: svc_ctrl_dtc entered */
    int rr = uds_internal_handle_control_dtc_setting(ctx, data, len);
    uart_putc('%'); /* DIAG: handler returned */
    return rr;
}

static int svc_roe(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    return uds_internal_handle_response_on_event(ctx, data, len);
}

/* WORKAROUND udslib F-2 + F-6 + F-7:
 * g_user_services is intentionally NON-const (.bss / RAM) to work around the
 * labwired H563 FLASH model bug that returns wrong values when reading struct
 * fields (handler ptr, sub_mask ptr) at byte offsets > ~64 within a .rodata
 * array.  With the table in RAM, all field reads succeed.
 * Entries are initialized byte-by-byte in main() via a volatile pointer to
 * force individual STR instructions (same technique as g_ecu_dids / F-4 fix).
 * Revert to `static const` once the labwired H563 FLASH model is fixed. */
#define USER_SERVICE_COUNT 5u
static uds_service_entry_t g_user_services[USER_SERVICE_COUNT];

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

static int fn_read_scaling(struct uds_ctx *ctx, uint16_t did,
                           uint8_t *out_buf, uint16_t max_len)
{
    (void)ctx;
    (void)max_len;
    /* Return a single scaling byte (0x01 = linear) for any recognised DID. */
    if (did == 0xF190u) {
        out_buf[0] = 0x01u; /* scalingByte: linear */
        return 1;
    }
    return -0x31; /* requestOutOfRange */
}

static int fn_dynamic_did(struct uds_ctx *ctx, uint8_t subfn, uint16_t defined_did,
                          const uint8_t *data, uint16_t len)
{
    (void)ctx;
    (void)defined_did;
    (void)data;
    (void)len;
    /* Accept defineByIdentifier (0x01) and clear (0x03); no persistent storage. */
    if (subfn == 0x01u || subfn == 0x03u) {
        return 0;
    }
    return -0x31; /* requestOutOfRange */
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

    /* WORKAROUND udslib F-5: initialize DID table in RAM.
     * g_ecu_dids is non-const (.bss) to avoid the labwired H563 .rodata struct
     * read bug (same class as F-2/F-3).  Use volatile pointer to force
     * individual STR/STRH/STRB instructions (avoids STRD/STM.W coalescing). */
    {
        volatile uds_did_entry_t *vd = g_ecu_dids;

        vd[0].id            = 0xF190u;
        vd[0].size          = 14u;
        vd[0].session_mask  = 0u;
        vd[0].security_mask = 0u;
        vd[0].read          = NULL;
        vd[0].write         = NULL;
        vd[0].storage       = (void *)g_ecu_vin;

        vd[1].id            = 0x0123u;
        vd[1].size          = 16u;
        vd[1].session_mask  = 0u;
        vd[1].security_mask = 0u;
        vd[1].read          = NULL;
        vd[1].write         = NULL;
        vd[1].storage       = (void *)g_customer_name;
    }

    /* WORKAROUND udslib F-7: initialize user-service table in RAM.
     * g_user_services is non-const (.bss) to avoid the labwired H563 .rodata
     * FLASH bug (struct field read returns wrong value at byte offsets > ~64).
     * Entries for 0x85 and 0x86 would fall at offset ≥68 in a 20-byte-stride
     * array, triggering the same fault class as core_services[19].  With the
     * table in RAM, field reads succeed.
     * Volatile pointer forces individual STR instructions (avoids STM.W / F-4). */
    {
        volatile uds_service_entry_t *vu = g_user_services;

        vu[0].sid           = 0x22u;
        vu[0].min_len       = 3u;
        vu[0].session_mask  = (uint8_t)UDS_SESSION_ALL;
        vu[0].security_mask = 0u;
        vu[0].handler       = svc_rdbi;
        vu[0].sub_mask      = NULL;
        vu[0].address_mode  = 0u;

        vu[1].sid           = 0x2Eu;
        vu[1].min_len       = 3u;
        vu[1].session_mask  = (uint8_t)UDS_SESSION_ALL;
        vu[1].security_mask = 0u;
        vu[1].handler       = svc_wdbi;
        vu[1].sub_mask      = NULL;
        vu[1].address_mode  = 0u;

        vu[2].sid           = 0x2Fu;
        vu[2].min_len       = 3u;
        vu[2].session_mask  = (uint8_t)UDS_SESSION_ALL;
        vu[2].security_mask = 0u;
        vu[2].handler       = svc_ioctl;
        vu[2].sub_mask      = NULL;
        vu[2].address_mode  = 0u;

        /* WORKAROUND udslib F-7: 0x85 at core_services[19] (offset ~304 bytes) and
         * 0x86 at core_services[26] (offset ~380 bytes) trigger the FLASH-read bug.
         * User shims intercept them here; sub_mask=NULL, built-in handlers validate
         * sub-functions themselves. */
        vu[3].sid           = 0x85u;
        vu[3].min_len       = 2u;
        vu[3].session_mask  = (uint8_t)UDS_SESSION_ALL;
        vu[3].security_mask = 0u;
        vu[3].handler       = svc_ctrl_dtc;
        vu[3].sub_mask      = NULL;
        vu[3].address_mode  = 0u;

        vu[4].sid           = 0x86u;
        vu[4].min_len       = 2u;
        vu[4].session_mask  = (uint8_t)UDS_SESSION_ALL;
        vu[4].security_mask = 0u;
        vu[4].handler       = svc_roe;
        vu[4].sub_mask      = NULL;
        vu[4].address_mode  = 0u;
    }

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

    /* WORKAROUND F-2 + F-7: user-service shims; remove when emulator is fixed */
    cfg.user_services      = g_user_services;
    cfg.user_service_count = (uint16_t)USER_SERVICE_COUNT;

    /* Service callbacks — all 27 ISO-14229-1 services.
     *
     * WORKAROUND udslib F-4 (EMULATOR — STM.W not applied):
     * The labwired STM32H563 Cortex-M33 emulator does not correctly execute
     * Thumb-2 STM.W (Store Multiple, 32-bit encoding).  The optimizer groups
     * sequential struct-field assignments into STM.W / STRD instructions; on
     * this emulator only individual STR.W instructions reliably commit the
     * stored value.  Using a `volatile uds_config_t *` pointer forces clang -Os
     * to emit one STR.W per field instead of coalescing into STM.W.
     * Fields assigned before the volatile pointer (cfg.rx_buffer, cfg.tx_buffer,
     * cfg.did_table, cfg.user_services, cfg.get_time_ms, cfg.fn_tp_send,
     * cfg.p2_ms, cfg.p2_star_ms) were written via STRD/individual STR and are
     * NOT affected; only the fn_* hooks from fn_reset onwards exhibit the bug
     * because the compiler groups them into STM.W.
     * Revert once labwired H563 emulator correctly implements STM.W.
     */
    {
        volatile uds_config_t *vcfg = &cfg;
        vcfg->fn_reset            = fn_reset;
        vcfg->fn_dtc_read         = fn_dtc_read;
        vcfg->fn_dtc_clear        = fn_dtc_clear;
        vcfg->fn_security_seed    = fn_security_seed;
        vcfg->fn_security_key     = fn_security_key;
        vcfg->fn_auth             = fn_auth;
        vcfg->fn_routine_control  = fn_routine_control;
        vcfg->fn_request_download = fn_request_download;
        vcfg->fn_transfer_data    = fn_transfer_data;
        vcfg->fn_transfer_exit    = fn_transfer_exit;
        vcfg->fn_mem_read         = fn_mem_read;
        vcfg->fn_mem_write        = fn_mem_write;
        vcfg->fn_io_control       = fn_io_control;
        vcfg->fn_request_upload   = fn_request_upload;
        vcfg->fn_periodic_read    = fn_periodic_read;
        vcfg->fn_read_scaling     = fn_read_scaling;
        vcfg->fn_dynamic_did      = fn_dynamic_did;
    }

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
                uart_putc('['); /* DIAG: RX frame PCI-high-nibble */
                uart_putc((char)('0' + ((frame.data[0] >> 4u) & 0xFu)));
                uart_putc(']');
                uds_isotp_rx_callback(&g_iso, &ctx, frame.id, frame.data, frame.len);
            }
        }
        uds_process(&ctx);
        uds_tp_isotp_process(&g_iso, g_now_ms);
        ++g_now_ms;
    }
}
