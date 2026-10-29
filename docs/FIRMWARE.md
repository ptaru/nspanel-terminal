# Display firmware internals

This document describes the native firmware that runs on the display side of the
NSPanel. It is for developers who want to understand, audit, modify or rebuild
the shipped TFT. For installation, use the [main README](../README.md); for the
build commands, see [building](BUILDING.md); for the wire format, see
[protocol](PROTOCOL.md).

> **Experimental.** The EU image is validated on one EU NSPanel. The US image
> is a source port validated offline; it has not been installed on US hardware.
> Package checks cover image layout and CRCs, but not electrical timing,
> worst-case stack use, long-duration operation or recovery from every
> interrupted upload. Keep a rollback TFT.

## At a glance

| Item | EU | US |
| --- | --- | --- |
| Panel | Sonoff NSPanel, display `NX4832F035_011C`, MCU code F130 | same |
| Orientation | landscape | portrait |
| Visible area | 454×320 inside a 480×320 controller | full 320×480 |
| Stock entry state | `0x20000101 == 2` | `0x20000101 == 1` |
| Native terminal | protocol 6, native 92 | protocol 6, native 92 |
| Base native version | 72 | 72 (unmodified blank exports) |
| Shipped TFT | [`firmware/terminal-eu.tft`](../firmware/terminal-eu.tft), 264,936 bytes | [`firmware/terminal-us.tft`](../firmware/terminal-us.tft), 264,984 bytes |
| Shipped SHA-256 | `a25aafc263b8980e1f91e9bfe6f363d2197c657bb5a2231f86d61a9828a3d10c` | `512aa5bca9966352d21d324f0d12c3e3e607f49dcb7a9018044145a495b1d74e` |
| Build input | [`firmware/base-eu.tft`](../firmware/base-eu.tft), SHA-256 `6314294af0822bfe1616d2f28ba4e81b15038a20b53abedd0f9aafacb64994ef` | [`firmware/base-us.tft`](../firmware/base-us.tft), SHA-256 `f3b55142ae43acb65b22f46d99913ba0bd4eff3622f93dc62a1ae350bfe7b02c` |
| Source | [`firmware/src/`](../firmware/src/) | same |
| Program size | 3,232 of 3,568 available bytes | 3,232 of 3,568 available bytes |
| Loader size | 300 of 844 bytes | 296 of 844 bytes |
| Watchdog | 3,000 ms of host silence | same |
| Live brightness | 0–100%, applied between rectangles, status 135 | same |

Both bases are unmodified 72 blank exports (the EU Editor export and the stock
US export). Each shipped image differs in
the 4 KiB terminal preamble, helper 0, the native version metadata, and the
package checksums. The EU image additionally applies the earlier EU native
patches (state, dim and baud handling). The UI assets are unchanged.

## What the firmware modification changes

The display normally runs the stock Nextion project interpreter and UI. The
terminal replaces **helper 0**, a small loadable routine in the display's
external flash, and uses the 1 KiB UART buffer and part of the suspended
project's RAM pool as a direct rectangle receiver.

The builder changes these regions in the selected base TFT:

1. **Preamble at file offset `0x1000`.** The first 3,232 bytes become the
   compiled terminal program; the rest of the 4 KiB region is `0xFF`. This
   region maps to external SPI address `0x11000`.
2. **Helper 0 at bundle offset `0x357B6 + 0x74`.** The 844-byte helper-0 slot
   becomes the 300-byte EU loader or 296-byte US loader, padded with `0xFF`.
3. **Helper-0 CRC** in the helper-0 entry at `0x357B6 + 0x10` and the enclosing
   helper bundle CRC at `0x357B6`.
4. **EU native patches** at `0x2439`, `0xC072`, `0xD134`/`0xD158` and
   `0xEB0C`/`0xEB10`. These carry the earlier EU state, dim and baud handling
   onto the stock 72 native image. The US profile has no native code patches.
5. **Native version metadata** at native offset `0xB8`, plus its two CRCs at
   `0xB4` and `0xBE`. Both bases report 72; both shipped images report 92.
6. **Package CRCs** that cover the file: resource allocation, header 1 and the
   whole-file final word.

Apart from those regions, the page/font/picture tables, encoded headers,
helpers 1–8 and the stock updater are preserved. The builder enforces this with
a patch-region whitelist; the shipped builds change 4,102 bytes (EU) and
4,083 bytes (US). The terminal can therefore return to the stock blank page, and
TFT updates use the stock Nextion updater.

The source does **not** create a standalone display firmware. It is an overlay
that calls fixed stock routines and is loaded by the stock helper dispatcher.

## System context

```text
ESP32 (ESPHome)                         Display MCU (STM32-class)
┌─────────────────────┐   UART 4 Mbaud  ┌──────────────────────────┐
│ nspanel_terminal    │ ──────────────▶ │ stock Nextion runtime    │
│  - LVGL / drawing   │                 │  - UI suspended          │
│  - PackBits encoder │ ◀────────────── │  - helper 0 loader       │
│  - credit window    │   32-byte       │  - RAM overlay @ 0x20000900
└─────────────────────┘   reports       │  - GPIO RGB565 + touch   │
                                        │  - stock LCD/backlight   │
                                        └──────────────────────────┘
```

The ESP32 owns the display UART during a session. It sends COBS-framed
protocol-6 records at 2 or 4 Mbaud and consumes 32-byte reverse reports. The
display writes pixels directly to the LCD through GPIO; there is no framebuffer
and no rectangle-sized decompression buffer on the display side.

The terminal lives in RAM while the stock UI is suspended. Project variables,
the page scheduler and touch handlers do not run. On exit, the loader restores
the borrowed UART/DMA state and lets the stock state-4 continuation reload the
project, which returns to the blank page.

## TFT package layout

The offsets below are common to both profiles. The EU file is 264,936 bytes;
the US file is 264,984 bytes, so the last four bytes move accordingly.

| File offset | Size | Contents |
| --- | ---: | --- |
| `0x0000` | 0x1000 | Container headers, identity, CRCs and native bundle table. |
| `0x1000` | 0x1000 | Terminal preamble. First 3,232 bytes are code; remainder `0xFF`. Maps to SPI `0x11000`. |
| `0x10000` | 0x30000 | Resource allocation (fonts, pictures, project data). Unchanged. |
| `0x14532` | — | Native bundle start. First record follows a 4-byte count. |
| `0x14566` | 60,352 | Encoded F130 native image (XOR 9 on disk). The EU profile applies native patches; both profiles update the version metadata. |
| `0x357B6` | 0x15E0 | Helper bundle: enclosing CRC, count, nine entries. Helper 0 starts at `+0x74`. |
| `0x357B6 + 0x74` | 844 | Helper 0 slot: EU 300-byte or US 296-byte loader plus `0xFF` padding. |
| last 4 bytes | 4 | Whole-file CRC word. |

Both bases report native version 72 and both shipped images report 92 at record
offset `0xB8`. The full-XOR image SHA-256 is
`be6ef7a50c901d1c8135387e502553f7ad295223c967cdaf2ca564b8251f3d79` for the EU
image and
`b8ee56812e8aa92e653ba5f46a5745726295451f58a658b993bcf0ae5aaff4fb` for the US
image. [`tools/verify_tft.py`](../tools/verify_tft.py) checks the profile-specific
values.

### Checksums

All integrity values use the same byte-as-word CRC: each byte is fed as a
zero-extended 32-bit word, MSB first, initial `0xFFFFFFFF`, no final XOR.

| Location | Coverage |
| --- | --- |
| `0x44` | Resource allocation `0x10000..0x40000` |
| `0xC4` | Header 1 `0x0000..0xC4` |
| `0x18C` | Header 2 `0xC8..0x18C` (unchanged here) |
| helper bundle + 0 | Helper bundle `+4..+0x15E0` |
| helper bundle + 0x10 | Helper 0 slot (offset, size, CRC entry) |
| last 4 bytes | Whole file except the word, XOR bytes `0x03`, `0x2E`, `0x3C` |

The builder recomputes the values affected by the preamble and helper slot; the
native image and resource CRCs do not change by construction, but the builder
still checks that the native region is byte-identical to the base.

## Entry and exit

The stock runtime reaches helper 0 through its normal width/height callback
(state 4). The loader runs with interrupts and the project interpreter
suspended.

1. **Preconditions.** The compiled profile's stock geometry (EU 480×320 or US
   320×480) and project state byte (`0x20000101 == 2` on EU, `== 1` on US),
   DMA channel 1 disabled and USART1 DMAR off. If any check fails,
   `helper_entry` returns 0 without changing anything.
2. **Save.** USART1 CR1/CR3 and the USART1 NVIC enable bit are saved.
3. **Copy.** The loader reads `POOL_BYTES` from external SPI `0x11000` to
   `0x20000900` using the stock SPI read routine, then verifies the CRC.
4. **Enter.** If valid, it calls `pool_entry` at `0x20000901` (Thumb). The
   program configures the UART and DMA, sends READY with live-brightness
   capability (134), then runs until EXIT, a fault or the watchdog.
5. **Restore.** The program stops DMA/UART reception, reports a final error if
   needed and restores the stock baud rate and UART/DMA state. The loader
   clears the borrowed buffer, restores the stock touch cache and continuation,
   re-enables the saved interrupt state and returns to the stock runtime.
6. **Return to UI.** The stock state-4 continuation reloads project variables
   and the blank page. The loader must not call that continuation while the
   overlay is still executing, because it overwrites the helper's code.

The return value is the held-touch mask (plus bit 5 for malformed contact data),
which the loader writes back to the stock touch cache.

> **Never call the finalization helper (helper 1) from a live helper-0 return
> chain.** The stock helper dispatcher would overwrite the running overlay.
> TFT updates are performed by the ESP32 after the terminal has exited.

## RAM map

The overlay borrows the middle of the stock SRAM layout. Addresses are fixed by
the stock runtime.

| Range | Bytes | Owner while the terminal runs |
| --- | ---: | --- |
| `0x20000000..0x200004F0` | 1,264 | Stock globals, touch cache, callback tables. Partly reused; preserved values are restored. |
| `0x200004F0..0x200008F0` | 1,024 | UART command buffer, used as the DMA ring. |
| `0x200008F0..0x200016F0` | 3,584 | Project variable pool. `sys0..2` occupy the first 16 bytes; the program starts at `0x20000900`. |
| `0x20000900..0x200016F0` | 3,568 | Terminal program (3,232 used in both profiles, 336 spare). |
| `0x200016F0..0x20001A74` | 900 | Shared graphics/project/helper scratch. The loader itself is loaded here. |
| `0x20001B1C` | — | Stock stack canary. Initial main SP is `0x20001F98`. |

The program keeps its decoder, touch state and 40-byte work buffer on the
stack. It has no heap, no libc and no writable static state.

## Receiver

Entry arguments come from the stock `sys0..sys2` variables:

| Argument | Meaning |
| --- | --- |
| `sys0` | Baud rate, exactly 2,000,000 or 4,000,000 |
| `sys1` | Touch polling, 0 or 1 |
| `sys2` low 16 bits | Nonzero session id |

The receiver:

- disables the stock USART1 interrupt, configures DMA1 channel 1 in circular
  memory-increment mode from USART1 RDR to `0x200004F0`, 1,024 bytes;
- enables USART1 DMA receive and sends READY (134);
- tracks the DMA write head and cumulative consumed offset;
- processes at most 32 encoded bytes before revisiting DMA/touch state;
- reports consumed bytes and touch at least every 256 bytes and on every event;
- treats UART error flags, DMA transfer error, a credit/ring violation or
  3,000 ms without input as a terminal error;
- restores the saved baud/UART/DMA state on every exit path.

The host may have at most 512 encoded bytes outstanding beyond the last
reported consumed offset. The 1 KiB ring is deliberately larger than the
credit window, so a conforming sender cannot overwrite unread bytes while the
display polls touch.

## Pixel path

A RECT record configures the LCD window through the stock ROM routine at
`0x08007F71` (inclusive endpoints). Each pixel byte is then written as:

1. write the data byte to GPIOA's low output latch (`0x50000014`);
2. assert WR low through BRR bit 12;
3. assert WR high through BSRR bit 12.

RGB565 bytes are sent high byte first. The repeated-pixel writer keeps the
colour and count in registers and emits four pixels per loop iteration, but
every byte still gets the same three-store sequence. No GPIO reinitialisation,
clock change or shortened WR pulse is introduced.

Electrical timing, pulse widths, tearing and the LCD controller's true limit
have **not** been measured with an oscilloscope. There is no timing guarantee.

## Touch

The terminal polls the stock Goodix controller through a small bit-banged I2C
path on PB0/PB1, retaining the stock open-drain pin configuration. It reads the
status register `0x814E`, up to five 8-byte contact records at `0x814F`, applies
the stock coordinate offsets and orientation transform, clears the report, and
publishes mask/fresh flags in reverse reports.

`fresh` is 1 for a valid report, 2 for malformed contact data and 3 for an
I2C/ACK failure. Bit 5 of the mask is a sticky fault. Only the first contact is
forwarded to the host, and only one pointer is supported. Polling runs about
every 10 ms when enabled.

## Live brightness

The terminal advertises live brightness by sending **status 134** instead of
128 at session start. A host that sees 134 sends a `BRIGHTNESS` record
(kind 7):

- no body, `x` = 0..100, `y = w = h = 0`;
- only accepted when no rectangle is active;
- header CRC and whole-record CRC must both pass;
- the receiver calls the stock backlight routine at `0x0800BBFC` with
  `force=1`, then reports **status 135**.

The stock routine updates the saved brightness byte and the PB8/TIM16
backlight output. The change takes effect between rectangles, without exiting
the terminal. A host without live-brightness support uses command-mode
brightness and briefly exits; it cannot stream to this TFT. **Update the ESP32
component before updating the TFT.**

In the offline US emulation, the stock US state-4 continuation writes its own
saved dim value back when the blank page is restored after EXIT. The live change
still applies during the session, and the ESPHome component re-applies its
selected level on the next terminal entry. This behaviour has not been checked
on US hardware.

## Watchdog

If no input bytes arrive for 3,000 ms, the receiver reports an error with flag
`0x2000`, restores the stock runtime and returns to the blank page. The 3-second
value tolerates ordinary host scheduling pauses without tearing down a healthy
session. It is a fixed constant in `terminal_receiver.c`; changing it requires a
rebuild and a new TFT.

The watchdog covers host silence, not a hung display or a failed ESP32 image.
A display that stays black after a crash is a recovery case; see
[recovery](RECOVERY.md).

## Building and reproducibility

The public source builds each shipped TFT byte-for-byte with the documented
clang target. The builder:

- compiles the receiver at `0x20000900` and the loader at `0x200016F0`;
- extracts the allocatable sections and applies only ARM REL relocations;
- generates the program-size/CRC/entry header for the loader;
- patches the preamble and helper slot and repairs the affected CRCs;
- asserts the native F130 image is unchanged for the selected profile.

See [building](BUILDING.md) for the commands, toolchain and troubleshooting.
Different clang versions can produce different (but structurally valid) code;
`tools/build_tft.py --panel eu --check` and `--panel us --check` are the
byte-for-byte gates for the two official images.

## Modifying safely

- **Size budget.** The receiver has 3,568 bytes and the loader 844. Adding
  large features may not fit. The builder fails closed if the limits are
  exceeded.
- **Protocol changes.** A new record kind or changed status needs a matching
  ESPHome component. Bump the component version and the native/protocol version
  together; do not ship a TFT that the host cannot drive safely.
- **Watchdog.** Edit the `3000` constant, rebuild, verify the package and
  upload. A shorter value increases the chance of tearing down a slow host; a
  longer value delays the blank fallback.
- **Brightness.** Keep the range check and the between-rectangles rule. The
  stock routine expects 0..100 and `force=1` for an immediate update.
- **Base image.** The patch is tied to the selected profile base and helper
  bundle. Do not substitute a TFT from a newer Nextion Editor or another MCU
  code; add a profile with its own geometry and native version instead.
- **Versioning.** `92` identifies our terminal revision on both panel profiles.
  A changed package must get a new version because the stock updater skips a
  valid image with the same version. Stock images report `72`.
- **Tests.** At minimum run `tools/build_tft.py --panel eu --check`,
  `tools/build_tft.py --panel us --check`, the matching `verify_tft.py`
  commands, and any host protocol tests available in your checkout. Hardware
  uploads are separate and risky.

## Limits

- EU and US NSPanel only; not NSPanel Pro.
- The EU image is validated on one EU panel. The US image is validated offline
  only and has not been run on US hardware.
- Blank UI only; there is no Sonoff UI in the shipped images.
- CRC integrity, not a cryptographic signature. Anyone who can replace the
  served TFT can replace the display firmware.
- No demonstrated standalone recovery bootloader; recovery depends on the
  stock updater and a reachable ESP32.
- No broad compatibility claim.
- No electrical timing, long-run endurance, tearing or worst-case stack proof.
- The EU visible area is 454×320; the rightmost 26 columns of its controller are
  not visible. The US panel exposes the full 320×480.

## Source map

| File | Role |
| --- | --- |
| [`panel_profile.h`](../firmware/src/panel_profile.h) | Compile-time EU/US visible and stock-entry geometry. |
| [`terminal_api.h`](../firmware/src/terminal_api.h) | Register addresses, stock entry points, touch report structure. |
| [`terminal_receiver.c`](../firmware/src/terminal_receiver.c) | Main receiver, entry/exit, reports, watchdog. |
| [`stream_protocol.h`](../firmware/src/stream_protocol.h) | Protocol-6 decoder, PackBits, brightness record. |
| [`stream_touch.h`](../firmware/src/stream_touch.h) | Goodix bit-banged I2C and polling. |
| [`terminal_loader.c`](../firmware/src/terminal_loader.c) | Helper-0 loader and restoration. |
| [`tools/build_tft.py`](../tools/build_tft.py) | Compiler, fixed-address assembler and TFT patcher. |
| [`tools/verify_tft.py`](../tools/verify_tft.py) | Profile-aware structural verifier. |
| [`tools/panel_profiles.py`](../tools/panel_profiles.py) | Shared EU/US base, version and hash metadata. |

Code is MIT licensed; see [LICENSE](../LICENSE). The TFT contains the Nextion
runtime; see [NOTICE](../NOTICE).
