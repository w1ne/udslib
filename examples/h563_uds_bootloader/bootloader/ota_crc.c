/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "ota_crc.h"

/* Reflected CRC-32/ISO-HDLC polynomial (0x04C11DB7 bit-reversed). */
#define CRC32_POLY 0xEDB88320UL

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        crc ^= (uint32_t) buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u) {
                crc = (crc >> 1u) ^ CRC32_POLY;
            } else {
                crc >>= 1u;
            }
        }
    }
    return crc;
}

uint32_t ota_crc32(const uint8_t *buf, uint32_t len)
{
    return ota_crc32_update(0xFFFFFFFFUL, buf, len) ^ 0xFFFFFFFFUL;
}
