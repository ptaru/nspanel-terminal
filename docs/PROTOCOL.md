# Terminal protocol

This is the wire format between the ESP32 host component and the V92 display
terminal. It is implemented by
[`firmware/src/stream_protocol.h`](../firmware/src/stream_protocol.h) and
[`components/nspanel_terminal/`](../components/nspanel_terminal/). See
[display firmware internals](FIRMWARE.md) for the display-side memory and entry
contract.

The ESP32 owns the display UART during a session. The terminal accepts
COBS-framed protocol-6 records with a session, sequence number and CRC16, and
answers with 32-byte reverse reports. Pixels are PackBits-compressed RGB565
rectangles; there is no framebuffer on the display.

## Session entry and exit

The host enters the terminal through the stock `touch_j` path with:

| Variable | Meaning |
| --- | --- |
| `sys0` | Baud rate, 2,000,000 or 4,000,000 |
| `sys1` | Touch polling, 0 or 1 |
| `sys2` low 16 bits | Nonzero session id |

The terminal switches to the requested baud, configures DMA, and sends READY
(status 134 for this build, because it supports live brightness). A host that
sees 128 knows the older V92 image and must use command-mode brightness.

`EXIT` or the display watchdog returns to the stock blank page at 115200 baud.
The host saves the page, `bkcmd` and `sys0..2` before entry and restores and
verifies them after exit. Terminal entry/exit is documented in
[FIRMWARE.md](FIRMWARE.md#entry-and-exit).

## Framing

Each forward record is COBS-encoded and terminated by a single `0x00`
delimiter. The decoder:

- treats a nonzero byte as data (except the first byte of a block, which is the
  COBS code);
- inserts a zero after every code block except when the code is `0xFF`;
- ends the frame at the delimiter and checks that the decoded length and CRC
  are complete.

A record is at most **537 encoded bytes** plus the delimiter (538 bytes on the
wire). The encoded limit is enforced independently of the decoded body limit.

## Record header

The decoded header is exactly 20 bytes. All integers are little endian.

| Offset | Size | Field |
| --- | ---: | --- |
| 0..1 | 2 | `R` `S` magic |
| 2 | 1 | Protocol version, **6** |
| 3 | 1 | Kind (see below) |
| 4..5 | 2 | Session id, nonzero |
| 6..7 | 2 | Sequence, modulo 65536 |
| 8..9 | 2 | Body length, 0..512 |
| 10..11 | 2 | `x` |
| 12..13 | 2 | `y` |
| 14..15 | 2 | `width` |
| 16..17 | 2 | `height` |
| 18..19 | 2 | CRC16-CCITT over bytes 0..17, initial `0xFFFF` |

The body follows immediately. A uint16 CRC16-CCITT over the complete header
and body follows the body; its initial value is also `0xFFFF`. The header CRC
and semantic checks must pass before any body bytes can reach the LCD.

## Record kinds

| Kind | Name | Body | Geometry | Meaning |
| ---: | --- | --- | --- | --- |
| 1 | `RECT` | none | `x,y,w,h` | Configure the LCD window and begin a rectangle. |
| 2 | `DATA` | 1..512 | zero | PackBits pixels for the active rectangle. |
| 3 | `END` | none | zero | Finish the rectangle; all pixel bytes must have been emitted. |
| 4 | `PING` | none | zero | Keep an idle session alive. |
| 5 | `EXIT` | none | zero | Return to the stock runtime. |
| 6 | `SYNC` | none | zero | Clear a protocol fault and abandon the active rectangle. |
| 7 | `BRIGHTNESS` | none | `x=0..100`, `y=w=h=0` | Apply live backlight brightness. |

Validation rules:

- `RECT` requires no active rectangle, a zero body, `w` and `h` nonzero,
  `x < 454`, `y < 320`, `w <= 454 - x` and `h <= 320 - y`.
- `DATA` requires an active rectangle and a nonzero body. Its body must decode
  to a whole number of pixels; an incomplete PackBits token is an error.
- `END` requires an active rectangle and exactly zero remaining pixel bytes.
- `PING`, `EXIT` and `SYNC` require no body and zero geometry.
- `BRIGHTNESS` requires no active rectangle, no body, zero `y/w/h` and
  `x <= 100`.
- Normal records must match the expected sequence. `SYNC` is accepted after a
  fault and may carry any sequence; the next record must use `SYNC`'s sequence
  plus one.
- An unknown kind, wrong session, bad CRC, oversized body or invalid geometry
  is a protocol error.

`RECT` configures the window only after the whole record has passed its CRC.
`END` increments the completed-rectangle counter but does not imply a tear-free
frame; it means the bytes were validated and emitted.

## PackBits body

`DATA` carries RGB565 pixels, **high byte first**. A token is one byte:

- high bit 0: literal run. Low seven bits are `run - 1`; the next `2 * run`
  bytes are that many RGB565 pixels.
- high bit 1: repeat run. Low seven bits are `run - 1`; the next two bytes are
  one RGB565 pixel, repeated `run` times.

Runs are 1..128 pixels. The decoder checks each run against the rectangle's
remaining pixel bytes before writing. Pixels are written directly to the LCD;
there is no rectangle-sized buffer in the display.

Pixel writes are optimistic: the body CRC is checked after the pixels have been
streamed. A damaged `DATA` record can therefore have written pixels before the
error is detected. **Recovery requires `SYNC` and a full redraw of the affected
rectangle.** The terminal does not replay a block into the LCD's current
cursor.

## Live brightness

`BRIGHTNESS` (kind 7) is the live-brightness extension. It has no body; the
percentage is in the header's `x` field. The header and whole-record CRCs
protect the value. It is accepted only between rectangles.

After validation, the receiver calls the stock backlight routine at
`0x0800BBFC` with `force=1`, which updates the saved brightness byte and the
PB8/TIM16 backlight output. It then reports **status 135**. The change is
applied without leaving the terminal.

Capability negotiation:

| Terminal | READY status | Host behaviour |
| --- | ---: | --- |
| This build | 134 | Queue a `BRIGHTNESS` record at the next idle point; wait for 135. |
| Older V92 | 128 | Use command-mode brightness, which briefly exits the terminal. |

The host must be updated **before** the TFT. An older host rejects status 134
safely and cannot stream to the new image with live brightness. The current
component implements both paths.

## Reverse reports

A report is 32 unescaped bytes:

| Offset | Size | Field |
| --- | ---: | --- |
| 0..1 | 2 | `S` `R` magic |
| 2 | 1 | Protocol version, 6 |
| 3 | 1 | Status (see below) |
| 4..5 | 2 | Session id |
| 6..7 | 2 | Next expected sequence |
| 8..11 | 4 | Cumulative consumed encoded bytes, modulo 2^32 |
| 12..13 | 2 | Completed rectangles |
| 14..15 | 2 | Flags |
| 16..19 | 4 | Stock runtime tick, nominal ms |
| 20 | 1 | Touch mask |
| 21 | 1 | Touch freshness |
| 22..23 | 2 | Contact 0 x |
| 24..25 | 2 | Contact 0 y |
| 26..27 | 2 | Byte window, always 512 |
| 28..29 | 2 | Observed ring high-water |
| 30..31 | 2 | CRC16-CCITT over bytes 0..29 |

Status values:

| Status | Name | Meaning |
| ---: | --- | --- |
| 128 | `READY` | Older terminal with command-mode brightness. |
| 129 | `BYE` | Clean exit. |
| 130 | `PROGRESS` | Credit/touch update. |
| 131 | `ERROR` | Protocol or hardware fault. |
| 132 | `SYNCED` | Fault cleared after `SYNC`. |
| 133 | `DONE` | A rectangle completed. |
| 134 | `READY_BRIGHTNESS` | This terminal; live brightness supported. |
| 135 | `BRIGHTNESS_DONE` | Brightness applied. |

Flags:

| Bit/value | Meaning |
| --- | --- |
| 0..3 | USART error flags (parity, noise, framing, overrun). |
| `0x0800` | DMA transfer error. |
| `0x1000` | Credit/ring violation. |
| `0x2000` | Host-inactivity timeout (3,000 ms). |

`touch.fresh` is 1 for a valid report, 2 for malformed contact data and 3 for
an I2C/ACK failure. Bit 5 of the touch mask is sticky fault indication. Only
one pointer is forwarded.

## Flow control

The host may have at most **512 encoded bytes outstanding** beyond the last
reported consumed offset. The count includes COBS code bytes and delimiters.

The terminal publishes credits:

- approximately every 256 consumed bytes;
- on any record event, including `SYNC`, `END`, `EXIT`, an error or a
  brightness acknowledgement;
- when a fresh touch report is available.

These are byte credits, not per-record acknowledgements. The host keeps the
reverse UART drained, fills the available window, and does not need to stop at
every record boundary. The 1 KiB DMA ring is larger than the 512-byte window so
a conforming sender cannot overwrite unread bytes while the display polls
touch.

A `PROGRESS` report's sequence can describe a partially received record; it is
not an acknowledgement of a whole block.

## Errors, resync and watchdog

- A malformed header, bad CRC, premature `END`, incorrect pixel count, bad
  COBS frame or sequence mismatch sets a protocol fault.
- After a fault, no pixels are written until a valid `SYNC`; the sender must
  redraw the entire affected rectangle.
- USART/DMA errors, a credit/ring violation or 3,000 ms without input stop the
  session and restore the stock runtime. A fresh entry is required.
- A CRC or credit error in a reverse report makes the host stop sending and
  recover through the display watchdog or an explicit exit.
- The display watchdog returns to the blank page after about 3 seconds of host
  silence. It covers host silence, not a hung display or a failed ESP32 image.

## Compatibility

- Protocol version 6 is tied to native version 92 on the display and the
  current ESPHome component. Do not mix a component and TFT from different
  protocol revisions.
- The live-brightness extension uses kind 7, READY 134 and BRIGHTNESS_DONE 135.
  It is backward-compatible only in the sense that an older host safely falls
  back from 134; the TFT cannot be driven with live brightness by that host.
- A new record kind, status, field meaning or pixel format is an incompatible
  protocol change. Bump the version and ship a matching host and TFT.
- The display UART must be owned exclusively by `nspanel_terminal` during a
  session. Do not configure ESPHome's separate `nextion` component on the same
  UART.
