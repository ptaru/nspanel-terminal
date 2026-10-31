#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "stream_engine.h"

namespace esphome::nspanel_terminal {
// RGB565 flush rows are two-byte aligned, so a pixel can be loaded directly.
// The attribute keeps the pixel/byte alias well defined.
using Pixel = uint16_t __attribute__((may_alias));

// Tile bits for one flushed row segment on the DirtyRows::TILE grid, in
// absolute column bits. A tile is set when any pixel of the segment inside it
// differs. source and target point at the first compared pixel, whose absolute
// column is x_start; the caller still owns the copy, this only compares.
//
// The flush used to call std::memcmp() once per tile and DirtyRows::mark()
// once per changed tile. This compares each tile in place instead, stops at
// the first difference in a tile and sets one mask bit per changed tile, so a
// changed row needs no libc call per tile. The row-level memcmp fast path in
// draw_pixels_at() stays as the cheap unchanged-row case.
inline uint64_t scan_dirty_row(const uint8_t *source, const uint8_t *target, unsigned x_start, unsigned w) {
  const Pixel *source_pixels = reinterpret_cast<const Pixel *>(source);
  const Pixel *target_pixels = reinterpret_cast<const Pixel *>(target);
  uint64_t mask = 0;
  uint64_t bit = uint64_t(1) << (x_start / DirtyRows::TILE);
  const unsigned end = x_start + w;
  unsigned x = x_start;
  while (x < end) {
    const unsigned tile_end = std::min(end, (x / DirtyRows::TILE + 1) * DirtyRows::TILE);
    bool differs = false;
    unsigned at = x - x_start;
    while (x + 2 <= tile_end) {
      if (source_pixels[at] != target_pixels[at] || source_pixels[at + 1] != target_pixels[at + 1]) {
        differs = true;
        break;
      }
      x += 2;
      at += 2;
    }
    if (x < tile_end) {
      if (source_pixels[at] != target_pixels[at]) differs = true;
      ++x;
    }
    if (differs) mask |= bit;
    x = tile_end;
    bit <<= 1;
  }
  return mask;
}
}  // namespace esphome::nspanel_terminal
