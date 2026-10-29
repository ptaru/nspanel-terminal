# The display firmware

`firmware/nspanel-v92-blank.tft` is the prebuilt custom native display firmware:
a **blank Nextion UI** with the V92 terminal runtime. It shows a single "waiting
for connection" page when the ESP32 is not driving the screen; it contains no
personal labels, pages, images or controls.

Target hardware: the **EU version** of the Sonoff NSPanel. The US version is
oriented differently; this image is not for
it.

This verified public blank V92 TFT is the base for future display changes.
Historical kitchen TFT build scripts retain their original inputs so earlier
trial hashes remain reproducible.

## Provenance

- The UI was exported with **Nextion Editor 1.65.1** for the EU NSPanel
  (`NX4832F035_011C`, 480×320).
- The base TFT reports native version 72, F130 image size 60,352 bytes, decoded
  F130 SHA-256
  `5eaaba40f797cf12543e2f78e08436f08b127486456c15e7d946d40f62045127`.
- The V92 patch adds the terminal loader and pool program, sets the native
  version to 92, and leaves the blank UI, font, page tables, encoded H2 header
  and F140 image untouched.
- Output: 264,936 bytes, SHA-256
  `82434ab839ed14af45c655bdf1980848649c6a48081ee593c5cce397df7f3628`.

## Why Editor 1.65.1

The V92 patch is tied to the F130 native image and helper bundle that ship with
Nextion Editor 1.65.1. A newer editor produced a 59,616-byte F130 at native
version 128, a 7,332-byte/4-entry helper bundle and a different page/H2 format.
Patching that would be a port, not a patch. Do not substitute a newer export.

## Offline validation

The shipped image has passed:

- outer header, resource and whole-file CRC checks;
- native version/identity and helper-bundle CRC checks;
- bootloader/installer emulation, reaching application entry;
- a V92 stream session, including `connect` reporting
  `NX4832F035_011C,92,61744` and a rectangle transfer;
- the whole-file finalization helper.

It was uploaded once to the project EU NSPanel on 29 October 2026. The terminal
reported full transfer, and a subsequent exit verified native92, baud and
restored registers/page. The user confirmed the blank page appears and terminal
re-entry works; broader panel compatibility is untested.

## Verify

```sh
python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
```

The verifier checks the size, SHA-256, native version, helper bundle and all four
outer CRCs.

## Rebuilding

The public repository ships the finished TFT. Rebuilding requires the private
V92 build inputs and a compatible 1.65.1 blank export; do not attempt to patch a
TFT from a different editor version.
