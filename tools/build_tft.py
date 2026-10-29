#!/usr/bin/env python3
"""Build a public NSPanel display TFT from a stock base and the C source.

The native terminal is compiled as two freestanding Cortex-M0+ objects, placed
at their fixed SRAM addresses, and patched into the documented regions of the
selected base TFT. The builder also repairs every affected package checksum.

Two profiles are supported:

* ``eu`` - landscape 454x320 visible area inside a 480x320 controller.
* ``us`` - portrait 320x480 visible area and controller.

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

from panel_profiles import PROFILES

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "firmware" / "src"

# Fixed addresses and package offsets shared by the EU and US stock exports. Do
# not change these without changing the loader and the documented TFT layout.
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
NATIVE_METADATA_START = 0xB4
NATIVE_METADATA_END = 0xD3
NATIVE_META_CRC_OFFSET = 0xB4
NATIVE_META_BODY_CRC_OFFSET = 0xBE


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


def compile_object(
    cc: str, stem: str, output: Path, include: Path, profile: Profile
) -> None:
    source = SRC / f"{stem}.c"
    if not source.is_file():
        fail(f"source file is missing: {source}")
    command = [
        cc,
        *COMPILE_FLAGS,
        *profile.defines,
        "-I",
        str(include),
        "-c",
        str(source),
        "-o",
        str(output),
    ]
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
    tag, relative, size, _reserved, _reserved2 = struct.unpack_from(
        "<HIIII", tft, NATIVE_BUNDLE + 16
    )
    if tag != 0xF130 or size != NATIVE_SIZE:
        fail(f"unexpected native record: tag={tag:#06x}, size={size}")
    return NATIVE_BUNDLE + relative


def native_version(tft: bytes) -> int:
    start = native_start(tft)
    return struct.unpack_from("<H", tft, start + NATIVE_VERSION_OFFSET)[0]


def patch_native_image(
    tft: bytes, version: int, patches: tuple[tuple[int, bytes], ...]
) -> bytes:
    """Apply decoded native patches, set the version and repair metadata CRCs.

    The native metadata at 0xB4..0xD2 is stored unencoded; the rest of the
    native image is XOR-encoded with the key at 0xC2. The CRC at 0xBE covers
    the encoded image outside the metadata block, and the CRC at 0xB4 covers
    the metadata from 0xB8 through 0xD2. This is the same layout used by the
    earlier EU version bumps.
    """
    from tft_crc import crc

    start = native_start(tft)
    raw = tft[start : start + NATIVE_SIZE]
    key = raw[0xC2]
    native = bytearray(
        value
        if NATIVE_METADATA_START <= offset < NATIVE_METADATA_END
        else value ^ key
        for offset, value in enumerate(raw)
    )
    for offset, data in patches:
        if offset < NATIVE_METADATA_END or offset + len(data) > NATIVE_SIZE:
            fail(f"invalid native patch range: {offset:#x}+{len(data)}")
        native[offset : offset + len(data)] = data
    struct.pack_into("<H", native, NATIVE_VERSION_OFFSET, version)
    encoded = bytearray(
        value
        if NATIVE_METADATA_START <= offset < NATIVE_METADATA_END
        else value ^ key
        for offset, value in enumerate(native)
    )
    struct.pack_into(
        "<I",
        encoded,
        NATIVE_META_BODY_CRC_OFFSET,
        crc(encoded[:NATIVE_METADATA_START] + encoded[NATIVE_METADATA_END:]),
    )
    struct.pack_into(
        "<I",
        encoded,
        NATIVE_META_CRC_OFFSET,
        crc(encoded[NATIVE_VERSION_OFFSET:NATIVE_METADATA_END]),
    )
    out = bytearray(tft)
    out[start : start + NATIVE_SIZE] = encoded
    return bytes(out)


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
    struct.pack_into(
        "<I", out, len(out) - 4, crc(out[:-4]) ^ out[3] ^ out[0x2E] ^ out[0x3C]
    )
    return bytes(out)


def build(
    base: bytes, work: Path, cc: str, profile: Profile
) -> tuple[bytes, dict[str, object]]:
    from tft_crc import crc

    if len(base) != profile.base_size:
        fail(f"base TFT is {len(base)} bytes, expected {profile.base_size}")
    base_hash = sha256(base)
    if base_hash != profile.base_sha256:
        fail(
            f"base TFT SHA-256 is {base_hash}, expected {profile.base_sha256}. "
            f"This source is tied to the documented {profile.name.upper()} base."
        )
    base_version = native_version(base)
    if base_version != profile.base_native_version:
        fail(
            f"base TFT reports native version {base_version}, expected "
            f"{profile.base_native_version}"
        )
    working = (
        base
        if base_version == profile.native_version and not profile.native_patches
        else patch_native_image(base, profile.native_version, profile.native_patches)
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

    compile_object(cc, "terminal_receiver", program_obj, work, profile)
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

    compile_object(cc, "terminal_loader", loader_obj, work, profile)
    loader, _ = assemble(loader_obj, LOADER_BASE)
    if len(loader) > LOADER_LIMIT:
        fail(f"helper-0 loader is {len(loader)} bytes; the slot allows {LOADER_LIMIT}")

    candidate = patch_package(working, program, loader)
    start = native_start(candidate)
    version = native_version(candidate)
    if version != profile.native_version:
        fail(
            f"patched package reports native version {version}, expected "
            f"{profile.native_version}"
        )

    native_allowed: set[int] = set()
    for offset, data in profile.native_patches:
        native_allowed |= set(range(offset, offset + len(data)))
    if base_version != profile.native_version:
        native_allowed |= set(
            range(NATIVE_VERSION_OFFSET, NATIVE_VERSION_OFFSET + 2)
        )
        native_allowed |= set(
            range(NATIVE_META_CRC_OFFSET, NATIVE_META_CRC_OFFSET + 4)
        )
        native_allowed |= set(
            range(NATIVE_META_BODY_CRC_OFFSET, NATIVE_META_BODY_CRC_OFFSET + 4)
        )
    native_changed = {
        offset
        for offset, (before, after) in enumerate(
            zip(base[start : start + NATIVE_SIZE], candidate[start : start + NATIVE_SIZE])
        )
        if before != after
    }
    if not native_changed <= native_allowed:
        outside = sorted(native_changed - native_allowed)[:8]
        fail(f"unexpected native image change outside the version metadata: {outside}")

    # Fail closed on any byte outside the documented patch regions. This keeps
    # the UI assets, native image, other helpers and updater byte-identical.
    entry = HELPER_BUNDLE + 8 + HELPER_INDEX * 12
    helper_start = HELPER_BUNDLE + HELPER_OFFSET
    allowed = set(range(PREAMBLE_START, PREAMBLE_END))
    allowed |= set(range(0x44, 0x48)) | set(range(0xC4, 0xC8))
    allowed |= set(range(HELPER_BUNDLE, HELPER_BUNDLE + 4))
    allowed |= set(range(entry, entry + 4)) | set(range(entry + 8, entry + 12))
    allowed |= set(range(helper_start, helper_start + HELPER_SLOT))
    allowed |= {start + offset for offset in native_allowed}
    allowed |= set(range(len(base) - 4, len(base)))
    changed = {
        i for i, (before, after) in enumerate(zip(base, candidate)) if before != after
    }
    if not changed <= allowed:
        outside = sorted(changed - allowed)[:8]
        fail(f"unexpected package change outside the patch regions: {outside}")

    report: dict[str, object] = {
        "panel": profile.name,
        "panel_width": profile.panel_width,
        "panel_height": profile.panel_height,
        "entry_width": profile.entry_width,
        "entry_height": profile.entry_height,
        "entry_state": profile.entry_state,
        "base_native_version": profile.base_native_version,
        "native_version": profile.native_version,
        "native_patches": len(profile.native_patches),
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
        "--panel",
        choices=sorted(PROFILES),
        default="eu",
        help="panel profile to build (default: eu)",
    )
    parser.add_argument(
        "--base",
        type=Path,
        help="override the profile's base TFT (the profile checksum still applies)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="output TFT (default: the profile's build/ path)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="require a byte-for-byte match with the profile's shipped TFT",
    )
    parser.add_argument("--cc", help="clang to use (default: $CC or clang on PATH)")
    parser.add_argument("--json", action="store_true", help="print a JSON build report")
    args = parser.parse_args()

    profile = PROFILES[args.panel]
    base_path = args.base or profile.base
    output_arg = args.output or profile.output
    if not base_path.is_file():
        fail(f"base TFT not found: {base_path}")
    output = output_arg.resolve()
    for protected in (profile.base, profile.shipped):
        if output == protected.resolve():
            fail(f"refusing to overwrite the checked-in TFT: {output_arg}")
    base = base_path.read_bytes()
    cc = args.cc or find_compiler()

    output_arg.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".build-", dir=output_arg.parent) as temp:
        candidate, report = build(base, Path(temp), cc, profile)

    if args.check:
        tried = []
        for shipped_path in dict.fromkeys((profile.shipped, output_arg)):
            if shipped_path.is_file():
                tried.append(shipped_path)
        if not tried:
            fail(f"no shipped {profile.name.upper()} TFT to compare against")
        shipped_path = tried[0]
        if candidate != shipped_path.read_bytes():
            fail(
                f"build does not match {shipped_path.relative_to(ROOT)}. "
                "A different compiler version or a source change can cause this; "
                "see docs/BUILDING.md."
            )
        report["check"] = str(shipped_path.relative_to(ROOT))

    tmp = output_arg.with_name(output_arg.name + ".tmp")
    tmp.write_bytes(candidate)
    tmp.replace(output_arg)
    report["output"] = str(output_arg)
    report["output_sha256"] = sha256(candidate)

    if args.json:
        print(json.dumps(report, indent=2))
        return
    print(f"wrote {output_arg}")
    print(f"  panel   : {profile.name} ({profile.panel_width}x{profile.panel_height})")
    print(f"  SHA-256 : {report['output_sha256']}")
    print(f"  program : {report['program_bytes']} / {report['program_limit']} bytes")
    print(f"  loader  : {report['loader_bytes']} / {report['loader_limit']} bytes")
    print(f"  changed : {report['changed_bytes']} bytes")
    if args.check:
        print(f"  check   : byte-for-byte match with {report['check']}")
    elif report["output_sha256"] == profile.shipped_sha256:
        print("  note    : output matches the shipped TFT")


if __name__ == "__main__":
    main()
