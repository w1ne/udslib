CC = gcc
CFLAGS = -Wall -Wextra -I../../include -I../../src/core
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
TARGET = uds_custom_service

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
