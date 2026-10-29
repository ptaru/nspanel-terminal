# Display firmware

The display firmware is the blank EU NSPanel V92/protocol 6 TFT with live
brightness and a fixed 3-second host-inactivity watchdog.

| File | Role | SHA-256 |
| --- | --- | --- |
| `nspanel-v92-blank.tft` | Shipped, hardware-tested image. Install this. | `a25aafc263b8980e1f91e9bfe6f363d2197c657bb5a2231f86d61a9828a3d10c` |
| `nspanel-v92-blank-base.tft` | V92 base used only as the build input. | `82434ab839ed14af45c655bdf1980848649c6a48081ee593c5cce397df7f3628` |
| `src/` | Native C source and source map. | — |

Both TFTs are 264,936 bytes and contain the same blank UI and native F130
image. The shipped image replaces the base's terminal preamble and helper 0
with the live-brightness receiver and loader, and repairs the affected
checksums. The base is **not** the shipped image; use it only to reproduce or
modify the build.

## Verify

```sh
python3 tools/verify_tft.py firmware/nspanel-v92-blank.tft
```

The verifier checks the size, SHA-256, native version, helper bundle and all
package CRCs. To verify a locally modified build, use
`--allow-modified`; it still performs every structural check but skips equality
with the official SHA-256.

The companion `.sha256` files can be checked from this directory:

```sh
shasum -a 256 -c nspanel-v92-blank.tft.sha256     # macOS
sha256sum -c nspanel-v92-blank.tft.sha256         # Linux
shasum -a 256 -c nspanel-v92-blank-base.tft.sha256
sha256sum -c nspanel-v92-blank-base.tft.sha256
```

## Build

From the repository root:

```sh
python3 tools/build_tft.py --check
```

`--check` requires a byte-for-byte match with `nspanel-v92-blank.tft`. See
[building](../docs/BUILDING.md) and [firmware internals](../docs/FIRMWARE.md).

## Provenance and status

The blank UI was exported with Nextion Editor 1.65.1. The patched terminal and
live-brightness extension were tested on one EU NSPanel; the display
acknowledged live 50% and 100% changes, and the user confirmed the change was
visible. The blank fallback contains no original Sonoff UI controls. The image
is experimental and has no broad compatibility or recovery guarantee.
