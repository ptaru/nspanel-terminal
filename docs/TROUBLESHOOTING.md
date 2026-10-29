# Troubleshooting

## Is this a US NSPanel?

This release is for the **EU version** only. The US version is oriented
differently. Do not flash it or try to
compensate with calibration; use a build made for that hardware.

## The component logs `native identity` and never starts

The display is not running the included V92 image. Query `connect`:

- version 72: the TFT upload did not take; use the installer.
- version 128: a newer-editor TFT was served; use the shipped file.
- anything else: stop. The panel is not a supported EU NSPanel or the TFT is
  wrong.

## `connect timeout`

- Check the display is powered (GPIO4 screen power) and the UART is on
  GPIO16/17 at 115200.
- Confirm the installer's `tft_url` is reachable and the upload completed.
- Power-cycle the display and ESP32, then retry once.

## TFT upload fails or stalls

- The URL must be reachable from the ESP32 and support HTTP Range requests.
- Verify the SHA-256 with `tools/verify_tft.py`.
- Do not use HTTPS with a self-signed certificate; ESPHome's HTTP client may
  reject it. Use plain HTTP on a trusted LAN or a valid certificate.
- Do not retry blindly after a partial transfer; read [recovery](RECOVERY.md).

## The display shows the blank page after boot

The ESP32 has not entered the terminal yet, the display watchdog won, or the
component failed its identity check. Check the ESP32 log. Call
`enter_terminal` once the host is up.

## The display stays black after an ESP32 crash

The display may be in terminal mode with no host. Wait for the watchdog, then
power-cycle the display. If it stays black, recover the TFT over serial.

## Touch is offset or does not work

- The supported geometry is 454×320 with calibration 0..453 / 0..319.
- Confirm the TFT is the shipped blank image.
- A US panel is oriented differently; stop rather than adjusting calibration.
- Do not enable multi-touch; only one pointer is supported.

## Colours are wrong

- The component expects RGB565 and handles byte order internally.
- A red/blue swap usually means a different display model or a TFT built for a
  different controller profile. Stop and verify the hardware.

## ESP32 is unreachable after an OTA

Automatic rollback is unavailable. Use USB serial recovery; see
[recovery](RECOVERY.md). Keep a known-good image.

## ESPHome says the component is missing

- If you are using a local checkout, set `type: local` and
  `path: ../components`.
- Otherwise use `source: github://ptaru/nspanel-terminal@v0.1.0`.
- Pin ESPHome 2026.10.0 if the API does not compile.

## I added the Nextion component too

Do not. `nextion` and `nspanel_terminal` both own the display UART. Use the
installer image for TFT updates and the panel image for normal operation.
