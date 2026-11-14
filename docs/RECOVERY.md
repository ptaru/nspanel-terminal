# Recovery and troubleshooting

A TFT upload writes the display's firmware. Keep the panel powered through the transfer. If it fails, inspect the logs and the served file before retrying; the URL must return the verified file with a content length and support HTTP byte ranges. A display that cannot receive a TFT over its UART may require physical recovery; that path is not guaranteed.

**Same-version installs retain the installed native code.** The stock version-equality decision keeps the display's current internal native image when the incoming TFT reports the same native version. Reinstalling a rebuilt image without bumping the version can therefore leave the panel running old native code even though the file transfer completed. Every native change must get a new version, and the host component must accept it; to force a rewrite, install an image with a different version first. This is why the 95 images are not interchangeable with the 94 release.

If the ESP32 is unreachable, try a power cycle and OTA; a failed ESP32 image may require serial flashing. The included TFT's display watchdog should return to the blank "waiting for connection" page after about 3 seconds of host silence. The NSPanel is mains-powered: disconnect power before opening it.

If the blank page appears but the terminal does not start, check ESPHome logs for identity, UART or PSRAM errors. Verify the model, native version and panel geometry: the included images report native 95. Use `width: 454`, `height: 320` for the EU panel or `width: 320`, `height: 480` for the US panel. Ensure the component and TFT come from the same checkout, and call `enter_terminal`. Install the matching ESP32 component before updating the TFT.

To restore a different display UI, upload a compatible backed-up TFT. Merely replacing the file served at the URL does not change the installed display. The `base-*.tft` files are the stock build inputs, not tested recovery images; keep your own dump of the panel before changing it.
