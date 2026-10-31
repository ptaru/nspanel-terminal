# Examples

Copy `secrets.yaml.example` to `secrets.yaml` and set your network credentials
and TFT URL. Both examples load the component from the tagged GitHub release
(`github://ptaru/nspanel-terminal@v0.5.0`), so ESPHome clones it on first use
and no local checkout is needed. They perform the first TFT installation and
later updates, and expose the brightness actions.

| File | Panel | Geometry |
| --- | --- | --- |
| `panel.yaml` | EU landscape | 454×320 (defaults) |
| `panel-us.yaml` | US portrait | 320×480 |

The US TFT and configuration are offline-validated but have not yet been run on
US hardware. Follow the [installation steps](../README.md#install) and review the
pin and PSRAM configuration before flashing.

## Working on the component

To test local edits instead of the release, replace the `external_components`
source with a path to your checkout:

```yaml
external_components:
  - source:
      type: local
      path: /path/to/nspanel-terminal/components
    components: [nspanel_terminal]
```

With the component from the release, `upload_tft` still fetches the TFT from
`tft_url`, which must be an HTTP server that supports content length and byte
ranges. Serve `firmware/terminal-eu.tft` or `firmware/terminal-us.tft` from the
tagged checkout, or copy it to your own server.
