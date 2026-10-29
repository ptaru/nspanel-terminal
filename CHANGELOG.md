# Changelog

## Unreleased

- Publish the native display firmware source, the V92 base TFT and a
  fixed-address builder that reproduces the shipped image byte-for-byte.
- Add in-depth display firmware, build and protocol documentation.
- Add a source-build verification mode and update the shipped TFT checksum file.
- Rename the native receiver, loader and protocol sources to describe their
  roles rather than the brightness extension.

## 0.2.0 — 2026-10-29

- Ship the hardware-tested blank V92 TFT with live brightness and a fixed 3-second host-inactivity watchdog.
- Add terminal TFT upload and brightness actions.
- Reduce streamed pixels by tracking changed 8-pixel tiles and keeping distant changes in separate rectangles.
- Add render measurement counters and timing trace hooks.
- Consolidate installation and recovery guidance.

## 0.1.0 — 2026-10-29

- Public packaging of the prebuilt custom native display firmware and the ESPHome display/touchscreen component.
- Blank-UI TFT (`nspanel-v92-blank.tft`) built from a compatible Nextion
  Editor 1.65.1 export and patched to native V92.
- Installer and graphical ESPHome examples.
- Install, compatibility, recovery, safety, protocol and troubleshooting docs.
- TFT verifier.
