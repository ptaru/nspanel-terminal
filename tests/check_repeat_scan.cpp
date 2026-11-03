// Offline prototype check; see ENCODER-NEXT-STEPS.md for setup/commands.
#include <cassert>
#include <cstdio>
#include "../components/nspanel_terminal/stream_engine.h"
#include "protocol_reference.h"
using namespace esphome::nspanel_terminal;
int main() {
  uint32_t random = 12345;
  unsigned cases = 0;
  std::vector<unsigned> lengths;
  for (unsigned i = 0; i <= 400; ++i) lengths.push_back(i);
  for (unsigned i : {5120u, 7264u, 145280u}) lengths.push_back(i);
  for (unsigned offset = 0; offset < 4; ++offset) {
    for (unsigned count : lengths) {
      for (unsigned pattern = 0; pattern < 10; ++pattern) {
        std::vector<uint8_t> input(offset + 2 * count);
        auto *p = input.data();
        if (offset) p += offset;
        for (unsigned i = 0; i < count; ++i) {
          random = random * 1664525u + 1013904223u;
          const uint16_t value = pattern == 0 ? 0x1234 : pattern == 1 ? i :
              pattern == 2 ? uint16_t(random >> 16) :
              pattern == 3 ? (i / 127) % 2 : pattern == 4 ? (i / 128) % 2 :
              pattern == 5 ? (i / 129) % 2 : pattern == 6 ? (i % 19 ? 0xffff : i) :
              pattern == 7 ? ((random & 31) ? 0x4321 : uint16_t(random >> 16)) :
              pattern == 8 ? (i / 2) % 7 : (i % 257) / 2;
          p[2 * i] = uint8_t(value); p[2 * i + 1] = uint8_t(value >> 8);
        }
        protocol::Encoder<StreamEngine::Pixels> fast(count, {p});
        protocol_reference::Encoder<StreamEngine::Pixels> ref(count, {p});
        std::vector<uint8_t> a, b;
        for (;;) {
          const bool more = fast.next(a);
          assert(more == ref.next(b));
          assert(a == b && fast.consumed() == ref.consumed());
          if (!more) break;
        }
        ++cases;
      }
    }
  }
  // A solid raw strip is all encoded by a single DATA prepare/next call.
  std::vector<uint8_t> solid(454 * 16 * 2, 0);
  protocol::Encoder<StreamEngine::Pixels> encoder(454 * 16, {solid.data()});
  std::vector<uint8_t> body;
  assert(encoder.next(body));
  assert(encoder.consumed() == 454 * 16 && body.size() == 171);
  assert(!encoder.next(body));
  std::printf("%u alignment/tail/run cases match frozen reference; solid strip = one 171-byte body\n", cases);
}
