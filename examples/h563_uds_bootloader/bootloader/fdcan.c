#include "fdcan.h"

#include <stddef.h>
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

/* ---- USART3 (UART over PA10/PB10 on H563 Nucleo) ---- */
#define USART3_BASE 0x40004800u
#define USART3_CR1  REG32(USART3_BASE + 0x00u)
#define USART3_ISR  REG32(USART3_BASE + 0x1Cu)
#define USART3_TDR  REG32(USART3_BASE + 0x28u)
#define USART_ISR_TXE  (1u << 7)
#define USART_CR1_UE   (1u << 0)
#define USART_CR1_TE   (1u << 3)

void uart_init(void)
{
    USART3_CR1 = USART_CR1_UE | USART_CR1_TE;
}

void uart_putc(char c)
{
    while ((USART3_ISR & USART_ISR_TXE) == 0u) {
    }
    USART3_TDR = (uint32_t)(uint8_t) c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

/* ---- FDCAN1 ---- */
#define FDCAN1_BASE       0x4000A400u
#define FDCAN_REG_TEST    0x010u
#define FDCAN_REG_CCCR    0x018u
#define FDCAN_REG_IR      0x050u
#define FDCAN_REG_RXF0S   0x090u
#define FDCAN_REG_RXF0A   0x094u
#define FDCAN_REG_TXBAR   0x0CCu

#define FDCAN_RAM_BASE    0x800u
#define FDCAN_RXF0_ELEM0  0x0B0u
#define FDCAN_TXBUF0      0x278u

#define CCCR_INIT  (1u << 0)
#define CCCR_CCE   (1u << 1)
#define TX_T1_BRS  (1u << 20)
#define TX_T1_FDF  (1u << 21)

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
    static const uint8_t map[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    return map[dlc & 0x0Fu];
}

static void write_payload(uint32_t payload_addr, const uint8_t *data, uint8_t len)
{
    for (uint32_t i = 0; i < 16u; ++i) {
        REG32(payload_addr + i * 4u) = 0u;
    }
    for (uint8_t i = 0; i < len; ++i) {
        uint32_t addr  = payload_addr + ((uint32_t) i / 4u) * 4u;
        uint32_t shift = ((uint32_t) i % 4u) * 8u;
        REG32(addr)    = REG32(addr) | ((uint32_t) data[i] << shift);
    }
}

static void read_payload(uint32_t payload_addr, uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; ++i) {
        uint32_t word = REG32(payload_addr + ((uint32_t) i / 4u) * 4u);
        data[i] = (uint8_t)((word >> (((uint32_t) i % 4u) * 8u)) & 0xFFu);
    }
}

void fdcan_start(void)
{
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = CCCR_INIT | CCCR_CCE;
    REG32(fdcan_reg(FDCAN_REG_TEST)) = 0u;
    REG32(fdcan_reg(FDCAN_REG_CCCR)) = 0u;
    while ((REG32(fdcan_reg(FDCAN_REG_CCCR)) & CCCR_INIT) != 0u) {
    }
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
    uint32_t base      = fdcan_ram(FDCAN_RXF0_ELEM0 + get_index * 72u);
    uint32_t r0        = REG32(base + 0u);
    uint32_t r1        = REG32(base + 4u);
    frame->id  = (r0 >> 18u) & 0x7FFu;
    frame->len = dlc_to_len((uint8_t)((r1 >> 16u) & 0x0Fu));
    frame->fd  = (r1 & TX_T1_FDF) != 0u;
    read_payload(base + 8u, frame->data, frame->len);
    REG32(fdcan_reg(FDCAN_REG_RXF0A)) = get_index;
    REG32(fdcan_reg(FDCAN_REG_IR))    = REG32(fdcan_reg(FDCAN_REG_IR));
    return true;
}

int can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    return fdcan_send_frame(id, data, len, len > 8u);
}
