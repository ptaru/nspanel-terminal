# Display firmware


These are blank NSPanel protocol-8 TFTs with a fixed 3-second host-inactivity watchdog. The EU image uses the 454×320 landscape framebuffer; the US image is a port to the 320×480 portrait panel.

| File | Panel | Role | SHA-256 |
| --- | --- | --- | --- |
| `terminal-eu.tft` | EU | EU hardware-tested. | `1a8cd088aba9bb0f142876d0be61ddebc183809e83affc40e84bd85dcef92df3` |
| `base-eu.tft` | EU | Unmodified blank Editor export (native 72) used only as the build input. | `6314294af0822bfe1616d2f28ba4e81b15038a20b53abedd0f9aafacb64994ef` |
| `terminal-us.tft` | US | Ported, offline-validated image; reports native 94. Not yet hardware-tested. | `47df397da5e7f8f8cad9858d2ca6b971ab0cceb3e5e40a582aef0db3987966dd` |
| `base-us.tft` | US | Unmodified blank Editor export (native 72) used only as the build input. | `f3b55142ae43acb65b22f46d99913ba0bd4eff3622f93dc62a1ae350bfe7b02c` |
| `src/` | both | Native C source and source map. | — |

The EU pair is 264,936 bytes; the US pair is 264,984 bytes. Both bases are unmodified blank Editor exports with native version 72, and their decoded F130 native images are byte-identical, so the two TFTs differ only in UI resources, geometry and file size. Each shipped image replaces its base's terminal preamble and helper 0 with the terminal receiver and loader, applies the same six shared native patches, repairs the affected checksums, and reports native revision 94. The EU builder additionally applies the state-selector patch at `0xC072`, because the EU panel enters at state 2 while the US panel enters at state 1. The base is **not** the shipped image; use it only to reproduce or modify the build.

## Verify

```sh
# EU
python3 tools/verify_tft.py firmware/terminal-eu.tft
# US
python3 tools/verify_tft.py --panel us firmware/terminal-us.tft
```

The verifier checks the size, SHA-256, native version, helper bundle and all package CRCs. To verify a locally modified build, use `--allow-modified`; it still performs every structural check but skips equality with the official SHA-256. Add `--panel us` for the US profile.

The companion `.sha256` files can be checked from this directory:

```sh
shasum -a 256 -c terminal-eu.tft.sha256     # macOS
sha256sum -c terminal-eu.tft.sha256         # Linux
shasum -a 256 -c terminal-us.tft.sha256
shasum -a 256 -c base-eu.tft.sha256
shasum -a 256 -c base-us.tft.sha256
```

## Build

From the repository root:

```sh
python3 tools/build_tft.py --panel eu --check
python3 tools/build_tft.py --panel us --check
```

`--check` requires a byte-for-byte match with the profile's shipped TFT. See [building](../docs/BUILDING.md) and [firmware internals](../docs/FIRMWARE.md).

## Status

The EU image is validated on one EU NSPanel. The US image is a source port of the same receiver and applies the same six shared native patches, keeping the stock state-selector byte; its package, checksums and ARM receiver are validated offline, but it has not been installed on a US panel. The blank fallbacks contain no Sonoff UI controls. The images are experimental and have no broad compatibility or recovery guarantee.
