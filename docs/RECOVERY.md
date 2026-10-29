# Recovery and troubleshooting

A TFT upload writes the display's firmware. Keep the panel powered through the transfer. If it fails, inspect the logs and the served file before retrying; the URL must return the verified file with a content length and support HTTP byte ranges. A display that cannot receive a TFT over its UART may require physical recovery; that path is not guaranteed.

If the ESP32 is unreachable, try a power cycle and OTA; a failed ESP32 image may require serial flashing. The display watchdog should return to the blank page after host silence. The NSPanel is mains-powered: disconnect power before opening it.

If the blank page appears but the terminal does not start, check ESPHome logs for identity, UART or PSRAM errors. Verify the model and V92 firmware, ensure the component and TFT come from the same checkout, and call `enter_terminal`. An older host cannot stream to the live-brightness TFT; update the ESP32 host first.

To return to an earlier display UI, upload a compatible backed-up TFT. Merely replacing the file served at the URL does not change the installed display. There is no universal stock TFT in this repository.
