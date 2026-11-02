#include <cassert>
#include <cstdint>
#include <vector>

#include "../components/nspanel_terminal/stream_engine.h"

using namespace esphome::nspanel_terminal;

static constexpr unsigned SESSION = 7;

static std::vector<uint8_t> reply(unsigned status, uint32_t consumed, unsigned rectangles = 0,
                                  unsigned mask = 0, unsigned freshness = 0) {
  std::vector<uint8_t> b(32);
  b[0] = 'S';
  b[1] = 'R';
  b[2] = 8;
  b[3] = uint8_t(status);
  protocol::put16(b, 4, SESSION);
  protocol::put16(b, 8, consumed);
  protocol::put16(b, 10, consumed >> 16);
  protocol::put16(b, 12, rectangles);
  b[20] = uint8_t(mask);
  b[21] = uint8_t(freshness);
  protocol::put16(b, 26, 512);
  protocol::put16(b, 30, protocol::crc(b.data(), 30));
  return b;
}

static void feed(StreamEngine &engine, const std::vector<uint8_t> &bytes) {
  protocol::Reply r;
  for (uint8_t b : bytes) engine.feed(b, 1, r);
}

static void send(StreamEngine &engine) {
  engine.prepare(1);
  const size_t n = engine.allowance();
  if (n) {
    engine.sent(n, 1);
    feed(engine, reply(protocol::PROGRESS, engine.sent_bytes()));
  }
}

static void run_to(StreamEngine &engine, StreamEngine::Stage want) {
  for (unsigned i = 0; i < 1000 && engine.stage() != want; ++i) send(engine);
  assert(engine.stage() == want);
}

int main() {
  const uint8_t cache[] = {1, 0, 0};  // one 1-byte DATA body

  // A final cached rectangle must keep its slot through a retry and until the
  // DONE reply, because the retry re-reads cache_ from RECT onwards.
  StreamEngine retried;
  retried.begin(SESSION, 0, 454, 320);
  feed(retried, reply(protocol::READY, 0));
  assert(retried.submit_cached(0, 0, 1, 1, cache, cache, sizeof cache, 1, true));
  assert(!retried.cache_releasable());
  run_to(retried, StreamEngine::Stage::DONE);
  assert(!retried.cache_releasable());
  feed(retried, reply(protocol::ERROR, retried.sent_bytes()));
  assert(retried.stage() == StreamEngine::Stage::SYNC);
  assert(!retried.cache_releasable());
  run_to(retried, StreamEngine::Stage::SYNCED);
  assert(!retried.cache_releasable());
  feed(retried, reply(protocol::SYNCED, retried.sent_bytes()));
  assert(retried.stage() == StreamEngine::Stage::RECT);
  assert(!retried.cache_releasable());
  run_to(retried, StreamEngine::Stage::DONE);
  assert(!retried.cache_releasable());
  feed(retried, reply(protocol::DONE, retried.sent_bytes(), 1));
  assert(retried.idle());
  assert(retried.cache_releasable());

  // A non-final rectangle cannot retry, so its slot is safe at BETWEEN.
  StreamEngine partial;
  partial.begin(SESSION, 0, 454, 320);
  feed(partial, reply(protocol::READY, 0));
  assert(partial.submit_cached(0, 0, 1, 1, cache, cache, sizeof cache, 1, false));
  run_to(partial, StreamEngine::Stage::BETWEEN);
  assert(partial.cache_releasable());

  // Multi-rectangle bursts also cannot retry; only the single-rectangle case
  // needs the DONE reply lifetime.
  StreamEngine burst;
  burst.begin(SESSION, 0, 454, 320);
  feed(burst, reply(protocol::READY, 0));
  assert(burst.submit_cached(0, 0, 1, 1, cache, cache, sizeof cache, 1, false));
  run_to(burst, StreamEngine::Stage::BETWEEN);
  assert(burst.submit_cached(0, 0, 1, 1, cache, cache, sizeof cache, 1, true));
  run_to(burst, StreamEngine::Stage::DONE);
  assert(burst.cache_releasable());

  // Raw snapshot retry must retain its slot just like cached input.
  const uint8_t pixels[] = {0x34, 0x12, 0x34, 0x12};
  StreamEngine raw;
  raw.begin(SESSION, 0, 454, 320);
  feed(raw, reply(protocol::READY, 0));
  assert(raw.submit(0, 0, 2, 1, pixels, 1, true));
  assert(!raw.cache_releasable());
  run_to(raw, StreamEngine::Stage::DONE);
  assert(!raw.cache_releasable());
  feed(raw, reply(protocol::ERROR, raw.sent_bytes()));
  assert(!raw.cache_releasable());
  run_to(raw, StreamEngine::Stage::SYNCED);
  feed(raw, reply(protocol::SYNCED, raw.sent_bytes()));
  assert(!raw.cache_releasable());
  run_to(raw, StreamEngine::Stage::DONE);
  feed(raw, reply(protocol::DONE, raw.sent_bytes(), 1));
  assert(raw.idle() && raw.cache_releasable());
  // Release eligibility must be checked before submitting the next queued job:
  // the new submit replaces snapshot_ and starts another retained lifetime.
  assert(raw.can_submit());
  assert(raw.submit(2, 0, 2, 1, pixels, 1, true));
  assert(!raw.cache_releasable());
  run_to(raw, StreamEngine::Stage::DONE);
  feed(raw, reply(protocol::DONE, raw.sent_bytes(), 2));
  assert(raw.can_submit() && raw.cache_releasable());

  // A dropped reverse byte shifts the fixed-size report stream. The parser
  // must slide to the next 'S','R' frame rather than fail the terminal.
  StreamEngine shifted;
  shifted.begin(SESSION, 0, 454, 320);
  const auto ready = reply(protocol::READY, 0);
  for (size_t i = 1; i < ready.size(); ++i) {
    protocol::Reply r;
    shifted.feed(ready[i], 1, r);
  }
  assert(!shifted.failed());
  feed(shifted, ready);
  assert(!shifted.failed());
  assert(shifted.stage() == StreamEngine::Stage::IDLE);
  assert(shifted.reverse_shifts() > 0);

  // A malformed contact or Goodix I2C fault is not a transport fault.
  StreamEngine touch;
  touch.begin(SESSION, 0, 454, 320);
  feed(touch, reply(protocol::READY, 0));
  feed(touch, reply(protocol::PROGRESS, 0, 0, 32, 3));
  assert(!touch.failed());
  assert(touch.touch_faults() == 1);
  feed(touch, reply(protocol::PROGRESS, 0, 0, 1, 1));
  assert(!touch.failed());
  assert(touch.stage() == StreamEngine::Stage::IDLE);

  return 0;
}
