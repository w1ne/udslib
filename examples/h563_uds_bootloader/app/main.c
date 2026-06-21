/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/*
 * main.c — demo app payload for the H563 OTA bootloader example.
 *
 * Freestanding — uses only <stdint.h>; no libc, no shims.
 *
 * Select the variant at compile time with -DAPP_VARIANT=A or -DAPP_VARIANT=B.
 *   App A: prints "APP-A v1\n"  (version 0x00010000)
 *   App B: prints "APP-B v2\n"  (version 0x00020000)
 *
 * The USART3 MMIO register offsets match the bootloader's fdcan.c/uart helpers
 * so the same physical UART (PA10/PB10 on the H563 Nucleo) is used throughout.
 */

#include <stdint.h>

#ifndef APP_VARIANT
#error "Define APP_VARIANT=A or APP_VARIANT=B"
#endif

/* ---- USART3 MMIO (STM32H563, same constants as bootloader fdcan.c) ---- */
#define REG32(addr)     (*(volatile uint32_t *)(uintptr_t)(addr))

#define USART3_BASE     0x40004800u
#define USART3_CR1      REG32(USART3_BASE + 0x00u)
#define USART3_ISR      REG32(USART3_BASE + 0x1Cu)
#define USART3_TDR      REG32(USART3_BASE + 0x28u)
#define USART_CR1_UE    (1u << 0)
#define USART_CR1_TE    (1u << 3)
#define USART_ISR_TXE   (1u << 7)

static void uart_init(void)
{
    USART3_CR1 = USART_CR1_UE | USART_CR1_TE;
}

static void uart_putc(char c)
{
    while ((USART3_ISR & USART_ISR_TXE) == 0u) {
    }
    USART3_TDR = (uint32_t)(uint8_t) c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

/* ---- Variant selection ------------------------------------------------- */

/*
 * Stringification: turn APP_VARIANT token (A or B) into "A" / "B" without
 * relying on sprintf or any other libc call.
 */
#define _STR(x)  #x
#define STR(x)   _STR(x)

#if APP_VARIANT == A
#define BANNER "APP-A v1\n"
#elif APP_VARIANT == B
#define BANNER "APP-B v2\n"
#else
#error "APP_VARIANT must be A or B"
#endif

/* ---- Entry point ------------------------------------------------------- */

int main(void)
{
    uart_init();
    uart_puts(BANNER);

    /* Spin — the bootloader will only jump here once per power cycle. */
    for (;;) {
    }

    return 0;
}
