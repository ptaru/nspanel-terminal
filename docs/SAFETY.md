# Safety and risk

This project modifies the display firmware and replaces the ESP32 firmware of a
mains-powered device. It is experimental. **You accept all risk.**

This release is for the **EU version** of the NSPanel only. The US version is
oriented differently; do not flash it.

## Electrical

- The NSPanel is mains-powered. Unplug it before opening the case.
- Only qualified people should work on mains wiring.
- Use a stable supply while uploading. A power cut during a TFT write can brick
  the display.
- Do not touch the display or board while flashing unless the procedure says to.

## Firmware

- The TFT upload writes the display's firmware. An interrupted upload may leave
  the display unusable until it is recovered over serial.
- The ESP32 bootloader has no automatic OTA rollback. A bad OTA can require USB
  serial recovery.
- Do not flash a TFT built with a different Nextion Editor version, a different
  display model, or for a different MCU code. It may not boot.
- Do not run ESPHome's `nextion` component and `nspanel_terminal` in the same
  image. They both own the display UART.
- Do not expose an unverified TFT over HTTP. Verify its SHA-256; an attacker who
  controls the URL could flash your panel.

## Data and privacy

- The shipped blank TFT contains no personal UI or site-specific labels.
- Your ESPHome configuration and `secrets.yaml` are yours; do not commit them.
- The TFT download URL may reveal your LAN address in logs. Use a local server
  or a release URL as appropriate.

## Warranty

The software and TFT are provided **as-is**, without warranty of any kind. The
authors are not liable for damage, data loss, bricked panels, or injury. This
project is not affiliated with Sonoff or ITEAD/Nextion.
