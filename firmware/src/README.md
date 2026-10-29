# Native display source

This directory contains the C source for the V92/protocol-6 terminal that runs
inside the display's helper overlay. It is the source for
[`../nspanel-v92-blank.tft`](../nspanel-v92-blank.tft), which is built by
[`../../tools/build_tft.py`](../../tools/build_tft.py).

| File | Purpose |
| --- | --- |
| `terminal_api.h` | Fixed USART/DMA/GPIO addresses, the stock baud setter and the touch-report structure. |
| `terminal_brightness.c` | Entry point, UART/DMA setup, stream loop, touch scheduling, reverse reports, watchdog and exit restoration. |
| `stream_brightness.h` | Protocol-6 COBS/PackBits decoder, header validation and the live-brightness record. |
| `stream_touch.h` | GPIO bit-banged I2C and Goodix polling. |
| `terminal_brightness_loader.c` | Helper-0 loader: copy, CRC check, call the overlay, restore the stock runtime. |

`terminal_brightness_meta.h` is generated at build time by `tools/build_tft.py`;
it contains the program size, CRC and Thumb entry address. It is not checked in
because it must match the compiled program exactly.

## Fixed geometry

| Item | Value |
| --- | --- |
| Display | EU NSPanel `NX4832F035_011C`, MCU code F130 |
| Visible area | 454×320 inside a 480×320 controller |
| Native version | 92 |
| Protocol | 6 |
| Program load address | `0x20000900` (entry `0x20000901`) |
| Program limit | `0x200016F0 - 0x20000900` = 3,568 bytes |
| Loader load address | `0x200016F0` |
| Loader limit | 844 bytes (the original helper-0 slot) |
| UART command/ring buffer | `0x200004F0..0x200008F0`, 1,024 bytes |
| Credit window | 512 encoded bytes |
| Default watchdog | 3,000 ms of host silence |

The source is specific to this base TFT and this display family. The stock
runtime addresses it calls are not portable to another Nextion editor version,
another MCU code or a US panel.

## Build

From the repository root:

```sh
python3 tools/build_tft.py --check
python3 tools/verify_tft.py build/nspanel-v92-blank.tft
```

`--check` requires a byte-for-byte match with the shipped TFT. See
[`docs/BUILDING.md`](../../docs/BUILDING.md) for toolchain details, modifying
the watchdog, and troubleshooting.

## Reading the source

The stock runtime exposes no public API. The code therefore calls fixed ROM
addresses, documented at the call sites:

- `0x08007F71` — window setup (inclusive coordinates).
- `0x0800BBFD` — stock backlight routine (Thumb entry; function at `0x0800BBFC`).
- `0x08009371` — copy from external SPI to SRAM.
- `0x0800275D` — byte-as-word CRC.
- `0x0800C7DD` / `0x08003571` — original post-helper continuation.
- `0x0800BF89` — stock baud setter.
- `0x0800968D` — stock touch-orientation transform.
- `0x0800C5C5` — clear a RAM range.

The display UART, DMA channel and GPIOs remain owned by the stock runtime;
the overlay borrows them only while the stock UI is suspended and restores
them before returning.

## Provenance and status

The code was developed against the project's V92 blank TFT and exercised on one
EU NSPanel. With the documented reference clang toolchain, the public build is
reproducible and matches the shipped image's SHA-256. Offline checks cover the package
layout, CRCs, the ARM receiver and the host protocol, but they do not establish
electrical timing, worst-case stack use, long-duration operation or recovery
from every interrupted upload. Treat this as experimental firmware.
