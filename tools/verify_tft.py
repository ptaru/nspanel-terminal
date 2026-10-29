#!/usr/bin/env python3
"""Verify a V92 blank NSPanel TFT: size, SHA-256, CRCs, identity and UI.

The default mode checks the official shipped image. Use --allow-modified for a
local source build: the SHA-256 is then informational, but every structural and
CRC check still runs.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tft_crc import crc  # noqa: E402

EXPECTED_SIZE = 264936
EXPECTED_SHA256 = "a25aafc263b8980e1f91e9bfe6f363d2197c657bb5a2231f86d61a9828a3d10c"
EXPECTED_F130_SIZE = 60352
EXPECTED_F130_SHA256 = "be6ef7a50c901d1c8135387e502553f7ad295223c967cdaf2ca564b8251f3d79"
EXPECTED_NATIVE_VERSION = 92
EXPECTED_ALLOCATION = 0x30000
EXPECTED_HELPER_SLOT = 0x74
EXPECTED_HELPER_SIZE = 844


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="TFT to verify")
    parser.add_argument(
        "--allow-modified",
        action="store_true",
        help="skip the official SHA-256 equality check for a local source build",
    )
    args = parser.parse_args()

    path = args.path
    if not path.is_file():
        fail(f"not a file: {path}")
    data = path.read_bytes()

    if len(data) != EXPECTED_SIZE:
        fail(f"size is {len(data)}, expected {EXPECTED_SIZE}")
    digest = hashlib.sha256(data).hexdigest()
    if not args.allow_modified and digest != EXPECTED_SHA256:
        fail(f"SHA-256 is {digest}, expected {EXPECTED_SHA256}")

    if u32(data, 0x04) != 0x14532:
        fail("unexpected native-bundle start")
    if u32(data, 0x18) != 0x31D96 or u32(data, 0x1E) != 0x31D96:
        fail("unexpected native-bundle end")
    if u32(data, 0x2A) != 0x1D864:
        fail("unexpected native-bundle length")
    if u32(data, 0x3C) != EXPECTED_SIZE:
        fail("header file-size mismatch")
    if u32(data, 0x40) != EXPECTED_ALLOCATION:
        fail("unexpected resource allocation size")

    tag, relative, size, _, _ = struct.unpack_from("<HIIII", data, 0x14532 + 16)
    start = 0x14532 + relative
    if tag != 0xF130 or size != EXPECTED_F130_SIZE:
        fail(f"unexpected F130 record: tag={tag:#06x}, size={size}")
    if struct.unpack_from("<H", data, start + 0xB8)[0] != EXPECTED_NATIVE_VERSION:
        fail("F130 native version is not 92")
    decoded = bytes(value ^ 9 for value in data[start : start + size])
    if hashlib.sha256(decoded).hexdigest() != EXPECTED_F130_SHA256:
        fail("unexpected patched F130 image hash")

    helper = 0x357B6
    offset, slot, _ = struct.unpack_from("<III", data, helper + 8)
    if offset != EXPECTED_HELPER_SLOT or slot != EXPECTED_HELPER_SIZE:
        fail(f"unexpected helper entry: offset={offset:#x}, slot={slot}")
    if crc(data[helper + offset : helper + offset + slot]) != u32(data, helper + 16):
        fail("helper 0 CRC mismatch")
    if crc(data[helper + 4 : helper + 0x15E0]) != u32(data, helper):
        fail("helper bundle CRC mismatch")

    if crc(data[0x10000 : 0x10000 + EXPECTED_ALLOCATION]) != u32(data, 0x44):
        fail("resource CRC mismatch")
    if crc(data[:0xC4]) != u32(data, 0xC4):
        fail("header 1 CRC mismatch")
    if crc(data[0xC8:0x18C]) != u32(data, 0x18C):
        fail("header 2 CRC mismatch")
    file_crc = crc(data[:-4]) ^ data[3] ^ data[0x2E] ^ data[0x3C]
    if file_crc != u32(data, len(data) - 4):
        fail("whole-file CRC mismatch")

    if b"waiting for connection" not in data:
        fail("blank UI text missing")
    for forbidden in (b"ceiling light", b"cabinet lights", b"Corridor", b"Living Room"):
        if forbidden in data:
            fail(f"personal label found: {forbidden!r}")

    official = digest == EXPECTED_SHA256
    print("OK")
    print(f"  file       : {path}")
    print(f"  size       : {len(data)} bytes")
    if official:
        print(f"  sha256     : {digest}")
    else:
        print(f"  sha256     : {digest} (modified build, not the official image)")
    print("  display    : NX4832F035_011C / F130 / 61744")
    print(f"  native     : version {EXPECTED_NATIVE_VERSION}")
    print("  UI         : blank 'waiting for connection' page")
    print("  crcs       : header1, header2, resources, helper, file all match")


if __name__ == "__main__":
    main()
