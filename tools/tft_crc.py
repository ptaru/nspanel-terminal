#!/usr/bin/env python3
"""CRC used by the Nextion TFT container and the NSPanel helper bundle.

The algorithm feeds each byte as a zero-extended 32-bit word, MSB first, with
no final XOR. It is deliberately small and dependency-free so the builder and
the verifier agree bit-for-bit.
"""
from __future__ import annotations

TABLE: list[int] = []
for _n in range(256):
    _r = _n << 24
    for _ in range(8):
        _r = ((_r << 1) ^ (0x04C11DB7 if _r & 0x80000000 else 0)) & 0xFFFFFFFF
    TABLE.append(_r)


def crc(data: bytes, seed: int = 0xFFFFFFFF) -> int:
    """Return the TFT/helper CRC of *data*."""
    value = seed
    for byte in data:
        value ^= byte
        for _ in range(4):
            value = ((value << 8) & 0xFFFFFFFF) ^ TABLE[value >> 24]
    return value
