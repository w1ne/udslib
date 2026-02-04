CC = gcc
CFLAGS = -Wall -Wextra -I../include -I../src/core

SRC = test_core.c ../src/core/uds_core.c
TARGET = unit_tests

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
