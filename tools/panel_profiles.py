"""Shared EU/US NSPanel TFT profiles.

Both profiles start from unmodified blank exports that report native 72: the EU
blank Editor export and the stock US export. Their container layouts, native
bundle offsets and helper-0 slots are the same, and their decoded F130 native
images are byte-identical, so the two TFTs differ only in file size, visible
geometry and UI resources. Both panels report stock entry state 2, so both
profiles apply the same seven native patches and their shipped native images
are byte-identical.

``native_version`` is the package version our terminal reports (95 for both
panels); ``base_native_version`` is what the base image reports before the
builder sets it. ``native_patches`` are decoded native byte patches applied
before the version metadata is rewritten. The stock updater treats the version
as package identity, so any changed package must be bumped rather than reused.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class Profile:
    name: str
    base: Path
    base_size: int
    base_sha256: str
    shipped: Path
    shipped_sha256: str
    base_native_version: int
    native_version: int
    panel_width: int
    panel_height: int
    entry_width: int
    entry_height: int
    entry_state: int
    native_patches: tuple[tuple[int, bytes], ...]
    native_image_sha256: str
    ui_dimensions: bytes
    output: Path

    @property
    def defines(self) -> list[str]:
        return [
            f"-DNSPANEL_PANEL_WIDTH={self.panel_width}",
            f"-DNSPANEL_PANEL_HEIGHT={self.panel_height}",
            f"-DNSPANEL_ENTRY_WIDTH={self.entry_width}",
            f"-DNSPANEL_ENTRY_HEIGHT={self.entry_height}",
            f"-DNSPANEL_ENTRY_STATE={self.entry_state}",
        ]

    @property
    def dimensions(self) -> str:
        return f"{self.panel_width}x{self.panel_height}"


# Both panels report stock entry state 2, so the same seven decoded patches are
# applied to either base. Keep this in one place so the images cannot diverge.
NATIVE_PATCHES = (
    (0x2439, bytes.fromhex("e0")),
    (0xC072, bytes.fromhex("02")),
    (0xD134, bytes.fromhex("07e0")),
    (0xD158, bytes.fromhex("a920fdf7e9fef5")),
    (0xD160, bytes.fromhex("2afc00e0")),
    (0xEB0C, bytes.fromhex("80841e")),
    (0xEB10, bytes.fromhex("00093d")),
)


PROFILES: dict[str, Profile] = {
    "eu": Profile(
        name="eu",
        base=ROOT / "firmware" / "base-eu.tft",
        base_size=264936,
        base_sha256="6314294af0822bfe1616d2f28ba4e81b15038a20b53abedd0f9aafacb64994ef",
        shipped=ROOT / "firmware" / "terminal-eu.tft",
        shipped_sha256="95c4b8319a01b8a76f14f695b02f7921bde21aa1f8b7d2bd3a43ef89336e56e8",
        base_native_version=72,
        native_version=95,
        panel_width=454,
        panel_height=320,
        entry_width=480,
        entry_height=320,
        entry_state=2,
        native_patches=NATIVE_PATCHES,
        native_image_sha256="649d0beb9bae192697293f20e4afdd244d1ef491155837aa60af2d05d1de5fb9",
        ui_dimensions=b"454,320",
        output=ROOT / "build" / "terminal-eu.tft",
    ),
    "us": Profile(
        name="us",
        base=ROOT / "firmware" / "base-us.tft",
        base_size=264984,
        base_sha256="f3b55142ae43acb65b22f46d99913ba0bd4eff3622f93dc62a1ae350bfe7b02c",
        shipped=ROOT / "firmware" / "terminal-us.tft",
        shipped_sha256="85fae671a931b08bf465cbddb7373ecbd81919a9bd8cdd4911b1aefeffbf0ba8",
        base_native_version=72,
        native_version=95,
        panel_width=320,
        panel_height=480,
        entry_width=320,
        entry_height=480,
        entry_state=2,
        native_patches=NATIVE_PATCHES,
        native_image_sha256="649d0beb9bae192697293f20e4afdd244d1ef491155837aa60af2d05d1de5fb9",
        ui_dimensions=b"320,480",
        output=ROOT / "build" / "terminal-us.tft",
    ),
}
