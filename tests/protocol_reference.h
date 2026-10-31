// Frozen protocol implementation from public commit 9400d12, for byte compatibility tests.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::nspanel_terminal::protocol_reference {
enum { RECT = 1, DATA, END, PING, EXIT, SYNC, BRIGHTNESS };
enum { READY = 128, BYE, PROGRESS, ERROR, SYNCED, DONE, READY_BRIGHTNESS, BRIGHTNESS_DONE };
constexpr unsigned WINDOW = 512, PAYLOAD = 512;
inline unsigned get16(const uint8_t *p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); }
inline uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t(get16(p + 2)) << 16); }
inline void put16(std::vector<uint8_t> &p, unsigned at, unsigned n) {
  p[at] = uint8_t(n);
  p[at + 1] = uint8_t(n >> 8);
}
inline uint16_t crc(const uint8_t *p, size_t n) {
  static constexpr auto table = []() {
    std::array<uint16_t, 256> t{};
    for (unsigned i = 0; i < 256; i++) {
      unsigned c = i << 8;
      for (unsigned j = 0; j < 8; j++) c = (c << 1) ^ ((c & 32768) ? 0x1021 : 0);
      t[i] = uint16_t(c);
    }
    return t;
  }();
  uint16_t c = 65535;
  while (n--) c = uint16_t((c << 8) ^ table[(c >> 8) ^ *p++]);
  return c;
}
inline std::vector<uint8_t> cobs(const std::vector<uint8_t> &raw) {
  std::vector<uint8_t> out;
  out.reserve(raw.size() + raw.size() / 254 + 2);
  out.push_back(0);
  size_t mark = 0;
  unsigned code = 1;
  for (uint8_t b : raw) {
    if (!b) {
      out[mark] = uint8_t(code);
      mark = out.size();
      out.push_back(0);
      code = 1;
    } else {
      out.push_back(b);
      if (++code == 255) {
        out[mark] = 255;
        mark = out.size();
        out.push_back(0);
        code = 1;
      }
    }
  }
  out[mark] = uint8_t(code);
  out.push_back(0);
  return out;
}
inline std::vector<uint8_t> record(unsigned session, unsigned seq, unsigned kind, unsigned x = 0, unsigned y = 0,
                                   unsigned w = 0, unsigned h = 0, const std::vector<uint8_t> &body = {},
                                   unsigned version = 6) {
  if (body.size() > PAYLOAD) return {};
  std::vector<uint8_t> b(20);
  b[0] = 'R';
  b[1] = 'S';
  b[2] = uint8_t(version);
  b[3] = uint8_t(kind);
  put16(b, 4, session);
  put16(b, 6, seq);
  put16(b, 8, body.size());
  put16(b, 10, x);
  put16(b, 12, y);
  put16(b, 14, w);
  put16(b, 16, h);
  put16(b, 18, crc(b.data(), 18));
  b.insert(b.end(), body.begin(), body.end());
  auto c = crc(b.data(), b.size());
  b.push_back(uint8_t(c));
  b.push_back(uint8_t(c >> 8));
  return cobs(b);
}
/* A sequential source is read exactly once per pixel. Two-pixel lookahead
 * survives record boundaries; output buffers are reused by the caller. */
template <class Pixel>
class Encoder {
  Pixel pixel;
  unsigned total, generated = 0, used = 0, have = 0;
  uint16_t look[2]{};
  void fill() {
    while (have < 2 && generated < total) look[have++] = uint16_t(pixel(generated++));
  }
  uint16_t take() {
    auto v = look[0];
    if (have == 2) look[0] = look[1];
    --have;
    ++used;
    fill();
    return v;
  }

 public:
  Encoder(unsigned count, Pixel source) : pixel(source), total(count) { fill(); }
  unsigned consumed() const { return used; }
  bool next(std::vector<uint8_t> &out) {
    // Keep hot counters local: a source callback may alias encoder members,
    // forcing field reloads/stores per pixel on the ESP32 size optimiser.
    unsigned g = generated, u = used, n = have, limit = total;
    uint16_t a = look[0], b = look[1];
    auto refill = [&]() {
      while (n < 2 && g < limit) {
        auto v = uint16_t(pixel(g++));
        if (n == 0)
          a = v;
        else
          b = v;
        ++n;
      }
    };
    auto pop = [&]() {
      auto v = a;
      a = b;
      --n;
      ++u;
      refill();
      return v;
    };
    out.clear();
    out.reserve(PAYLOAD);
    while (u < limit && out.size() + 3 <= PAYLOAD) {
      if (n == 2 && a == b) {
        uint16_t v = a;
        unsigned run = 2;
        u += 2;
        n = 0;
        while (run < 128 && g < limit) {
          auto candidate = uint16_t(pixel(g++));
          if (candidate != v) {
            a = candidate;
            n = 1;
            break;
          }
          ++run;
          ++u;
        }
        refill();
        out.push_back(uint8_t(127 + run));
        out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v));
      } else {
        unsigned room = std::min(128u, unsigned((PAYLOAD - out.size() - 1) / 2));
        size_t mark = out.size();
        out.push_back(0);
        unsigned run = 0;
        do {
          auto v = pop();
          out.push_back(uint8_t(v >> 8));
          out.push_back(uint8_t(v));
          ++run;
        } while (run < room && n && !(n == 2 && a == b));
        out[mark] = uint8_t(run - 1);
      }
    }
    generated = g;
    used = u;
    have = n;
    look[0] = a;
    look[1] = b;
    return !out.empty();
  }
};
}  // namespace esphome::nspanel_terminal::protocol_reference
