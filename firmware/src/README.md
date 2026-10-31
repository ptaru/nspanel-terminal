# Native display source

This directory contains the C source for the NSPanel protocol-7 terminal that runs
inside the display's helper overlay. It builds both the EU
[`../terminal-eu.tft`](../terminal-eu.tft) and the US
[`../terminal-us.tft`](../terminal-us.tft) through
[`../../tools/build_tft.py`](../../tools/build_tft.py).

| File | Purpose |
| --- | --- |
| `panel_profile.h` | Compile-time panel geometry. Defaults to the EU layout; the builder overrides it for the US profile. |
| `terminal_api.h` | Fixed USART/DMA/GPIO addresses, the stock baud setter and the touch-report structure. |
| `terminal_receiver.c` | Entry point, UART/DMA setup, stream loop, touch scheduling, reverse reports, watchdog and exit restoration. |
| `stream_protocol.h` | Protocol-7 COBS/PackBits decoder, header validation and the live-brightness record. |
| `stream_touch.h` | GPIO bit-banged I2C and Goodix polling. |
| `terminal_loader.c` | Helper-0 loader: copy, CRC check, call the overlay, restore the stock runtime. |

`terminal_receiver_meta.h` is generated at build time by `tools/build_tft.py`;
it contains the program size, CRC and Thumb entry address. It is not checked in
because it must match the compiled program exactly.

## Panel profiles

| Item | EU | US |
| --- | --- | --- |
| Display | `NX4832F035_011C`, MCU code F130 | same |
| Orientation | landscape | portrait |
| Visible area | 454×320 inside a 480×320 controller | full 320×480 |
| Stock helper geometry | 480×320 | 320×480 |
| Stock entry state | 2 | 1 |
| Base native version | 72 | 72 |
| Native code patches | 7 | 0 |
| Shipped native version | 93 | 93 |
| Build profile | `--panel eu` | `--panel us` |
| Protocol | 7 | 7 |
| Program load address | `0x20000900` (entry `0x20000901`) | same |
| Program limit | `0x200016F0 - 0x20000900` = 3,568 bytes | same |
| Loader load address | `0x200016F0` | same |
| Loader limit | 844 bytes (the helper-0 slot) | same |
| UART command/ring buffer | `0x200004F0..0x200008F0`, 1,024 bytes | same |
| Credit window | 512 encoded bytes | same |
| Default watchdog | 3,000 ms of host silence | same |

The source is specific to these base TFTs and this display family. The stock
runtime addresses it calls are not portable to another Nextion editor version,
another MCU code or NSPanel Pro. Both bases are unmodified 72 blank exports
(the EU Editor export and the stock US export). The EU profile applies seven
EU native patches (state, dim and
baud handling); the US profile leaves the stock US native code alone. Both
profiles set the native version to 93 and repair its metadata CRCs. Both bases
use the same container offsets and load addresses.

## Build

From the repository root:

```sh
python3 tools/build_tft.py --panel eu --check
python3 tools/verify_tft.py build/terminal-eu.tft

python3 tools/build_tft.py --panel us --check
python3 tools/verify_tft.py --panel us build/terminal-us.tft
```

`--check` requires a byte-for-byte match with the profile's shipped TFT. See
[`docs/BUILDING.md`](../../docs/BUILDING.md) for toolchain details, modifying
the watchdog, and troubleshooting.

## Reading the source

The stock runtime exposes no public API. The code therefore calls fixed ROM
addresses, documented at the call sites:

- `0x08007F71` — window setup (inclusive coordinates).
- `0x0800BBFD` — stock backlight routine (Thumb entry; function at `0x0800BBFC`).
- `0x08009371` — copy from external SPI to SRAM.
- `0x0800275D` — byte-as-word CRC.
- `0x0800C7DD` / `0x08003571` — stock post-helper continuation.
- `0x0800BF89` — stock baud setter.
- `0x0800968D` — stock touch-orientation transform.
- `0x0800C5C5` — clear a RAM range.

The display UART, DMA channel and GPIOs remain owned by the stock runtime; the
overlay borrows them only while the stock UI is suspended and restores them
before returning.

## Status

The EU code is validated on one EU NSPanel. The US build is a source port using
the same receiver; its package, checksums and ARM receiver are validated
offline, but it has not been installed on US hardware. With the documented
clang toolchain, both builds are reproducible and match their shipped image
SHA-256. The package verifier covers image layout and CRCs, but it does not
establish electrical timing, worst-case stack use, long-duration operation or
recovery from every interrupted upload. Treat this as experimental firmware.
