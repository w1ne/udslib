TARGET    := h563_uds_bootloader
BUILD_DIR := build
UDSLIB_DIR ?= /tmp/udslib-labwired-scope
CLANG     ?= clang
# Derive rust-lld from the active rustc toolchain; override with RUST_LLD=...
RUST_LLD  ?= $(shell rustc --print sysroot)/lib/rustlib/$(shell rustc -vV | sed -n 's/host: //p')/bin/rust-lld

CPUFLAGS     := --target=arm-none-eabi -mcpu=cortex-m33 -mthumb
COMMON_CFLAGS := $(CPUFLAGS) -ffreestanding -fno-builtin -ffunction-sections \
                 -fdata-sections -Os -g -I. -Iinclude -I$(UDSLIB_DIR)/include \
                 -I$(UDSLIB_DIR)/src/core
APP_CFLAGS   := $(COMMON_CFLAGS) -Wall -Wextra -Werror
UDS_CFLAGS   := $(COMMON_CFLAGS) -Wall -Wextra -Wno-unused-parameter \
                -Wno-missing-field-initializers
LDFLAGS := -flavor gnu -m armelf -T bootloader.ld --gc-sections \
           -Map=$(BUILD_DIR)/$(TARGET).map

APP_SRCS := startup.c main.c libc_stubs.c fdcan.c
UDS_SRCS := \
  $(UDSLIB_DIR)/src/core/uds_core.c \
  $(UDSLIB_DIR)/src/services/uds_service_session.c \
  $(UDSLIB_DIR)/src/services/uds_service_security.c \
  $(UDSLIB_DIR)/src/services/uds_service_flash.c \
  $(UDSLIB_DIR)/src/services/uds_service_data.c \
  $(UDSLIB_DIR)/src/services/uds_service_maintenance.c \
  $(UDSLIB_DIR)/src/transport/uds_tp_isotp.c

APP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
UDS_OBJS := $(patsubst $(UDSLIB_DIR)/%.c,$(BUILD_DIR)/udslib/%.o,$(UDS_SRCS))
ELF      := $(BUILD_DIR)/$(TARGET).elf

all: check-udslib check-lld $(ELF)

check-udslib:
	@test -f "$(UDSLIB_DIR)/include/uds/uds_core.h" || \
	  (echo "UDSLIB_DIR must point at a UDSLib checkout (got: $(UDSLIB_DIR))" >&2; exit 1)

check-lld:
	@test -x "$(RUST_LLD)" || \
	  (echo "rust-lld not found at: $(RUST_LLD)  — set RUST_LLD=<path>" >&2; exit 1)

$(ELF): $(APP_OBJS) $(UDS_OBJS) bootloader.ld
	$(RUST_LLD) $(LDFLAGS) $(APP_OBJS) $(UDS_OBJS) -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CLANG) $(APP_CFLAGS) -c $< -o $@

$(BUILD_DIR)/udslib/%.o: $(UDSLIB_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CLANG) $(UDS_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check-udslib check-lld clean
