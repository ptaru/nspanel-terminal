#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "protocol.h"
#include "stream_engine.h"

namespace esphome::nspanel_terminal {
// Encode `rows` rows of `w` RGB565 pixels from a strided source into the
// length-prefixed PackBits cache used by StreamEngine::submit_cached.
// Each row is encoded separately; the receiver concatenates the chunks and
// still sees the rectangle's w*h pixels. Returns the encoded size, or 0 if the
// cache would overflow.
inline size_t encode_rows(const uint8_t *source, size_t stride, int x_offset, unsigned w, unsigned rows,
                          uint8_t *cache, size_t capacity) {
  size_t size = 0;
  std::vector<uint8_t> body;
  body.reserve(protocol::PAYLOAD);
  for (unsigned row = 0; row < rows; ++row) {
    protocol::Encoder<StreamEngine::Pixels> encoder(w, {source + row * stride + 2 * size_t(x_offset)});
    while (encoder.next(body)) {
      if (size + 2 + body.size() > capacity) return 0;
      cache[size++] = uint8_t(body.size());
      cache[size++] = uint8_t(body.size() >> 8);
      std::memcpy(cache + size, body.data(), body.size());
      size += body.size();
    }
  }
  return size;
}
}  // namespace esphome::nspanel_terminal
