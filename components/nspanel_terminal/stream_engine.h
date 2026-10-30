#pragma once
#include <optional>

#include "protocol.h"

namespace esphome::nspanel_terminal {
// Platform-independent transport. No waits, UART calls or borrowed LVGL buffers.
class StreamEngine {
 public:
  enum class Stage { READY, IDLE, RECT, DATA, END, DONE, SYNC, SYNCED, BRIGHTNESS_ACK, EXIT, BYE, STOPPED, FAILED };
  struct Pixels {
    const uint8_t *data;
    uint16_t operator()(unsigned i) const { return protocol::get16(data + 2 * i); }
  };
  void begin(unsigned session, uint32_t now, unsigned width, unsigned height) {
    *this = StreamEngine();
    session_ = session;
    width_ = width;
    height_ = height;
    stage_ = Stage::READY;
    last_report_ = last_wire_ = now;
  }
  bool submit(unsigned x, unsigned y, unsigned w, unsigned h, const uint8_t *snapshot, uint32_t now) {
    if (stage_ != Stage::IDLE || !snapshot || !w || !h || x >= width_ || y >= height_ || w > width_ - x ||
        h > height_ - y)
      return false;
    x_ = x;
    y_ = y;
    w_ = w;
    h_ = h;
    snapshot_ = snapshot;
    cached_ = false;
    retries_ = 0;
    stage_ = Stage::RECT;
    last_report_ = now;
    return true;
  }
  bool submit_cached(unsigned x, unsigned y, unsigned w, unsigned h, const uint8_t *snapshot,
                     const uint8_t *cache, size_t cache_size, uint32_t now) {
    if (!cache || !cache_size || !submit(x, y, w, h, snapshot, now)) return false;
    cache_ = cache;
    cache_size_ = cache_size;
    cache_at_ = 0;
    cached_ = true;
    return true;
  }
  // One bounded record per call. Progress resumes on the next main-loop pass.
  void prepare(uint32_t now) {
    if (failed() || stage_ == Stage::STOPPED) return;
    if (uint32_t(now - last_report_) > (stage_ == Stage::IDLE ? 1800u : 1000u)) {
      fail("reverse timeout");
      return;
    }
    if (pending()) return;
    tx_.clear();
    at_ = 0;
    switch (stage_) {
      case Stage::IDLE:
        if (exit_) {
          stage_ = Stage::EXIT;
          last_report_ = now;
          prepare(now);
        } else if (brightness_pending_) {
          tx_ = protocol::record(session_, seq_++, protocol::BRIGHTNESS, brightness_percent_);
          brightness_pending_ = false;
          stage_ = Stage::BRIGHTNESS_ACK;
        } else if (uint32_t(now - last_wire_) >= 80) {
          tx_ = protocol::record(session_, seq_++, protocol::PING);
          ++heartbeats_;
        }
        break;
      case Stage::RECT:
        tx_ = protocol::record(session_, seq_++, protocol::RECT, x_, y_, w_, h_);
        if (cached_) cache_at_ = 0;
        else encoder_.emplace(w_ * h_, Pixels{snapshot_});
        stage_ = Stage::DATA;
        break;
      case Stage::DATA:
        if (cached_ && cache_at_ < cache_size_) {
          if (cache_size_ - cache_at_ < 2) {
            fail("cached length bounds");
            break;
          }
          const unsigned length = protocol::get16(cache_ + cache_at_);
          cache_at_ += 2;
          if (!length || length > protocol::PAYLOAD || length > cache_size_ - cache_at_) {
            fail("cached data bounds");
            break;
          }
          body_.assign(cache_ + cache_at_, cache_ + cache_at_ + length);
          cache_at_ += length;
          protocol::data_record(session_, seq_++, body_, raw_, tx_);
        } else if (!cached_ && encoder_->next(body_)) {
          protocol::data_record(session_, seq_++, body_, raw_, tx_);
        } else {
          stage_ = Stage::END;
          prepare(now);
        }
        break;
      case Stage::END:
        tx_ = protocol::record(session_, seq_++, protocol::END);
        stage_ = Stage::DONE;
        break;
      case Stage::SYNC:
        tx_ = protocol::record(session_, seq_++, protocol::SYNC);
        tx_.insert(tx_.begin(), 0);
        stage_ = Stage::SYNCED;
        break;
      case Stage::EXIT:
        tx_ = protocol::record(session_, seq_++, protocol::EXIT);
        stage_ = Stage::BYE;
        break;
      default:
        break;
    }
  }
  size_t allowance() const {
    if (sent_ - consumed_ > 512 || !pending()) return 0;
    return std::min<size_t>(tx_.size() - at_, 512 - (sent_ - consumed_));
  }
  const uint8_t *data() const { return tx_.data() + at_; }
  // Reset the reverse-timeout clock without putting a record on the wire.
  void keepalive(uint32_t now) { last_report_ = now; }
  void sent(size_t n, uint32_t now) {
    if (n > allowance()) {
      fail("local credit overrun");
      return;
    }
    at_ += n;
    sent_ += uint32_t(n);
    last_wire_ = now;
  }
  // Return true for each validated report, including ERROR and touch samples.
  bool feed(uint8_t b, uint32_t now, protocol::Reply &r) {
    if (failed() || stage_ == Stage::STOPPED) return false;
    rx_[have_++] = b;
    if (have_ != 32) return false;
    have_ = 0;
    if (!protocol::reply(rx_, session_, r)) {
      fail("reverse integrity");
      return false;
    }
    if (uint32_t(r.consumed - consumed_) > uint32_t(sent_ - consumed_)) {
      fail("credit ahead");
      return false;
    }
    consumed_ = r.consumed;
    last_report_ = now;
    mask_ = r.mask;
    if (r.flags || r.freshness > 1 || (r.mask & 32)) {
      fail("receiver or touch fault");
      return true;
    }
    if (r.status == protocol::READY || r.status == protocol::READY_BRIGHTNESS) {
      if (stage_ != Stage::READY || r.seq || r.consumed || r.rectangles) {
        fail("unexpected READY");
        return true;
      }
      stage_ = Stage::IDLE;
      live_brightness_ = r.status == protocol::READY_BRIGHTNESS;
    } else if (r.status == protocol::ERROR) {
      if (++retries_ > 2) {
        fail("receiver CRC fault");
        return true;
      }
      tx_.clear();
      at_ = 0;
      stage_ = Stage::SYNC;
    } else if (r.status == protocol::SYNCED) {
      if (stage_ != Stage::SYNCED || pending()) {
        fail("unexpected SYNC");
        return true;
      }
      stage_ = snapshot_ ? Stage::RECT : Stage::IDLE;
      if (!snapshot_) retries_ = 0;
    } else if (r.status == protocol::DONE) {
      if (stage_ != Stage::DONE || pending() || r.rectangles != uint16_t(completed_ + 1)) {
        fail("unexpected DONE");
        return true;
      }
      completed_ = uint16_t(r.rectangles);
      retries_ = 0;
      snapshot_ = nullptr;
      encoder_.reset();
      stage_ = Stage::IDLE;
    } else if (r.status == protocol::BRIGHTNESS_DONE) {
      if (stage_ != Stage::BRIGHTNESS_ACK || pending()) {
        fail("unexpected brightness ACK");
        return true;
      }
      stage_ = Stage::IDLE;
    } else if (r.status == protocol::BYE) {
      if (stage_ != Stage::BYE || pending()) {
        fail("unexpected BYE");
        return true;
      }
      stage_ = Stage::STOPPED;
    }
    return true;
  }
  void request_exit() { exit_ = true; }
  void request_brightness(uint8_t percent) {
    brightness_percent_ = percent;
    brightness_pending_ = true;
  }
  bool live_brightness() const { return live_brightness_; }
  void fail(const char *reason) {
    error_ = reason;
    stage_ = Stage::FAILED;
  }
  bool pending() const { return at_ < tx_.size(); }
  bool failed() const { return stage_ == Stage::FAILED; }
  bool idle() const { return stage_ == Stage::IDLE && !pending(); }
  Stage stage() const { return stage_; }
  unsigned mask() const { return mask_; }
  unsigned completed() const { return completed_; }
  uint32_t sent_bytes() const { return sent_; }
  unsigned heartbeats() const { return heartbeats_; }
  const char *error() const { return error_; }

 private:
  Stage stage_ = Stage::STOPPED;
  unsigned session_ = 0, seq_ = 0, x_ = 0, y_ = 0, w_ = 0, h_ = 0, retries_ = 0, mask_ = 0, heartbeats_ = 0;
  unsigned width_ = 454, height_ = 320;
  uint16_t completed_ = 0;
  uint32_t sent_ = 0, consumed_ = 0, last_wire_ = 0, last_report_ = 0;
  bool exit_ = false;
  bool live_brightness_ = false, brightness_pending_ = false;
  uint8_t brightness_percent_ = 100;
  const uint8_t *snapshot_ = nullptr;
  const uint8_t *cache_ = nullptr;
  size_t cache_size_ = 0, cache_at_ = 0;
  bool cached_ = false;
  std::optional<protocol::Encoder<Pixels>> encoder_;
  std::vector<uint8_t> tx_, body_, raw_;
  size_t at_ = 0;
  uint8_t rx_[32]{};
  unsigned have_ = 0;
  const char *error_ = "none";
};

// Dirty rows are claimed/cleared at snapshot time, never at DONE. Subsequent
// rendering therefore remains dirty even when a superseded snapshot completes.
struct DirtyRows {
  // Eight-pixel tiles keep a row change compact. The EU panel uses 57 columns
  // for 454 pixels; the US panel uses 40 for 320. Both fit in one uint64_t.
  static constexpr unsigned TILE = 8;
  static constexpr unsigned MAX_COLUMNS = 64;
  static constexpr unsigned MAX_ROWS = 480;
  std::array<uint64_t, MAX_ROWS> rows{};
  unsigned next = 0;
  unsigned width = 454, height = 320, columns = 57;
  uint64_t full = (uint64_t(1) << 57) - 1;

  void set_dimensions(unsigned panel_width, unsigned panel_height) {
    width = panel_width;
    height = std::min(panel_height, MAX_ROWS);
    columns = (width + TILE - 1) / TILE;
    if (columns == 0) columns = 1;
    if (columns > MAX_COLUMNS) columns = MAX_COLUMNS;
    full = columns == 64 ? ~uint64_t(0) : ((uint64_t(1) << columns) - 1);
    next = 0;
    rows.fill(0);
  }
  void mark(unsigned x, unsigned y, unsigned w = 1) {
    if (!w || x >= width || y >= height) return;
    const unsigned first = x / TILE;
    const unsigned last = (std::min<unsigned>(width, x + w) - 1) / TILE;
    rows[y] |= (full >> (columns - (last - first + 1))) << first;
  }
  void all() {
    next = 0;
    rows.fill(0);
    for (unsigned y = 0; y < height; ++y) rows[y] = full;
  }
  bool empty() const {
    for (unsigned y = 0; y < height; ++y)
      if (rows[y]) return false;
    return true;
  }
  bool take(unsigned &x, unsigned &y, unsigned &w, unsigned &h) {
    unsigned scanned = 0;
    while (scanned < height && rows[(next + scanned) % height] == 0) ++scanned;
    if (scanned == height) return false;
    y = (next + scanned) % height;
    // Span the whole row, gaps included: a rectangle costs about a millisecond of round trip, a clean 8-pixel
    // tile only a fraction of that on the display's LCD bus.
    const unsigned first = __builtin_ctzll(rows[y]);
    const unsigned last = 63 - __builtin_clzll(rows[y]);
    const uint64_t mask = (full >> (columns - (last - first + 1))) << first;
    x = first * TILE;
    const unsigned right = std::min<unsigned>(width, (last + 1) * TILE);
    w = right - x;
    h = 1;
    rows[y] &= ~mask;
    // Consume only matching columns. Dirty tiles outside the span stay queued.
    while (h < 16 && y + h < height && (rows[y + h] & mask)) {
      rows[y + h] &= ~mask;
      ++h;
    }
    next = (y + (rows[y] ? 0 : h)) % height;
    return true;
  }
};
struct TouchSample {
  bool down = false;
  uint16_t x = 0, y = 0;
};
class TouchQueue {
 public:
  bool push(bool down, uint16_t x, uint16_t y) {
    if (down == last_.down && (!down || (last_.x == x && last_.y == y))) return true;
    TouchSample next{down, x, y};
    if (size_ && down && items_[(head_ + size_ - 1) % 32].down)
      items_[(head_ + size_ - 1) % 32] = next;
    else {
      if (size_ == 32) return false;
      items_[(head_ + size_) % 32] = next;
      ++size_;
    }
    last_ = next;
    return true;
  }
  bool pop(TouchSample &s) {
    if (!size_) return false;
    s = items_[head_];
    head_ = (head_ + 1) % 32;
    --size_;
    return true;
  }
  bool empty() const { return size_ == 0; }
  unsigned size() const { return size_; }

 private:
  std::array<TouchSample, 32> items_{};
  unsigned head_ = 0, size_ = 0;
  TouchSample last_{};
};
}  // namespace esphome::nspanel_terminal
