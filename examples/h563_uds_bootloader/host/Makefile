# Host CAN-FD flash tool
UDSLIB_DIR ?= ../../..
CC         := gcc
CFLAGS     := -O2 -g -Wall -Wextra -Werror -I$(UDSLIB_DIR)/include
# Links the AES-CMAC/SHA helpers from libmbedcrypto (the crypto library shipped
# in the libmbedtls-dev package on Debian/Ubuntu). See README.md.
LDFLAGS    := -lmbedcrypto

all: flash_tool

flash_tool: flash_tool.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f flash_tool

.PHONY: all clean
