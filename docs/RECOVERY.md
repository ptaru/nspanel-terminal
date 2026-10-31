# Recovery and troubleshooting

A TFT upload writes the display's firmware. Keep the panel powered through the transfer. If it fails, inspect the logs and the served file before retrying; the URL must return the verified file with a content length and support HTTP byte ranges. A display that cannot receive a TFT over its UART may require physical recovery; that path is not guaranteed.

If the ESP32 is unreachable, try a power cycle and OTA; a failed ESP32 image may require serial flashing. The included TFT's display watchdog should return to the blank "waiting for connection" page after about 3 seconds of host silence. The NSPanel is mains-powered: disconnect power before opening it.

If the blank page appears but the terminal does not start, check ESPHome logs for identity, UART or PSRAM errors. Verify the model, native version and panel geometry: the included images report native 93. Use `width: 454`, `height: 320` for the EU panel or `width: 320`, `height: 480` for the US panel. Ensure the component and TFT come from the same checkout, and call `enter_terminal`. A host without live-brightness support cannot stream to the live-brightness TFT; update the ESP32 host first.

To restore a different display UI, upload a compatible backed-up TFT. Merely replacing the file served at the URL does not change the installed display. The `*-base.tft` files are the stock build inputs, not tested recovery images; keep your own dump of the panel before changing it.
