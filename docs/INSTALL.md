# Install

This is a two-image process. The installer image flashes the prebuilt display
firmware once; the panel image then owns the display. Keep both images and a way
to recover the ESP32.

## 0. Before you start

- This guide is for the **EU version** of the Sonoff NSPanel only. The US
  version is oriented differently; stop if
  you have one. NSPanel Pro is not supported.
- Back up your ESP32 configuration and, if possible, the panel's original TFT.
- Read [safety](SAFETY.md) and [recovery](RECOVERY.md).
- Use a stable power supply. Do not upload during a power cut or while the panel
  is likely to reboot.

## 1. Install ESPHome

The public examples are tested with ESPHome **2026.10.0**:

```sh
python3.12 -m venv .venv
.venv/bin/pip install 'esphome==2026.10.0'
```

Use the `esphome` command from that environment.

## 2. Prepare the examples

From the `public/` directory:

```sh
cd examples
cp secrets.yaml.example secrets.yaml
```

Edit `secrets.yaml` with your Wi-Fi, OTA and AP passwords. Leave `tft_url` until
the next step.

`panel.yaml` points at the `v0.1.0` release tag, which matches the included TFT.
If you are editing a local clone and want to test uncommitted changes, replace
the `external_components` block with:

```yaml
external_components:
  - source:
      type: local
      path: ../components
    components: [nspanel_terminal]
```

## 3. Serve the TFT

The ESP32 must be able to fetch the TFT over HTTP. A local server is simplest:

```sh
cd ../firmware
python3 -m http.server 8000
```

Set `tft_url` in `examples/secrets.yaml` to:

```text
http://<your-computer-ip>:8000/nspanel-v92-blank.tft
```

The server must support HTTP Range requests. GitHub release assets generally do;
a plain static file server does. Verify the file first:

```sh
cd ..
python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
```

Expected SHA-256:
`82434ab839ed14af45c655bdf1980848649c6a48081ee593c5cce397df7f3628`.

## 4. Flash the installer

```sh
cd examples
esphome run installer.yaml
```

Use USB serial for the first flash, or OTA if the existing firmware is
reachable. The installer is a normal ESPHome device with `upload_tft` exposed.

## 5. Upload the TFT

From Home Assistant, the ESPHome API, or the ESPHome dashboard, call the
`upload_tft` action. The display will show its update progress; the transfer is
normally under a minute for the blank image, but allow several minutes on a slow
server.

**Do not remove power during the upload.** If it fails, read
[recovery](RECOVERY.md) before retrying.

When it finishes, the display reboots into the blank "waiting for connection"
page. A `connect` query should now report version **92**.

If it still reports version 72, the TFT upload did not take. If it reports 128,
the wrong TFT was served.

## 6. Flash the panel firmware

```sh
esphome run panel.yaml
```

The component starts the terminal at boot. You should see the example LVGL
screen. Touch the button; the label should change.

## 7. Verify exit and re-entry

Call `exit_terminal`. The display should return to the blank Nextion page.
Call `enter_terminal`; the LVGL screen should return.

If the ESP32 stops sending data, the display's watchdog returns to the blank
page after roughly 300 ms. A display that stays black after an ESP32 crash is a
recovery case; see [recovery](RECOVERY.md).

## Updating later

- ESP32-only changes: OTA the panel image. The TFT does not need reflashing.
- TFT changes: exit the terminal, flash the installer, upload the new TFT, then
  flash the panel image again. Do not run the Nextion and nspanel_terminal
  components in the same image.
