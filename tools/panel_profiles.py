"""Shared EU/US NSPanel TFT profiles.

Both profiles start from unmodified blank exports that report native 72: the EU
blank Editor export and the stock US export. The EU profile applies the earlier
EU native patches (state, dim and baud handling) before setting the version; the
US profile leaves the stock US native code alone. The two bases share the same container layout, native bundle offset
and helper-0 slot, but differ in file size, visible geometry and UI resources.

``native_version`` is the package version our terminal reports (93 for both
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


PROFILES: dict[str, Profile] = {
    "eu": Profile(
        name="eu",
        base=ROOT / "firmware" / "base-eu.tft",
        base_size=264936,
        base_sha256="6314294af0822bfe1616d2f28ba4e81b15038a20b53abedd0f9aafacb64994ef",
        shipped=ROOT / "firmware" / "terminal-eu.tft",
        shipped_sha256="925b0f35109e72354816a96d9af0b07cc9f92beaf1aed3ba576a2424b3c0ae3c",
        base_native_version=72,
        native_version=93,
        panel_width=454,
        panel_height=320,
        entry_width=480,
        entry_height=320,
        entry_state=2,
        native_patches=(
            (0x2439, bytes.fromhex("e0")),
            (0xC072, bytes.fromhex("02")),
            (0xD134, bytes.fromhex("07e0")),
            (0xD158, bytes.fromhex("a920fdf7e9fef5")),
            (0xD160, bytes.fromhex("2afc00e0")),
            (0xEB0C, bytes.fromhex("80841e")),
            (0xEB10, bytes.fromhex("00093d")),
        ),
        native_image_sha256="f70971a29a93af36379f5a4c7a142ef487f2052b1885cc9d60b51d68ecb4180f",
        ui_dimensions=b"454,320",
        output=ROOT / "build" / "terminal-eu.tft",
    ),
    "us": Profile(
        name="us",
        base=ROOT / "firmware" / "base-us.tft",
        base_size=264984,
        base_sha256="f3b55142ae43acb65b22f46d99913ba0bd4eff3622f93dc62a1ae350bfe7b02c",
        shipped=ROOT / "firmware" / "terminal-us.tft",
        shipped_sha256="20ba711b65e58febac29f758ea2724a2f71b9202c028a4e45abef64ca404e73c",
        base_native_version=72,
        native_version=93,
        panel_width=320,
        panel_height=480,
        entry_width=320,
        entry_height=480,
        entry_state=1,
        native_patches=(),
        native_image_sha256="9d7072e8a4bbccc616ed973cea298d5f7b4a3196b211e848720d1bc6e9b0f205",
        ui_dimensions=b"320,480",
        output=ROOT / "build" / "terminal-us.tft",
    ),
}
