"""Shared EU/US NSPanel TFT profiles.

Both profiles start from unmodified blank exports that report native 72: the EU
blank Editor export and the stock US export. The EU profile applies the earlier
EU native patches (state, dim and baud handling) before setting the version; the
US profile leaves the stock US native code alone. The two bases share the same container layout, native bundle offset
and helper-0 slot, but differ in file size, visible geometry and UI resources.

``native_version`` is the package version our terminal reports (94 for both
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
        shipped_sha256="1a8cd088aba9bb0f142876d0be61ddebc183809e83affc40e84bd85dcef92df3",
        base_native_version=72,
        native_version=94,
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
        native_image_sha256="ba28093151d47aab5568fc1cec8e58b000b2c2b091bb9fe2fbcd17150fa97b06",
        ui_dimensions=b"454,320",
        output=ROOT / "build" / "terminal-eu.tft",
    ),
    "us": Profile(
        name="us",
        base=ROOT / "firmware" / "base-us.tft",
        base_size=264984,
        base_sha256="f3b55142ae43acb65b22f46d99913ba0bd4eff3622f93dc62a1ae350bfe7b02c",
        shipped=ROOT / "firmware" / "terminal-us.tft",
        shipped_sha256="98b2a5f6478fa438b3d6e21acb9883e1d30efd7d1a85ae8f0cc0390c2a441c3d",
        base_native_version=72,
        native_version=94,
        panel_width=320,
        panel_height=480,
        entry_width=320,
        entry_height=480,
        entry_state=1,
        native_patches=(),
        native_image_sha256="b934d529a0ad3da8097e2da0fc28ff58419af51842d28cb1832199bec893db33",
        ui_dimensions=b"320,480",
        output=ROOT / "build" / "terminal-us.tft",
    ),
}
