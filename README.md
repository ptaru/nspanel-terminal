# NSPanel terminal

Custom firmware for the Nextion display inside the EU and US Sonoff NSPanel. It lets the ESP32 draw arbitrary RGB565 pixels over UART and receive touch input. The included ESPHome component requires LVGL to render the interface.

The TFT replaces the Sonoff interface with a blank fallback page. If the host stops transmitting, the display returns to that page after about three seconds. The firmware targets `NX4832F035_011C` displays with MCU code F130.

| Panel | Geometry | Display firmware |
| --- | --- | --- |
| EU landscape | 454×320 | [`terminal-eu.tft`](firmware/terminal-eu.tft) |
| US portrait | 320×480 | [`terminal-us.tft`](firmware/terminal-us.tft) |

The EU firmware has been tested on one panel. The US firmware is validated offline only. Checksums are listed in [display firmware](firmware/README.md).

## Install

1. Keep a backup TFT and review [recovery](docs/RECOVERY.md). An interrupted display update can require physical recovery.
2. Choose the [EU](examples/panel.yaml) or [US](examples/panel-us.yaml) ESPHome example. Copy `examples/secrets.yaml.example` to `examples/secrets.yaml` and set your credentials.
3. Serve the matching TFT over HTTP with content length and byte-range support, set `tft_url`, and verify the file with `python3 tools/verify_tft.py --panel eu firmware/terminal-eu.tft` or the corresponding `us` paths.
4. Flash the ESP32 configuration, then call its `upload_tft` API action. Keep power stable until the transfer finishes, then check rendering and touch.

Use the component and TFT from the same release. The examples pin `v0.8.0`. For local builds, see [component configuration](examples/README.md#working-on-the-component). The component owns the display UART; do not share it with another Nextion component.

## Rendering

Use `lvgl:` widgets and `lvgl.page.show`. Standard ESPHome display `lambda:` and `pages:` rendering are unsupported; lambdas in automations are supported. See [component configuration](components/nspanel_terminal/README.md) for settings, API calls and optional LVGL IRAM placement.

For non-LVGL display drawing, pin the component to `github://ptaru/nspanel-terminal@v0.6.0` and use that tag's [configuration documentation](https://github.com/ptaru/nspanel-terminal/tree/v0.6.0) and matching TFT. That version supports display lambdas and pages and requires PSRAM. Its TFT is incompatible with the current component.

## Documentation

- [Building](docs/BUILDING.md): compile and verify both TFTs offline.
- [Firmware internals](docs/FIRMWARE.md) and [protocol](docs/PROTOCOL.md): display implementation and UART format.
- [Examples](examples/README.md) and [optional keyboard](components/button_keyboard/README.md): ESPHome configuration.
- [Contributing](CONTRIBUTING.md) and [changelog](CHANGELOG.md).

Code is MIT licensed; see [LICENSE](LICENSE). The TFTs contain the Nextion runtime; see [NOTICE](NOTICE).
