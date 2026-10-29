# Building the display firmware

The display TFT is built from the retained V92 base and the native C source in
[`firmware/src/`](../firmware/src/). The build is offline: it uses a C compiler
and Python's standard library, not ESPHome, the Nextion Editor or a device.

> The builder never uploads. Flashing a TFT is a separate, risky operation;
> see the [installation steps](../README.md#install) and
> [recovery](RECOVERY.md).

## Requirements

- Python **3.9 or newer** (tested with 3.12 and 3.15).
- A **clang** with the ARM backend and the `armv6m-none-eabi` target.
  - macOS: install the Xcode Command Line Tools (`xcode-select --install`).
  - Debian/Ubuntu: `sudo apt install clang`.
  - Fedora: `sudo dnf install clang`.
  - Windows: use WSL2 and a Linux clang.
- The repository checkout, including `firmware/nspanel-v92-blank-base.tft`.
- No Unicorn, Capstone, ESPHome or Nextion Editor is needed for the build.

The reference image was built with **Apple clang 21.0.0
(clang-2100.4.1.1)** on macOS. Other clang versions are supported, but they may
emit different code; see [reproducibility](#reproducibility).

Check that clang can target the display's CPU:

```sh
printf 'int f(void){return 1;}\n' > /tmp/nspanel-target-check.c
clang -target armv6m-none-eabi -mcpu=cortex-m0plus -mthumb -Oz \
  -c /tmp/nspanel-target-check.c -o /tmp/nspanel-target-check.o
```

If that fails, install a full LLVM/clang toolchain; Xcode's clang normally
works on macOS.

## Quick start

From the repository root:

```sh
python3 tools/build_tft.py --check
python3 tools/verify_tft.py build/nspanel-v92-blank.tft
```

Or use the optional shortcuts:

```sh
make check    # byte-for-byte build check
make verify   # build check, then verify the output
```

Expected output:

```text
wrote .../build/nspanel-v92-blank.tft
  SHA-256 : a25aafc263b8980e1f91e9bfe6f363d2197c657bb5a2231f86d61a9828a3d10c
  program : 3232 / 3568 bytes
  loader  : 300 / 844 bytes
  changed : 2872 bytes
  check   : byte-for-byte match with the shipped TFT
```

`--check` requires the output to match
`firmware/nspanel-v92-blank.tft` exactly. Without `--check`, the builder writes
the image and reports its SHA-256 but does not require the official hash; this
is what you use after editing the source.

The default output is `build/nspanel-v92-blank.tft`, which is ignored by Git.
Choose another path with `--output`:

```sh
python3 tools/build_tft.py --output /tmp/nspanel-test.tft
python3 tools/verify_tft.py --allow-modified /tmp/nspanel-test.tft
```

`verify_tft.py --allow-modified` still checks the size, native image, helper
layout, all package CRCs and the blank UI; it only skips equality with the
official SHA-256. That is the right check for a modified build. The official
verifier is stricter:

```sh
python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
```

## What the builder does

| Step | Detail |
| --- | --- |
| 1 | Reads `firmware/nspanel-v92-blank-base.tft`, checks its size and SHA-256. |
| 2 | Compiles `terminal_receiver.c` for `armv6m-none-eabi` / Cortex-M0+. |
| 3 | Places allocatable ELF sections at `0x20000900` and applies the ARM REL relocations used by the object. |
| 4 | Checks the program fits the 3,568-byte pool and that `pool_entry` is at `0x20000901`. |
| 5 | Generates `terminal_receiver_meta.h` with the program size, CRC and entry. |
| 6 | Compiles `terminal_loader.c` at `0x200016F0` and checks it fits 844 bytes. |
| 7 | Replaces the 4 KiB preamble and helper-0 slot, then repairs the affected helper, resource, header and whole-file CRCs. |
| 8 | Checks the native F130 image is unchanged, enforces the patch-region whitelist and confirms native version 92. |
| 9 | Writes the TFT atomically to the requested output. |

The compiler invocation is:

```text
clang -target armv6m-none-eabi -mcpu=cortex-m0plus -mthumb -Oz \
  -ffreestanding -fno-builtin -fno-unwind-tables \
  -fno-asynchronous-unwind-tables -fstack-usage -Wall -Wextra -Werror
```

There is no linker, startup code or libc. `tools/elf_thumb.py` is a deliberately
small, fail-closed ELF32 reader: it supports only the section and relocation
types these objects use and rejects undefined symbols or unsupported
relocations. It is not a general linker.

`terminal_receiver_meta.h` is generated in a temporary build directory. It is
not checked in, because a stale copy would let the loader disagree with the
program. Do not compile the loader by hand without generating that header.

## Reproducibility

The official image is reproducible byte-for-byte with the reference Apple clang
21.0.0 toolchain:

```sh
python3 tools/build_tft.py --check
```

"Reproducible" here means *with this compiler configuration*. Clang versions
can differ in instruction selection, inlining or section layout, so a different
toolchain may produce a different program of the same size and structure. The
builder will still patch and checksum it correctly, but `--check` will fail.
Do not upload a binary just because it builds:

1. run `python3 tools/build_tft.py`;
2. run `python3 tools/verify_tft.py --allow-modified build/nspanel-v92-blank.tft`;
3. keep a known-good TFT and test on a spare panel if possible;
4. follow the [installation](../README.md#install) and
   [recovery](RECOVERY.md) procedures.

The source is tied to the documented base. The builder rejects any other base
by SHA-256. A TFT from a newer Nextion Editor has a different F130 image,
helper bundle and page format; patching it would be a port, not this build.

## Changing the watchdog

The fixed 3,000 ms timeout is in
[`terminal_receiver.c`](../firmware/src/terminal_receiver.c):

```c
if (now - last >= 3000) { /* host-inactivity watchdog */
```

Change the constant, then rebuild and verify:

```sh
python3 tools/build_tft.py
python3 tools/verify_tft.py --allow-modified build/nspanel-v92-blank.tft
```

A shorter timeout tears down a healthy session if the host pauses; a longer
timeout delays the blank fallback after an ESP32 crash. The host component does
not need a matching change, but the TFT naturally has a new SHA-256. Update any
served-file checksum and your rollback notes.

## Changing brightness or protocol behaviour

Live brightness is implemented in
[`stream_protocol.h`](../firmware/src/stream_protocol.h) and
[`terminal_receiver.c`](../firmware/src/terminal_receiver.c). Keep these
rules unless you are deliberately changing the protocol:

- `BRIGHTNESS` has no body and carries 0..100 in the header's `x` field.
- It is accepted only when no rectangle is active.
- The range, header CRC and body CRC must all pass.
- The stock backlight routine is called with `force=1`.
- The terminal reports 134 at session start and 135 after applying the change.
- The ESP32 component must understand the same records. A changed kind, status
  or field layout requires a matching host release; never ship a TFT an older
  host cannot drive.

The size limits are hard: 3,568 bytes for the receiver and 844 for the loader.
The builder fails if a change exceeds them. Removing proven-unused code is
safer than truncating a feature.

## Uploading a build

The builder only writes a local file. To install a custom TFT:

1. verify it with `tools/verify_tft.py --allow-modified`;
2. serve it over HTTP with a content length and byte-range support;
3. point `tft_url` at it and use the installer or `upload_tft` action;
4. keep the previous TFT and a recovery path.

`upload_tft` replaces display firmware. A failed or interrupted transfer can
leave the display unusable; read [recovery](RECOVERY.md) before retrying. The
hosted file and the installed file are independent: changing what a URL serves
does not change the panel.

## Troubleshooting

**`build_tft.py: error: clang was not found`**
Install clang and put it on `PATH`, or set `CC=/path/to/clang`.

**`unable to create target: 'No available targets are compatible...'`**
Use a clang built with the ARM backend. Xcode's clang, upstream LLVM and the
distribution clang packages normally include it; a stripped-down toolchain may
not.

**`base TFT SHA-256 is ..., expected ...`**
You pointed `--base` at the wrong file. Use the checked-in
`firmware/nspanel-v92-blank-base.tft`. The patch offsets are only valid for that
base.

**`build does not match firmware/nspanel-v92-blank.tft`**
The source or compiler differs. If you did not edit the source, try the
reference compiler. If you did edit it, this is expected: run the structural
verifier with `--allow-modified` instead of `--check`.

**`terminal program is ... bytes; the project pool allows ...`**
The receiver exceeded 3,568 bytes. Reduce code/data or split the change. Do not
move the pool boundary without re-auditing the stock memory map.

**`compiled pool_entry is not at the documented Thumb entry address`**
A code change moved the entry symbol. The loader calls `0x20000901`; keep
`pool_entry` first in the placed sections or update the documented layout and
loader together.

**`terminal_receiver_meta.h: No such file or directory`**
Compile through `tools/build_tft.py`; the header is generated there.

**Verifier says a CRC or F130 hash is wrong**
The package was modified incorrectly, or a tool other than the builder wrote
it. Rebuild from the base. Do not upload it.

## Files

| Path | Purpose |
| --- | --- |
| `firmware/src/` | Native display source. |
| `firmware/nspanel-v92-blank-base.tft` | Base build input, SHA-256 `82434a...`. |
| `firmware/nspanel-v92-blank.tft` | Shipped image, SHA-256 `a25aaf...`. |
| `tools/build_tft.py` | Compiler and TFT patcher. |
| `tools/elf_thumb.py` | Fixed-address ELF32 section/relocation reader. |
| `tools/tft_crc.py` | TFT/helper CRC. |
| `tools/verify_tft.py` | Package verifier. |
| `build/` | Ignored local build output. |
