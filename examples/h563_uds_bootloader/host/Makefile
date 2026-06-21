# Host CAN-FD flash tool
UDSLIB_DIR ?= ../../..
CC         := gcc
CFLAGS     := -O2 -g -Wall -Wextra -I$(UDSLIB_DIR)/include
LDFLAGS    := -lmbedcrypto

all: flash_tool

flash_tool: flash_tool.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f flash_tool

.PHONY: all clean
