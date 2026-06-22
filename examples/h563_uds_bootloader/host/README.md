# h563 UDS OTA — Host CAN-FD Flash Tool

SocketCAN CAN-FD host tool for the STM32H563 dual-bank UDS OTA bootloader.

## Build

```sh
make -C examples/h563_uds_bootloader/host UDSLIB_DIR=/path/to/udslib
```

Requires the mbedTLS development package (Debian/Ubuntu: `sudo apt install
libmbedtls-dev`), which provides `libmbedcrypto`. The tool links it via
`-lmbedcrypto` (see `Makefile`).

## Usage

```sh
./flash_tool <can-iface> <image.bin>
```

Example with a PCAN adapter at 500 kbit/s + 2 Mbit/s data:

```sh
sudo ip link set can0 up type can bitrate 500000 dbitrate 2000000 fd on
./flash_tool can0 examples/h563_uds_bootloader/app/app_b_image.bin
```

The tool:
1. Enters programming session (0x10 02)
2. Reads active bank via DID 0xF1A0 (0x22) and computes inactive app base
3. Security access with AES-128-CMAC key derivation (0x27 01/02)
4. Requests download to inactive bank (0x34)
5. Transfers image in chunks (0x36 × N)
6. Transfer exit (0x37)
7. Validates image (0x31 01 FF01)
8. Activates software — triggers bank swap + reset (0x31 01 FF02)

## vcan Smoke Test

```sh
sudo bash examples/h563_uds_bootloader/host/test_vcan.sh
```

The smoke test verifies ISO-TP frame construction and checks vcan availability.
Full end-to-end requires a host-ECU stub (not yet built) bound to vcan0.
