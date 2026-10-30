#include "nspanel_terminal.h"

#include <driver/uart.h>
#include <esp_http_client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/components/uart/uart_component_esp_idf.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "nspanel_touchscreen.h"
#ifdef USE_LVGL
#include <lvgl.h>
#endif
namespace esphome::nspanel_terminal {
static const char *const TAG = "nspanel_terminal";
NSPanelDisplay::~NSPanelDisplay() {
  task_stop_ = true;
  while (task_running_) vTaskDelay(1);
  RAMAllocator<uint8_t>().deallocate(benchmark_cache_, size_t(width_) * height_ * 2 + 8192);
  RAMAllocator<uint8_t>().deallocate(benchmark_snapshot_, size_t(width_) * height_ * 2);
  RAMAllocator<uint8_t>().deallocate(snapshot_, size_t(width_) * 16 * 2);
}
void NSPanelDisplay::setup() {
  RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_EXTERNAL);
  const size_t framebuffer_bytes = size_t(width_) * height_ * 2;
  this->buffer_ = allocator.allocate(framebuffer_bytes);
  snapshot_ = allocator.allocate(size_t(width_) * 16 * 2);
  if (!buffer_ || !snapshot_) {
    ESP_LOGE(TAG, "PSRAM framebuffer/snapshot allocation failed");
    mark_failed();
    state_ = State::STOPPED;
    return;
  }
  std::memset(buffer_, 0, framebuffer_bytes);
  dirty_.set_dimensions(width_, height_);
  dirty_.all();
  deadline_ = millis() + 2000;
  task_stop_ = false;
  if (xTaskCreatePinnedToCore(task_entry_, "nspanel_uart", 6144, this, 2, &task_, 0) != pdPASS) {
    ESP_LOGE(TAG, "Transport task creation failed");
    mark_failed();
    state_ = State::STOPPED;
  } else while (!task_running_) vTaskDelay(1);
}
void NSPanelDisplay::task_entry_(void *arg) {
  auto *self = static_cast<NSPanelDisplay *>(arg);
  self->task_running_ = true;
  self->task_loop_();
  self->task_running_ = false;
  vTaskDelete(nullptr);
}
void NSPanelDisplay::task_loop_() {
  int incoming = -1;
  while (!task_stop_) {
    bool streaming;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      streaming = state_ == State::STREAM;
      if (streaming && !abandon_requested_) service_stream_(incoming);
      incoming = -1;
    }
    if (streaming) {
      auto *idf = static_cast<uart::IDFUARTComponent *>(parent_);
      uint8_t b;
      if (uart_read_bytes(uart_port_t(idf->get_hw_serial_number()), &b, 1, pdMS_TO_TICKS(1)) == 1)
        incoming = b;
    } else vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void NSPanelDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "NSPanel protocol6 display, %ux%u RGB565, PSRAM, UART 4 Mbaud", unsigned(width_),
                unsigned(height_));
  LOG_UPDATE_INTERVAL(this);
}
void NSPanelDisplay::trace_(uint8_t type, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  if (!diagnostics_) return;
  trace_buf_[trace_head_] = TraceEvent{micros(), a, b, c, d, type};
  trace_head_ = (trace_head_ + 1) % trace_buf_.size();
  if (trace_count_ < trace_buf_.size()) ++trace_count_;
}
void NSPanelDisplay::trace_edge(bool down, uint16_t x, uint16_t y) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  trace_(down ? 1 : 2, x, y);
}
void NSPanelDisplay::log_timings() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!diagnostics_ || !trace_count_) return;
  unsigned start = (trace_head_ + trace_buf_.size() - trace_count_) % trace_buf_.size();
  for (unsigned i = 0; i < trace_count_; ++i) {
    const auto &e = trace_buf_[(start + i) % trace_buf_.size()];
    ESP_LOGI(TAG, "T %u %u %u %u %u %u", unsigned(e.type), unsigned(e.t), e.a, e.b, e.c, e.d);
  }
}
void NSPanelDisplay::start_render_measurement() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  measurement_bytes_ = bytes_sent();
  measurement_rectangles_ = completed_rectangles();
  measurement_faults_ = faults_;
  measurement_resyncs_ = resyncs_;
  measurement_started_ms_ = millis();
  clear_trace();
  diagnostics_ = true;
  measurement_active_ = true;
  ESP_LOGI(TAG, "Render measurement started");
}
void NSPanelDisplay::report_render_measurement() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!measurement_active_) {
    ESP_LOGW(TAG, "Render measurement has not started");
    return;
  }
  diagnostics_ = false;
  ESP_LOGI(TAG, "Render measurement: %u ms, %u UART bytes, %u rectangles, %u faults, %u resyncs, %u trace events%s",
           unsigned(millis() - measurement_started_ms_), unsigned(bytes_sent() - measurement_bytes_),
           unsigned(uint16_t(completed_rectangles() - measurement_rectangles_)),
           faults_ - measurement_faults_, resyncs_ - measurement_resyncs_, trace_count_,
           trace_count_ == trace_buf_.size() ? " (buffer may have wrapped)" : "");
  diagnostics_ = true;
  log_timings();
  diagnostics_ = false;
  measurement_active_ = false;
}
void NSPanelDisplay::start_render_benchmark(unsigned repeats) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (state_ != State::STREAM || !stream_.idle() || !buffer_ || !snapshot_ || benchmark_active_ ||
      render_benchmark_active_ || render_start_pending_ || exit_requested_) {
    ESP_LOGW(TAG, "Render benchmark requires an idle streaming terminal");
    return;
  }
  render_repeats_ = std::min(std::max(repeats, 1u), unsigned(render_results_.size()));
  render_index_ = 0;
  render_benchmark_active_ = true;
  render_start_pending_ = true;
  render_repeat_active_ = false;
  render_repeat_complete_ = false;
  render_wait_refresh_ = false;
  render_stats_ = RenderStats{};
  ESP_LOGI(TAG, "Render benchmark: %u full-screen repeats", render_repeats_);
}
void NSPanelDisplay::begin_render_repeat_() {
  // Called from loop() with the mutex held. The refresh runs on LVGL's next
  // timer tick; the transport task keeps servicing the link while it renders.
  if (!render_benchmark_active_ || state_ != State::STREAM || !buffer_ || !snapshot_) {
    cancel_render_benchmark_("terminal left streaming");
    return;
  }
  if (refresh_active_ || lvgl_refreshing_) {
    // Let the in-flight refresh finish before invalidating again.
    render_start_pending_ = true;
    return;
  }
  render_stats_ = RenderStats{};
  render_repeat_started_us_ = micros();
  render_first_write_us_ = render_last_write_us_ = 0;
  render_wait_kind_ = 0;
  render_wait_refresh_ = false;
  render_refresh_started_us_ = 0;
  render_bytes_start_ = bytes_sent();
  render_rect_start_ = completed_rectangles();
  render_fault_start_ = faults_;
  render_resync_start_ = resyncs_;
  render_repeat_active_ = true;
  render_repeat_complete_ = false;
  dirty_.all();
#ifdef USE_LVGL
  if (auto *display = lv_display_get_default(); display != nullptr) {
    if (auto *screen = lv_screen_active(); screen != nullptr) {
      render_wait_refresh_ = true;
      render_refresh_deadline_ms_ = millis() + 5000;
      lv_obj_invalidate(screen);
    }
  }
#endif
  if (!render_wait_refresh_ && render_index_ == 0)
    ESP_LOGW(TAG, "Render benchmark has no LVGL display; measuring transport only");
  ESP_LOGI(TAG, "Render benchmark %u/%u started", render_index_ + 1, render_repeats_);
}
void NSPanelDisplay::on_lvgl_refresh_start_() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  lvgl_refreshing_ = true;
  if (render_benchmark_active_ && render_repeat_active_ && render_wait_refresh_ && !render_refresh_started_us_)
    render_refresh_started_us_ = micros();
}
void NSPanelDisplay::on_lvgl_refresh_ready_() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  lvgl_refreshing_ = false;
  refresh_active_ = false;
  if (render_benchmark_active_ && render_repeat_active_ && render_wait_refresh_) {
    if (render_refresh_started_us_) render_stats_.refresh_us += micros() - render_refresh_started_us_;
    render_refresh_started_us_ = 0;
    render_wait_refresh_ = false;
    dirty_.all();
  }
}
void NSPanelDisplay::close_render_wait_() {
  if (!render_wait_kind_) return;
  const uint32_t waited = micros() - render_wait_started_us_;
  if (render_wait_kind_ == 1)
    render_stats_.credit_wait_us += waited;
  else
    render_stats_.uart_wait_us += waited;
  render_wait_kind_ = 0;
}
void NSPanelDisplay::finish_render_repeat_() {
  if (!render_benchmark_active_ || render_index_ >= render_repeats_) return;
  render_repeat_active_ = false;
  const RenderStats &st = render_results_[render_index_];
  const unsigned repeat = render_index_ + 1;
  const unsigned util_total = st.total_us ? unsigned((uint64_t(st.bytes) * 250) / st.total_us) : 0;
  const unsigned util_transfer = st.transfer_us ? unsigned((uint64_t(st.bytes) * 250) / st.transfer_us) : 0;
  ESP_LOGI(TAG,
           "Render benchmark %u/%u: total=%u ms, refresh=%u ms, transfer=%u ms, %u bytes, %u rects, "
           "%u%% total wire, %u%% transfer wire, %u faults, %u resyncs",
           repeat, render_repeats_, unsigned(st.total_us / 1000), unsigned(st.refresh_us / 1000),
           unsigned(st.transfer_us / 1000), unsigned(st.bytes), unsigned(st.rectangles), util_total, util_transfer,
           unsigned(st.faults), unsigned(st.resyncs));
  ESP_LOGI(TAG,
           "Render phases %u/%u: copy=%u us encode=%u us write=%u us credit_wait=%u us/%u uart_wait=%u us/%u "
           "done_wait=%u us service=%u reports=%u",
           repeat, render_repeats_, unsigned(st.copy_us), unsigned(st.encode_us), unsigned(st.write_us),
           unsigned(st.credit_wait_us), unsigned(st.credit_stalls), unsigned(st.uart_wait_us),
           unsigned(st.uart_stalls), unsigned(st.done_wait_us), unsigned(st.service_calls), unsigned(st.reports));
  ++render_index_;
  if (render_index_ < render_repeats_) {
    render_start_pending_ = true;
    return;
  }
  log_render_summary_();
  render_benchmark_active_ = false;
}
void NSPanelDisplay::log_render_summary_() {
  if (!render_index_) return;
  uint64_t total = 0, refresh = 0, transfer = 0, copy = 0, encode = 0, write = 0;
  uint64_t credit = 0, uart = 0, done = 0, bytes = 0;
  uint32_t min_total = 0xffffffffu, max_total = 0;
  for (unsigned i = 0; i < render_index_; ++i) {
    const RenderStats &s = render_results_[i];
    total += s.total_us;
    refresh += s.refresh_us;
    transfer += s.transfer_us;
    copy += s.copy_us;
    encode += s.encode_us;
    write += s.write_us;
    credit += s.credit_wait_us;
    uart += s.uart_wait_us;
    done += s.done_wait_us;
    bytes += s.bytes;
    min_total = std::min(min_total, s.total_us);
    max_total = std::max(max_total, s.total_us);
  }
  const uint64_t n = render_index_;
  auto avg = [n](uint64_t sum) { return unsigned(sum / n); };
  const unsigned util_total = total ? unsigned((bytes * 250) / total) : 0;
  const unsigned util_transfer = transfer ? unsigned((bytes * 250) / transfer) : 0;
  ESP_LOGI(TAG,
           "Render benchmark summary (%u): total avg=%u ms (min=%u max=%u), refresh avg=%u ms, transfer avg=%u ms, "
           "wire avg=%u%% total/%u%% transfer",
           unsigned(n), avg(total) / 1000, unsigned(min_total / 1000), unsigned(max_total / 1000), avg(refresh) / 1000,
           avg(transfer) / 1000, util_total, util_transfer);
  ESP_LOGI(TAG,
           "Render phases avg: copy=%u us encode=%u us write=%u us credit_wait=%u us uart_wait=%u us done_wait=%u us",
           avg(copy), avg(encode), avg(write), avg(credit), avg(uart), avg(done));
}
void NSPanelDisplay::cancel_render_benchmark_(const char *reason) {
  if (!render_benchmark_active_ && !render_start_pending_) return;
  render_benchmark_active_ = false;
  render_start_pending_ = false;
  render_repeat_active_ = false;
  render_repeat_complete_ = false;
  render_wait_refresh_ = false;
  render_refresh_started_us_ = 0;
  render_wait_kind_ = 0;
  ESP_LOGW(TAG, "Render benchmark cancelled: %s", reason);
}
void NSPanelDisplay::start_protocol_benchmark() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (benchmark_active_ || render_benchmark_active_ || render_start_pending_ || state_ != State::STREAM ||
      !stream_.idle() || !buffer_ || !snapshot_ || exit_requested_) {
    ESP_LOGW(TAG, "Protocol benchmark requires an idle streaming terminal");
    return;
  }
  RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_EXTERNAL);
  allocator.deallocate(benchmark_cache_, size_t(width_) * height_ * 2 + 8192);
  benchmark_cache_ = nullptr;
  benchmark_cache_size_ = 0;
  allocator.deallocate(benchmark_snapshot_, size_t(width_) * height_ * 2);
  benchmark_snapshot_ = allocator.allocate(size_t(width_) * height_ * 2);
  if (!benchmark_snapshot_) {
    ESP_LOGE(TAG, "Protocol benchmark PSRAM allocation failed");
    return;
  }
  benchmark_active_ = true;
  benchmark_legacy_ = false;
  benchmark_reuse_ = false;
  benchmark_preencoded_ = false;
  benchmark_mode_ = benchmark_next_ = 0;
  benchmark_started_ms_ = millis();
  benchmark_bytes_ = bytes_sent();
  benchmark_rectangles_ = completed_rectangles();
  benchmark_faults_ = faults_;
  benchmark_resyncs_ = resyncs_;
  benchmark_build_us_ = benchmark_encode_us_ = 0;
  benchmark_service_calls_ = benchmark_credit_stalls_ = benchmark_uart_stalls_ = 0;
  benchmark_reports_ = benchmark_highwater_ = 0;
  benchmark_idle_credit_stalls_ = benchmark_write_us_ = 0;
  ESP_LOGI(TAG, "Protocol benchmark started: solid, gradient, blocks (4 Mbaud, 8N1)");
}
void NSPanelDisplay::start_legacy_protocol_benchmark() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (width_ != 454 || height_ != 320) {
    ESP_LOGW(TAG, "Legacy benchmark requires the EU 454x320 panel");
    return;
  }
  start_protocol_benchmark();
  if (!benchmark_active_) return;
  benchmark_legacy_ = true;
  ESP_LOGI(TAG, "Legacy V92 patterns: solid, gradient, blocks, random; 8 full-screen frames per mode");
}
void NSPanelDisplay::start_legacy_protocol_benchmark_reuse() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  start_legacy_protocol_benchmark();
  if (benchmark_active_) benchmark_reuse_ = true;
}
void NSPanelDisplay::start_legacy_protocol_benchmark_preencoded() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  start_legacy_protocol_benchmark();
  if (benchmark_active_) benchmark_preencoded_ = true;
}
void NSPanelDisplay::benchmark_report_() {
  static constexpr const char *NAMES[] = {"solid", "gradient", "blocks", "random"};
  const uint32_t elapsed = millis() - benchmark_started_ms_;
  const uint32_t bytes = bytes_sent() - benchmark_bytes_;
  const unsigned util = elapsed ? unsigned((uint64_t(bytes) * 100) / (uint64_t(elapsed) * 400)) : 0;
  ESP_LOGI(TAG, "%s %s: %u ms, %u UART bytes, %u rectangles, %u%% wire utilisation, %u faults, %u resyncs",
           benchmark_legacy_ ? "Legacy benchmark" : "Protocol benchmark", NAMES[benchmark_mode_], unsigned(elapsed), unsigned(bytes),
           unsigned(uint16_t(completed_rectangles() - benchmark_rectangles_)), util,
           faults_ - benchmark_faults_, resyncs_ - benchmark_resyncs_);
  if (benchmark_legacy_)
    ESP_LOGI(TAG, "Legacy phases %s: build=%u us encode=%u us write=%u us service=%u credit_stalls=%u idle_credit=%u uart_stalls=%u reports=%u highwater=%u",
             NAMES[benchmark_mode_], unsigned(benchmark_build_us_), unsigned(benchmark_encode_us_),
             unsigned(benchmark_write_us_), benchmark_service_calls_, benchmark_credit_stalls_,
             benchmark_idle_credit_stalls_, benchmark_uart_stalls_,
             benchmark_reports_, benchmark_highwater_);
  RAMAllocator<uint8_t>().deallocate(benchmark_cache_, size_t(width_) * height_ * 2 + 8192);
  benchmark_cache_ = nullptr;
  benchmark_cache_size_ = 0;
  ++benchmark_mode_;
  benchmark_next_ = 0;
  benchmark_started_ms_ = millis();
  benchmark_bytes_ = bytes_sent();
  benchmark_rectangles_ = completed_rectangles();
  benchmark_faults_ = faults_;
  benchmark_resyncs_ = resyncs_;
  benchmark_build_us_ = benchmark_encode_us_ = 0;
  benchmark_service_calls_ = benchmark_credit_stalls_ = benchmark_uart_stalls_ = 0;
  benchmark_reports_ = benchmark_highwater_ = 0;
  benchmark_idle_credit_stalls_ = benchmark_write_us_ = 0;
  if (benchmark_mode_ == (benchmark_legacy_ ? 4u : 3u)) {
    benchmark_active_ = false;
    RAMAllocator<uint8_t>().deallocate(benchmark_snapshot_, size_t(width_) * height_ * 2);
    benchmark_snapshot_ = nullptr;
    dirty_.all();
    ESP_LOGI(TAG, "%s complete; restoring UI", benchmark_legacy_ ? "Legacy benchmark" : "Protocol benchmark");
    benchmark_legacy_ = false;
  }
}
void NSPanelDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (this->buffer_ == nullptr || x < 0 || y < 0 || x >= width_ || y >= height_) return;
  const uint16_t rgb565 = (uint16_t(color.r & 0xf8) << 8) | (uint16_t(color.g & 0xfc) << 3) | (color.b >> 3);
  const size_t offset = 2u * (size_t(y) * width_ + size_t(x));
  if (this->buffer_[offset] == uint8_t(rgb565) && this->buffer_[offset + 1] == uint8_t(rgb565 >> 8)) return;
  this->buffer_[offset] = uint8_t(rgb565);
  this->buffer_[offset + 1] = uint8_t(rgb565 >> 8);
  this->dirty_.mark(x, y);
}

void NSPanelDisplay::update() {
  if (this->buffer_ == nullptr) return;
  // A lambda/page writer may make many pixel calls. Hold new snapshots until
  // the whole update finishes, while the transport task keeps servicing UART.
  set_refresh_active(true);
  this->do_update_();
  set_refresh_active(false);
}

void NSPanelDisplay::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                    display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                                    int x_offset, int y_offset, int x_pad) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (this->buffer_ == nullptr) return;
  // Gate snapshots from the first flush of an LVGL refresh to REFR_READY. The
  // refresh-start hook only marks that a refresh is in progress; gating there
  // would also stall the transport while LVGL renders, even when the refresh
  // changes nothing. The LVGL flag keeps a direct bulk draw outside a refresh
  // from leaving the gate set.
  if (refresh_hooks_installed_ && lvgl_refreshing_) refresh_active_ = true;
  if (order != display::COLOR_ORDER_RGB || bitness != display::COLOR_BITNESS_565 || big_endian ||
      this->get_rotation() != display::DISPLAY_ROTATION_0_DEGREES || this->is_clipping() || x_start < 0 ||
      y_start < 0 || w <= 0 || h <= 0 || x_start + w > width_ || y_start + h > height_ ||
      x_offset < 0 || y_offset < 0 || x_pad < 0) {
    display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset,
                                     x_pad);
    return;
  }
  const size_t source_stride = size_t(x_offset + w + x_pad) * 2u;
  int changed = 0;
  for (int row = 0; row < h; ++row) {
    const uint8_t *source = ptr + size_t(y_offset + row) * source_stride + 2u * x_offset;
    uint8_t *target = this->buffer_ + 2u * (size_t(y_start + row) * width_ + x_start);
    if (std::memcmp(source, target, size_t(w) * 2u) == 0) continue;
    // Compare on the same 8-pixel grid as DirtyRows. A wide LVGL flush can
    // contain several independent changes, with unchanged tiles between them.
    for (int col = x_start / 8; col <= (x_start + w - 1) / 8; ++col) {
      const int first = std::max(x_start, col * 8) - x_start;
      const int last = std::min(x_start + w, (col + 1) * 8) - x_start;
      if (std::memcmp(source + 2 * first, target + 2 * first, size_t(last - first) * 2u) != 0)
        this->dirty_.mark(x_start + first, y_start + row, last - first);
    }
    std::memcpy(target, source, size_t(w) * 2u);
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
bool NSPanelDisplay::identity_ok_(const char *reply) const {
  // The stock connect reply starts with the model, native version and MCU code:
  // NX4832F035_011C,<native>,61744,... . Our EU and US images both report
  // native revision 92; stock images report 72. Panel geometry is checked
  // separately by the loader through the stock entry dimensions and state.
  static constexpr char MODEL[] = "NX4832F035_011C,";
  const char *field = std::strstr(reply, MODEL);
  if (field == nullptr) return false;
  field += sizeof(MODEL) - 1;
  char *end = nullptr;
  const unsigned long version = std::strtoul(field, &end, 10);
  if (end == field || *end != ',') return false;
  char *record_end = nullptr;
  const unsigned long record = std::strtoul(end + 1, &record_end, 10);
  if (record_end == end + 1 || *record_end != ',') return false;
  return version == 92 && record == 61744;
}
void NSPanelDisplay::service_stream_(int incoming) {
  uint32_t started = micros();
  uint8_t bytes[257];
  size_t n = incoming >= 0 ? 1 : 0;
  if (n) bytes[0] = uint8_t(incoming);
  const size_t queued = std::min<size_t>(available(), sizeof bytes - n);
  if (queued && !read_array(bytes + n, queued)) {
    fail_("UART read");
    return;
  }
  n += queued;
  for (size_t i = 0; i < n; i++) {
    protocol::Reply r;
    const unsigned completed = stream_.completed();
    if (!stream_.feed(bytes[i], millis(), r)) continue;
    if (render_repeat_active_) ++render_stats_.reports;
    if (benchmark_legacy_) {
      ++benchmark_reports_;
      benchmark_highwater_ = std::max(benchmark_highwater_, r.highwater);
    }
    if (r.status == protocol::READY_BRIGHTNESS) {
      ESP_LOGI(TAG, "Live brightness capability confirmed");
      if (brightness_selected_) stream_.request_brightness(brightness_percent_);
    }
    if (r.status == protocol::BRIGHTNESS_DONE)
      ESP_LOGI(TAG, "Live brightness ACK received");
    if (r.status == protocol::ERROR && !stream_.failed()) ++resyncs_;
    if (stream_.completed() != completed) {
      if (render_repeat_active_ && render_last_write_us_) {
        render_stats_.done_wait_us += micros() - render_last_write_us_;
        render_last_write_us_ = 0;
        // The final wait belongs to the DONE round trip, not to encoding.
        render_wait_kind_ = 0;
      }
      trace_(5, uint16_t(millis() - submit_ms_));
    }
    if (touchscreen_ && r.freshness == 1 && !pending_touches_.push(r.mask & 1, r.x, r.y)) {
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
  if (stream_.idle() && benchmark_active_ && (exit_requested_ || abandon_requested_)) {
    benchmark_active_ = false;
    RAMAllocator<uint8_t>().deallocate(benchmark_cache_, size_t(width_) * height_ * 2 + 8192);
    benchmark_cache_ = nullptr;
    RAMAllocator<uint8_t>().deallocate(benchmark_snapshot_, size_t(width_) * height_ * 2);
    benchmark_snapshot_ = nullptr;
    dirty_.all();
    ESP_LOGW(TAG, "Protocol benchmark cancelled");
  }
  if (stream_.idle() && benchmark_active_) {
    const unsigned count = benchmark_legacy_ ? 8 : 1;
    if (benchmark_next_ == count) benchmark_report_();
    else {
      ++benchmark_next_;
      const unsigned x = 0, y = 0, w = width_, h = height_;
      const uint32_t build_started = benchmark_legacy_ ? micros() : 0;
      if ((!benchmark_reuse_ && !benchmark_preencoded_) || benchmark_next_ == 1)
        for (unsigned row = 0; row < h; ++row)
          for (unsigned col = 0; col < w; ++col) {
          uint16_t pixel;
          if (benchmark_legacy_) {
            if (benchmark_mode_ == 0) pixel = 0x07e0;
            else if (benchmark_mode_ == 1) {
              const uint16_t column = uint16_t((col * 31 / 453) << 11 | (col >> 4));
              const uint16_t line = uint16_t((row * 63 / 319) << 5 | (row >> 4));
              pixel = column ^ line;
            } else if (benchmark_mode_ == 2) pixel = (col % 80 < 60 && row % 24 < 16) ? 0xffff : 0;
            else {
              uint32_t v = (row * w + col + 1) * 747796405u + 2891336453u;
              v = ((v >> ((v >> 28) + 4)) ^ v) * 277803737u;
              pixel = uint16_t((v >> 22) ^ v);
            }
          } else if (benchmark_mode_ == 0) pixel = 0x39e7;
          else if (benchmark_mode_ == 1) pixel = uint16_t((((x + col) >> 3) & 31) << 11 |
                                                         (((y + row) >> 2) & 63) << 5 |
                                                         (((x + col + y + row) >> 3) & 31));
          else pixel = ((col / 12 + row / 16) & 1) ? 0xf800 : 0x001f;
          const size_t at = 2u * (row * w + col);
          benchmark_snapshot_[at] = uint8_t(pixel);
          benchmark_snapshot_[at + 1] = uint8_t(pixel >> 8);
          }
      if (benchmark_legacy_) benchmark_build_us_ += micros() - build_started;
      if (benchmark_preencoded_ && benchmark_next_ == 1) {
        const size_t capacity = size_t(w) * h * 2 + 8192;
        RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_EXTERNAL);
        benchmark_cache_ = allocator.allocate(capacity);
        if (!benchmark_cache_) {
          benchmark_active_ = false;
          dirty_.all();
          ESP_LOGE(TAG, "Benchmark encoded-cache PSRAM allocation failed");
          return;
        }
        protocol::Encoder<StreamEngine::Pixels> encoder(w * h, {benchmark_snapshot_});
        std::vector<uint8_t> body;
        benchmark_cache_size_ = 0;
        const uint32_t encode_started = micros();
        while (encoder.next(body)) {
          if (benchmark_cache_size_ + 2 + body.size() > capacity) {
            benchmark_active_ = false;
            allocator.deallocate(benchmark_cache_, capacity);
            benchmark_cache_ = nullptr;
            benchmark_cache_size_ = 0;
            dirty_.all();
            ESP_LOGE(TAG, "Benchmark encoded cache overflow");
            return;
          }
          benchmark_cache_[benchmark_cache_size_++] = uint8_t(body.size());
          benchmark_cache_[benchmark_cache_size_++] = uint8_t(body.size() >> 8);
          std::memcpy(benchmark_cache_ + benchmark_cache_size_, body.data(), body.size());
          benchmark_cache_size_ += body.size();
        }
        ESP_LOGI(TAG, "Preencoded %u bytes for %s in %u us", unsigned(benchmark_cache_size_),
                 benchmark_mode_ == 0 ? "solid" : benchmark_mode_ == 1 ? "gradient" : benchmark_mode_ == 2 ? "blocks" : "random",
                 unsigned(micros() - encode_started));
        benchmark_started_ms_ = millis();
        benchmark_bytes_ = bytes_sent();
      }
      const bool submitted = benchmark_preencoded_ ?
          stream_.submit_cached(x, y, w, h, benchmark_snapshot_, benchmark_cache_, benchmark_cache_size_, millis()) :
          stream_.submit(x, y, w, h, benchmark_snapshot_, millis());
      if (!submitted) {
        benchmark_active_ = false;
        RAMAllocator<uint8_t>().deallocate(benchmark_cache_, size_t(width_) * height_ * 2 + 8192);
        benchmark_cache_ = nullptr;
        RAMAllocator<uint8_t>().deallocate(benchmark_snapshot_, size_t(width_) * height_ * 2);
        benchmark_snapshot_ = nullptr;
        dirty_.all();
        fail_("benchmark submit");
        return;
      }
      submit_ms_ = millis();
    }
  }
  if (stream_.idle() && !benchmark_active_ && !exit_requested_ && !refresh_active_ && !render_wait_refresh_) {
    unsigned x, y, w, h;
    if (dirty_.take(x, y, w, h)) {
      const uint32_t copy_started = render_repeat_active_ ? micros() : 0;
      for (unsigned row = 0; row < h; row++)
        std::memcpy(snapshot_ + 2 * row * w, buffer_ + 2 * ((y + row) * width_ + x), 2 * w);
      if (render_repeat_active_) render_stats_.copy_us += micros() - copy_started;
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
    if (benchmark_legacy_) ++benchmark_service_calls_;
    if (render_wait_refresh_) {
      // No frame is pending yet; keep the reverse timeout fresh without
      // putting keepalive PINGs into the measured frame transfer.
      stream_.keepalive(millis());
      break;
    }
    if (render_repeat_active_) ++render_stats_.service_calls;
    const bool timed_encode = benchmark_legacy_ || render_repeat_active_;
    const uint32_t encode_started = timed_encode ? micros() : 0;
    stream_.prepare(millis());
    if (benchmark_legacy_) benchmark_encode_us_ += micros() - encode_started;
    if (render_repeat_active_) render_stats_.encode_us += micros() - encode_started;
    if (stream_.failed()) {
      fail_(stream_.error());
      return;
    }
    const size_t allowance = stream_.allowance();
    const size_t available = parent_->available_for_write();
    size_t count = std::min(allowance, available);
    if (!count) {
      if (render_repeat_active_ && stream_.pending() && !render_wait_kind_) {
        if (!allowance) {
          render_wait_kind_ = 1;
          ++render_stats_.credit_stalls;
        } else if (!available) {
          render_wait_kind_ = 2;
          ++render_stats_.uart_stalls;
        }
        if (render_wait_kind_) render_wait_started_us_ = micros();
      }
      if (benchmark_legacy_ && stream_.pending()) {
        if (!allowance) {
          ++benchmark_credit_stalls_;
          auto *idf = static_cast<uart::IDFUARTComponent *>(parent_);
          if (uart_wait_tx_done(uart_port_t(idf->get_hw_serial_number()), 0) == ESP_OK)
            ++benchmark_idle_credit_stalls_;
        }
        else if (!available) ++benchmark_uart_stalls_;
      }
      break;
    }
    if (render_repeat_active_) {
      close_render_wait_();
      if (!render_first_write_us_) render_first_write_us_ = micros();
    }
    const uint32_t write_started = timed_encode ? micros() : 0;
    write_array(stream_.data(), count);
    if (benchmark_legacy_) benchmark_write_us_ += micros() - write_started;
    if (render_repeat_active_) {
      render_stats_.write_us += micros() - write_started;
      render_last_write_us_ = micros();
    }
    stream_.sent(count, millis());
  } while (uint32_t(micros() - started) < 1500);
  if (render_repeat_active_ && !render_wait_refresh_ && stream_.idle() && dirty_.empty() &&
      completed_rectangles() != render_rect_start_) {
    const uint32_t ended = micros();
    render_repeat_active_ = false;
    render_stats_.total_us = ended - render_repeat_started_us_;
    render_stats_.transfer_us = render_first_write_us_ ? ended - render_first_write_us_ : 0;
    render_stats_.bytes = bytes_sent() - render_bytes_start_;
    render_stats_.rectangles = uint16_t(completed_rectangles() - render_rect_start_);
    render_stats_.faults = uint16_t(faults_ - render_fault_start_);
    render_stats_.resyncs = uint16_t(resyncs_ - render_resync_start_);
    render_results_[render_index_] = render_stats_;
    render_repeat_complete_ = true;
  }
}
void NSPanelDisplay::fail_(const char *reason) {
  benchmark_active_ = false;
  cancel_render_benchmark_(reason);
  // The stream may still reference the benchmark snapshot until it is reset
  // on re-entry, so retain it here and release it at the next safe start.
  ++faults_;
  ESP_LOGE(TAG, "Transport stopped: %s", reason);
  restore_page_ = false;
  dirty_.all();
  touch_reset_pending_ = true;
  pending_touches_ = TouchQueue();
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
  benchmark_active_ = false;
  RAMAllocator<uint8_t>().deallocate(benchmark_cache_, size_t(width_) * height_ * 2 + 8192);
  benchmark_cache_ = nullptr;
  RAMAllocator<uint8_t>().deallocate(benchmark_snapshot_, size_t(width_) * height_ * 2);
  benchmark_snapshot_ = nullptr;
  state_ = State::STOPPED;
  terminal_active_ = false;
  high_frequency_.stop();
  ESP_LOGI(TAG, "UART restored to115200; terminal stopped");
  if (startup_retry_ && !exit_requested_) request_enter();
}
void NSPanelDisplay::request_abandon() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (state_ != State::STREAM) return;
  abandon_requested_ = true;
  exit_requested_ = true;
  abandon_deadline_ = millis() + 4000;
  high_frequency_.stop();
  ESP_LOGW(TAG, "deliberate host stop: suppressing all records for 4s");
}
void NSPanelDisplay::request_enter() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
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
void NSPanelDisplay::request_tft_upload() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (tft_url_.empty() || upload_pending_) {
    ESP_LOGE(TAG, "TFT upload unavailable or already pending");
    return;
  }
  upload_pending_ = true;
  startup_retry_ = false;
  if (state_ != State::STOPPED) request_exit();
}
void NSPanelDisplay::request_brightness(int percent) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (percent < 0 || percent > 100) {
    ESP_LOGE(TAG, "Brightness must be 0-100%%");
    return;
  }
  brightness_percent_ = uint8_t(percent);
  brightness_selected_ = true;
  if (state_ == State::STREAM && stream_.live_brightness()) {
    stream_.request_brightness(brightness_percent_);
    return;
  }
  brightness_pending_ = true;
  if (state_ == State::STOPPED || state_ == State::BRIGHTNESS_SETTLE || state_ == State::BRIGHTNESS_VERIFY) return;
  brightness_resume_ = true;
  if (state_ != State::BOOT_WAIT) {
    startup_retry_ = false;
    request_exit();
  }
}

bool NSPanelDisplay::upload_ack_(uint32_t &position, uint32_t expected) {
  uint8_t reply[5]{};
  size_t count = 0;
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    uint8_t b;
    if (available() && read_byte(&b)) {
      if (!count && b != 0x05 && b != 0x08) continue;
      reply[count++] = b;
      if (reply[0] == 0x05 || count == 5) break;
    } else {
      App.feed_wdt();
      delay(1);
    }
  }
  if (!count) return false;
  if (reply[0] == 0x05) {
    position = expected;
    return true;
  }
  if (count != 5) return false;
  position = uint32_t(reply[1]) | uint32_t(reply[2]) << 8 | uint32_t(reply[3]) << 16 | uint32_t(reply[4]) << 24;
  if (position == 0) position = expected;
  return true;
}

bool NSPanelDisplay::upload_tft_() {
  esp_http_client_config_t config{};
  config.url = tft_url_.c_str();
  config.timeout_ms = 5000;
  auto http = esp_http_client_init(&config);
  if (!http) return false;
  esp_http_client_set_method(http, HTTP_METHOD_HEAD);
  uint32_t size = 0;
  if (esp_http_client_open(http, 0) == ESP_OK) {
    esp_http_client_fetch_headers(http);
    if (esp_http_client_get_status_code(http) == 200)
      size = esp_http_client_get_content_length(http);
  }
  esp_http_client_close(http);
  if (size < 4096 || size > 16777216) {
    ESP_LOGE(TAG, "TFT server must provide Content-Length (size=%u)", unsigned(size));
    esp_http_client_cleanup(http);
    return false;
  }
  drain_();
  auto *idf = static_cast<uart::IDFUARTComponent *>(parent_);
  const auto port = uart_port_t(idf->get_hw_serial_number());
  auto command = [this, port](const char *s) {
    write_str(s);
    const uint8_t end[] = {255, 255, 255};
    write_array(end, sizeof end);
    uart_wait_tx_done(port, pdMS_TO_TICKS(1000));
  };
  // Leave Nextion protocol reparse mode (recmod=1, e.g. Lovelace UI), otherwise no command gets through.
  command("DRAKJHSUYDGBNCJHGJKSHBDN");
  command("recmod=0");
  command("sleep=0");
  command("dim=100");
  delay(250);
  drain_();
  char request[64];
  std::snprintf(request, sizeof request, "whmi-wris %u,115200,1", unsigned(size));
  command(request);
  uint32_t position = 0;
  if (!upload_ack_(position, 0) || position != 0) {
    ESP_LOGE(TAG, "Display did not accept TFT upload");
    esp_http_client_cleanup(http);
    return false;
  }
  ESP_LOGI(TAG, "Uploading TFT: %u bytes", unsigned(size));
  std::vector<uint8_t> buffer(4096);
  unsigned retries = 0;
  while (position < size) {
    const uint32_t start = position;
    const size_t count = std::min<size_t>(buffer.size(), size - start);
    char range[64];
    std::snprintf(range, sizeof range, "bytes=%u-%u", unsigned(start), unsigned(start + count - 1));
    esp_http_client_set_method(http, HTTP_METHOD_GET);
    esp_http_client_set_header(http, "Range", range);
    if (esp_http_client_open(http, 0) != ESP_OK) {
      esp_http_client_cleanup(http);
      return false;
    }
    esp_http_client_fetch_headers(http);
    const int code = esp_http_client_get_status_code(http);
    if (code != 206 || esp_http_client_get_content_length(http) != int(count)) {
      ESP_LOGE(TAG, "TFT range request failed at %u: HTTP %d", unsigned(start), code);
      esp_http_client_cleanup(http);
      return false;
    }
    size_t read = 0;
    const uint32_t read_start = millis();
    while (read < count && millis() - read_start < 5000) {
      int n = esp_http_client_read(http, reinterpret_cast<char *>(buffer.data() + read), count - read);
      if (n > 0) read += n;
      App.feed_wdt();
    }
    esp_http_client_close(http);
    if (read != count) {
      esp_http_client_cleanup(http);
      return false;
    }
    write_array(buffer.data(), count);
    if (uart_wait_tx_done(port, pdMS_TO_TICKS(5000)) != ESP_OK ||
        !upload_ack_(position, start + count) || position > size || position == start) {
      esp_http_client_cleanup(http);
      return false;
    }
    if (position < start + count && ++retries > 5) {
      esp_http_client_cleanup(http);
      return false;
    }
    if (position == start + count) retries = 0;
    if ((position / 4096) % 32 == 0 || position == size)
      ESP_LOGI(TAG, "TFT upload %u/%u", unsigned(position), unsigned(size));
    App.feed_wdt();
  }
  ESP_LOGI(TAG, "TFT upload transfer complete");
  esp_http_client_cleanup(http);
  return true;
}
void NSPanelDisplay::loop() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
#ifdef USE_LVGL
  if (!refresh_hooks_installed_) {
    if (auto *display = lv_display_get_default()) {
      lv_display_add_event_cb(display, [](lv_event_t *e) {
        static_cast<NSPanelDisplay *>(lv_event_get_user_data(e))->on_lvgl_refresh_start_();
      }, LV_EVENT_REFR_START, this);
      lv_display_add_event_cb(display, [](lv_event_t *e) {
        static_cast<NSPanelDisplay *>(lv_event_get_user_data(e))->on_lvgl_refresh_ready_();
      }, LV_EVENT_REFR_READY, this);
      refresh_hooks_installed_ = true;
      ESP_LOGI(TAG, "LVGL refresh tracking and snapshot gating enabled");
    }
  }
#endif
  if (render_start_pending_ && !refresh_active_ && !lvgl_refreshing_) {
    render_start_pending_ = false;
    begin_render_repeat_();
  }
  if (render_wait_refresh_ && int32_t(millis() - render_refresh_deadline_ms_) >= 0) {
    ESP_LOGW(TAG, "Render benchmark refresh timeout; measuring transport only");
    render_wait_refresh_ = false;
    render_refresh_started_us_ = 0;
    render_stats_.refresh_us = 0;
    render_repeat_started_us_ = micros();
    dirty_.all();
  }
  if (render_repeat_complete_) {
    render_repeat_complete_ = false;
    finish_render_repeat_();
  }
  if (touchscreen_) {
    if (touch_reset_pending_) {
      touchscreen_->reset_contacts();
      touch_reset_pending_ = false;
    }
    TouchSample sample;
    if (pending_touches_.pop(sample)) {
      if (!touchscreen_->new_report(sample.down, sample.x, sample.y)) {
        fail_("touch report rejected");
      }
    }
  }
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
      state_ != State::VERIFY_PAGE && state_ != State::VERIFY_IDENTITY && state_ != State::BRIGHTNESS_SETTLE &&
      state_ != State::BRIGHTNESS_VERIFY && state_ != State::STOPPED) {
    if (state_ == State::CONFIG)
      fail_("entry cancelled");
    else
      state_ = State::STOPPED;
    return;
  }
  switch (state_) {
    case State::BOOT_WAIT:
      if (overdue) {
        if (initial_brightness_ >= 0 && !brightness_pending_) {
          brightness_percent_ = initial_brightness_;
          brightness_pending_ = true;
          brightness_resume_ = true;
        }
        initial_brightness_ = -1;
        if (brightness_pending_)
          state_ = State::STOPPED;
        else
          send_query_(State::CONNECT, "connect");
      }
      break;
    case State::CONNECT:
      if (read_terminated_()) {
        rx_[std::min(rx_count_, rx_.size() - 1)] = 0;
        if (!identity_ok_(reinterpret_cast<char *>(rx_.data())))
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
      stream_.begin(session_, now, width_, height_);
      if (exit_requested_) stream_.request_exit();
      state_ = State::STREAM;
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
        if (!identity_ok_(reinterpret_cast<char *>(rx_.data()))) {
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
    case State::BRIGHTNESS_SETTLE:
      if (overdue) send_query_(State::BRIGHTNESS_VERIFY, "get dim");
      break;
    case State::BRIGHTNESS_VERIFY:
      if (read_fixed_(8)) {
        if (rx_[0] == 0x71 && rx_[5] == 255 && rx_[6] == 255 && rx_[7] == 255 &&
            protocol::get32(rx_.data() + 1) == brightness_percent_)
          ESP_LOGI(TAG, "Brightness verified at %u%%", brightness_percent_);
        else
          ESP_LOGE(TAG, "Brightness verification failed");
        state_ = State::STOPPED;
        if (!brightness_pending_ && brightness_resume_) {
          brightness_resume_ = false;
          request_enter();
        }
      } else if (overdue) {
        ESP_LOGE(TAG, "Brightness verification timed out");
        state_ = State::STOPPED;
        if (!brightness_pending_ && brightness_resume_) {
          brightness_resume_ = false;
          request_enter();
        }
      }
      break;
    case State::STOPPED:
      if (upload_pending_) {
        upload_pending_ = false;
        const bool ok = upload_tft_();
        ESP_LOGI(TAG, "TFT upload %s", ok ? "complete" : "failed");
        brightness_pending_ = true;  // Upload wakes the display at 100%; restore the selected level.
        brightness_resume_ = true;
        state_ = State::BOOT_WAIT;
        exit_requested_ = false;
        deadline_ = millis() + (ok ? 3000 : 1000);
      } else if (brightness_pending_) {
        brightness_pending_ = false;
        drain_();
        char command[24];
        std::snprintf(command, sizeof command, "dim=%u", brightness_percent_);
        send_command_(command);
        state_ = State::BRIGHTNESS_SETTLE;
        deadline_ = millis() + 40;
      }
      break;
  }
}
void NSPanelDisplay::on_shutdown() {
  task_stop_ = true;
  while (task_running_) vTaskDelay(1);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
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
