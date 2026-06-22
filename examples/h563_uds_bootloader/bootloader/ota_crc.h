/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file ota_crc.h
 * @brief CRC-32/ISO-HDLC shared across the bootloader.
 *
 * Single implementation used by both app_is_valid() and the
 * CheckProgrammingDependencies (0xFF01) routine so the algorithm
 * is never duplicated.
 */

#ifndef OTA_CRC_H
#define OTA_CRC_H

#include <stdint.h>

/**
 * Update a running CRC-32/ISO-HDLC accumulator.
 *
 * @param crc  Current accumulator value (initialise with 0xFFFFFFFF).
 * @param buf  Pointer to input bytes.
 * @param len  Number of bytes to process.
 * @return     Updated accumulator (XOR with 0xFFFFFFFF to obtain the final digest).
 */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *buf, uint32_t len);

/**
 * Compute CRC-32/ISO-HDLC over a buffer.
 *
 * @param buf  Pointer to input bytes.
 * @param len  Number of bytes.
 * @return     CRC-32 digest.
 */
uint32_t ota_crc32(const uint8_t *buf, uint32_t len);

#endif /* OTA_CRC_H */
