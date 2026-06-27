/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/* Host shim replacement for the example's main.h (shadows Core/Inc/main.h). */
#ifndef HALSHIM_MAIN_H
#define HALSHIM_MAIN_H
#include "stm32f1xx_hal.h"
void Error_Handler(void);
#endif
