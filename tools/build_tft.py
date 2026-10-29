#!/usr/bin/env python3
"""Build the public NSPanel display TFT from the retained base and C source.

The native terminal is compiled as two freestanding Cortex-M0+ objects, placed
at their fixed SRAM addresses, and patched into the documented regions of the
base TFT. The builder also repairs every affected package checksum.

It deliberately does not upload anything. See docs/BUILDING.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "firmware" / "src"
BASE = ROOT / "firmware" / "nspanel-v92-blank-base.tft"
SHIPPED = ROOT / "firmware" / "nspanel-v92-blank.tft"
DEFAULT_OUTPUT = ROOT / "build" / "nspanel-v92-blank.tft"

BASE_SIZE = 264936
BASE_SHA256 = "82434ab839ed14af45c655bdf1980848649c6a48081ee593c5cce397df7f3628"
SHIPPED_SHA256 = "a25aafc263b8980e1f91e9bfe6f363d2197c657bb5a2231f86d61a9828a3d10c"

# Fixed addresses and package offsets for this V92 base. Do not change these
# without changing the loader and the documented TFT layout.
PROGRAM_BASE = 0x20000900
PROGRAM_LIMIT = 0x200016F0
LOADER_BASE = 0x200016F0
LOADER_LIMIT = 844
PREAMBLE_START = 0x1000
PREAMBLE_END = 0x2000
HELPER_BUNDLE = 0x357B6
HELPER_INDEX = 0
HELPER_OFFSET = 0x74
HELPER_SLOT = 844
NATIVE_BUNDLE = 0x14532
NATIVE_SIZE = 60352
NATIVE_VERSION_OFFSET = 0xB8
EXPECTED_NATIVE_VERSION = 92

COMPILE_FLAGS = [
    "-target",
    "armv6m-none-eabi",
    "-mcpu=cortex-m0plus",
    "-mthumb",
    "-Oz",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-fstack-usage",
    "-Wall",
    "-Wextra",
    "-Werror",
]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fail(message: str) -> None:
    print(f"build_tft.py: error: {message}", file=sys.stderr)
    raise SystemExit(1)


def find_compiler() -> str:
    requested = os.environ.get("CC")
    if requested:
        found = shutil.which(requested)
        if not found:
            fail(f"CC={requested!r} was not found")
        return found
    found = shutil.which("clang")
    if not found:
        fail(
            "clang was not found. Install a clang with the ARM backend "
            "(Xcode Command Line Tools on macOS, or the clang package on Linux) "
            "and put it on PATH, or set CC=/path/to/clang."
        )
    return found


def compile_object(cc: str, stem: str, output: Path, include: Path) -> None:
    source = SRC / f"{stem}.c"
    if not source.is_file():
        fail(f"source file is missing: {source}")
    command = [cc, *COMPILE_FLAGS, "-I", str(include), "-c", str(source), "-o", str(output)]
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        print("command:", " ".join(command), file=sys.stderr)
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        fail(f"compiling {source.name} failed")


def assemble(elf_path: Path, base: int) -> tuple[bytes, dict[str, int]]:
    """Place the ELF's allocatable sections at their fixed SRAM addresses."""
    from elf_thumb import Elf

    elf = Elf(elf_path, base)
    payload = bytearray(elf.end - base)
    for index, part in elf.data.items():
        start = elf.addr[index] - base
        payload[start : start + len(part)] = part
    return bytes(payload), dict(elf.symbols)


def native_start(tft: bytes) -> int:
    tag, relative, size, _reserved, _reserved2 = struct.unpack_from("<HIIII", tft, NATIVE_BUNDLE + 16)
    if tag != 0xF130 or size != NATIVE_SIZE:
        fail(f"unexpected native record: tag={tag:#06x}, size={size}")
    return NATIVE_BUNDLE + relative


def patch_package(base: bytes, program: bytes, loader: bytes) -> bytes:
    from tft_crc import crc

    out = bytearray(base)
    out[PREAMBLE_START:PREAMBLE_END] = program + b"\xff" * (
        PREAMBLE_END - PREAMBLE_START - len(program)
    )

    entry = HELPER_BUNDLE + 8 + HELPER_INDEX * 12
    offset, slot, _old_crc = struct.unpack_from("<III", out, entry)
    if offset != HELPER_OFFSET or slot != HELPER_SLOT:
        fail(f"unexpected helper 0 layout: offset={offset:#x}, slot={slot}")
    helper_start = HELPER_BUNDLE + offset
    out[helper_start : helper_start + slot] = loader + b"\xff" * (slot - len(loader))
    struct.pack_into("<I", out, entry + 8, crc(out[helper_start : helper_start + slot]))
    struct.pack_into(
        "<I", out, HELPER_BUNDLE, crc(out[HELPER_BUNDLE + 4 : HELPER_BUNDLE + 0x15E0])
    )

    # The preamble and helper slot sit outside these regions, but repairing all
    # affected package checksums keeps the output verifiable by verify_tft.py.
    struct.pack_into("<I", out, 0x44, crc(out[0x10000:0x40000]))
    struct.pack_into("<I", out, 0xC4, crc(out[:0xC4]))
    struct.pack_into("<I", out, len(out) - 4, crc(out[:-4]) ^ out[3] ^ out[0x2E] ^ out[0x3C])
    return bytes(out)


def build(base: bytes, work: Path, cc: str) -> tuple[bytes, dict[str, object]]:
    from tft_crc import crc

    if len(base) != BASE_SIZE:
        fail(f"base TFT is {len(base)} bytes, expected {BASE_SIZE}")
    base_hash = sha256(base)
    if base_hash != BASE_SHA256:
        fail(
            f"base TFT SHA-256 is {base_hash}, expected {BASE_SHA256}. "
            "This source is tied to the documented V92 base."
        )

    program_obj = work / "terminal_receiver.o"
    loader_obj = work / "terminal_loader.o"
    meta = work / "terminal_receiver_meta.h"

    stale_meta = SRC / "terminal_receiver_meta.h"
    if stale_meta.exists():
        fail(
            f"remove the stale {stale_meta}: it would override the generated "
            "header and can make the loader disagree with the program"
        )

    compile_object(cc, "terminal_receiver", program_obj, work)
    program, symbols = assemble(program_obj, PROGRAM_BASE)
    if len(program) > PROGRAM_LIMIT - PROGRAM_BASE:
        fail(
            f"terminal program is {len(program)} bytes; the project pool allows "
            f"{PROGRAM_LIMIT - PROGRAM_BASE}"
        )
    if symbols.get("pool_entry") != PROGRAM_BASE + 1:
        fail("compiled pool_entry is not at the documented Thumb entry address")

    meta.write_text(
        "/* generated by tools/build_tft.py; do not edit */\n"
        f"#define POOL_BYTES {len(program)}u\n"
        f"#define POOL_CRC 0x{crc(program):08x}u\n"
        f"#define POOL_ENTRY 0x{PROGRAM_BASE + 1:08x}u\n"
    )

    compile_object(cc, "terminal_loader", loader_obj, work)
    loader, _ = assemble(loader_obj, LOADER_BASE)
    if len(loader) > LOADER_LIMIT:
        fail(f"helper-0 loader is {len(loader)} bytes; the slot allows {LOADER_LIMIT}")

    candidate = patch_package(base, program, loader)
    start = native_start(candidate)
    if struct.unpack_from("<H", candidate, start + NATIVE_VERSION_OFFSET)[0] != EXPECTED_NATIVE_VERSION:
        fail("patched package does not report native version 92")
    if candidate[0x14566 : 0x14566 + NATIVE_SIZE] != base[0x14566 : 0x14566 + NATIVE_SIZE]:
        fail("native F130 image changed; this builder should only replace RAM code")

    # Fail closed on any byte outside the documented patch regions. This keeps
    # the UI assets, native image, other helpers and updater byte-identical.
    entry = HELPER_BUNDLE + 8 + HELPER_INDEX * 12
    helper_start = HELPER_BUNDLE + HELPER_OFFSET
    allowed = set(range(PREAMBLE_START, PREAMBLE_END))
    allowed |= set(range(0x44, 0x48)) | set(range(0xC4, 0xC8))
    allowed |= set(range(HELPER_BUNDLE, HELPER_BUNDLE + 4))
    allowed |= set(range(entry, entry + 4)) | set(range(entry + 8, entry + 12))
    allowed |= set(range(helper_start, helper_start + HELPER_SLOT))
    allowed |= set(range(len(base) - 4, len(base)))
    changed = {i for i, (before, after) in enumerate(zip(base, candidate)) if before != after}
    if not changed <= allowed:
        outside = sorted(changed - allowed)[:8]
        fail(f"unexpected package change outside the patch regions: {outside}")

    report: dict[str, object] = {
        "base_sha256": base_hash,
        "program_bytes": len(program),
        "program_limit": PROGRAM_LIMIT - PROGRAM_BASE,
        "loader_bytes": len(loader),
        "loader_limit": LOADER_LIMIT,
        "changed_bytes": len(changed),
        "candidate_sha256": sha256(candidate),
        "candidate_bytes": len(candidate),
    }
    return candidate, report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base",
        type=Path,
        default=BASE,
        help="V92 base TFT (default: firmware/nspanel-v92-blank-base.tft)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="output TFT (default: build/nspanel-v92-blank.tft)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="require a byte-for-byte match with firmware/nspanel-v92-blank.tft",
    )
    parser.add_argument("--cc", help="clang to use (default: $CC or clang on PATH)")
    parser.add_argument("--json", action="store_true", help="print a JSON build report")
    args = parser.parse_args()

    if not args.base.is_file():
        fail(f"base TFT not found: {args.base}")
    output = args.output.resolve()
    for protected in (BASE, SHIPPED):
        if output == protected.resolve():
            fail(f"refusing to overwrite the checked-in TFT: {args.output}")
    base = args.base.read_bytes()
    cc = args.cc or find_compiler()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".build-", dir=args.output.parent) as temp:
        candidate, report = build(base, Path(temp), cc)

    if args.check:
        shipped = SHIPPED.read_bytes()
        if candidate != shipped:
            fail(
                "build does not match firmware/nspanel-v92-blank.tft. "
                "A different compiler version or a source change can cause this; "
                "see docs/BUILDING.md."
            )
        report["check"] = "matches firmware/nspanel-v92-blank.tft"

    tmp = args.output.with_name(args.output.name + ".tmp")
    tmp.write_bytes(candidate)
    tmp.replace(args.output)
    report["output"] = str(args.output)
    report["output_sha256"] = sha256(candidate)

    if args.json:
        print(json.dumps(report, indent=2))
        return
    print(f"wrote {args.output}")
    print(f"  SHA-256 : {report['output_sha256']}")
    print(f"  program : {report['program_bytes']} / {report['program_limit']} bytes")
    print(f"  loader  : {report['loader_bytes']} / {report['loader_limit']} bytes")
    print(f"  changed : {report['changed_bytes']} bytes")
    if args.check:
        print("  check   : byte-for-byte match with the shipped TFT")
    elif report["output_sha256"] == SHIPPED_SHA256:
        print("  note    : output matches the shipped TFT")


if __name__ == "__main__":
    main()
