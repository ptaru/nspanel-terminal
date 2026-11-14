# Native display source

This directory contains the C source for the NSPanel protocol-8 terminal that runs inside the display's helper overlay. It builds both the EU [`../terminal-eu.tft`](../terminal-eu.tft) and the US [`../terminal-us.tft`](../terminal-us.tft) through [`../../tools/build_tft.py`](../../tools/build_tft.py).

| File | Purpose |
| --- | --- |
| `panel_profile.h` | Compile-time panel geometry. Defaults to the EU layout; the builder overrides it for the US profile. |
| `terminal_api.h` | Fixed USART/DMA/GPIO addresses, the stock baud setter and the touch-report structure. |
| `terminal_receiver.c` | Entry point, UART/DMA setup, stream loop, touch scheduling, reverse reports, watchdog and exit restoration. |
| `stream_protocol.h` | Protocol-8 COBS/PackBits decoder and record validation. |
| `stream_touch.h` | GPIO bit-banged I2C and Goodix polling. |
| `terminal_loader.c` | Helper-0 loader: copy, CRC check, call the overlay, restore the stock runtime. |

`terminal_receiver_meta.h` is generated at build time by `tools/build_tft.py`; it contains the program size, CRC and Thumb entry address. It is not checked in because it must match the compiled program exactly.

## Panel profiles

| Item | EU | US |
| --- | --- | --- |
| Display | `NX4832F035_011C`, MCU code F130 | same |
| Orientation | landscape | portrait |
| Visible area | 454×320 inside a 480×320 controller | full 320×480 |
| Stock helper geometry | 480×320 | 320×480 |
| Stock entry state | 2 | 2 |
| Base native version | 72 | 72 |
| Native code patches | 7 | 7 |
| Shipped native version | 95 | 95 |
| Build profile | `--panel eu` | `--panel us` |
| Protocol | 8 | 8 |
| Program load address | `0x20000900` (entry `0x20000901`) | same |
| Program limit | `0x200016F0 - 0x20000900` = 3,568 bytes | same |
| Loader load address | `0x200016F0` | same |
| Loader limit | 844 bytes (the helper-0 slot) | same |
| UART command/ring buffer | `0x200004F0..0x200008F0`, 1,024 bytes | same |
| Credit window | 512 encoded bytes | same |
| Default watchdog | 3,000 ms of host silence | same |

The source is specific to these base TFTs and this display family. The stock runtime addresses it calls are not portable to another Nextion editor version, another MCU code or NSPanel Pro. Both bases are unmodified blank Editor exports with native version 72, and their decoded F130 native images are byte-identical (SHA-256 `5612e8be…`), so every patch offset and stock byte below is the same for both. Both panels report stock entry state 2, so both profiles apply all seven patches and produce byte-identical native images. Both profiles set the native version to 95 and repair its metadata CRCs, and both use the same container offsets and load addresses.

## Native patches

| Offset | EU | US | Effect |
| --- | :---: | :---: | --- |
| `0x2439` | ✓ | ✓ | Disables the stock TFT updater's resource-skip shortcut, so an upload writes the resource allocation instead of skipping it when the stored and incoming resource metadata match (the `[0x200000F4] == 1` path). |
| `0xC072` | ✓ | ✓ | Rewrites the stock state selector `cmp r0,#1` to `cmp r0,#2`. Both the EU and US panels report state 2 at `0x20000101`, so both need this patch. |
| `0xD134`, `0xD158`, `0xD160` | ✓ | ✓ | In the stock state-4/`touch_j` continuation: skips the brightness force/restore and the touch/orientation re-init at `0x0800B820`, then emits `A9 FF FF FF` on the UART before the stock continuation resumes. |
| `0xEB0C`, `0xEB10` | ✓ | ✓ | Stock baud whitelist entries 0 and 1: 2,400/4,800 → 2,000,000/4,000,000. |

All seven patches are applied byte-for-byte to both images, so the shipped native images are identical. The stock state selector accepts only state 1; without the `0xC072` patch the state-2 entry path is rejected and the overlay never loads.

## Build

From the repository root:

```sh
python3 tools/build_tft.py --panel eu --check
python3 tools/verify_tft.py build/terminal-eu.tft

python3 tools/build_tft.py --panel us --check
python3 tools/verify_tft.py --panel us build/terminal-us.tft
```

`--check` requires a byte-for-byte match with the profile's shipped TFT. See [`docs/BUILDING.md`](../../docs/BUILDING.md) for toolchain details, modifying the watchdog, and troubleshooting.

## Reading the source

The stock runtime exposes no public API. The code therefore calls fixed ROM addresses, documented at the call sites:

- `0x08007F71` — window setup (inclusive coordinates).
- `0x0800BBFD` — stock backlight routine (Thumb entry; function at `0x0800BBFC`).
- `0x08009371` — copy from external SPI to SRAM.
- `0x0800275D` — byte-as-word CRC.
- `0x0800C7DD` / `0x08003571` — stock post-helper continuation.
- `0x0800BF89` — stock baud setter.
- `0x0800968D` — stock touch-orientation transform.
- `0x0800C5C5` — clear a RAM range.

The display UART, DMA channel and GPIOs remain owned by the stock runtime; the overlay borrows them only while the stock UI is suspended and restores them before returning.

## Status

The EU code is validated on one EU NSPanel. The US build is a source port using the same receiver and the same seven native patches; its package, checksums and ARM receiver are validated offline, and an external tester reported the state-2 build working on a US panel. The maintainer has not installed it on US hardware, so it has no in-house hardware evidence. With the documented clang toolchain, both builds are reproducible and match their shipped image SHA-256. The package verifier covers image layout and CRCs, but it does not establish electrical timing, worst-case stack use, long-duration operation or recovery from every interrupted upload. Treat this as experimental firmware.
