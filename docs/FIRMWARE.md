# Display firmware internals

This document describes the native firmware that runs on the display side of the
NSPanel. It is for developers who want to understand, audit, modify or rebuild
the shipped TFT. For installation, use the [main README](../README.md); for the
build commands, see [building](BUILDING.md); for the wire format, see
[protocol](PROTOCOL.md).

> **Experimental, one panel.** The current image was exercised on one EU
> NSPanel. Offline checks cover the package, the ARM receiver and the host
> protocol, but not electrical timing, worst-case stack use, long-duration
> operation or recovery from every interrupted upload. Keep a rollback TFT.

## At a glance

| Item | Value |
| --- | --- |
| Panel | EU Sonoff NSPanel, display `NX4832F035_011C`, MCU code F130 |
| Visible area | 454×320 inside a 480×320 controller |
| Native terminal | V92, protocol 6 |
| Shipped TFT | [`firmware/nspanel-v92-blank.tft`](../firmware/nspanel-v92-blank.tft), 264,936 bytes |
| Shipped SHA-256 | `a25aafc263b8980e1f91e9bfe6f363d2197c657bb5a2231f86d61a9828a3d10c` |
| Build input | [`firmware/nspanel-v92-blank-base.tft`](../firmware/nspanel-v92-blank-base.tft), SHA-256 `82434ab839ed14af45c655bdf1980848649c6a48081ee593c5cce397df7f3628` |
| Source | [`firmware/src/`](../firmware/src/) |
| Program size | 3,232 of 3,568 available bytes |
| Loader size | 300 of 844 bytes |
| Watchdog | 3,000 ms of host silence |
| Live brightness | 0–100%, applied between rectangles, status 135 |

The shipped image and the base are both native V92 and blank. They differ in
the 4 KiB terminal preamble, helper 0, and the package checksums that cover
those regions. The native F130 image and the UI assets are byte-identical.

## What the firmware modification changes

The display normally runs the stock Nextion project interpreter and UI. The
terminal replaces **helper 0**, a small loadable routine in the display's
external flash, and uses the 1 KiB UART buffer and part of the suspended
project's RAM pool as a direct rectangle receiver.

Only four things are modified in the base TFT:

1. **Preamble at file offset `0x1000`.** The first 3,232 bytes become the
   compiled terminal program; the rest of the 4 KiB region is `0xFF`. This
   region maps to external SPI address `0x11000`.
2. **Helper 0 at bundle offset `0x357B6 + 0x74`.** The original 844-byte slot
   becomes the 300-byte loader, padded with `0xFF`.
3. **Helper-0 CRC** in the helper-0 entry at `0x357B6 + 0x10` and the enclosing
   helper bundle CRC at `0x357B6`.
4. **Package CRCs** that cover the file: resource allocation, header 1 and the
   whole-file final word.

Everything else is preserved: the decoded F130 native image, page/font/picture
tables, encoded headers, helpers 1–8 and the stock updater. The builder enforces
this with a patch-region whitelist; the shipped build changes 2,872 bytes. That
is why the terminal can return to the stock blank page and why a same-version
TFT update still follows the stock install path.

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

Relevant offsets in the shipped 264,936-byte image:

| File offset | Size | Contents |
| --- | ---: | --- |
| `0x0000` | 0x1000 | Container headers, identity, CRCs and native bundle table. |
| `0x1000` | 0x1000 | Terminal preamble. First 3,232 bytes are code; remainder `0xFF`. Maps to SPI `0x11000`. |
| `0x10000` | 0x30000 | Resource allocation (fonts, pictures, project data). Unchanged. |
| `0x14532` | — | Native bundle start. First record follows a 4-byte count. |
| `0x14566` | 60,352 | Encoded F130 native image (XOR 9 on disk). Unchanged. |
| `0x357B6` | 0x15E0 | Helper bundle: enclosing CRC, count, nine entries. Helper 0 starts at `+0x74`. |
| `0x357B6 + 0x74` | 844 | Helper 0 slot: 300-byte loader plus `0xFF` padding. |
| last 4 bytes | 4 | Whole-file CRC word. |

The F130 record reports native version 92 at record offset `0xB8`. The decoded
image SHA-256 is
`be6ef7a50c901d1c8135387e502553f7ad295223c967cdaf2ca564b8251f3d79`.
[`tools/verify_tft.py`](../tools/verify_tft.py) checks all of this.

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

1. **Preconditions.** Width 480, height 320, state byte `0x20000101 == 2`,
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
6. **Return to UI.** The original state-4 continuation reloads project
   variables and the blank page. The loader must not call that continuation
   while the overlay is still executing, because it overwrites the helper's
   code.

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
| `0x20000900..0x200016F0` | 3,568 | Terminal program (3,232 used, 336 spare). |
| `0x200016F0..0x20001A74` | 900 | Shared graphics/project/helper scratch. The loader itself is loaded here. |
| `0x20001B1C` | — | Stock stack canary. Initial main SP is `0x20001F98`. |

The program keeps its decoder, touch state and 40-byte work buffer on the
stack. It has no heap, no libc and no writable static state. The lab
emulator checks assert that the observed stack pointer stays above the
`0x20001B1C` canary; that is path coverage, not a formal worst-case stack
proof.

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
have **not** been measured with an oscilloscope. The performance evidence is
instruction counts and a single-panel visual check, not a timing guarantee.

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
the terminal. A host that only knows the older V92 image uses command-mode
brightness and briefly exits; an older host cannot stream to this TFT. **Update
the ESP32 component before updating the TFT.**

## Watchdog

If no input bytes arrive for 3,000 ms, the receiver reports an error with flag
`0x2000`, restores the stock runtime and returns to the blank page. The 3-second
value replaced an earlier 300 ms watchdog so ordinary host scheduling pauses do
not tear down a healthy session. It is a fixed constant in
`terminal_brightness.c`; changing it requires a rebuild and a new TFT.

The watchdog covers host silence, not a hung display or a failed ESP32 image.
A display that stays black after a crash is a recovery case; see
[recovery](RECOVERY.md).

## Building and reproducibility

The public source builds the shipped TFT byte-for-byte with the documented
clang target. The builder:

- compiles the receiver at `0x20000900` and the loader at `0x200016F0`;
- extracts the allocatable sections and applies only ARM REL relocations;
- generates the program-size/CRC/entry header for the loader;
- patches the preamble and helper slot and repairs the affected CRCs;
- asserts the native F130 image is unchanged.

See [building](BUILDING.md) for the commands, toolchain and troubleshooting.
Different clang versions can produce different (but structurally valid) code;
`tools/build_tft.py --check` is the byte-for-byte gate for the official image.

## Modifying safely

- **Size budget.** The receiver has 3,568 bytes and the loader 844. Adding
  large features may not fit. The builder fails closed if the limits are
  exceeded.
- **Protocol changes.** A new record kind or changed status needs a matching
  ESPHome component. Bump the component version and the native/protocol version
  together; do not ship a TFT that an older host cannot drive safely.
- **Watchdog.** Edit the `3000` constant, rebuild, verify the package and
  upload. A shorter value increases the chance of tearing down a slow host; a
  longer value delays the blank fallback.
- **Brightness.** Keep the range check and the between-rectangles rule. The
  stock routine expects 0..100 and `force=1` for an immediate update.
- **Base image.** The patch is tied to the documented base and helper bundle.
  Do not substitute a TFT from a newer Nextion Editor or another MCU code.
- **Tests.** At minimum run `tools/build_tft.py --check`,
  `tools/verify_tft.py`, and any host protocol tests available in your
  checkout. Hardware uploads are separate and risky.

## Limits

- EU NSPanel only; not the US panel and not NSPanel Pro.
- Blank UI only; there is no original Sonoff UI in the shipped image.
- CRC integrity, not a cryptographic signature. Anyone who can replace the
  served TFT can replace the display firmware.
- No demonstrated standalone recovery bootloader; recovery depends on the
  stock updater and a reachable ESP32.
- One panel, one hardware trial. No broad compatibility claim.
- No electrical timing, long-run endurance, tearing or worst-case stack proof.
- The visible area is 454×320; the rightmost 26 columns of the controller are
  not visible.

## Source map

| File | Role |
| --- | --- |
| [`terminal_api.h`](../firmware/src/terminal_api.h) | Register addresses, stock entry points, touch report structure. |
| [`terminal_brightness.c`](../firmware/src/terminal_brightness.c) | Main receiver, entry/exit, reports, watchdog. |
| [`stream_brightness.h`](../firmware/src/stream_brightness.h) | Protocol-6 decoder, PackBits, brightness record. |
| [`stream_touch.h`](../firmware/src/stream_touch.h) | Goodix bit-banged I2C and polling. |
| [`terminal_brightness_loader.c`](../firmware/src/terminal_brightness_loader.c) | Helper-0 loader and restoration. |
| [`tools/build_tft.py`](../tools/build_tft.py) | Compiler, fixed-address assembler and TFT patcher. |
| [`tools/verify_tft.py`](../tools/verify_tft.py) | Structural verifier. |

Code is MIT licensed; see [LICENSE](../LICENSE). The TFT contains the Nextion
runtime; see [NOTICE](../NOTICE).
