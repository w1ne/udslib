CC = gcc
CFLAGS = -Wall -Wextra -I../../include -I../../src/core

# Source files
SRC = main.c ../../src/core/uds_core.c ../../src/transport/uds_tp_isotp.c

# Output binary
TARGET = uds_host_sim

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
