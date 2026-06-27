/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
/* Host HAL shim: just enough of the STM32F1 HAL to compile and run
 * examples/f103_cubemx_uds_ecu/Core/Src/uds_ecu_app.c unmodified on a PC.
 * CAN -> in-memory frame FIFOs, FLASH -> RAM array, CRC -> STM32 CRC32. */
#ifndef HALSHIM_STM32F1XX_HAL_H
#define HALSHIM_STM32F1XX_HAL_H

#include <stdint.h>
#include <string.h>

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;

#define ENABLE 1
#define DISABLE 0

/* --- CAN --- */
#define CAN_ID_STD 0u
#define CAN_RTR_DATA 0u
#define CAN_RX_FIFO0 0u
#define CAN_FILTERMODE_IDMASK 0u
#define CAN_FILTERSCALE_32BIT 1u

typedef struct
{
    uint32_t StdId, ExtId, IDE, RTR, DLC;
} CAN_TxHeaderTypeDef;

typedef struct
{
    uint32_t StdId, ExtId, IDE, RTR, DLC, FilterMatchIndex;
} CAN_RxHeaderTypeDef;

typedef struct
{
    uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow;
    uint32_t FilterFIFOAssignment, FilterBank, FilterMode, FilterScale, FilterActivation,
        SlaveStartFilterBank;
} CAN_FilterTypeDef;

typedef struct
{
    void *Instance;
} CAN_HandleTypeDef;

typedef struct
{
    void *Instance;
} CRC_HandleTypeDef;

/* --- FLASH --- */
#define FLASH_TYPEPROGRAM_HALFWORD 0u
#define FLASH_TYPEERASE_PAGES 0u

typedef struct
{
    uint32_t TypeErase, Banks, PageAddress, NbPages;
} FLASH_EraseInitTypeDef;

/* Virtual flash window mapped at the firmware's APP_REGION_ADDR. */
#define SHIM_FLASH_BASE 0x0800E000u
#define SHIM_FLASH_SIZE 0x2000u
extern uint8_t g_shim_flash[SHIM_FLASH_SIZE];

/* --- Virtual CAN bus: ECU TX queue (to tester) + ECU RX queue (from tester) --- */
typedef struct
{
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
} shim_frame_t;

#define SHIM_Q 64
typedef struct
{
    shim_frame_t f[SHIM_Q];
    int head, tail;
} shim_fifo_t;

extern shim_fifo_t g_ecu_tx; /* ECU -> tester */
extern shim_fifo_t g_ecu_rx; /* tester -> ECU */
extern uint32_t g_shim_tick;
extern int g_shim_reset_called;

static inline int shim_push(shim_fifo_t *q, uint32_t id, const uint8_t *d, uint8_t len)
{
    int n = (q->tail + 1) % SHIM_Q;
    if (n == q->head) return -1;
    q->f[q->tail].id = id;
    q->f[q->tail].len = len;
    memset(q->f[q->tail].data, 0, 8);
    if (len) memcpy(q->f[q->tail].data, d, len);
    q->tail = n;
    return 0;
}
static inline int shim_pop(shim_fifo_t *q, shim_frame_t *out)
{
    if (q->head == q->tail) return -1;
    *out = q->f[q->head];
    q->head = (q->head + 1) % SHIM_Q;
    return 0;
}
static inline int shim_count(shim_fifo_t *q)
{
    return (q->tail - q->head + SHIM_Q) % SHIM_Q;
}

/* --- HAL API used by uds_ecu_app.c --- */
static inline uint32_t HAL_GetTick(void)
{
    return g_shim_tick;
}
static inline void HAL_NVIC_SystemReset(void)
{
    g_shim_reset_called = 1;
}

static inline HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f)
{
    (void) h;
    (void) f;
    return HAL_OK;
}
static inline HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h)
{
    (void) h;
    return HAL_OK;
}

static inline HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *t,
                                                     uint8_t *data, uint32_t *mailbox)
{
    (void) h;
    *mailbox = 0;
    return shim_push(&g_ecu_tx, t->StdId, data, (uint8_t) t->DLC) == 0 ? HAL_OK : HAL_ERROR;
}

static inline uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo)
{
    (void) h;
    (void) fifo;
    return (uint32_t) shim_count(&g_ecu_rx);
}

static inline HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo,
                                                     CAN_RxHeaderTypeDef *hdr, uint8_t *data)
{
    (void) h;
    (void) fifo;
    shim_frame_t fr;
    if (shim_pop(&g_ecu_rx, &fr) != 0) return HAL_ERROR;
    hdr->StdId = fr.id;
    hdr->DLC = fr.len;
    hdr->IDE = CAN_ID_STD;
    hdr->RTR = CAN_RTR_DATA;
    memcpy(data, fr.data, 8);
    return HAL_OK;
}

/* All three TX mailboxes always free -> reset fires immediately once requested. */
static inline uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h)
{
    (void) h;
    return 3u;
}

static inline void HAL_FLASH_Unlock(void)
{
}
static inline void HAL_FLASH_Lock(void)
{
}

static inline HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t addr, uint64_t data)
{
    (void) type;
    if (addr < SHIM_FLASH_BASE || addr + 2u > SHIM_FLASH_BASE + SHIM_FLASH_SIZE) return HAL_ERROR;
    uint32_t off = addr - SHIM_FLASH_BASE;
    g_shim_flash[off] = (uint8_t) (data & 0xFF);
    g_shim_flash[off + 1] = (uint8_t) ((data >> 8) & 0xFF);
    return HAL_OK;
}

static inline HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *err)
{
    *err = 0xFFFFFFFFu;
    if (e->PageAddress < SHIM_FLASH_BASE) return HAL_ERROR;
    uint32_t off = e->PageAddress - SHIM_FLASH_BASE;
    uint32_t bytes = e->NbPages * 0x400u;
    if (off + bytes > SHIM_FLASH_SIZE) return HAL_ERROR;
    memset(&g_shim_flash[off], 0xFF, bytes);
    return HAL_OK;
}

/* STM32 CRC unit: poly 0x04C11DB7, init 0xFFFFFFFF, MSB-first, word-wise. The
 * firmware passes (uint32_t*)APP_REGION_ADDR; map that back to the shim flash. */
static inline uint32_t HAL_CRC_Calculate(CRC_HandleTypeDef *h, uint32_t *buf, uint32_t words)
{
    (void) h;
    const uint8_t *base = g_shim_flash;
    if ((uint32_t) (uintptr_t) buf >= SHIM_FLASH_BASE) {
        base = &g_shim_flash[(uint32_t) (uintptr_t) buf - SHIM_FLASH_BASE];
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t w = 0; w < words; w++) {
        uint32_t word;
        memcpy(&word, base + w * 4u, 4u);
        crc ^= word;
        for (int i = 0; i < 32; i++) {
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1);
        }
    }
    return crc;
}

#endif
