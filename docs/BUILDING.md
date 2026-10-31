# Building the display firmware

The display TFTs are built from the stock EU and US bases and the native C source
in [`firmware/src/`](../firmware/src/). The build is offline: it uses a C
compiler and Python's standard library, not ESPHome, the Nextion Editor or a
device.

> The builder never uploads. Flashing a TFT is a separate, risky operation; see
> the [installation steps](../README.md#install) and [recovery](RECOVERY.md).

## Profiles

| Profile | Panel | Visible area | Base native | Native patches | Shipped native | Base | Shipped |
| --- | --- | --- | ---: | ---: | ---: | --- | --- |
| `eu` | EU landscape | 454×320 in a 480×320 controller | 72 | 7 | 93 | `firmware/base-eu.tft` | `firmware/terminal-eu.tft` |
| `us` | US portrait | 320×480 | 72 | 0 | 93 | `firmware/base-us.tft` | `firmware/terminal-us.tft` |

Both bases are unmodified 72 blank exports (the EU Editor export and the stock
US export). The EU profile applies the
EU native patches (state, dim and baud handling); the US profile has no
native code patches. Both profiles set the native version metadata to 93 and
repair its two CRCs. The two bases share the same container offsets, native
bundle location and helper-0 slot, but each profile is tied to its own base
SHA-256.

## Requirements

- Python **3.9 or newer** (tested with 3.12 and 3.15).
- A **clang** with the ARM backend and the `armv6m-none-eabi` target.
  - macOS: install the Xcode Command Line Tools (`xcode-select --install`).
  - Debian/Ubuntu: `sudo apt install clang`.
  - Fedora: `sudo dnf install clang`.
  - Windows: use WSL2 and a Linux clang.
- The repository checkout, including both `*-base.tft` files.
- No Unicorn, Capstone, ESPHome or Nextion Editor is needed for the build.

Both official images are reproducible with **Apple clang 21.0.0
(clang-2100.4.1.1)** on macOS. Other clang versions are supported, but they may
emit different code; see [reproducibility](#reproducibility).

Check that clang can target the display's CPU:

```sh
printf 'int f(void){return 1;}\n' > /tmp/nspanel-target-check.c
clang -target armv6m-none-eabi -mcpu=cortex-m0plus -mthumb -Oz \
  -c /tmp/nspanel-target-check.c -o /tmp/nspanel-target-check.o
```

If that fails, install a full LLVM/clang toolchain; Xcode's clang normally works
on macOS.

## Quick start

From the repository root:

```sh
python3 tools/build_tft.py --panel eu --check
python3 tools/verify_tft.py build/terminal-eu.tft

python3 tools/build_tft.py --panel us --check
python3 tools/verify_tft.py --panel us build/terminal-us.tft
```

Or use the optional shortcuts:

```sh
make check    # byte-for-byte build checks for both profiles
make verify   # build checks, then verify both outputs
```

Expected EU output:

```text
wrote .../build/terminal-eu.tft
  panel   : eu (454x320)
  SHA-256 : 925b0f35109e72354816a96d9af0b07cc9f92beaf1aed3ba576a2424b3c0ae3c
  program : 3276 / 3568 bytes
  loader  : 292 / 844 bytes
  changed : 4154 bytes
  check   : byte-for-byte match with firmware/terminal-eu.tft
```

Expected US output:

```text
wrote .../build/terminal-us.tft
  panel   : us (320x480)
  SHA-256 : 20ba711b65e58febac29f758ea2724a2f71b9202c028a4e45abef64ca404e73c
  program : 3276 / 3568 bytes
  loader  : 288 / 844 bytes
  changed : 4127 bytes
  check   : byte-for-byte match with firmware/terminal-us.tft
```

`--check` requires the output to match the profile's shipped TFT exactly.
Without `--check`, the builder writes the image and reports its SHA-256 but does
not require the official hash; this is what you use after editing the source.

The default output is `build/terminal-eu.tft` or
`build/terminal-us.tft`, which is ignored by Git. Choose another path with
`--output` and select the profile with `--panel`:

```sh
python3 tools/build_tft.py --panel us --output /tmp/terminal-us-test.tft
python3 tools/verify_tft.py --panel us --allow-modified /tmp/terminal-us-test.tft
```

`verify_tft.py --allow-modified` still checks the size, native image, helper
layout, all package CRCs and the blank UI; it only skips equality with the
official SHA-256. That is the right check for a modified build. The official
verifier is stricter:

```sh
python3 tools/verify_tft.py firmware/terminal-eu.tft
python3 tools/verify_tft.py --panel us firmware/terminal-us.tft
```

## What the builder does

| Step | Detail |
| --- | --- |
| 1 | Reads the selected profile's base TFT and checks its size and SHA-256. |
| 2 | Compiles `terminal_receiver.c` for `armv6m-none-eabi` / Cortex-M0+ with the profile's geometry defines. |
| 3 | Places allocatable ELF sections at `0x20000900` and applies the ARM REL relocations used by the object. |
| 4 | Checks the program fits the 3,568-byte pool and that `pool_entry` is at `0x20000901`. |
| 5 | Generates `terminal_receiver_meta.h` with the program size, CRC and entry. |
| 6 | Compiles `terminal_loader.c` at `0x200016F0` and checks it fits 844 bytes. |
| 7 | Applies the profile's native code patches, sets the native version metadata, replaces the 4 KiB preamble and helper-0 slot, then repairs the native metadata, helper, resource, header and whole-file CRCs. |
| 8 | Checks that native changes are limited to the profile patches and version metadata, enforces the patch-region whitelist and confirms the profile's shipped native version. |
| 9 | Writes the TFT atomically to the requested output. |

The compiler invocation is:

```text
clang -target armv6m-none-eabi -mcpu=cortex-m0plus -mthumb -Oz \
  -ffreestanding -fno-builtin -fno-unwind-tables \
  -fno-asynchronous-unwind-tables -fstack-usage -Wall -Wextra -Werror \
  -DNSPANEL_PANEL_WIDTH=<w> -DNSPANEL_PANEL_HEIGHT=<h> \
  -DNSPANEL_ENTRY_WIDTH=<w> -DNSPANEL_ENTRY_HEIGHT=<h>
```

The geometry defaults to the EU layout, so a plain compile without the defines
still produces the EU receiver. There is no linker, startup code or libc.
`tools/elf_thumb.py` is a deliberately small, fail-closed ELF32 reader: it
supports only the section and relocation types these objects use and rejects
undefined symbols or unsupported relocations. It is not a general linker.

`terminal_receiver_meta.h` is generated in a temporary build directory. It is not
checked in, because a stale copy would let the loader disagree with the program.
Do not compile the loader by hand without generating that header.

## Reproducibility

Both official images are reproducible byte-for-byte with Apple clang 21.0.0:

```sh
python3 tools/build_tft.py --panel eu --check
python3 tools/build_tft.py --panel us --check
```

"Reproducible" here means *with this compiler configuration*. Clang versions can
differ in instruction selection, inlining or section layout, so a different
toolchain may produce a different program of the same size and structure. The
builder will still patch and checksum it correctly, but `--check` will fail. Do
not upload a binary just because it builds:

1. run `python3 tools/build_tft.py --panel <profile>`;
2. run `python3 tools/verify_tft.py --panel <profile> --allow-modified <output>`;
3. keep a known-good TFT and test on a spare panel if possible;
4. follow the [installation](../README.md#install) and [recovery](RECOVERY.md)
   procedures.

The source is tied to each documented base. The builder rejects any other base
by SHA-256. A TFT from a newer Nextion Editor has a different F130 image, helper
bundle and page format; patching it would be a port, and it needs a new profile
with its own geometry and native version.

`93` is the terminal revision for both panel profiles. A changed package must
be given a new version because the stock updater skips a valid image with the
same version. Stock images report `72`.

## Changing the watchdog

The fixed 3,000 ms timeout is in
[`terminal_receiver.c`](../firmware/src/terminal_receiver.c):

```c
if (now - last >= 3000) { /* host-inactivity watchdog */
```

Change the constant, then rebuild and verify both profiles:

```sh
python3 tools/build_tft.py --panel eu
python3 tools/verify_tft.py --allow-modified build/terminal-eu.tft
python3 tools/build_tft.py --panel us
python3 tools/verify_tft.py --panel us --allow-modified build/terminal-us.tft
```

A shorter timeout tears down a healthy session if the host pauses; a longer
timeout delays the blank fallback after an ESP32 crash. The host component does
not need a matching change, but each TFT naturally has a new SHA-256. Update any
served-file checksum and your rollback notes.

## Changing brightness or protocol behaviour

Live brightness is implemented in
[`stream_protocol.h`](../firmware/src/stream_protocol.h) and
[`terminal_receiver.c`](../firmware/src/terminal_receiver.c). Keep these rules
unless you are deliberately changing the protocol:

- `BRIGHTNESS` has no body and carries 0..100 in the header's `x` field.
- It is accepted only when no rectangle is active.
- The range, header CRC and body CRC must all pass.
- The stock backlight routine is called with `force=1`.
- The terminal reports 134 at session start and 135 after applying the change.
- The ESP32 component must understand the same records. A changed kind, status or
  field layout requires a matching host release; never ship a TFT the host cannot
  drive.

The size limits are hard: 3,568 bytes for the receiver and 844 for the loader.
The builder fails if a change exceeds them. Removing proven-unused code is safer
than truncating a feature.

## Uploading a build

The builder only writes a local file. To install a custom TFT:

1. verify it with `tools/verify_tft.py --panel <profile> --allow-modified`;
2. serve it over HTTP with a content length and byte-range support;
3. point `tft_url` at it and call `upload_tft` from the panel firmware;
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
You pointed `--base` at the wrong file or selected the wrong profile. Use the
checked-in base for that profile. The patch offsets and native version are tied
to it.

**`build does not match firmware/nspanel-...tft`**
The source or compiler differs. If you did not edit the source, try the
documented compiler. If you did edit it, this is expected: run the structural
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

**Verifier says a CRC, native version or F130 hash is wrong**
The package was modified incorrectly, the wrong profile was selected, or a tool
other than the builder wrote it. Rebuild from the correct base. Do not upload it.

## Files

| Path | Purpose |
| --- | --- |
| `firmware/src/` | Native display source, including `panel_profile.h`. |
| `firmware/base-eu.tft` | EU base build input, SHA-256 `631429...`. |
| `firmware/terminal-eu.tft` | EU shipped image, SHA-256 `a25aaf...`. |
| `firmware/base-us.tft` | US base build input, SHA-256 `f3b551...`. |
| `firmware/terminal-us.tft` | US shipped image, SHA-256 `512aa5...`. |
| `tools/build_tft.py` | Profile-aware compiler and TFT patcher. |
| `tools/panel_profiles.py` | Shared EU/US base, geometry and hash metadata. |
| `tools/elf_thumb.py` | Fixed-address ELF32 section/relocation reader. |
| `tools/tft_crc.py` | TFT/helper CRC. |
| `tools/verify_tft.py` | Profile-aware package verifier. |
| `build/` | Ignored local build output. |
