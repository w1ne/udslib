TARGET    := h563_uds_bootloader
BUILD_DIR := build
UDSLIB_DIR ?= /tmp/udslib-labwired-scope
CLANG     ?= clang
# Derive rust-lld from the active rustc toolchain; override with RUST_LLD=...
RUST_LLD  ?= $(shell rustc --print sysroot)/lib/rustlib/$(shell rustc -vV | sed -n 's/host: //p')/bin/rust-lld

# mbedTLS 3.6 source tree
MBEDTLS_DIR := /home/andrii/projects/udslib/zephyr-workspace/modules/crypto/mbedtls-3.6

CPUFLAGS     := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb
COMMON_CFLAGS := $(CPUFLAGS) -ffreestanding -fno-builtin -ffunction-sections \
                 -fdata-sections -Os -g -I. -Iinclude -I$(UDSLIB_DIR)/include \
                 -I$(UDSLIB_DIR)/src/core
APP_CFLAGS   := $(COMMON_CFLAGS) -Wall -Wextra -Werror
UDS_CFLAGS   := $(COMMON_CFLAGS) -Wall -Wextra -Wno-unused-parameter \
                -Wno-missing-field-initializers
# mbedTLS: relaxed warnings, no -Werror; needs its own include paths
MBEDTLS_CFLAGS := $(CPUFLAGS) -ffreestanding -fno-builtin -ffunction-sections \
                  -fdata-sections -Os -g \
                  -I$(MBEDTLS_DIR)/include \
                  -I$(MBEDTLS_DIR)/library \
                  -I. -Iinclude \
                  -DMBEDTLS_CONFIG_FILE='"bootloader_mbedtls_config.h"' \
                  -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
                  -Wno-missing-field-initializers
LDFLAGS := -flavor gnu -m armelf -T bootloader.ld --gc-sections \
           -Map=$(BUILD_DIR)/$(TARGET).map

APP_SRCS     := startup.c main.c libc_stubs.c fdcan.c flash_h5.c
SEC_CMAC_SRC := sec_cmac.c
UDS_SRCS := \
  $(UDSLIB_DIR)/src/core/uds_core.c \
  $(UDSLIB_DIR)/src/services/uds_service_session.c \
  $(UDSLIB_DIR)/src/services/uds_service_security.c \
  $(UDSLIB_DIR)/src/services/uds_service_flash.c \
  $(UDSLIB_DIR)/src/services/uds_service_data.c \
  $(UDSLIB_DIR)/src/services/uds_service_maintenance.c \
  $(UDSLIB_DIR)/src/transport/uds_tp_isotp.c

MBEDTLS_SRCS := \
  $(MBEDTLS_DIR)/library/aes.c \
  $(MBEDTLS_DIR)/library/cipher.c \
  $(MBEDTLS_DIR)/library/cipher_wrap.c \
  $(MBEDTLS_DIR)/library/cmac.c \
  $(MBEDTLS_DIR)/library/platform.c \
  $(MBEDTLS_DIR)/library/platform_util.c \
  $(MBEDTLS_DIR)/library/constant_time.c \
  $(MBEDTLS_DIR)/library/memory_buffer_alloc.c \
  $(MBEDTLS_DIR)/library/error.c

APP_OBJS      := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
SEC_CMAC_OBJ  := $(BUILD_DIR)/sec_cmac.o
UDS_OBJS      := $(patsubst $(UDSLIB_DIR)/%.c,$(BUILD_DIR)/udslib/%.o,$(UDS_SRCS))
MBEDTLS_OBJS  := $(patsubst $(MBEDTLS_DIR)/library/%.c,$(BUILD_DIR)/mbedtls/%.o,$(MBEDTLS_SRCS))
ELF           := $(BUILD_DIR)/$(TARGET).elf

all: check-udslib check-lld $(ELF)

check-udslib:
	@test -f "$(UDSLIB_DIR)/include/uds/uds_core.h" || \
	  (echo "UDSLIB_DIR must point at a UDSLib checkout (got: $(UDSLIB_DIR))" >&2; exit 1)

check-lld:
	@test -x "$(RUST_LLD)" || \
	  (echo "rust-lld not found at: $(RUST_LLD)  — set RUST_LLD=<path>" >&2; exit 1)

$(ELF): $(APP_OBJS) $(SEC_CMAC_OBJ) $(UDS_OBJS) $(MBEDTLS_OBJS) bootloader.ld
	$(RUST_LLD) $(LDFLAGS) $(APP_OBJS) $(SEC_CMAC_OBJ) $(UDS_OBJS) $(MBEDTLS_OBJS) -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CLANG) $(APP_CFLAGS) -c $< -o $@

$(BUILD_DIR)/udslib/%.o: $(UDSLIB_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CLANG) $(UDS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CLANG) $(MBEDTLS_CFLAGS) -c $< -o $@

# sec_cmac.c links against mbedTLS headers: compile with MBEDTLS_CFLAGS
# (which already includes -Wall -Wextra but NOT -Werror).
$(SEC_CMAC_OBJ): $(SEC_CMAC_SRC) | $(BUILD_DIR)
	$(CLANG) $(MBEDTLS_CFLAGS) -Wall -Wextra -Werror -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

# Host test: compile and run RFC 4493 AES-CMAC vectors on the build machine
CMAC_TEST_SRCS := sec_cmac_test.c sec_cmac.c \
  $(MBEDTLS_DIR)/library/aes.c \
  $(MBEDTLS_DIR)/library/cipher.c \
  $(MBEDTLS_DIR)/library/cipher_wrap.c \
  $(MBEDTLS_DIR)/library/cmac.c \
  $(MBEDTLS_DIR)/library/platform.c \
  $(MBEDTLS_DIR)/library/platform_util.c \
  $(MBEDTLS_DIR)/library/constant_time.c \
  $(MBEDTLS_DIR)/library/memory_buffer_alloc.c \
  $(MBEDTLS_DIR)/library/error.c

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

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check-udslib check-lld clean cmac-test
