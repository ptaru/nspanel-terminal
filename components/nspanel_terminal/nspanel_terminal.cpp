#include "nspanel_terminal.h"

#include <driver/uart.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esphome/components/uart/uart_component_esp_idf.h"
#include "esphome/core/log.h"
#include "nspanel_touchscreen.h"
namespace esphome::nspanel_terminal {
static const char *const TAG = "nspanel_terminal";
NSPanelDisplay::~NSPanelDisplay() { RAMAllocator<uint8_t>().deallocate(snapshot_, 454 * 16 * 2); }
void NSPanelDisplay::setup() {
  RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_EXTERNAL);
  this->buffer_ = allocator.allocate(454 * 320 * 2);
  snapshot_ = allocator.allocate(454 * 16 * 2);
  if (!buffer_ || !snapshot_) {
    ESP_LOGE(TAG, "PSRAM framebuffer/snapshot allocation failed");
    mark_failed();
    state_ = State::STOPPED;
    return;
  }
  std::memset(buffer_, 0, 454 * 320 * 2);
  dirty_.all();
  deadline_ = millis() + 2000;
}
void NSPanelDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "NSPanel V92 protocol6 display, 454x320 RGB565, PSRAM, UART 4 Mbaud");
  LOG_UPDATE_INTERVAL(this);
}
void NSPanelDisplay::trace_(uint8_t type, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  if (!diagnostics_) return;
  trace_buf_[trace_head_] = TraceEvent{micros(), a, b, c, d, type};
  trace_head_ = (trace_head_ + 1) % trace_buf_.size();
  if (trace_count_ < trace_buf_.size()) ++trace_count_;
}
void NSPanelDisplay::trace_edge(bool down, uint16_t x, uint16_t y) { trace_(down ? 1 : 2, x, y); }
void NSPanelDisplay::log_timings() {
  if (!diagnostics_ || !trace_count_) return;
  unsigned start = (trace_head_ + trace_buf_.size() - trace_count_) % trace_buf_.size();
  for (unsigned i = 0; i < trace_count_; ++i) {
    const auto &e = trace_buf_[(start + i) % trace_buf_.size()];
    ESP_LOGI(TAG, "T %u %u %u %u %u %u", unsigned(e.type), unsigned(e.t), e.a, e.b, e.c, e.d);
  }
}
void NSPanelDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (this->buffer_ == nullptr || x < 0 || y < 0 || x >= protocol::WIDTH || y >= protocol::HEIGHT) return;
  const uint16_t rgb565 = (uint16_t(color.r & 0xf8) << 8) | (uint16_t(color.g & 0xfc) << 3) | (color.b >> 3);
  const size_t offset = 2u * (size_t(y) * protocol::WIDTH + size_t(x));
  if (this->buffer_[offset] == uint8_t(rgb565) && this->buffer_[offset + 1] == uint8_t(rgb565 >> 8)) return;
  this->buffer_[offset] = uint8_t(rgb565);
  this->buffer_[offset + 1] = uint8_t(rgb565 >> 8);
  this->dirty_.mark(x, y);
}

void NSPanelDisplay::update() {
  if (this->buffer_ != nullptr) this->do_update_();
}

void NSPanelDisplay::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                    display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                                    int x_offset, int y_offset, int x_pad) {
  if (this->buffer_ == nullptr) return;
  if (order != display::COLOR_ORDER_RGB || bitness != display::COLOR_BITNESS_565 || big_endian ||
      this->get_rotation() != display::DISPLAY_ROTATION_0_DEGREES || this->is_clipping() || x_start < 0 ||
      y_start < 0 || w <= 0 || h <= 0 || x_start + w > protocol::WIDTH || y_start + h > protocol::HEIGHT ||
      x_offset < 0 || y_offset < 0 || x_pad < 0) {
    display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset,
                                     x_pad);
    return;
  }
  const size_t source_stride = size_t(x_offset + w + x_pad) * 2u;
  int changed = 0;
  for (int row = 0; row < h; ++row) {
    const uint8_t *source = ptr + size_t(y_offset + row) * source_stride + 2u * x_offset;
    uint8_t *target = this->buffer_ + 2u * (size_t(y_start + row) * protocol::WIDTH + x_start);
    if (std::memcmp(source, target, size_t(w) * 2u) == 0) continue;
    std::memcpy(target, source, size_t(w) * 2u);
    this->dirty_.mark(x_start, y_start + row, w);
    ++changed;
  }
  if (changed) trace_(3, uint16_t(w), uint16_t(h), uint16_t(x_start), uint16_t(y_start));
}

void NSPanelDisplay::drain_() {
  uint8_t b;
  while (available() && read_byte(&b)) {
  }
  rx_count_ = 0;
}
void NSPanelDisplay::send_command_(const char *command) {
  command_.assign(command, command + std::strlen(command));
  command_.insert(command_.end(), 3, 255);
  command_at_ = 0;
  next_command_byte_ = micros();
  rx_count_ = 0;
  high_frequency_.start();
}
bool NSPanelDisplay::pump_command_() {
  if (command_at_ < command_.size()) {
    if (int32_t(micros() - next_command_byte_) >= 0 && parent_->available_for_write()) {
      write_byte(command_[command_at_++]);
      next_command_byte_ = micros() + 190;
    }
    return false;
  }
  if (!command_.empty()) {
    auto *idf = static_cast<uart::IDFUARTComponent *>(parent_);
    if (uart_wait_tx_done(uart_port_t(idf->get_hw_serial_number()), 0) != ESP_OK) return false;
    command_.clear();
    command_at_ = 0;
    high_frequency_.stop();
  }
  return true;
}
void NSPanelDisplay::send_query_(State next, const char *command) {
  drain_();
  send_command_(command);
  state_ = next;
  deadline_ = millis() + 700;
}
bool NSPanelDisplay::read_fixed_(size_t length) {
  while (rx_count_ < length && available()) {
    if (!read_byte(&rx_[rx_count_])) return false;
    ++rx_count_;
  }
  return rx_count_ == length;
}
bool NSPanelDisplay::read_terminated_() {
  uint8_t b;
  while (available() && read_byte(&b)) {
    if (rx_count_ == rx_.size()) return false;
    rx_[rx_count_++] = b;
    if (rx_count_ >= 3 && rx_[rx_count_ - 1] == 255 && rx_[rx_count_ - 2] == 255 && rx_[rx_count_ - 3] == 255)
      return true;
  }
  return false;
}
bool NSPanelDisplay::change_baud_(uint32_t baud) {
  auto *idf = static_cast<uart::IDFUARTComponent *>(parent_);
  if (uart_set_baudrate(uart_port_t(idf->get_hw_serial_number()), baud) != ESP_OK) return false;
  parent_->set_baud_rate(baud);
  return true;
}
void NSPanelDisplay::service_stream_() {
  uint32_t started = micros();
  uint8_t bytes[256];
  size_t n = std::min<size_t>(available(), sizeof bytes);
  if (n && !read_array(bytes, n)) {
    fail_("UART read");
    return;
  }
  for (size_t i = 0; i < n; i++) {
    protocol::Reply r;
    const unsigned completed = stream_.completed();
    if (!stream_.feed(bytes[i], millis(), r)) continue;
    if (r.status == protocol::ERROR && !stream_.failed()) ++resyncs_;
    if (stream_.completed() != completed) trace_(5, uint16_t(millis() - submit_ms_));
    if (touchscreen_ && r.freshness == 1 && !touchscreen_->new_report(r.mask & 1, r.x, r.y)) {
      fail_("touch edge queue overflow");
      return;
    }
  }
  if (stream_.failed()) {
    fail_(stream_.error());
    return;
  }
  if (stream_.stage() == StreamEngine::Stage::STOPPED) {
    restore_page_ = stream_.mask() == 0;
    terminal_active_ = false;
    high_frequency_.stop();
    state_ = State::RECOVER_WAIT;
    deadline_ = millis() + 500;
    return;
  }
  if (stream_.idle() && !exit_requested_) {
    unsigned x, y, w, h;
    if (dirty_.take(x, y, w, h)) {
      for (unsigned row = 0; row < h; row++)
        std::memcpy(snapshot_ + 2 * row * w, buffer_ + 2 * ((y + row) * 454 + x), 2 * w);
      if (!stream_.submit(x, y, w, h, snapshot_, millis())) {
        dirty_.all();
        fail_("rectangle submit");
        return;
      }
      submit_ms_ = millis();
      trace_(4, uint16_t(w), uint16_t(h), uint16_t(x), uint16_t(y));
    }
  }
  do {
    stream_.prepare(millis());
    if (stream_.failed()) {
      fail_(stream_.error());
      return;
    }
    size_t count = std::min(stream_.allowance(), parent_->available_for_write());
    if (!count) break;
    write_array(stream_.data(), count);
    stream_.sent(count, millis());
  } while (uint32_t(micros() - started) < 1500);
  if (stream_.idle())
    high_frequency_.stop();
  else
    high_frequency_.start();
}
void NSPanelDisplay::fail_(const char *reason) {
  ++faults_;
  ESP_LOGE(TAG, "Transport stopped: %s", reason);
  restore_page_ = false;
  dirty_.all();
  if (touchscreen_) touchscreen_->reset_contacts();
  if (startup_retry_ && startup_attempts_ < 2)
    ++startup_attempts_;
  else
    startup_retry_ = false;
  command_.clear();
  command_at_ = 0;
  high_frequency_.stop();
  state_ = State::RECOVER_WAIT;
  deadline_ = millis() + (terminal_active_ ? 500 : 40);
}
void NSPanelDisplay::finish_recovery_() {
  state_ = State::STOPPED;
  terminal_active_ = false;
  high_frequency_.stop();
  ESP_LOGI(TAG, "UART restored to115200; terminal stopped");
  if (startup_retry_ && !exit_requested_) request_enter();
}
void NSPanelDisplay::request_abandon() {
  if (state_ != State::STREAM) return;
  abandon_requested_ = true;
  exit_requested_ = true;
  abandon_deadline_ = millis() + 4000;
  high_frequency_.stop();
  ESP_LOGW(TAG, "deliberate host stop: suppressing all records for 4s");
}
void NSPanelDisplay::request_enter() {
  if (state_ != State::STOPPED || !buffer_ || !snapshot_) return;
  if (!startup_retry_) {
    startup_retry_ = true;
    startup_attempts_ = 0;
  }
  exit_requested_ = false;
  snapshots_ready_ = false;
  restore_page_ = false;
  dirty_.all();
  rx_count_ = 0;
  state_ = State::BOOT_WAIT;
  deadline_ = millis() + 300;
}
void NSPanelDisplay::loop() {
  const uint32_t loop_us = micros();
  if (last_loop_us_) {
    const uint32_t gap = loop_us - last_loop_us_;
    if (gap > max_gap_us_) max_gap_us_ = gap;
  }
  last_loop_us_ = loop_us;
  ++loops_;
  if (!pump_command_()) return;
  const uint32_t now = millis();
  const bool overdue = int32_t(now - deadline_) >= 0;
  if (exit_requested_ && state_ != State::STREAM && state_ != State::SWITCHING && state_ != State::RECOVER_WAIT &&
      state_ != State::RECOVER_CONFIG && state_ != State::RECOVER_BAUD && state_ != State::VERIFY_SYS0 &&
      state_ != State::VERIFY_SYS1 && state_ != State::VERIFY_SYS2 && state_ != State::VERIFY_BKCMD &&
      state_ != State::VERIFY_PAGE && state_ != State::VERIFY_IDENTITY && state_ != State::STOPPED) {
    if (state_ == State::CONFIG)
      fail_("entry cancelled");
    else
      state_ = State::STOPPED;
    return;
  }
  switch (state_) {
    case State::BOOT_WAIT:
      if (overdue) send_query_(State::CONNECT, "connect");
      break;
    case State::CONNECT:
      if (read_terminated_()) {
        rx_[std::min(rx_count_, rx_.size() - 1)] = 0;
        if (!std::strstr(reinterpret_cast<char *>(rx_.data()), "NX4832F035_011C,92,61744,"))
          fail_("native identity");
        else
          send_query_(State::PAGE, "sendme");
      } else if (overdue)
        fail_("connect timeout");
      break;
    case State::PAGE:
      if (read_fixed_(5)) {
        if (rx_[0] != 0x66 || rx_[2] != 255 || rx_[3] != 255 || rx_[4] != 255)
          fail_("page reply");
        else {
          page_ = rx_[1];
          send_query_(State::BKCMD, "get bkcmd");
        }
      } else if (overdue)
        fail_("page timeout");
      break;
    case State::BKCMD:
    case State::SYS0:
    case State::SYS1:
    case State::SYS2:
      if (read_fixed_(8)) {
        if (rx_[0] != 0x71 || rx_[5] != 255 || rx_[6] != 255 || rx_[7] != 255) {
          fail_("register reply");
          break;
        }
        uint32_t v = protocol::get32(rx_.data() + 1);
        if (state_ == State::BKCMD) {
          if (v > 3) {
            fail_("bkcmd");
            break;
          }
          saved_bkcmd_ = v;
          send_query_(State::SYS0, "get sys0");
        } else if (state_ == State::SYS0) {
          saved_sys_[0] = v;
          send_query_(State::SYS1, "get sys1");
        } else if (state_ == State::SYS1) {
          saved_sys_[1] = v;
          send_query_(State::SYS2, "get sys2");
        } else {
          saved_sys_[2] = v;
          snapshots_ready_ = true;
          session_ = uint16_t(micros()) | 1;
          config_step_ = 0;
          state_ = State::CONFIG;
          deadline_ = now;
        }
      } else if (overdue)
        fail_("register timeout");
      break;
    case State::CONFIG:
      if (overdue) {
        drain_();
        char command[40];
        switch (config_step_++) {
          case 0:
            send_command_("bkcmd=0");
            break;
          case 1:
            send_command_("sys0=4000000");
            break;
          case 2:
            send_command_("sys1=1");
            break;
          case 3:
            std::snprintf(command, sizeof command, "sys2=%u", session_);
            send_command_(command);
            break;
          default:
            send_command_("touch_j");
            state_ = State::SWITCHING;
            return;
        }
        deadline_ = millis() + 40;
      }
      break;
    case State::SWITCHING:
      terminal_active_ = true;
      if (!change_baud_(4000000)) {
        fail_("baud switch");
        break;
      }
      stream_.begin(session_, now);
      if (exit_requested_) stream_.request_exit();
      state_ = State::STREAM;
      high_frequency_.start();
      break;
    case State::STREAM:
      if (abandon_requested_) {
        if (int32_t(millis() - abandon_deadline_) >= 0) {
          abandon_requested_ = false;
          fail_("deliberate host stop");
        }
        break;
      }
      if (stream_.stage() != StreamEngine::Stage::READY) startup_retry_ = false;
      service_stream_();
      break;
    case State::RECOVER_WAIT:
      if (overdue) {
        if (!change_baud_(115200)) {
          ESP_LOGE(TAG, "UART baud restoration failed");
          finish_recovery_();
          break;
        }
        send_query_(State::RECOVER_BAUD, "get baud");
      }
      break;
    case State::RECOVER_BAUD:
      if (read_fixed_(8)) {
        if (rx_[0] != 0x71 || protocol::get32(rx_.data() + 1) != 115200 || rx_[5] != 255 || rx_[6] != 255 ||
            rx_[7] != 255) {
          startup_retry_ = false;
          finish_recovery_();
          break;
        }
        recovery_step_ = 0;
        state_ = State::RECOVER_CONFIG;
        deadline_ = now;
      } else if (overdue) {
        startup_retry_ = false;
        finish_recovery_();
      }
      break;
    case State::RECOVER_CONFIG:
      if (!overdue) break;
      if (!snapshots_ready_) {
        finish_recovery_();
        break;
      }
      {
        char command[40];
        drain_();
        switch (recovery_step_++) {
          case 0:
            std::snprintf(command, sizeof command, "sys0=%ld", long(int32_t(saved_sys_[0])));
            break;
          case 1:
            std::snprintf(command, sizeof command, "sys1=%ld", long(int32_t(saved_sys_[1])));
            break;
          case 2:
            std::snprintf(command, sizeof command, "sys2=%ld", long(int32_t(saved_sys_[2])));
            break;
          case 3:
            std::snprintf(command, sizeof command, "bkcmd=%u", saved_bkcmd_);
            break;
          case 4:
            if (restore_page_) {
              std::snprintf(command, sizeof command, "page %u", page_);
              break;
            }
            [[fallthrough]];
          default:
            send_query_(State::VERIFY_SYS0, "get sys0");
            return;
        }
        send_command_(command);
        deadline_ = millis() + 40;
      }
      break;
    case State::VERIFY_SYS0:
    case State::VERIFY_SYS1:
    case State::VERIFY_SYS2:
    case State::VERIFY_BKCMD:
      if (read_fixed_(8)) {
        unsigned index = state_ == State::VERIFY_SYS0 ? 0 : state_ == State::VERIFY_SYS1 ? 1 : 2;
        uint32_t wanted = state_ == State::VERIFY_BKCMD ? saved_bkcmd_ : saved_sys_[index];
        if (rx_[0] != 0x71 || rx_[5] != 255 || rx_[6] != 255 || rx_[7] != 255 ||
            protocol::get32(rx_.data() + 1) != wanted) {
          ESP_LOGE(TAG, "Register restoration verification failed");
          startup_retry_ = false;
          finish_recovery_();
          break;
        }
        if (state_ == State::VERIFY_SYS0)
          send_query_(State::VERIFY_SYS1, "get sys1");
        else if (state_ == State::VERIFY_SYS1)
          send_query_(State::VERIFY_SYS2, "get sys2");
        else if (state_ == State::VERIFY_SYS2)
          send_query_(State::VERIFY_BKCMD, "get bkcmd");
        else
          send_query_(State::VERIFY_PAGE, "sendme");
      } else if (overdue) {
        ESP_LOGE(TAG, "Restoration query timeout");
        startup_retry_ = false;
        finish_recovery_();
      }
      break;
    case State::VERIFY_PAGE:
      if (read_fixed_(5)) {
        if (rx_[0] != 0x66 || rx_[2] != 255 || rx_[3] != 255 || rx_[4] != 255 || (restore_page_ && rx_[1] != page_)) {
          ESP_LOGE(TAG, "Page restoration verification failed");
          startup_retry_ = false;
          finish_recovery_();
          break;
        }
        send_query_(State::VERIFY_IDENTITY, "connect");
      } else if (overdue) {
        ESP_LOGE(TAG, "Page query timeout");
        startup_retry_ = false;
        finish_recovery_();
      }
      break;
    case State::VERIFY_IDENTITY:
      if (read_terminated_()) {
        rx_[std::min(rx_count_, rx_.size() - 1)] = 0;
        if (!std::strstr(reinterpret_cast<char *>(rx_.data()), "NX4832F035_011C,92,61744,")) {
          ESP_LOGE(TAG, "Restored identity mismatch");
          startup_retry_ = false;
        } else
          ESP_LOGI(TAG, "Verified native92, baud, saved registers and page after exit");
        finish_recovery_();
      } else if (overdue) {
        ESP_LOGE(TAG, "Restored identity timeout");
        startup_retry_ = false;
        finish_recovery_();
      }
      break;
    case State::STOPPED:
      break;
  }
}
void NSPanelDisplay::on_shutdown() {
  if (state_ == State::STREAM && stream_.idle()) {
    stream_.request_exit();
    stream_.prepare(millis());
    auto n = std::min(stream_.allowance(), parent_->available_for_write());
    if (n) write_array(stream_.data(), n);
  }
  // In-flight or partial records are safely abandoned by the display watchdog.
  high_frequency_.stop();
}
}  // namespace esphome::nspanel_terminal
