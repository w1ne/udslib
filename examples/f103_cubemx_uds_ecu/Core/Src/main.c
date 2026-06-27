/* USER CODE BEGIN Header */
/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * STM32F103C8 (Blue Pill) UDS ECU over bxCAN. CubeMX-style scaffold: peripheral
 * init here, the UDS reprogramming logic in uds_ecu_app.c. CAN1 on PA11/PA12 at
 * 500 kbit/s; 72 MHz from an 8 MHz HSE.
 */
/* USER CODE END Header */

#include "main.h"
/* USER CODE BEGIN Includes */
#include "uds_ecu_app.h"
/* USER CODE END Includes */

CAN_HandleTypeDef hcan;
CRC_HandleTypeDef hcrc;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN_Init(void);
static void MX_CRC_Init(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_CRC_Init();
    MX_CAN_Init();

    /* USER CODE BEGIN 2 */
    uds_app_init(&hcan, &hcrc);
    /* USER CODE END 2 */

    while (1) {
        /* USER CODE BEGIN WHILE */
        uds_app_poll();
        /* USER CODE END WHILE */
    }
}

/**
 * @brief System Clock Configuration: HSE 8 MHz -> PLL x9 -> 72 MHz SYSCLK.
 *        AHB 72 MHz, APB1 36 MHz (CAN/timers), APB2 72 MHz.
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType =
        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2; /* 36 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1; /* 72 MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief CAN1 init: 500 kbit/s at 36 MHz APB1 (prescaler 4, 1+15+2 = 18 tq).
 */
static void MX_CAN_Init(void)
{
    hcan.Instance = CAN1;
    hcan.Init.Prescaler = 4;
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan.Init.TimeSeg1 = CAN_BS1_15TQ;
    hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff = ENABLE;
    hcan.Init.AutoWakeUp = DISABLE;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;
    if (HAL_CAN_Init(&hcan) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_CRC_Init(void)
{
    hcrc.Instance = CRC;
    if (HAL_CRC_Init(&hcrc) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void) file;
    (void) line;
}
#endif
