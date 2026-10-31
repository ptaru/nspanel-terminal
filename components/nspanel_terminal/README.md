# nspanel_terminal

This ESPHome component implements an RGB565 display and single-pointer
touchscreen for the EU and US NSPanel terminal. It requires ESPHome
2026.10.x, ESP32 PSRAM and exclusive ownership of the display UART. See
[the EU example](../../examples/panel.yaml) and
[the US example](../../examples/panel-us.yaml) for wiring, LVGL and API actions.

The component supports the two panel geometries:

| Panel | `width` | `height` | Visible area | Native |
| --- | ---: | ---: | --- | ---: |
| EU | 454 (default) | 320 (default) | 454×320 in a 480×320 controller | 93 |
| US | 320 | 480 | full 320×480 | 93 |

```yaml
display:
  - platform: nspanel_terminal
    id: panel
    uart_id: tf_uart
    width: 320
    height: 480
    update_interval: never
    auto_clear_enabled: false
```

`width` and `height` also set the display metadata used by LVGL. Other sizes
are rejected. Select the orientation with these values and leave ESPHome's
`rotation` at 0; the panel's native project already supplies the correct
orientation. The included images report native revision 93; the component also
accepts revision 92 for protocol 6 compatibility. The display loader checks the
stock entry geometry and state, so an EU TFT in a 320×480 configuration (or the
reverse) fails safely instead of streaming into the wrong framebuffer.

The component tracks changed pixels in 8-pixel column tiles and selects dirty
rectangles for transmission. The included TFTs use a fixed 3-second host-inactivity
watchdog.

The current component services UART records and heartbeats in a task pinned to
ESP32 core 0. Rendering and API actions remain on the ESPHome main loop. A
recursive lock protects the framebuffer and session state; queued touch samples
return to the main loop before entering ESPHome's touchscreen code. The STM
watchdog remains the recovery path if the transport task itself stops.

When LVGL is present, the display tracks refresh start and ready events. It
holds new framebuffer snapshots from the first flush of a refresh until all
flush callbacks in that refresh have completed, so one LVGL paint is never
split into mid-refresh snapshots. The transport can keep sending during
rendering before the first flush.
For ESPHome display lambdas and display pages, the component applies the same
gate around the complete `update()` call. The transport task continues handling
UART traffic while the writer runs.

`brightness` under `display:` optionally sets the initial backlight level from 0
to 100. At runtime call `id(panel).request_brightness(percent)`, where `percent`
is an integer in that range. The included TFTs apply the change between
rectangles without exiting the terminal. TFTs without live brightness fall back
to a brief exit and re-entry.

Set `tft_url` to an HTTP URL with content length and byte-range support, and call
`id(panel).request_tft_upload()` to install or update the display. The component
stops the terminal before uploading and reconnects afterward. This works for the
first installation as well as later updates, so no separate installer image is
needed. `request_exit()` returns to the blank display page; `request_enter()`
starts streaming again. Follow the
[installation and recovery guidance](../../README.md).

The matching display-side receiver is in [`firmware/src/`](../../firmware/src/);
its package layout and memory map are in
[display firmware internals](../../docs/FIRMWARE.md), and the build is in
[building](../../docs/BUILDING.md).
