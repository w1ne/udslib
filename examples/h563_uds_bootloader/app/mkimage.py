#!/usr/bin/env python3
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""
mkimage.py — pack a raw app .bin into a valid OTA image.

Usage:
    mkimage.py <app.bin> <out_image.bin> [--version 0xVVVVVVVV]

Output layout (matches ota_image.h / app_jump.c):

    Offset  Size  Field
    ------  ----  -----
     0       4    magic      = 0xC0DEBEEF  (little-endian u32)
     4       4    image_size = len(payload) in bytes
     8       4    crc32      = CRC-32/ISO-HDLC over payload bytes
    12       4    version    = caller-supplied (default 0x00010000)
    16+      N    payload    = raw .bin contents

CRC algorithm: CRC-32/ISO-HDLC
  Poly:     0x04C11DB7 (bit-reflected: 0xEDB88320)
  Init:     0xFFFFFFFF
  XOR-out:  0xFFFFFFFF
  Reflect:  input and output

Python's zlib.crc32() computes exactly this algorithm (it returns the
final XOR-out already applied and the result is unsigned on Python 3).
This matches the bootloader's ota_crc32() function in ota_crc.c.
"""

import argparse
import struct
import sys
import zlib

OTA_IMAGE_MAGIC = 0xC0DEBEEF
OTA_IMAGE_HDR_SIZE = 16
OTA_IMAGE_MAX_PAYLOAD = 0xE8000 - OTA_IMAGE_HDR_SIZE  # 0xE7FF0

def build_image(payload: bytes, version: int) -> bytes:
    """
    Build a complete OTA image: [16-byte header][payload].

    The header is always little-endian:
      uint32_t magic       = OTA_IMAGE_MAGIC
      uint32_t image_size  = len(payload)
      uint32_t crc32       = CRC-32/ISO-HDLC over payload
      uint32_t version     = supplied version word
    """
    if len(payload) == 0:
        raise ValueError("payload must not be empty")
    if len(payload) > OTA_IMAGE_MAX_PAYLOAD:
        raise ValueError(
            f"payload too large: {len(payload)} > {OTA_IMAGE_MAX_PAYLOAD}"
        )

    # zlib.crc32 on Python 3 always returns an unsigned 32-bit integer that
    # matches CRC-32/ISO-HDLC (init=0xFFFFFFFF, xorout=0xFFFFFFFF, reflected).
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    header = struct.pack(
        "<4I",
        OTA_IMAGE_MAGIC,
        len(payload),
        crc,
        version,
    )
    assert len(header) == OTA_IMAGE_HDR_SIZE

    return header + payload


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pack a raw app .bin into an OTA image with ota_image_header_t."
    )
    parser.add_argument("app_bin", help="Input raw binary (.bin)")
    parser.add_argument("out_image", help="Output OTA image (.bin)")
    parser.add_argument(
        "--version",
        default="0x00010000",
        help="32-bit version word, hex (default: 0x00010000 = v1.0.0)",
    )
    args = parser.parse_args()

    version = int(args.version, 0)
    if version > 0xFFFFFFFF:
        print(f"error: version {args.version} exceeds 32 bits", file=sys.stderr)
        return 1

    try:
        with open(args.app_bin, "rb") as f:
            payload = f.read()
    except OSError as e:
        print(f"error reading {args.app_bin}: {e}", file=sys.stderr)
        return 1

    try:
        image = build_image(payload, version)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    try:
        with open(args.out_image, "wb") as f:
            f.write(image)
    except OSError as e:
        print(f"error writing {args.out_image}: {e}", file=sys.stderr)
        return 1

    crc = struct.unpack_from("<I", image, 8)[0]
    print(
        f"wrote {len(image)} bytes → {args.out_image}  "
        f"(payload={len(payload)}B  crc=0x{crc:08X}  version=0x{version:08X})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
