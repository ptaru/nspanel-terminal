# Changelog

## 0.6.0 — 2026-10-31

- Speed up PackBits encoding and DATA framing with bounded direct buffer writes
  and GCC speed optimisation confined to the hot routines.
- Add byte-for-byte compatibility tests against the previous encoder across
  packet/run boundaries, every legal DATA payload length and protocols 6/7.
- Keep the protocol, credit window and both TFTs unchanged. Existing 0.5.0
  installations need only update the ESP32 component; no TFT upload is required.
  The US build remains offline-validated only.

## 0.5.0 — 2026-10-31

- Add protocol 7 and native 93 TFTs with burst completion and live brightness.
- Improve dirty-rectangle selection and retain protocol 6 compatibility.
- Add portable protocol and rectangle coverage checks.
- Load the component from the tagged GitHub release in both examples, so they
  no longer assume a local checkout.

## 0.4.0 — 2026-10-30

- Add a `run_render_benchmark` diagnostic action. It forces one or more
  full-screen LVGL refreshes through the normal dirty-rectangle path and
  reports total, refresh, transfer, copy, encode, UART-write, credit-wait and
  DONE-wait timings plus wire utilisation. It also reports the span from the
  first rectangle submit to the final DONE.
- Wake the ESP32 transport task when reverse UART data arrives, reducing idle gaps during sustained display updates. The EU historical gradient and random workloads improved from about 2.6 s and 9.3 s to 2.22 s and 7.19 s over eight full-screen frames, with unchanged wire content and no transport faults. The TFT receiver is unchanged.
- Include the `esp_http_client` IDF component, which current ESPHome releases exclude by default, so TFT upload builds on ESP-IDF. (#1)
- Leave Nextion protocol reparse mode (`recmod=1`) before a TFT upload, so the upload also starts from TFTs that use it, such as the Lovelace UI. (#1)
- Hold UART snapshots from the first LVGL flush instead of `REFR_START`, so a refresh that renders but changes nothing does not stall the transport. A direct bulk draw outside an LVGL refresh no longer leaves the gate set. (#1)
- Merge dirty runs on a row while the gaps stay within 16 tiles (128 px), splitting at wider gaps. This keeps the page-switch rectangle reduction without streaming large unchanged spans. (#1)

Thanks to @randybb for the TFT upload and redraw fixes in #1.

## 0.3.0 — 2026-10-29

- Add US portrait support: `terminal-us.tft` and `base-us.tft` for the 320×480 panel, alongside the EU `terminal-eu.tft` and `base-eu.tft`.
- Build both images from unmodified stock 72 blank exports. The EU profile applies seven earlier EU native patches; the US profile uses the stock US native code. Both shipped images report native revision 92.
- Add `width`/`height` display configuration and make the framebuffer, dirty-tile tracker, touch bounds and protocol engine use the configured panel size.
- Require native revision 92 in the component; panel geometry remains selected by `width`/`height` and checked by the display loader.
- Add EU and US build/verify profiles and coherent TFT names.
- Run UART streaming, heartbeats and reply processing in a dedicated ESP32 task so blocking UI work does not interrupt the terminal session.
- Pass touch samples back to the ESPHome main loop before updating the touchscreen.
- Hold new UART snapshots until LVGL reports that its refresh cycle has finished.
- Remove the separate installer example. The panel firmware installs the first TFT and later updates through its `upload_tft` action.

## 0.2.0 — 2026-10-29

- Ship the hardware-tested blank V92 TFT with live brightness and a fixed 3-second host-inactivity watchdog.
- Add terminal TFT upload and brightness actions.
- Publish the native display firmware source, the V92 base TFT and a fixed-address builder that reproduces the shipped image byte-for-byte.
- Add in-depth display firmware, build and protocol documentation.
- Add a source-build verification mode and fix the shipped TFT checksum file.
- Rename the native receiver, loader and protocol sources to describe their roles rather than the brightness extension.
- Reduce streamed pixels by tracking changed 8-pixel tiles and keeping distant changes in separate rectangles.
- Add render measurement counters and timing trace hooks.
- Consolidate installation and recovery guidance.

## 0.1.0 — 2026-10-29

- Public packaging of the prebuilt custom native display firmware and the ESPHome display/touchscreen component.
- Blank-UI TFT (`terminal-eu.tft`) built from a compatible Nextion
  Editor 1.65.1 export and patched to native V92.
- Installer and graphical ESPHome examples.
- Install, compatibility, recovery, safety, protocol and troubleshooting docs.
- TFT verifier.
