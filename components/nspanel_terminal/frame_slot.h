#pragma once
#include <cstdint>

namespace esphome::nspanel_terminal {
// Access under the transport mutex. Cancellation invalidates publication but
// must not recycle storage while an encoder is still writing outside the lock.
struct FrameSlot {
  uint8_t state = 0;  // 0 free, 1 encoding, 2 queued/active
  bool encoding() const { return state == 1; }
  bool reserve() {
    if (state != 0) return false;
    state = 1;
    return true;
  }
  void cancel() {
    if (state != 1) state = 0;
  }
  bool finish_encoding(uint64_t started_generation, uint64_t current_generation) {
    if (started_generation == current_generation) return true;
    state = 0;
    return false;
  }
};
}  // namespace esphome::nspanel_terminal
