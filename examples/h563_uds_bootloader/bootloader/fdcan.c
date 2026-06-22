#include "fdcan.h"

#include <stddef.h>
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *) (uintptr_t) (addr))

/* ---- USART3 (UART over PA10/PB10 on H563 Nucleo) ---- */
#define USART3_BASE 0x40004800u
#define USART3_CR1 REG32(USART3_BASE + 0x00u)
#define USART3_ISR REG32(USART3_BASE + 0x1Cu)
#define USART3_TDR REG32(USART3_BASE + 0x28u)
#define USART_ISR_TXE (1u << 7)
#define USART_CR1_UE (1u << 0)
#define USART_CR1_TE (1u << 3)

void uart_init(void)
{
    USART3_CR1 = USART_CR1_UE | USART_CR1_TE;
}

void uart_putc(char c)
{
    while ((USART3_ISR & USART_ISR_TXE) == 0u) {
    }
    USART3_TDR = (uint32_t) (uint8_t) c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

/* ---- FDCAN1 ---- */
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

static uint32_t fdcan_reg(uint32_t offset)
{
    return FDCAN1_BASE + offset;
}

static uint32_t fdcan_ram(uint32_t offset)
{
    return FDCAN1_BASE + FDCAN_RAM_BASE + offset;
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

static void write_payload(uint32_t payload_addr, const uint8_t *data, uint8_t len)
{
    /* Write 16 zero words to clear the element (up to 64-byte CAN-FD payload). */
    volatile uint32_t *wp = (volatile uint32_t *) (uintptr_t) payload_addr;
    for (uint32_t i = 0u; i < 16u; ++i) {
        wp[i] = 0u;
    }
    /* Pack payload bytes into 32-bit words (little-endian: byte 0 in LSB). */
    uint8_t nwords = (uint8_t) ((len + 3u) >> 2u);
    for (uint8_t w = 0u; w < nwords; ++w) {
        uint8_t base = (uint8_t) (w * 4u);
        uint32_t word = (uint32_t) data[base];
        if ((uint8_t) (base + 1u) < len) {
            word |= (uint32_t) data[base + 1u] << 8u;
        }
        if ((uint8_t) (base + 2u) < len) {
            word |= (uint32_t) data[base + 2u] << 16u;
        }
        if ((uint8_t) (base + 3u) < len) {
            word |= (uint32_t) data[base + 3u] << 24u;
        }
        wp[w] = word;
    }
}

static void read_payload(uint32_t payload_addr, uint8_t *data, uint8_t len)
{
    const volatile uint32_t *rp = (const volatile uint32_t *) (uintptr_t) payload_addr;
    uint8_t nwords = (uint8_t) ((len + 3u) >> 2u);
    for (uint8_t w = 0u; w < nwords; ++w) {
        uint32_t word = rp[w];
        uint8_t base = (uint8_t) (w * 4u);
        data[base] = (uint8_t) (word & 0xFFu);
        if ((uint8_t) (base + 1u) < len) {
            data[base + 1u] = (uint8_t) ((word >> 8u) & 0xFFu);
        }
        if ((uint8_t) (base + 2u) < len) {
            data[base + 2u] = (uint8_t) ((word >> 16u) & 0xFFu);
        }
        if ((uint8_t) (base + 3u) < len) {
            data[base + 3u] = (uint8_t) ((word >> 24u) & 0xFFu);
        }
    }
}

void fdcan_start(void)
{
#ifdef SIM_OTA_TESTER
    /* Internal loopback: CCCR.TEST enables the TEST register; TEST.LBCK
     * routes TX back into RXF0 so the tester and server share one FIFO. */
#define CCCR_TEST (1u << 7)
#define TEST_LBCK (1u << 4)
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = CCCR_INIT | CCCR_CCE | CCCR_TEST;
    REG32(fdcan_reg(FDCAN_REG_TEST)) = TEST_LBCK;
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = CCCR_TEST; /* clear INIT, keep TEST */
    while ((REG32(fdcan_reg(FDCAN_REG_CCCR)) & CCCR_INIT) != 0u) {
    }
#else
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = CCCR_INIT | CCCR_CCE;
    REG32(fdcan_reg(FDCAN_REG_TEST)) = 0u;
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = 0u;
    while ((REG32(fdcan_reg(FDCAN_REG_CCCR)) & CCCR_INIT) != 0u) {
    }
#endif
}

int fdcan_send_frame(uint32_t id, const uint8_t *data, uint8_t len, bool fd)
{
    if (id > 0x7FFu || len > 64u) {
        return -1;
    }
    uint32_t base = fdcan_ram(FDCAN_TXBUF0);
    REG32(base + 0u) = (id & 0x7FFu) << 18u;
    REG32(base + 4u) = ((uint32_t) len_to_dlc(len) << 16u) | (fd ? (TX_T1_FDF | TX_T1_BRS) : 0u);
    write_payload(base + 8u, data, len);
    REG32(fdcan_reg(FDCAN_REG_TXBAR)) = 1u;
    return 0;
}

bool fdcan_poll_rx_frame(can_frame_t *frame)
{
    uint32_t rxf0s = REG32(fdcan_reg(FDCAN_REG_RXF0S));
    if ((rxf0s & 0x7Fu) == 0u) {
        return false;
    }
    uint32_t get_index = (rxf0s >> 8u) & 0x3Fu;
    uint32_t base = fdcan_ram(FDCAN_RXF0_ELEM0 + get_index * 72u);
    uint32_t r0 = REG32(base + 0u);
    uint32_t r1 = REG32(base + 4u);
    frame->id = (r0 >> 18u) & 0x7FFu;
    frame->len = dlc_to_len((uint8_t) ((r1 >> 16u) & 0x0Fu));
    frame->fd = (r1 & TX_T1_FDF) != 0u;
    read_payload(base + 8u, frame->data, frame->len);
    REG32(fdcan_reg(FDCAN_REG_RXF0A)) = get_index;
    REG32(fdcan_reg(FDCAN_REG_IR)) = REG32(fdcan_reg(FDCAN_REG_IR));
    return true;
}

int can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    return fdcan_send_frame(id, data, len, len > 8u);
}
