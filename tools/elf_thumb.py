#!/usr/bin/env python3
"""Minimal, fail-closed ELF32 ARM REL reader for fixed-address objects.

The display firmware is not linked at build time: clang emits a relocatable
object and this module places its allocatable sections at a fixed SRAM address,
applying only the relocation types used by the terminal objects. It is not a
general-purpose linker and does not produce a bootable image.
"""
from __future__ import annotations

import struct
from pathlib import Path


class ElfError(Exception):
    """Raised for an ELF feature this loader does not implement."""


class Elf:
    """An ELF32 little-endian ARM relocatable object placed at *base*."""

    def __init__(self, path: Path, base: int = 0x08001000) -> None:
        blob = Path(path).read_bytes()
        if blob[:7] != b"\x7fELF\x01\x01\x01":
            raise ElfError(f"{path}: not a 32-bit little-endian ELF")
        if struct.unpack_from("<HH", blob, 16) != (1, 40):
            raise ElfError(f"{path}: expected ET_REL / EM_ARM")

        shoff = struct.unpack_from("<I", blob, 32)[0]
        shentsize, shnum, _shstrndx = struct.unpack_from("<HHH", blob, 46)
        self.sections = [
            list(struct.unpack_from("<10I", blob, shoff + i * shentsize)) for i in range(shnum)
        ]

        # Lay allocatable PROGBITS/NOBITS sections out in file order, honouring
        # their alignment. This is the fixed-address analogue of a linker's
        # section placement and matches the addresses in docs/FIRMWARE.md.
        self.addr: dict[int, int] = {}
        self.data: dict[int, bytearray] = {}
        cursor = base
        for index, section in enumerate(self.sections):
            if not section[2] & 2 or section[1] not in (1, 8):
                continue
            align = max(section[8], 1)
            cursor = (cursor + align - 1) & ~(align - 1)
            self.addr[index] = cursor
            if section[1] == 8:  # SHT_NOBITS
                self.data[index] = bytearray(section[5])
            else:
                self.data[index] = bytearray(blob[section[4] : section[4] + section[5]])
            cursor += section[5]
        self.end = cursor

        self.symbols: dict[str, int] = {}
        self.functions: dict[str, tuple[int, int]] = {}
        symbol_tables: dict[int, list[tuple[str, int, int]]] = {}
        for index, section in enumerate(self.sections):
            if section[1] != 2:  # SHT_SYMTAB
                continue
            strings_section = self.sections[section[6]]
            strings = blob[
                strings_section[4] : strings_section[4] + strings_section[5]
            ]
            symbols: list[tuple[str, int, int]] = []
            for offset in range(section[4], section[4] + section[5], 16):
                name_offset, value, size, info, _other, section_index = struct.unpack_from(
                    "<IIIBBH", blob, offset
                )
                name = strings[name_offset:].split(b"\0")[0].decode("ascii", "replace")
                symbols.append((name, value, section_index))
                if section_index in self.addr and name:
                    self.symbols[name] = self.addr[section_index] + value
                    if info & 15 == 2:  # STT_FUNC
                        self.functions[name] = (self.addr[section_index] + value, size)
            symbol_tables[index] = symbols

        self.relocations: list[dict[str, str | int]] = []
        for section in self.sections:
            if section[1] != 9 or section[7] not in self.data:  # SHT_REL
                continue
            target_data = self.data[section[7]]
            target_start = self.addr[section[7]]
            for offset in range(section[4], section[4] + section[5], 8):
                place_offset, info = struct.unpack_from("<II", blob, offset)
                relocation_type = info & 0xFF
                name, value, symbol_section = symbol_tables[section[6]][info >> 8]
                if symbol_section not in self.addr:
                    raise ElfError(f"undefined symbol {name!r} in {path}")
                target = self.addr[symbol_section] + value
                place = target_start + place_offset
                if relocation_type == 2:  # R_ARM_ABS32
                    current = struct.unpack_from("<I", target_data, place_offset)[0]
                    struct.pack_into("<I", target_data, place_offset, (current + target) & 0xFFFFFFFF)
                elif relocation_type == 3:  # R_ARM_REL32
                    current = struct.unpack_from("<I", target_data, place_offset)[0]
                    struct.pack_into(
                        "<I", target_data, place_offset, (current + target - place) & 0xFFFFFFFF
                    )
                elif relocation_type in (10, 30):  # R_ARM_THM_CALL / R_ARM_THM_JUMP24
                    self._patch_thumb_branch(target_data, place_offset, target, place, name)
                else:
                    raise ElfError(
                        f"unsupported relocation type {relocation_type} for {name!r}"
                    )
                self.relocations.append(
                    {"type": relocation_type, "symbol": name, "address": hex(place)}
                )

    @staticmethod
    def _patch_thumb_branch(
        data: bytearray, offset: int, target: int, place: int, symbol: str
    ) -> None:
        high, low = struct.unpack_from("<HH", data, offset)
        sign = (high >> 10) & 1
        j1 = (low >> 13) & 1
        j2 = (low >> 11) & 1
        i1 = 1 ^ (j1 ^ sign)
        i2 = 1 ^ (j2 ^ sign)
        immediate = (
            (sign << 24)
            | (i1 << 23)
            | (i2 << 22)
            | ((high & 1023) << 12)
            | ((low & 2047) << 1)
        )
        if sign:
            immediate -= 1 << 25
        delta = (target & ~1) + immediate - place
        if delta % 2 or not -(1 << 24) <= delta < (1 << 24):
            raise ElfError(f"Thumb branch out of range for {symbol!r}")
        encoded = delta & 0x1FFFFFF
        sign = encoded >> 24
        i1 = (encoded >> 23) & 1
        i2 = (encoded >> 22) & 1
        j1 = 1 ^ (i1 ^ sign)
        j2 = 1 ^ (i2 ^ sign)
        struct.pack_into(
            "<HH",
            data,
            offset,
            (high & 0xF800) | (sign << 10) | ((encoded >> 12) & 1023),
            (low & 0xD000) | (j1 << 13) | (j2 << 11) | ((encoded >> 1) & 2047),
        )
