# Display firmware

`nspanel-v92-blank.tft` is the blank EU NSPanel V92/protocol 6 TFT with live brightness. It is 264,936 bytes and has SHA-256 `b1ad0d700265eeeaf073ed029118c24901a3ff0b70f62f40a731d9d243efc41a`.

The blank UI was exported with Nextion Editor 1.65.1. The patched terminal and live brightness extension were tested on one EU NSPanel; the display acknowledged live 50% and 100% changes, and the user confirmed the change was visible. The blank fallback contains no original Sonoff UI controls.

Verify the file before use:

```sh
python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
```
