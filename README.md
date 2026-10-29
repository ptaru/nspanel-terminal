# NSPanel terminal

Use an EU Sonoff NSPanel as an ESPHome display and touchscreen. The ESP32 streams RGB565 rectangles to the display's STM over UART; the included TFT provides the terminal and a blank fallback page. This project is experimental and targets the **EU NSPanel** (`NX4832F035_011C`, MCU code F130), not the US panel or NSPanel Pro.

The included V92/protocol 6 TFT supports live brightness changes and has been tested on one EU panel. Its SHA-256 is `b1ad0d700265eeeaf073ed029118c24901a3ff0b70f62f40a731d9d243efc41a`. The component supports ESPHome 2026.10.x, PSRAM, a 454×320 visible area and single-pointer touch.

## Install

1. Back up the panel's existing TFT and ESP32 configuration. Keep power stable during display updates; a failed transfer can require recovery.
2. Copy `examples/secrets.yaml.example` to `examples/secrets.yaml` and fill in your credentials and `tft_url`. Serve `firmware/nspanel-v92-blank.tft` over HTTP from a server that supports `HEAD` and byte-range requests. For example, Home Assistant's `/local/` file service can host it.
3. Verify the file:

   ```sh
   python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
   ```

4. For the first installation, flash `examples/installer.yaml` and call its `upload_tft` API action. Wait for completion and the display restart.
5. Flash `examples/panel.yaml`. It loads the component from this checkout. Verify that the ESPHome UI renders and touch works.

The sample configuration uses the EU NSPanel GPIO pins and PSRAM wiring. Review it before installing, especially if your board differs. Use the TFT and component from the same checkout: the live-brightness TFT advertises a capability older component versions do not recognize. The `v0.1.0` tag predates these changes; do not use it with the current TFT.

## Operate

The example exposes ESPHome API actions `exit_terminal`, `enter_terminal`, `upload_tft` and `set_brightness`. Set `percent` to an integer from 0 to 100. The shipped TFT applies brightness between streamed rectangles and acknowledges it without leaving the terminal. `brightness` under the display sets the initial level at boot.

For subsequent TFT updates, serve the verified TFT at the configured URL and call `upload_tft` from the panel firmware. The component owns the display UART; do not configure a separate Nextion component on that UART at the same time. If the host stops transmitting, the display watchdog returns to the blank page. The blank TFT does not contain the original Sonoff UI.

See [protocol](docs/PROTOCOL.md), [recovery](docs/RECOVERY.md), [component configuration](components/nspanel_terminal/README.md), and [examples](examples/README.md). The TFT contains the Nextion runtime; see [NOTICE](NOTICE). Code is MIT licensed; see [LICENSE](LICENSE).
