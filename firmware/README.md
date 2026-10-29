# Firmware

## `nspanel-v92-blank.tft`

Prebuilt custom native display firmware for the NSPanel's Nextion display. It
contains the V92 terminal runtime and a blank Nextion UI with one "waiting for
connection" page.

- Size: 264,936 bytes
- SHA-256: `82434ab839ed14af45c655bdf1980848649c6a48081ee593c5cce397df7f3628`
- Panel: EU version of the Sonoff NSPanel
- Native version after patching: 92
- Base editor: Nextion Editor 1.65.1

Verify before serving it:

```sh
python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
```

Do not replace this with an export from a newer Nextion Editor. The helper
bundle and page format differ; see [docs/TFT.md](../docs/TFT.md).

There is no "stock Sonoff UI" fallback in this image. If you need the original
UI, keep a backup of your panel's own TFT.
