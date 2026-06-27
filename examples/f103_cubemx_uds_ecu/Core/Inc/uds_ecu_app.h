/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file uds_ecu_app.h
 * @brief UDS ECU application glue for the STM32F103 + bxCAN example.
 *
 * Kept in a separate translation unit (not in main.c) so CubeMX regeneration
 * never touches it. main.c calls uds_app_init() once after the peripherals are
 * up, then uds_app_poll() forever from the super-loop.
 */

#ifndef UDS_ECU_APP_H
#define UDS_ECU_APP_H

#include "main.h"

/**
 * @brief Bind the UDS stack to the CAN and CRC peripherals and start serving.
 * @param hcan  Initialised CAN1 handle (from MX_CAN_Init).
 * @param hcrc  Initialised CRC handle (from MX_CRC_Init).
 */
void uds_app_init(CAN_HandleTypeDef *hcan, CRC_HandleTypeDef *hcrc);

/**
 * @brief Pump the UDS stack: drain the CAN RX FIFO, run ISO-TP timing, and
 *        perform a deferred ECU reset once the last response has left the bus.
 *        Call as fast as possible from the main loop.
 */
void uds_app_poll(void);

#endif /* UDS_ECU_APP_H */
