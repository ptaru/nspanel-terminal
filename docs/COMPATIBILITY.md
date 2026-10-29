# Compatibility

This release targets the **EU version of the Sonoff NSPanel**. The US version is
oriented differently, so do not flash it.
This release is also not for NSPanel Pro or other displays.

- ESPHome **2026.10.x**; tested with 2026.10.0 on the Arduino framework.
- Visible area is 454×320 of the 480×320 controller.
- Single-pointer touch only.
- Brightness setting is available in the current checkout. The included TFT
  briefly exits and re-enters for a runtime change; a separate candidate
  supports live changes and was tested on one EU NSPanel.
- No automatic ESP32 OTA rollback.
- The rightmost 26 columns of the controller are not visible.

Use the included TFT. Do not substitute a TFT exported from a newer Nextion
Editor; the runtime patch is specific to the included image. If the display does
not report version 92 after the upload, stop and check the
[troubleshooting guide](TROUBLESHOOTING.md) rather than trying other images.

## Known limits

- The display watchdog returns to the blank UI after about 300 ms of host
  silence. A host crash is recovered by rebooting the ESP32.
- The blank TFT has passed offline checks but has not been widely hardware
  tested. Test on a spare panel first.
