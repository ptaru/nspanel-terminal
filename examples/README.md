# Examples

These examples are for the **EU version** of the Sonoff NSPanel. The US version
is oriented differently.

There are two separate ESP32 images:

1. **`installer.yaml`** — uses ESPHome's standard `nextion` component to
   download and flash the TFT once. It exposes `upload_tft`.
2. **`panel.yaml`** — uses `nspanel_terminal` for the display and touchscreen.
   It must not contain the `nextion` component.

Create a private `secrets.yaml` in this directory:

```sh
cp secrets.yaml.example secrets.yaml
```

The example points at the published release tag:

```yaml
external_components:
  - source: github://ptaru/nspanel-terminal@v0.1.0
    components: [nspanel_terminal]
```

The tag matches the included V92 TFT. For local development against a checkout,
replace the source with `type: local` and `path: ../components`. ESPHome caches
the clone for a day; use `refresh: always` while iterating.

The suggested flow is: flash installer → call `upload_tft` → flash panel. See
[the install guide](../docs/INSTALL.md).
