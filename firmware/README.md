# Display firmware

`nspanel-v92-blank.tft` is the blank EU NSPanel V92/protocol 6 TFT with live brightness and a fixed 3-second host-inactivity watchdog. It is 264,936 bytes and has SHA-256 `a25aafc263b8980e1f91e9bfe6f363d2197c657bb5a2231f86d61a9828a3d10c`.

The blank UI was exported with Nextion Editor 1.65.1. The patched terminal and live brightness extension were tested on one EU NSPanel; the display acknowledged live 50% and 100% changes, and the user confirmed the change was visible. The blank fallback contains no original Sonoff UI controls.

Verify the file before use:

```sh
python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
```
