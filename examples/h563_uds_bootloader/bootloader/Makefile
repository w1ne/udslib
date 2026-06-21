TARGET    := h563_uds_bootloader
BUILD_DIR := build
UDSLIB_DIR ?= /tmp/udslib-labwired-scope
CC        := arm-none-eabi-gcc

# mbedTLS 3.6 source tree (override with MBEDTLS_DIR=... ; defaults to the copy
# vendored under the udslib zephyr workspace, derived from UDSLIB_DIR so the
# build is portable to any checkout / CI machine).
MBEDTLS_DIR ?= $(UDSLIB_DIR)/zephyr-workspace/modules/crypto/mbedtls-3.6

# Cortex-M33, real newlib-nano libc: NO -ffreestanding / -fno-builtin, NO shim
# include path. Standard headers + functions come from newlib.
CPUFLAGS      := -mcpu=cortex-m33 -mthumb
COMMON_CFLAGS := $(CPUFLAGS) -Os -g -ffunction-sections -fdata-sections \
                 -I. -I$(UDSLIB_DIR)/include -I$(UDSLIB_DIR)/src/core
APP_CFLAGS   := $(COMMON_CFLAGS) -Wall -Wextra -Werror
UDS_CFLAGS   := $(COMMON_CFLAGS) -Wall -Wextra -Werror -Wno-unused-parameter \
                -Wno-missing-field-initializers
# mbedTLS: relaxed warnings, no -Werror; needs its own include paths + config.
MBEDTLS_CFLAGS := $(CPUFLAGS) -Os -g -ffunction-sections -fdata-sections \
                  -I$(MBEDTLS_DIR)/include \
                  -I$(MBEDTLS_DIR)/library \
                  -I. \
                  -DMBEDTLS_CONFIG_FILE='"bootloader_mbedtls_config.h"' \
                  -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
                  -Wno-missing-field-initializers

# Link with gcc: newlib-nano libc + nosys.specs syscall stubs. -nostartfiles
# keeps our own startup.c Reset/vector table.
LDFLAGS := $(CPUFLAGS) --specs=nano.specs --specs=nosys.specs -nostartfiles \
           -T bootloader.ld -Wl,--gc-sections \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map

APP_SRCS     := startup.c main.c syscalls.c fdcan.c flash_h5.c ota_crc.c app_jump.c
SEC_CMAC_SRC := sec_cmac.c
UDS_SRCS := \
  $(UDSLIB_DIR)/src/core/uds_core.c \
  $(UDSLIB_DIR)/src/services/uds_service_session.c \
  $(UDSLIB_DIR)/src/services/uds_service_security.c \
  $(UDSLIB_DIR)/src/services/uds_service_flash.c \
  $(UDSLIB_DIR)/src/services/uds_service_data.c \
  $(UDSLIB_DIR)/src/services/uds_service_maintenance.c \
  $(UDSLIB_DIR)/src/services/uds_service_mem.c \
  $(UDSLIB_DIR)/src/services/uds_service_io.c \
  $(UDSLIB_DIR)/src/services/uds_service_link.c \
  $(UDSLIB_DIR)/src/services/uds_service_roe.c \
  $(UDSLIB_DIR)/src/transport/uds_tp_isotp.c

MBEDTLS_SRCS := \
  $(MBEDTLS_DIR)/library/aes.c \
  $(MBEDTLS_DIR)/library/cipher.c \
  $(MBEDTLS_DIR)/library/cipher_wrap.c \
  $(MBEDTLS_DIR)/library/cmac.c \
  $(MBEDTLS_DIR)/library/platform_util.c \
  $(MBEDTLS_DIR)/library/constant_time.c

APP_OBJS      := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
SEC_CMAC_OBJ  := $(BUILD_DIR)/sec_cmac.o
UDS_OBJS      := $(patsubst $(UDSLIB_DIR)/%.c,$(BUILD_DIR)/udslib/%.o,$(UDS_SRCS))
MBEDTLS_OBJS  := $(patsubst $(MBEDTLS_DIR)/library/%.c,$(BUILD_DIR)/mbedtls/%.o,$(MBEDTLS_SRCS))
ELF           := $(BUILD_DIR)/$(TARGET).elf

all: check-udslib $(ELF)

check-udslib:
	@test -f "$(UDSLIB_DIR)/include/uds/uds_core.h" || \
	  (echo "UDSLIB_DIR must point at a UDSLib checkout (got: $(UDSLIB_DIR))" >&2; exit 1)

$(ELF): $(APP_OBJS) $(SEC_CMAC_OBJ) $(UDS_OBJS) $(MBEDTLS_OBJS) bootloader.ld
	$(CC) $(LDFLAGS) $(APP_OBJS) $(SEC_CMAC_OBJ) $(UDS_OBJS) $(MBEDTLS_OBJS) -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(BUILD_DIR)/udslib/%.o: $(UDSLIB_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(UDS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(MBEDTLS_CFLAGS) -c $< -o $@

# sec_cmac.c links against mbedTLS headers: compile with MBEDTLS_CFLAGS plus
# -Werror (it is our code, held to the strict bar).
$(SEC_CMAC_OBJ): $(SEC_CMAC_SRC) | $(BUILD_DIR)
	$(CC) $(MBEDTLS_CFLAGS) -Wall -Wextra -Werror -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

# Host test: compile and run RFC 4493 AES-CMAC vectors on the build machine.
CMAC_TEST_SRCS := sec_cmac_test.c sec_cmac.c \
  $(MBEDTLS_DIR)/library/aes.c \
  $(MBEDTLS_DIR)/library/cipher.c \
  $(MBEDTLS_DIR)/library/cipher_wrap.c \
  $(MBEDTLS_DIR)/library/cmac.c \
  $(MBEDTLS_DIR)/library/platform_util.c \
  $(MBEDTLS_DIR)/library/constant_time.c

CMAC_TEST_CFLAGS := -O2 -g \
  -I$(MBEDTLS_DIR)/include \
  -I$(MBEDTLS_DIR)/library \
  -I. \
  -DMBEDTLS_CONFIG_FILE='"bootloader_mbedtls_config.h"' \
  -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
  -Wno-missing-field-initializers

cmac-test: $(BUILD_DIR)
	gcc $(CMAC_TEST_CFLAGS) $(CMAC_TEST_SRCS) -o $(BUILD_DIR)/cmac_test
	$(BUILD_DIR)/cmac_test

# Host unit test: validates OTA image header CRC/magic logic without MCU code.
IMAGE_TEST_SRCS := app_image_test.c ota_crc.c
IMAGE_TEST_CFLAGS := -O2 -g -I. -Wall -Wextra -Werror

image-test: $(BUILD_DIR)
	gcc $(IMAGE_TEST_CFLAGS) $(IMAGE_TEST_SRCS) -o $(BUILD_DIR)/image_test
	$(BUILD_DIR)/image_test

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check-udslib clean cmac-test image-test
