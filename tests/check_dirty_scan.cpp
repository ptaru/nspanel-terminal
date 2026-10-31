#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../components/nspanel_terminal/dirty_scan.h"
using namespace esphome::nspanel_terminal;

// The pre-existing per-tile comparison, kept as the behavioural reference.
// source and target point at the segment start, as in draw_pixels_at().
static uint64_t reference_mask(const uint8_t *source, const uint8_t *target, unsigned x_start, unsigned w) {
  DirtyRows dirty;
  dirty.set_dimensions(454, 1);
  for (unsigned col = x_start / DirtyRows::TILE; col <= (x_start + w - 1) / DirtyRows::TILE; ++col) {
    const unsigned first = std::max(x_start, col * DirtyRows::TILE) - x_start;
    const unsigned last = std::min(x_start + w, (col + 1) * DirtyRows::TILE) - x_start;
    if (std::memcmp(source + 2 * first, target + 2 * first, size_t(last - first) * 2) != 0)
      dirty.mark(x_start + first, 0, last - first);
  }
  return dirty.rows[0];
}

int main() {
  const unsigned width = 454;
  std::vector<uint8_t> source(2 * width), target(2 * width);
  source.assign(source.size(), 0);
  target.assign(target.size(), 0);
  uint32_t rng = 0x12345678;
  auto next = [&]() {
    rng = rng * 1664525u + 1013904223u;
    return rng;
  };
  // Deterministic edge cases, including starts and widths that do not sit on
  // an 8-pixel boundary and the short final column of the EU panel.
  for (unsigned trial = 0; trial < 2000; ++trial) {
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(next());
    target = source;
    const unsigned w = 1 + next() % width;
    const unsigned x_start = next() % (width - w + 1);
    const unsigned flips = next() % 24;
    for (unsigned i = 0; i < flips; ++i) {
      const unsigned x = x_start + next() % w;
      target[2 * x] ^= uint8_t(1u << (next() % 8));
    }
    // Differences outside the segment must not add bits.
    if (x_start > 0) target[2 * (x_start - 1)] ^= 1;
    if (x_start + w < width) target[2 * (x_start + w)] ^= 1;
    const auto saved = target;
    const uint8_t *segment_source = source.data() + 2 * x_start;
    const uint8_t *segment_target = target.data() + 2 * x_start;
    const uint64_t expected = reference_mask(segment_source, segment_target, x_start, w);
    const uint64_t actual = scan_dirty_row(segment_source, segment_target, x_start, w);
    assert(actual == expected);
    assert(target == saved);  // the scan must not copy or otherwise write
    assert((actual & ~((uint64_t(1) << ((width + 7) / 8)) - 1)) == 0);
  }
  // Single pixels at tile boundaries set exactly one tile each.
  target = source;
  target[2 * 7] ^= 1;
  target[2 * 8] ^= 1;
  assert(scan_dirty_row(source.data(), target.data(), 0, 16) == (uint64_t(3) << 0));
  target = source;
  target[2 * 8] ^= 1;
  assert(scan_dirty_row(source.data(), target.data(), 0, 16) == (uint64_t(1) << 1));
  target = source;
  target[2 * 453] ^= 1;
  assert(scan_dirty_row(source.data(), target.data(), 0, 454) == (uint64_t(1) << 56));
  assert(scan_dirty_row(source.data(), source.data(), 0, 454) == 0);
  // A changed pixel outside the segment is not part of its mask.
  target = source;
  target[2 * 100] ^= 1;
  assert(scan_dirty_row(source.data(), target.data(), 0, 100) == 0);
  assert(scan_dirty_row(source.data() + 2 * 101, target.data() + 2 * 101, 101, 10) == 0);
  // A single-pixel segment cannot reach a neighbouring tile.
  for (unsigned x = 0; x < width; ++x) {
    target = source;
    target[2 * x] ^= 1;
    assert(scan_dirty_row(source.data() + 2 * x, target.data() + 2 * x, x, 1) ==
           (uint64_t(1) << (x / DirtyRows::TILE)));
  }
  puts("dirty scan: 2000 random segments and tile-boundary cases match the per-tile comparison");
}
