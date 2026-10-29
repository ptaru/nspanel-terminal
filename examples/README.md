# Examples

Copy `secrets.yaml.example` to `secrets.yaml` and set your network credentials
and TFT URL. Both examples use the component from `../components`, perform the
first TFT installation and later updates, and expose brightness actions.

| File | Panel | Geometry |
| --- | --- | --- |
| `panel.yaml` | EU landscape | 454×320 (defaults) |
| `panel-us.yaml` | US portrait | 320×480 |

The US TFT and configuration are offline-validated but have not yet been run on
US hardware. Follow the [installation steps](../README.md#install) and review the
pin and PSRAM configuration before flashing.
