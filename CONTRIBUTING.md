# Contributing

This repository packages an experimental EU/US NSPanel display terminal: the
ESPHome component, the native display receiver and the TFT builds. Read
[display firmware internals](docs/FIRMWARE.md), [building](docs/BUILDING.md)
and [protocol](docs/PROTOCOL.md) before changing the display side.

The shipped TFTs are known-good artifacts, and the EU image is hardware-tested.
Source changes are welcome, but they must not silently change a package layout
or the protocol contract.

## Build and check

From the repository root:

```sh
make verify
```

This rebuilds both TFTs from their bases and requires a byte-for-byte match
with the shipped images, then runs the structural verifiers. If you
intentionally changed the source, `make check` will fail (as designed); use:

```sh
python3 tools/build_tft.py --panel eu
python3 tools/verify_tft.py --allow-modified build/terminal-eu.tft
python3 tools/build_tft.py --panel us
python3 tools/verify_tft.py --panel us --allow-modified build/terminal-us.tft
```

Do not commit `build/`, object files or generated headers.

## Source rules

- The receiver has 3,568 bytes at `0x20000900`; the loader has 844 bytes at
  `0x200016F0`. The builder fails if either limit is exceeded.
- No heap, libc, writable static state or floating point. The overlay is
  freestanding and restores the stock runtime state.
- Fixed stock addresses and RAM ownership are documented in the source and in
  [FIRMWARE.md](docs/FIRMWARE.md). Do not move the pool boundary or reuse the
  helper slot without a new memory audit.
- The builder may replace only the 4 KiB preamble and helper 0. Preserve the
  native F130 image, page/font/picture tables, helpers 1–8 and the updater.
- Panel geometry belongs in `firmware/src/panel_profile.h` and
  `tools/panel_profiles.py`; do not reintroduce fixed 454×320 constants.
- A new record kind, status or field meaning is a protocol change. Update the
  ESPHome component and the TFT together; do not ship a TFT the host cannot
  drive.
- Keep `docs/PROTOCOL.md` and the source comments in sync with the code.

## Hardware changes

The EU image is validated on one EU NSPanel. The US image and any modified
build are not hardware-validated just because they compile. Before uploading:

- verify the file and its checksum;
- keep the previous TFT and a recovery path;
- check the target URL and installed state;
- do not retry a failed transfer blindly.

See [recovery](docs/RECOVERY.md). The panel is mains-powered; follow the safety
guidance and do not open it unless qualified.

## Licensing

Repository code and the native source are MIT licensed (see [LICENSE](LICENSE)).
The TFT contains the Nextion runtime; see [NOTICE](NOTICE). Do not add vendor
source, personal UI content or credentials to the repository.
