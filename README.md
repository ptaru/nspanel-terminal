# NSPanel terminal

Release 0.5.0.


Turn an EU or US Sonoff NSPanel into a general-purpose ESPHome display and
touchscreen. ESPHome draws the interface on the ESP32 and sends it to the
panel's screen over the internal serial link. The supplied display image
replaces the Sonoff interface with a blank "waiting for connection" page, which
is shown when the terminal is not running. This project is experimental and
targets the NSPanel display `NX4832F035_011C` (MCU code F130), not NSPanel Pro.

| Panel | Orientation | Screen size | Firmware version | Image to install | SHA-256 |
| --- | --- | --- | ---: | --- | --- |
| EU | landscape | 454×320 in a 480×320 controller | 93 | `firmware/terminal-eu.tft` | `925b0f35…3c0ae3c` |
| US | portrait | 320×480 | 93 | `firmware/terminal-us.tft` | `20ba711b…404e73c` |

Both terminal images report native revision 93. Both retained bases are
unmodified 72 exports (the EU Editor export and the stock US export); the EU
build applies EU native patches, while the US build leaves the
stock US native code alone. Panel geometry is
selected by `width`/`height` and checked by the display loader, not inferred
from the version.

Both images replace the stock Sonoff interface and let ESPHome control the
screen and receive touch input. Brightness changes take effect while the UI is
running. If ESPHome stops sending data, the panel returns to its blank fallback
page after about three seconds, so a crashed or disconnected host does not leave
the screen frozen.

The EU image has been tested on a real EU panel. The US image is built from the
stock US image and validated offline (package structure and checksums); it has not been run on US hardware yet. Treat it as experimental and
keep the original US image for rollback.

The display firmware source is included in [`firmware/src/`](firmware/src/) and
builds both images from the original Sonoff images. See
[display firmware internals](docs/FIRMWARE.md) for the implementation and
[building](docs/BUILDING.md) for the toolchain and commands.

## Install

1. Back up the panel's existing TFT and ESP32 configuration. Keep power stable during display updates; a failed transfer can require recovery.
2. Copy `examples/secrets.yaml.example` to `examples/secrets.yaml` and fill in your credentials and `tft_url`. Serve the TFT for your panel over HTTP from a server that supports `HEAD` and byte-range requests. For example, Home Assistant's `/local/` file service can host it.
3. Verify the file:

   ```sh
   # EU
   python3 tools/verify_tft.py firmware/terminal-eu.tft
   # US
   python3 tools/verify_tft.py --panel us firmware/terminal-us.tft
   ```

4. Flash `examples/panel.yaml` for the EU panel or `examples/panel-us.yaml` for the US panel — USB serial for the first installation, or OTA if the current firmware is reachable. Both load the component from the `v0.5.0` tag on GitHub and expose `upload_tft`, so no local checkout is needed.
5. Call `upload_tft`. The component stops the terminal, installs the TFT from `tft_url`, and restarts the display. Wait for completion, then verify that the ESPHome UI renders and touch works.

The sample configurations use the NSPanel GPIO pins and PSRAM wiring. Review them
before installing, especially if your board differs. Keep the component and the
TFT from the same release (`v0.5.0`; use the `main` branch for development) and
select the matching panel geometry:

```yaml
display:
  - platform: nspanel_terminal
    # EU landscape (default)
    width: 454
    height: 320

  # US portrait
  # width: 320
  # height: 480
```

Only `454x320` and `320x480` are accepted. The component rejects the wrong
native identity rather than streaming into a mismatched framebuffer.

## Operate

The examples expose ESPHome API actions `exit_terminal`, `enter_terminal`,
`upload_tft` and `set_brightness`. Set `percent` to an integer from 0 to 100.
The TFT applies brightness between streamed rectangles and acknowledges it
without leaving the terminal. `brightness` under the display sets the initial
level at boot.

For later TFT updates, serve the verified TFT at the configured URL and call
`upload_tft` from the panel firmware. The component owns the display UART; do
not configure a separate Nextion component on that UART at the same time. If the
host stops transmitting, the display watchdog returns to the blank page. The
blank TFT does not contain the Sonoff UI.

## Build from source

The display build is offline and needs only Python and a clang with the ARM
backend, not ESPHome:

```sh
python3 tools/build_tft.py --panel eu --check
python3 tools/verify_tft.py build/terminal-eu.tft

python3 tools/build_tft.py --panel us --check
python3 tools/verify_tft.py --panel us build/terminal-us.tft
```

Or run `make verify`, which checks both profiles. `--check` reproduces the
shipped TFT byte-for-byte. For a modified build, omit `--check` and verify with
`tools/verify_tft.py --allow-modified` (add `--panel us` for the US image). The
builder never uploads. See [building](docs/BUILDING.md) and the
[source map](firmware/src/).

See [firmware internals](docs/FIRMWARE.md), [protocol](docs/PROTOCOL.md),
[recovery](docs/RECOVERY.md),
[component configuration](components/nspanel_terminal/README.md),
[examples](examples/README.md), and [contributing](CONTRIBUTING.md). The TFT
contains the Nextion runtime; see [NOTICE](NOTICE). Code is MIT licensed; see
[LICENSE](LICENSE).
