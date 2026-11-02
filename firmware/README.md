# Display firmware


These are blank NSPanel protocol-8 TFTs with live brightness and a fixed
3-second host-inactivity watchdog. The EU image uses the 454×320 landscape
framebuffer; the US image is a port to the 320×480 portrait panel.

| File | Panel | Role | SHA-256 |
| --- | --- | --- | --- |
| `terminal-eu.tft` | EU | Development candidate; EU upload/redraw smoke-tested. | `1a8cd088aba9bb0f142876d0be61ddebc183809e83affc40e84bd85dcef92df3` |
| `base-eu.tft` | EU | Unmodified blank Editor export (native 72) used only as the build input. | `6314294af0822bfe1616d2f28ba4e81b15038a20b53abedd0f9aafacb64994ef` |
| `terminal-us.tft` | US | Ported, offline-validated image; reports native 94. Not yet hardware-tested. | `98b2a5f6478fa438b3d6e21acb9883e1d30efd7d1a85ae8f0cc0390c2a441c3d` |
| `base-us.tft` | US | Stock US TFT (native 72) used only as the build input. | `f3b55142ae43acb65b22f46d99913ba0bd4eff3622f93dc62a1ae350bfe7b02c` |
| `src/` | both | Native C source and source map. | — |

The EU pair is 264,936 bytes; the US pair is 264,984 bytes. Both bases are
unmodified 72 blank exports: the EU blank Editor export and the stock US
export. Each shipped image replaces its base's
terminal preamble and helper 0 with the live-brightness receiver and loader,
repairs the affected checksums, and reports native revision 94. The EU builder
also applies EU native patches (state, dim and baud handling) and
updates the version metadata. The base is **not** the shipped image; use it only
to reproduce or modify the build.

## Verify

```sh
# EU
python3 tools/verify_tft.py firmware/terminal-eu.tft
# US
python3 tools/verify_tft.py --panel us firmware/terminal-us.tft
```

The verifier checks the size, SHA-256, native version, helper bundle and all
package CRCs. To verify a locally modified build, use `--allow-modified`; it
still performs every structural check but skips equality with the official
SHA-256. Add `--panel us` for the US profile.

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

`--check` requires a byte-for-byte match with the profile's shipped TFT. See
[building](../docs/BUILDING.md) and [firmware internals](../docs/FIRMWARE.md).

## Status

The EU image is validated on one EU NSPanel. The US image is a source port of the same
receiver; its package, checksums and ARM receiver are validated offline, but it
has not been installed on a US panel. The blank fallbacks contain no Sonoff UI
controls. The images are experimental and have no broad compatibility or
recovery guarantee.
