# Protocol summary

The display runs the V92 terminal. The ESP32 is the only UART owner while the
terminal is active.

## Wire format

- COBS-delimited records over the display UART.
- Each record has a small header, a sequence/session field, a CRC-16, and a
  body. The receiver validates the header and body CRC.
- Records: `RECT`, `DATA`, `END`, `PING`, `SYNC`, `EXIT`.
- `RECT` carries `x, y, w, h`. `DATA` carries PackBits-compressed RGB565
  pixels. `END` closes the rectangle.
- Reverse reports are fixed 32-byte frames carrying cumulative consumed-byte
  credits, completed-rectangle count, flags, touch samples and a high-water
  mark.
- The host may have at most 512 encoded bytes outstanding beyond the last
  reported consumed offset.

## Pixels and geometry

- RGB565, little-endian byte order.
- Rendering/touch geometry is 454×320; the controller is 480×320 but the
  rightmost 26 columns are not visible.
- The host keeps a 454×320 framebuffer and a 454×16 snapshot. Dirty rows are
  claimed at snapshot time, merged into rectangles, and scheduled round-robin.

## Liveness and recovery

- The host sends a heartbeat record while idle (about every 80 ms).
- The display's watchdog returns to the Nextion UI after roughly 300 ms of
  silence.
- An explicit `EXIT` returns to the original UI.
- CRC faults request `SYNC` and redraw the affected rectangle; repeated or
  fatal faults stop the session.
- Touch press/release edges are queued; movement is coalesced. One pointer.

## Entry and exit

On entry the host saves page, `bkcmd` and `sys0..2` registers, switches the
display into the terminal and starts streaming. On exit it restores the saved
values and verifies model, native version and baud before reporting success.
A host crash is handled by the display watchdog, not by the exiting host.

The full protocol notes and measured results live in the private research
repository. This document describes the public contract used by the component.
