# Recovery

There is **no warranty** and no guaranteed recovery from every failure. Read
this before flashing. The panel is mains-powered; unplug it before opening it.

This release is for the **EU version** of the NSPanel only. The US version is
oriented differently; do not attempt these
steps on one.

## If the ESP32 is unreachable

Automatic OTA rollback is not available on the NSPanel's bootloader. If a bad
ESP32 image leaves the panel unreachable:

1. Try a power cycle and a fresh OTA from ESPHome.
2. If it still does not connect, flash the ESP32 over USB serial. The NSPanel
   uses the usual ESP32 serial pins (GPIO1/GPIO3, GPIO0 to ground for boot
   mode); you may need to open the panel and use the programming pads. Keep a
   known-good ESP32 image on hand.
3. Do not repeatedly OTA the same broken image without a recovery path.

A display left in terminal mode after an ESP32 reboot is not an emergency: its
watchdog returns to the Nextion UI after roughly 300 ms of host silence.

## If the TFT upload fails

Signs: the display stays on the update screen, reboots to an old version, or
does not respond.

1. **Do not power off in the middle of the upload.** If it already failed, wait
   for the display to reboot or show an error.
2. Check the URL from a phone/computer on the same network. It must serve the
   exact file and support HTTP Range requests. Verify the SHA-256.
3. Power-cycle the display and the ESP32, then retry the upload once. Do not
   retry blindly.
4. A `connect` response with version 72 means the upload did not take; version
   128 means the wrong TFT was served.
5. If the display no longer responds to `connect` or shows a blank/black screen,
   the display may need serial recovery. The display UART is the same
   ESP32-side UART (GPIO16/GPIO17). With a USB-TTL adapter at 115200 and the
   display's bootloader, re-upload a known-good TFT. This requires opening the
   panel and is not a beginner operation; use the NSPanel board pinout and a
   current backup.

## If the display shows the blank page but the ESP32 UI does not start

1. Confirm the TFT reports version 92 on `connect`.
2. Call `enter_terminal` from the API.
3. Check the ESP32 log for `native identity`, `connect timeout`, framebuffer
   allocation failure or UART errors.
4. Reflash `panel.yaml`; the component starts the terminal at boot.

## If colours or touch are wrong

- Colour order/geometry is fixed by the component; a wrong image means the
  display is not the supported model or the TFT is not V92.
- Touch uses the fixed 0..453 / 0..319 calibration. If your unit is offset,
  check the panel revision and the TFT's orientation before changing the
  calibration.

## If you want to go back

There is no universal "stock" TFT. The blank page in this repository is the
fallback UI, not Sonoff's original UI. If you need the original UI, you must
have backed it up or obtained a compatible image for your panel. Flashing a TFT
from a different editor version or model is unsafe. Exit the terminal first,
then use the installer image to upload your backup.
