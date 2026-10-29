# Contributing

This repository packages an experimental EU NSPanel display terminal: the
ESPHome component, the native display receiver and the TFT build. Read
[display firmware internals](docs/FIRMWARE.md), [building](docs/BUILDING.md)
and [protocol](docs/PROTOCOL.md) before changing the display side.

The shipped TFT is a known-good artifact. Source changes are welcome, but they
must not silently change its package layout or the protocol contract.

## Build and check

From the repository root:

```sh
make verify
```

This rebuilds the TFT from the retained base and requires a byte-for-byte match
with `firmware/nspanel-v92-blank.tft`, then runs the structural verifier. If you
intentionally changed the source, `make check` will fail (as designed); use:

```sh
python3 tools/build_tft.py
python3 tools/verify_tft.py --allow-modified build/nspanel-v92-blank.tft
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
- A new record kind, status or field meaning is a protocol change. Update the
  ESPHome component and the TFT together; do not ship a TFT an older host
  cannot drive.
- Keep `docs/PROTOCOL.md` and the source comments in sync with the code.

## Hardware changes

The image was tested on one EU NSPanel. A build is not hardware-validated just
because it compiles. Before uploading:

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
