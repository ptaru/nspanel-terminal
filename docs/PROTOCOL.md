# Terminal protocol

The ESP32 owns the display UART during a terminal session. The V92 receiver accepts COBS-framed protocol 6 records with a session, sequence number and CRC16. `RECT`, `DATA` and `END` carry PackBits-compressed RGB565 rectangles; `PING`, `SYNC` and `EXIT` handle liveness and recovery. The host limits unacknowledged encoded data to 512 bytes and consumes 32-byte reports containing credits, errors and touch state.

The included TFT advertises live brightness with ready status 134. A `BRIGHTNESS` record (kind 7) carries a 0–100 value in the header's `x` field and is accepted between rectangles. The STM calls its backlight routine and returns status 135. The host queues changes until idle. With an older V92 TFT the current host uses normal command mode and briefly exits the terminal.

The host saves page, `bkcmd` and `sys0..2` on entry, switches to 4 Mbaud, and restores and verifies the saved state at 115200 on exit. The packaged TFT's display watchdog returns to the blank page after roughly 3 seconds of host silence. The visible geometry is 454×320 within a 480×320 controller; touch supports one pointer.
