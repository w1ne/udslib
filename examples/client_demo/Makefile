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
      ../../src/services/uds_service_mem.c
OBJ = $(SRC:.c=.o)
TARGET = uds_client_demo

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
