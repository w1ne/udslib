CC = gcc
CFLAGS = -Wall -Wextra -I../../include -I../../src/core
SRC = main.c ../../src/core/uds_core.c ../../src/transport/uds_tp_isotp.c
OBJ = $(SRC:.c=.o)
TARGET = uds_client_demo

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
