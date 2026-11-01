# nspanel_terminal

This ESPHome component implements an RGB565 display and single-pointer
touchscreen for the EU and US NSPanel terminal. It requires ESPHome
2026.10.x, LVGL for UI rendering and exclusive ownership of the display UART;
PSRAM is optional. Standard ESPHome display `lambda:` and `pages:` rendering
are unsupported in the current version. See
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
    # frame_slots_ram: internal  # or psram
```

The display UART must be 115200 8N1 with both TX and RX; entry switches it to
4 Mbaud and recovery restores 115200. The component no longer overwrites the
UART buffer settings; the examples set `tx_buffer_size: 2048` and
`rx_full_threshold: 32` for the intended latency. The touchscreen uses the
standard `display:` key to select the panel. The older `panel_id` key is
accepted only for v0.7.0 compatibility, must match `display:`, and is
deprecated.

`width` and `height` also set the display metadata used by LVGL. Other sizes
are rejected. Select the orientation with these values; a non-zero display
`rotation` is rejected, and the panel's native project already supplies the
correct orientation. The included images report native revision 93; the component also
accepts revision 92 for protocol 6 compatibility. The display loader checks the
stock entry geometry and state, so an EU TFT in a 320×480 configuration (or the
reverse) fails safely instead of streaming into the wrong framebuffer.

The component does not keep a full-frame mirror. Each LVGL flush area is
PackBits-encoded outside the transport lock into one of four reserved slots
and queued for the transport, which sends the length-prefixed payload with
the existing rectangle protocol. Flush areas are split into 16-row rectangles; DATA records and runs
can cross row boundaries. Contiguous sources extend repeats with aligned
32-bit comparisons, while strided sources keep the scalar path. The slots
allocate when the stream starts and prefer internal RAM, so LVGL's draw buffer is placed
first. When every slot is in flight the flush waits briefly for the transport to
release one, then continues with the remaining rows. A timed-out area is
dropped and a full repaint is scheduled instead of leaving stale pixels.

`frame_slots_ram` selects where the four encoded frame slots live. `internal`
(default) prefers internal RAM and falls back to external RAM if needed. `psram`
allocates all four slots in PSRAM, requires the `psram:` component, and frees
about 74 KB of internal RAM on the EU panel; external slots measured roughly
10-14 ms slower per full redraw than internal slots in the earlier A/B. The
LVGL draw buffer is separate: ESPHome allocates it according to `buffer_size`
and falls back to PSRAM when the internal allocation fails.

The component no longer depends on the `psram` component; a panel can run
without external memory when the slot and LVGL buffer sizes leave room for WiFi
and the API. The diagnostic
protocol benchmarks still allocate their own external buffers and fail
gracefully without PSRAM. The included TFTs use a fixed 3-second host-inactivity
watchdog.

The current component services UART records and heartbeats in a task pinned to
ESP32 core 0. Rendering and API actions remain on the ESPHome main loop. A
recursive lock protects the queue and session state. A reset invalidates the
producer generation without recycling a slot while its encoder is writing.
Retryable payloads remain reserved until DONE; completed slots are released
before another job replaces their owner. Queued touch samples return to the
main loop before entering ESPHome's touchscreen code. Out-of-range
or queued touch samples are dropped, and a malformed Goodix report is ignored
rather than stopping the stream. The STM watchdog remains the recovery path if
the transport task itself stops.

The display tracks LVGL refresh start and ready events. Since nothing is
mirrored, entering the stream invalidates the active screen so LVGL repaints
and the flush areas are queued again. The transport may send queued areas while
LVGL renders the next band.

The component requires LVGL. ESPHome display `lambda:`, `pages:`,
`auto_clear_enabled: true` and `show_test_card: true` are rejected at
validation because they would draw into a framebuffer that no longer exists.
Use LVGL widgets and `lvgl.page.show` instead. This does not prohibit lambdas
in automations. For the temporary v0.6.0 sender pin, PSRAM requirements and
matching TFT details, see [rendering compatibility](../../README.md#rendering-compatibility).

`brightness` under `display:` optionally sets the initial backlight level from 0
to 100. At runtime call `id(panel).request_brightness(percent)`, where `percent`
is an integer in that range. The included TFTs apply the change between
rectangles without exiting the terminal. TFTs without live brightness fall back
to a brief exit and re-entry.

Set `tft_url` to an HTTP URL with content length and byte-range support, and call
`id(panel).request_tft_upload()` to install or update the display. The component
stops the terminal before uploading, runs the transfer in a separate task so
the API and OTA remain responsive, and reconnects afterward. This works for the
first installation as well as later updates, so no separate installer image is
needed. `request_exit()` returns to the blank display page; `request_enter()`
starts streaming again. Follow the
[installation and recovery guidance](../../README.md).

The matching display-side receiver is in [`firmware/src/`](../../firmware/src/);
its package layout and memory map are in
[display firmware internals](../../docs/FIRMWARE.md), and the build is in
[building](../../docs/BUILDING.md).
