# nspanel_terminal

This ESPHome component implements a 454×320 RGB565 display and single-pointer touchscreen for the EU NSPanel V92 terminal. It requires ESPHome 2026.10.x, ESP32 PSRAM and exclusive ownership of the display UART. See [the example](../../examples/panel.yaml) for wiring, LVGL and API actions.

Version 0.2.0 tracks changed pixels in 8-pixel column tiles. Separate runs on a row are streamed as separate rectangles, reducing unchanged background traffic. The included TFT uses a fixed 3-second host-inactivity watchdog.

`brightness` under `display:` optionally sets the initial backlight level from 0 to 100. At runtime call `id(panel).request_brightness(percent)`, where `percent` is an integer in that range. The included TFT applies the change between rectangles without exiting the terminal. Older V92 TFTs fall back to a brief exit and re-entry.

Set `tft_url` to an HTTP URL with content length and byte-range support, and call `id(panel).request_tft_upload()` to update the display. The component exits the stream before uploading and reconnects afterward. `request_exit()` returns to the blank display page; `request_enter()` starts streaming again. Use the separate installer for the first installation and follow the [installation and recovery guidance](../../README.md).
