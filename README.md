# NSPanel terminal

Prebuilt custom native firmware for the NSPanel's Nextion display, with an
ESPHome `display:` / `touchscreen:` component for LVGL, native drawing, or any
pixel-level UI.

The ESP32 owns the screen and streams RGB565 rectangles to the display over the
existing UART. The normal Nextion UI remains available when the terminal exits
or the display watchdog fires. The firmware is prebuilt, so users do not need
the Nextion Editor toolchain.

> **EU NSPanel only.** The US version is oriented differently. Do not flash
> this release on a US panel or NSPanel Pro.

This repository packages the prebuilt display firmware and the ESPHome component.
It is experimental, provided **as-is**, and comes with **no warranty**.

## What is here

| Path | Purpose |
| --- | --- |
| [`components/nspanel_terminal/`](components/nspanel_terminal/) | ESPHome display/touchscreen component. |
| [`firmware/nspanel-v92-blank.tft`](firmware/nspanel-v92-blank.tft) | Prebuilt custom native display firmware with a minimal blank UI. |
| [`examples/installer.yaml`](examples/installer.yaml) | One-time ESP32 firmware that uploads the TFT. |
| [`examples/panel.yaml`](examples/panel.yaml) | Graphical ESP32 firmware using the component. |
| [`docs/`](docs/) | Install, compatibility, recovery, safety, protocol and troubleshooting. |
| [`tools/verify_tft.py`](tools/verify_tft.py) | Verifies the shipped TFT's size, SHA-256 and CRCs. |

## Using it from GitHub

Point ESPHome at the release tag:

```yaml
external_components:
  - source: github://ptaru/nspanel-terminal@v0.1.0
    components: [nspanel_terminal]
```

`v0.1.0` matches the included V92 TFT. The component and display firmware are a
matched pair; bump the tag and ship a matching TFT when the protocol changes.
The new terminal uploader and brightness action currently require this checkout
as a local external component until a new tag is published.
ESPHome clones the repository and looks for `components/nspanel_terminal/`; the
clone is cached for a day, so use `refresh: always` while iterating.

## Status

- Display firmware: **prebuilt custom native**, V92 / protocol6.
- The component and protocol path have been hardware-tested on the EU NSPanel.
- The blank-UI TFT in this repository has passed offline package checks:
  outer CRCs, bootloader/installer emulation to application entry, the V92
  helper stream session, and the whole-file finalization helper.
- Visible area is 454×320 inside the 480×320 controller.
- Brightness can be set at boot or changed through the component API. The
  included TFT uses a brief exit/re-entry; a separate blank-base candidate
  supports live changes and was tested on one EU NSPanel.

## Requirements

- The **EU version** of the Sonoff NSPanel (not the US version; not NSPanel Pro).
- ESPHome **2026.10.x** (tested with 2026.10.0).
- A local HTTP server (or GitHub release) for the TFT download.
- A way to recover the ESP32 over serial if an OTA goes wrong.

Read [compatibility](docs/COMPATIBILITY.md) before flashing anything.

## Quick start

1. **Back up** your ESP32 configuration and, if possible, your panel's original
   TFT. Note the current display version.
2. Create a private secrets file in `examples/`:

   ```sh
   cd examples
   cp secrets.yaml.example secrets.yaml
   ```

3. Serve the TFT locally and point `tft_url` at it:

   ```sh
   cd ../firmware
   python3 -m http.server 8000
   ```

   `tft_url` must be reachable from the ESP32, for example
   `http://192.168.1.10:8000/nspanel-v92-blank.tft`.

4. Verify the TFT before using it (from `public/`):

   ```sh
   cd ..
   python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
   ```

5. Flash the installer, then call its `upload_tft` action:

   ```sh
   cd examples
   esphome run installer.yaml
   ```

   Wait for the upload to finish. Do not remove power.

6. Flash the graphical firmware:

   ```sh
   esphome run panel.yaml
   ```

7. Confirm the UI renders and touch works. Call `exit_terminal` to return to the
   blank Nextion page; call `enter_terminal` to return to the ESP32 UI.

Full instructions: [install](docs/INSTALL.md).

## Safety

This writes the display's firmware. An interrupted upload can leave the display
unusable until it is recovered over serial. Read [safety](docs/SAFETY.md) and
[recovery](docs/RECOVERY.md) first. The panel is mains-powered; do not open it
unless you are qualified.

## Licence and provenance

The component and repository code are MIT licensed. The TFT contains the
Nextion display runtime and is intended for use with genuine NSPanel hardware.
This project is not affiliated with Sonoff or ITEAD/Nextion. See [NOTICE](NOTICE)
and [docs/TFT.md](docs/TFT.md).
