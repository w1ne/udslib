/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/*
 * main.c — demo app payload for the H563 OTA bootloader example.
 *
 * Freestanding — uses only <stdint.h>; no libc, no shims.
 *
 * Select the variant at compile time with -DAPP_VARIANT=1, 2, or 3.
 *   App A      (variant 1): prints "APP-A v1\n",   calls boot_confirm().
 *   App B-good (variant 2): prints "APP-B v2\n",   calls boot_confirm().
 *   App B-bad  (variant 3): prints "APP-B-BAD v2\n", does NOT confirm
 *                           → demonstrates automatic rollback after
 *                             MAX_BOOT_ATTEMPTS failed attempts.
 *
 * The USART3 MMIO register offsets match the bootloader's fdcan.c/uart helpers
 * so the same physical UART (PA10/PB10 on the H563 Nucleo) is used throughout.
 */

#include "boot_confirm.h"
#include <stdint.h>

#ifndef APP_VARIANT
#error "Define APP_VARIANT=1 (App A), 2 (App B-good), or 3 (App B-bad)"
#endif

/* ---- USART3 MMIO (STM32H563, same constants as bootloader fdcan.c) ---- */
#define REG32(addr) (*(volatile uint32_t *) (uintptr_t) (addr))

#define USART3_BASE 0x40004800u
#define USART3_CR1 REG32(USART3_BASE + 0x00u)
#define USART3_ISR REG32(USART3_BASE + 0x1Cu)
#define USART3_TDR REG32(USART3_BASE + 0x28u)
#define USART_CR1_UE (1u << 0)
#define USART_CR1_TE (1u << 3)
#define USART_ISR_TXE (1u << 7)

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

/* ---- Variant selection ------------------------------------------------- */

/*
 * APP_VARIANT is a NUMBER set by the Makefile. Comparing bare token names in
 * `#if` does not work — undefined identifiers evaluate to 0. Use integers.
 *
 *   1 = App A      — good app; confirms after banner.
 *   2 = App B-good — good app; confirms after banner.
 *   3 = App B-bad  — unhealthy app; does NOT confirm → triggers rollback.
 */
#if APP_VARIANT == 1
#define BANNER "APP-A v1\n"
#define DO_CONFIRM 1
#elif APP_VARIANT == 2
#define BANNER "APP-B v2\n"
#define DO_CONFIRM 1
#elif APP_VARIANT == 3
#define BANNER "APP-B-BAD v2\n"
#define DO_CONFIRM 0
#else
#error "APP_VARIANT must be 1 (App A), 2 (App B-good), or 3 (App B-bad)"
#endif

/* ---- Entry point ------------------------------------------------------- */

int main(void)
{
    uart_init();
    uart_puts(BANNER);

#if DO_CONFIRM
    /*
     * App A and App B-good: confirm the boot so the bootloader clears the
     * pending flag.  Any subsequent boot from this bank will proceed directly
     * to BL-JUMP without incrementing the attempt counter.
     */
    boot_confirm();
#endif
    /* DO_CONFIRM == 0 (App B-bad): deliberately omit boot_confirm() so the
     * bootloader will count this as a failed attempt and eventually roll back. */

    /* Spin — the bootloader will only jump here once per power cycle. */
    for (;;) {
    }

    return 0;
}
