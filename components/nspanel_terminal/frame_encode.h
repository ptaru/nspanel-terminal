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
// Rows form one pixel sequence, so DATA records and runs can cross row
// boundaries. Returns the encoded size, or 0 if the cache would overflow.
inline size_t encode_rows(const uint8_t *source, size_t stride, int x_offset, unsigned w, unsigned rows,
                          uint8_t *cache, size_t capacity) {
  auto encode = [&](auto &encoder) -> size_t {
    size_t size = 0;
    std::vector<uint8_t> body;
    body.reserve(protocol::PAYLOAD);
    while (encoder.next(body)) {
      if (size + 2 + body.size() > capacity) return 0;
      cache[size++] = uint8_t(body.size());
      cache[size++] = uint8_t(body.size() >> 8);
      std::memcpy(cache + size, body.data(), body.size());
      size += body.size();
    }
    return size;
  };
  if (stride == 2 * size_t(w)) {
    protocol::Encoder<StreamEngine::Pixels> encoder(w * rows, {source + 2 * size_t(x_offset)});
    return encode(encoder);
  }
  struct StridedPixels {
    const uint8_t *cursor;
    size_t stride;
    unsigned width;
    unsigned column = 0;
    uint16_t operator()(unsigned) {
      if (column == width) {
        column = 0;
        cursor += stride - 2 * size_t(width);
      }
      const uint16_t value = uint16_t(cursor[0]) | (uint16_t(cursor[1]) << 8);
      cursor += 2;
      ++column;
      return value;
    }
  };
  protocol::Encoder<StridedPixels> encoder(w * rows, {source + 2 * size_t(x_offset), stride, w});
  return encode(encoder);
}
}  // namespace esphome::nspanel_terminal
