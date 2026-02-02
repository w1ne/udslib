CC = gcc
CFLAGS = -Wall -Wextra -I../../include -I../../src/core

# Source files
SRC = main.c \
      ../../src/core/uds_core.c \
      ../../src/services/uds_service_data.c \
      ../../src/services/uds_service_flash.c \
      ../../src/services/uds_service_maintenance.c \
      ../../src/services/uds_service_mem.c \
      ../../src/services/uds_service_security.c \
      ../../src/services/uds_service_session.c \
      ../../src/transport/uds_tp_isotp.c

# Output binary
TARGET = uds_host_sim

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
