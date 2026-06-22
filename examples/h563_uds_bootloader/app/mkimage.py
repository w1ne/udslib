#!/usr/bin/env python3
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""
mkimage.py — pack a raw app .bin into a valid, RSA-2048-signed OTA image.

Usage:
    mkimage.py <app.bin> <out_image.bin> [--version 0xVVVVVVVV]
                                         [--key signing_key_dev.pem]
                                         [--tamper-signature]

Output layout (matches ota_image.h / app_jump.c):

    Offset            Size     Field
    ------            ----     -----
    0x000              4       magic      = 0xC0DEBEEF  (little-endian u32)
    0x004              4       image_size = len(payload) in bytes
    0x008              4       crc32      = CRC-32/ISO-HDLC over payload bytes
    0x00C              4       version    = caller-supplied (default 0x00010000)
    0x010           0x3F0      RESERVED padding (0xFF bytes)
    0x400              N       payload    = raw .bin contents (vector table at byte 0)
    0x400+N          256       signature  = RSA-2048 PKCS#1 v1.5 over SHA-256(payload)

The header region is 0x400 (1024) bytes so the app vector table lands at
app_base + 0x400, which is 1024-byte aligned — satisfying Cortex-M33 VTOR
alignment for a full STM32H563 vector table (~147 entries, next pow2 = 1024).

CRC algorithm: CRC-32/ISO-HDLC
  Poly:     0x04C11DB7 (bit-reflected: 0xEDB88320)
  Init:     0xFFFFFFFF
  XOR-out:  0xFFFFFFFF
  Reflect:  input and output
Python's zlib.crc32() computes exactly this algorithm. Matches ota_crc32().

Image authenticity (secure boot):
  Scheme:    RSA-2048, PKCS#1 v1.5 (RSASSA-PKCS1-v1_5)
  Hash:      SHA-256 over the payload bytes (the SAME bytes the CRC covers)
  Signature: 256 raw big-endian bytes (RSA-2048), appended after the payload.
The bootloader verifies it with mbedtls_rsa_pkcs1_verify against the baked
public key (modulus n + exponent e = 65537). RSA verify (m^65537 mod n) is
~100x cheaper than an ECDSA-P256 verify, completing well under the simulator's
50M-instruction cap. The private key used here (app/signing_key_dev.pem) is a
DEMO key committed for the example only — a real product keeps the signing key
in an HSM / offline and NEVER in the repo.
"""

import argparse
import struct
import sys
import zlib

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

OTA_IMAGE_MAGIC = 0xC0DEBEEF
OTA_IMAGE_HDR_SIZE = 0x400          # total header region: 1024 bytes
OTA_IMAGE_STRUCT_SIZE = 16          # ota_image_header_t: magic/image_size/crc32/version
OTA_IMAGE_MAX_PAYLOAD = 0xE8000 - OTA_IMAGE_HDR_SIZE  # 0xE7C00
OTA_SIG_SIZE = 256                  # RSA-2048 signature, raw big-endian


def sign_payload(payload: bytes, key_path: str) -> bytes:
    """
    Sign SHA-256(payload) with the RSA-2048 private key in key_path (PEM) using
    PKCS#1 v1.5. Returns the 256-byte big-endian signature.
    """
    with open(key_path, "rb") as f:
        priv = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(priv, rsa.RSAPrivateKey) or priv.key_size != 2048:
        raise ValueError(f"{key_path}: not an RSA-2048 private key")

    sig = priv.sign(payload, padding.PKCS1v15(), hashes.SHA256())
    if len(sig) != OTA_SIG_SIZE:
        raise ValueError(f"unexpected signature length {len(sig)} != {OTA_SIG_SIZE}")
    return sig


def build_image(payload: bytes, version: int, key_path: str,
                tamper_signature: bool = False) -> bytes:
    """
    Build a complete, signed OTA image:
      [0x400-byte header region][payload][256-byte RSA-2048 PKCS#1 v1.5 signature].

    Header region layout:
      [0x000..0x010)  ota_image_header_t (16 bytes, little-endian):
        uint32_t magic       = OTA_IMAGE_MAGIC
        uint32_t image_size  = len(payload)
        uint32_t crc32       = CRC-32/ISO-HDLC over payload
        uint32_t version     = supplied version word
      [0x010..0x400)  RESERVED padding (0xFF, 1008 bytes)

    The signature covers SHA-256 of the payload (same bytes as the CRC). When
    tamper_signature is set, one byte of the signature is flipped so the image
    keeps a valid CRC but FAILS authenticity verification — used to prove the
    signature gate in negative demos/tests.
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

    struct_bytes = struct.pack(
        "<4I",
        OTA_IMAGE_MAGIC,
        len(payload),
        crc,
        version,
    )
    assert len(struct_bytes) == OTA_IMAGE_STRUCT_SIZE

    pad_bytes = bytes([0xFF]) * (OTA_IMAGE_HDR_SIZE - OTA_IMAGE_STRUCT_SIZE)
    header = struct_bytes + pad_bytes
    assert len(header) == OTA_IMAGE_HDR_SIZE

    signature = sign_payload(payload, key_path)
    assert len(signature) == OTA_SIG_SIZE
    if tamper_signature:
        # Flip one bit of the last signature byte: CRC still passes, signature
        # verification must fail.
        signature = signature[:-1] + bytes([signature[-1] ^ 0x01])

    return header + payload + signature


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pack a raw app .bin into a signed OTA image with ota_image_header_t."
    )
    parser.add_argument("app_bin", help="Input raw binary (.bin)")
    parser.add_argument("out_image", help="Output OTA image (.bin)")
    parser.add_argument(
        "--version",
        default="0x00010000",
        help="32-bit version word, hex (default: 0x00010000 = v1.0.0)",
    )
    parser.add_argument(
        "--key",
        default="signing_key_dev.pem",
        help="RSA-2048 private key (PEM) used to sign the payload "
             "(default: signing_key_dev.pem — a DEMO key).",
    )
    parser.add_argument(
        "--tamper-signature",
        action="store_true",
        help="Flip one signature byte: image stays CRC-good but signature-bad "
             "(for negative secure-boot demos/tests).",
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
        image = build_image(payload, version, args.key, args.tamper_signature)
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    try:
        with open(args.out_image, "wb") as f:
            f.write(image)
    except OSError as e:
        print(f"error writing {args.out_image}: {e}", file=sys.stderr)
        return 1

    crc = struct.unpack_from("<I", image, 8)[0]
    tamper = "  [SIGNATURE TAMPERED]" if args.tamper_signature else ""
    print(
        f"wrote {len(image)} bytes → {args.out_image}  "
        f"(payload={len(payload)}B  crc=0x{crc:08X}  version=0x{version:08X}  "
        f"sig=256B RSA-2048){tamper}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
