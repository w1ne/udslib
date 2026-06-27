# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#
# STM32F103C8 + bxCAN UDS ECU - CubeMX-style bare Makefile build.
#
# The ST HAL / CMSIS trees are NOT vendored into udslib. Point the variables
# below at them. Two easy ways to get them:
#   1. Open f103_cubemx_uds_ecu.ioc in STM32CubeMX and "Generate Code" - it
#      drops a Drivers/ tree next to this Makefile, and the defaults just work.
#   2. Clone ST's repos and pass their paths on the command line, e.g.:
#        make HAL_DIR=/path/stm32f1xx_hal_driver \
#             CMSIS_DEV=/path/cmsis_device_f1 \
#             CMSIS_CORE=/path/cmsis_core/CMSIS/Core/Include

TARGET    := f103_cubemx_uds_ecu
BUILD_DIR := build

# --- Dependency locations (override on the command line as needed) ---
CUBE_DIR   ?= Drivers
HAL_DIR    ?= $(CUBE_DIR)/STM32F1xx_HAL_Driver
CMSIS_DEV  ?= $(CUBE_DIR)/CMSIS/Device/ST/STM32F1xx
CMSIS_CORE ?= $(CUBE_DIR)/CMSIS/Include
UDSLIB_DIR ?= ../..

CROSS ?= arm-none-eabi-
CC    := $(CROSS)gcc
SZ    := $(CROSS)size
OBJCOPY := $(CROSS)objcopy

CPU    := -mcpu=cortex-m3 -mthumb
DEFS   := -DUSE_HAL_DRIVER -DSTM32F103xB
OPT    := -Os -g3

INCLUDES := \
  -ICore/Inc \
  -I$(HAL_DIR)/Inc \
  -I$(CMSIS_DEV)/Include \
  -I$(CMSIS_CORE) \
  -I$(UDSLIB_DIR)/include \
  -I$(UDSLIB_DIR)/src/core

CFLAGS := $(CPU) $(DEFS) $(OPT) $(INCLUDES) \
          -ffunction-sections -fdata-sections -Wall -fstack-usage
ASFLAGS := $(CPU) $(OPT) -ffunction-sections -fdata-sections

LDSCRIPT := STM32F103C8TX_FLASH.ld
LDFLAGS  := $(CPU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map

# --- Application + CubeMX scaffold ---
C_SRCS := \
  Core/Src/main.c \
  Core/Src/stm32f1xx_it.c \
  Core/Src/stm32f1xx_hal_msp.c \
  Core/Src/system_stm32f1xx.c \
  Core/Src/uds_ecu_app.c

# --- ST HAL drivers actually used ---
HAL_SRCS := \
  $(HAL_DIR)/Src/stm32f1xx_hal.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_rcc.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_rcc_ex.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_gpio.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_gpio_ex.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_cortex.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_can.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_flash.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_flash_ex.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_crc.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_pwr.c \
  $(HAL_DIR)/Src/stm32f1xx_hal_exti.c

# --- UDSLib ---
UDS_SRCS := \
  $(UDSLIB_DIR)/src/core/uds_core.c \
  $(UDSLIB_DIR)/src/services/uds_service_data.c \
  $(UDSLIB_DIR)/src/services/uds_service_flash.c \
  $(UDSLIB_DIR)/src/services/uds_service_io.c \
  $(UDSLIB_DIR)/src/services/uds_service_link.c \
  $(UDSLIB_DIR)/src/services/uds_service_maintenance.c \
  $(UDSLIB_DIR)/src/services/uds_service_mem.c \
  $(UDSLIB_DIR)/src/services/uds_service_roe.c \
  $(UDSLIB_DIR)/src/services/uds_service_security.c \
  $(UDSLIB_DIR)/src/services/uds_service_session.c \
  $(UDSLIB_DIR)/src/services/uds_dtc_store.c \
  $(UDSLIB_DIR)/src/transport/uds_tp_isotp.c

ASM_SRCS := Core/Startup/startup_stm32f103xb.s

OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SRCS:.c=.o)))
OBJS += $(addprefix $(BUILD_DIR)/,$(notdir $(HAL_SRCS:.c=.o)))
OBJS += $(addprefix $(BUILD_DIR)/,$(notdir $(UDS_SRCS:.c=.o)))
OBJS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SRCS:.s=.o)))

vpath %.c $(sort $(dir $(C_SRCS) $(HAL_SRCS) $(UDS_SRCS)))
vpath %.s $(sort $(dir $(ASM_SRCS)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(CC) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary -S $< $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
