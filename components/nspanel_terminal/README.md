# nspanel_terminal

ESPHome `display:` and `touchscreen:` implementation for the prebuilt custom
native firmware on the **EU version of the Sonoff NSPanel** (V92/protocol6).

## Usage

```yaml
external_components:
  - source: github://ptaru/nspanel-terminal@v0.1.0
    components: [nspanel_terminal]

display:
  - platform: nspanel_terminal
    id: panel
    uart_id: tf_uart
    update_interval: never
    auto_clear_enabled: false
    tft_url: !secret tft_url
    brightness: 100

touchscreen:
  - platform: nspanel_terminal
    id: panel_touch
    display: panel
    panel_id: panel
    calibration:
      x_min: 0
      x_max: 453
      y_min: 0
      y_max: 319
```

The component owns the display UART. Add an ESPHome API action calling
`id(panel).request_tft_upload()` to update the TFT from `tft_url`. The URL must
serve HTTP byte ranges. The component exits terminal mode before transfer and
reconnects after upload. Do not add ESPHome's `nextion` component to the same
UART.
The first TFT installation still uses `installer.yaml`, since the terminal
requires V92 to enter and exit its stream protocol.

`brightness` optionally sets the backlight percentage at boot (0–100). For
runtime changes, expose an API action with a `percent: int` variable that calls
`id(panel).request_brightness(percent)`. The current V92 TFT uses the STM's
normal `dim` command, which briefly exits and re-enters terminal mode. A new
blank-base TFT candidate adds a live brightness record; the component detects
its capability report and applies the change without leaving the stream. When
the terminal is already stopped, it uses the normal command mode. The live
extension was hardware-tested on one EU NSPanel on 29 October 2026; the
included public TFT still uses the original capability response.
Use the local component source while testing this change; the example's
`v0.1.0` tag refers to the earlier published version.

## Requirements

- The EU version of the Sonoff NSPanel.
- ESPHome 2026.10.x (tested with 2026.10.0), Arduino framework.

The component sets the UART TX ring to 2048 bytes and the RX threshold to 32
bytes before UART setup. It uses a 454×320 RGB565 framebuffer and a 454×16
snapshot.

## Behaviour

- Non-blocking protocol6 transport with byte credits and PackBits records.
- Dirty-row tracking, 16-row snapshots, round-robin scheduling.
- CRC/SYNC redraw for the affected rectangle; fatal faults stop safely.
- Touch press/release edges are queued; one pointer.
- Entry saves page/registers/baud; exit verifies and restores them.
- Idle heartbeats keep the display watchdog from returning to the blank UI
  while the host is alive.
- `exit_terminal` returns to the Nextion UI. If the host stops sending data,
  the display watchdog returns to the stock/blank page after about 300 ms.

## Files

| File | Role |
| --- | --- |
| `display.py`, `touchscreen.py` | ESPHome schema and code generation. |
| `protocol.h` | Protocol6 record encoding and reverse-report validation. |
| `stream_engine.h` | Platform-independent transport, dirty rows and touch queue. |
| `nspanel_terminal.cpp` | Framebuffer, UART scheduling, entry/exit and recovery. |
| `nspanel_touchscreen.h` | ESPHome single-pointer event delivery. |

## Limitations

- EU NSPanel only; the US version is oriented differently. The component also
  stops if the included TFT is not installed.
- 454×320 visible geometry; the rightmost 26 columns of the 480×320 controller
  are not visible.
- Single pointer; no multi-touch.
- The currently shipped TFT briefly exits and re-enters for a brightness
  change. The live-brightness TFT is a separate paired candidate.
- TFT upload currently supports HTTP URLs only.
- Hardware-specific timings and the blank TFT need validation on more panels.
