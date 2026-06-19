# Crypto back-end: `make` (mbedTLS, default) or `make CRYPTO=wolfssl`.
# Same example, same output — only aes_cmac() and the linked lib change.
CRYPTO ?= mbedtls
CC = gcc
CFLAGS = -Wall -Wextra -I../../include -I../../src/core

ifeq ($(CRYPTO),wolfssl)
CFLAGS += -DUSE_WOLFSSL
LDLIBS = -lwolfssl
else
LDLIBS = -lmbedcrypto
endif

SRC = main.c \
      ../../src/core/uds_core.c \
      ../../src/transport/uds_tp_isotp.c \
      ../../src/services/uds_service_session.c \
      ../../src/services/uds_service_data.c \
      ../../src/services/uds_service_security.c \
      ../../src/services/uds_service_maintenance.c \
      ../../src/services/uds_service_flash.c \
      ../../src/services/uds_service_mem.c \
      ../../src/services/uds_service_io.c \
      ../../src/services/uds_service_link.c \
      ../../src/services/uds_service_roe.c
TARGET = uds_security_access_mbedtls

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
