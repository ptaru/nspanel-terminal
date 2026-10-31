#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../components/nspanel_terminal/frame_encode.h"
using namespace esphome::nspanel_terminal;

// Reference decoder for the length-prefixed PackBits cache format. The
// receiver treats the concatenated chunks as one pixel sequence.
static std::vector<uint16_t> decode(const std::vector<uint8_t> &cache, unsigned pixels) {
  std::vector<uint16_t> out;
  out.reserve(pixels);
  size_t at = 0;
  while (at < cache.size()) {
    assert(at + 2 <= cache.size());
    const unsigned length = unsigned(cache[at]) | (unsigned(cache[at + 1]) << 8);
    at += 2;
    assert(length && length <= 512 && at + length <= cache.size());
    size_t end = at + length;
    while (at < end) {
      const uint8_t control = cache[at++];
      if (control < 128) {
        const unsigned run = control + 1;
        assert(at + 2 * run <= end);
        for (unsigned i = 0; i < run; ++i) {
          out.push_back(uint16_t(cache[at] << 8 | cache[at + 1]));
          at += 2;
        }
      } else {
        const unsigned run = control - 127;
        assert(at + 2 <= end);
        const uint16_t value = uint16_t(cache[at] << 8 | cache[at + 1]);
        at += 2;
        for (unsigned i = 0; i < run; ++i) out.push_back(value);
      }
    }
  }
  return out;
}

int main() {
  const unsigned width = 454, rows = 16, stride = (width + 6) * 2;
  const unsigned x_offset = 3;
  const size_t capacity = width * rows * 2 + 4096;
  std::vector<uint8_t> source(stride * rows + 2 * x_offset);
  std::vector<uint8_t> cache(capacity);
  uint32_t random = 12345;
  for (unsigned pattern = 0; pattern < 5; ++pattern) {
    for (unsigned y = 0; y < rows; ++y) {
      for (unsigned x = 0; x < width; ++x) {
        random = random * 1664525u + 1013904223u;
        uint16_t value;
        switch (pattern) {
          case 0: value = 0; break;
          case 1: value = uint16_t(x * 37 + y * 11); break;
          case 2: value = (x / 32) % 2 ? 0xffff : 0; break;
          case 3: value = uint16_t(random >> 16); break;
          default: value = uint16_t((x + y) < 200 ? 0x1234 : random >> 16); break;
        }
        const size_t at = 2 * x_offset + y * stride + 2 * x;
        source[at] = uint8_t(value);
        source[at + 1] = uint8_t(value >> 8);
      }
    }
    const size_t size = encode_rows(source.data(), stride, x_offset, width, rows, cache.data(), capacity);
    assert(size > 0 && size <= capacity);
    const auto decoded = decode({cache.begin(), cache.begin() + size}, width * rows);
    assert(decoded.size() == width * rows);
    for (unsigned i = 0; i < decoded.size(); ++i) {
      const size_t at = 2 * x_offset + (i / width) * stride + 2 * (i % width);
      const uint16_t expected = uint16_t(source[at] | (source[at + 1] << 8));
      assert(decoded[i] == expected);
    }
  }
  // A cache one byte short must be rejected instead of overrunning.
  const size_t full = encode_rows(source.data(), stride, x_offset, width, rows, cache.data(), capacity);
  assert(full > 0);
  std::vector<uint8_t> tight(full - 1);
  assert(encode_rows(source.data(), stride, x_offset, width, rows, tight.data(), tight.size()) == 0);
  puts("frame encode: strided rows round-trip through the cached PackBits format");
}
