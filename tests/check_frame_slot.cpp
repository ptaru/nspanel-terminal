#include <cassert>
#include "../components/nspanel_terminal/frame_slot.h"
using namespace esphome::nspanel_terminal;
int main() {
  FrameSlot encoding, queued, active;
  uint64_t generation = 0;
  assert(encoding.reserve());
  assert(encoding.encoding());
  const auto original = generation;
  queued.state = active.state = 2;
  // A failure while encoding clears the queue, but preserves writer ownership.
  ++generation;
  encoding.cancel(); queued.cancel(); active.cancel();
  assert(!encoding.reserve());
  assert(queued.reserve() && active.reserve());
  // Even after re-entry, stale output cannot be published into the new session.
  ++generation;
  encoding.cancel();
  assert(!encoding.finish_encoding(original, generation));
  assert(encoding.reserve());
  assert(encoding.finish_encoding(generation, generation));
  encoding.state = 2;
  assert(!encoding.encoding());
  encoding.cancel();
  assert(encoding.reserve());
  // Cancellation is safe while idle and for multiple outstanding writers.
  queued.cancel(); active.cancel();
  assert(!queued.reserve() && !active.reserve());
  assert(!queued.finish_encoding(original, generation));
  assert(!active.finish_encoding(original, generation));
  assert(queued.reserve() && active.reserve());
}
