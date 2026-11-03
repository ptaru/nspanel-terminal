#include "nspanel_terminal.h"

#include <driver/uart.h>
#include <esp_http_client.h>
#include <esp_memory_utils.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/components/uart/uart_component_esp_idf.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "nspanel_touchscreen.h"
#include "frame_encode.h"
#ifdef USE_LVGL
#include <lvgl.h>
#endif
namespace esphome::nspanel_terminal {
static const char *const TAG = "nspanel_terminal";
NSPanelDisplay::~NSPanelDisplay() {
  task_stop_ = true;
  wake_transport_();
  signal_slot_();
  while (task_running_) vTaskDelay(1);
  if (work_signal_) { vSemaphoreDelete(work_signal_); work_signal_ = nullptr; }
  if (slot_signal_) { vSemaphoreDelete(slot_signal_); slot_signal_ = nullptr; }
  // The upload task owns the UART while it runs; do not free the slot storage
  // underneath it.
  while (upload_running_) vTaskDelay(1);
  if (job_storage_) RAMAllocator<uint8_t>().deallocate(job_storage_, job_capacity_ * JOB_SLOTS);
  job_storage_ = nullptr;
}
uart::IDFUARTComponent *NSPanelDisplay::idf_uart_() const {
  // ESPHome 2026.10 selects IDFUARTComponent for every ESP32 configuration,
  // including framework: arduino. only_on_esp32() and UART_DEVICE_SCHEMA make
  // this the only possible parent type.
  return static_cast<uart::IDFUARTComponent *>(parent_);
}
bool NSPanelDisplay::init_frame_jobs_() {
  // LVGL allocates its draw buffer during setup; allocate the slots only when
  // the stream starts so the smaller, hotter LVGL buffer wins internal RAM.
  // frame_slots_ram: psram makes the slot placement explicit so the caller can
  // trade the measured PSRAM cost for internal-RAM headroom.
  const uint8_t flags = frame_slots_psram_ ? uint8_t(RAMAllocator<uint8_t>::ALLOC_EXTERNAL)
                                           : uint8_t(RAMAllocator<uint8_t>::PREFER_INTERNAL);
  RAMAllocator<uint8_t> allocator(flags);
  job_capacity_ = size_t(width_) * JOB_ROWS * 2u + 4096u;
  job_storage_ = allocator.allocate(job_capacity_ * JOB_SLOTS);
  if (!job_storage_) {
    ESP_LOGE(TAG, "Frame slot allocation failed in %s", frame_slots_psram_ ? "PSRAM" : "internal RAM");
    return false;
  }
  frame_slots_internal_ = esp_ptr_internal(job_storage_);
  ESP_LOGI(TAG, "Frame slots: %u x %u bytes in %s RAM", unsigned(JOB_SLOTS), unsigned(job_capacity_),
           frame_slots_internal_ ? "internal" : "external");
  for (unsigned i = 0; i < JOB_SLOTS; ++i) {
    jobs_[i].cache = job_storage_ + size_t(i) * job_capacity_;
    jobs_[i].state = 0;
    jobs_[i].size = 0;
  }
  job_head_ = job_tail_ = job_queued_ = 0;
  active_job_ = nullptr;
  return true;
}
NSPanelDisplay::FrameJob *NSPanelDisplay::acquire_job_(std::unique_lock<std::recursive_mutex> &lock) {
  // Bound the wait so a stalled receiver cannot hold the main loop for the
  // whole reverse timeout. Feed the task watchdog while yielding to the
  // transport task.
  const uint32_t started = millis();
  while (job_storage_ && state_ == State::STREAM && !task_stop_) {
    for (auto &job : jobs_)
      if (job.reserve()) {
        return &job;
      }
    const uint32_t elapsed = millis() - started;
    if (elapsed >= 250) return nullptr;
    const uint32_t remaining = 250 - elapsed;
    lock.unlock();
    App.feed_wdt();
    // Latched signal covers a release between the scan and the wait. The
    // short timeout also feeds the watchdog and preserves the overall limit.
    xSemaphoreTake(slot_signal_, pdMS_TO_TICKS(std::min<uint32_t>(remaining, 10)));
    lock.lock();
  }
  return nullptr;
}
void NSPanelDisplay::drop_frame_(const char *reason) {
  ++dropped_frames_;
  if ((dropped_frames_ & 0x3f) == 1)
    ESP_LOGW(TAG, "Dropped frame area: %s (%u total)", reason, dropped_frames_);
  if (state_ == State::STREAM) repaint_pending_ = true;
}
void NSPanelDisplay::enqueue_job_(FrameJob *job) {
  job->state = 2;
  job_queue_[job_head_] = unsigned(job - jobs_.data());
  job_head_ = (job_head_ + 1) % JOB_SLOTS;
  ++job_queued_;
  wake_transport_();
}
NSPanelDisplay::FrameJob *NSPanelDisplay::dequeue_job_() {
  if (!job_queued_) return nullptr;
  FrameJob *job = &jobs_[job_queue_[job_tail_]];
  job_tail_ = (job_tail_ + 1) % JOB_SLOTS;
  --job_queued_;
  return job;
}
void NSPanelDisplay::release_active_job_() {
  if (!active_job_) return;
  active_job_->state = 0;
  active_job_ = nullptr;
  signal_slot_();
}
void NSPanelDisplay::clear_jobs_() {
  ++job_generation_;
  for (auto &job : jobs_) job.cancel();
  signal_slot_();
  wake_transport_();
  job_head_ = job_tail_ = job_queued_ = 0;
  active_job_ = nullptr;
}
void NSPanelDisplay::invalidate_screen_() {
#ifdef USE_LVGL
  if (auto *display = lv_display_get_default(); display != nullptr)
    if (auto *screen = lv_screen_active(); screen != nullptr) lv_obj_invalidate(screen);
#endif
}
void NSPanelDisplay::request_repaint_() {
  dirty_.all();
  invalidate_screen_();
}
void NSPanelDisplay::setup() {
  dirty_.set_dimensions(width_, height_);
  dirty_.all();
  repaint_pending_ = true;
  deadline_ = millis() + 2000;
  task_stop_ = false;
  slot_signal_ = xSemaphoreCreateBinaryStatic(&slot_signal_storage_);
  work_signal_ = xSemaphoreCreateBinaryStatic(&work_signal_storage_);
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
  uint32_t runnable_started = 0;
  using Wait = StreamEngine::Wait;
  while (!task_stop_) {
    Wait wait = Wait::INACTIVE;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (state_ == State::STREAM) {
        service_stream_(incoming);
        if (state_ == State::STREAM && !stream_.failed()) {
          const bool queued = !exit_requested_ && !job_queue_empty_();
          wait = stream_.next_work(millis(), queued, parent_->available_for_write());
        }
      }
      incoming = -1;
    }
    if (task_stop_) break;
    if (wait == Wait::RUNNABLE) {
      const uint32_t now = micros();
      if (!runnable_started) runnable_started = now;
      if (uint32_t(now - runnable_started) >= 4000) {
        vTaskDelay(pdMS_TO_TICKS(1));
        runnable_started = 0;
      }
      // The mutex is released between bounded service passes. RX is drained
      // again before any further writes; never wait for RX with runnable TX.
      continue;
    }
    runnable_started = 0;
    if (wait == Wait::RX || wait == Wait::TX_SPACE) {
      auto *idf = idf_uart_();
      uint8_t b;
      if (uart_read_bytes(uart_port_t(idf->get_hw_serial_number()), &b, 1, pdMS_TO_TICKS(1)) == 1)
        incoming = b;
    } else if (wait == Wait::PRODUCER) {
      // Work publications are latched even if they precede this wait. A one
      // tick fallback bounds touch/report latency while no producer is ready.
      xSemaphoreTake(work_signal_, pdMS_TO_TICKS(1));
    } else {
      xSemaphoreTake(work_signal_, pdMS_TO_TICKS(10));
    }
  }
}
void NSPanelDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "NSPanel protocol8 LVGL flush display, %ux%u RGB565, UART 4 Mbaud", unsigned(width_),
                unsigned(height_));
  LOG_UPDATE_INTERVAL(this);
}
void NSPanelDisplay::on_lvgl_refresh_start_() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  lvgl_refreshing_ = true;
}
void NSPanelDisplay::on_lvgl_refresh_ready_() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  lvgl_refreshing_ = false;
  refresh_active_ = false;
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
  std::unique_lock<std::recursive_mutex> lock(mutex_);
  // Gate snapshots while LVGL is producing flush areas.
  if (refresh_hooks_installed_ && lvgl_refreshing_) refresh_active_ = true;
  // Without a mirror there is no fallback framebuffer. Only the RGB565 fast
  // path is supported; anything else is a firmware fault, not a slow path.
  if (order != display::COLOR_ORDER_RGB || bitness != display::COLOR_BITNESS_565 || big_endian ||
      this->get_rotation() != display::DISPLAY_ROTATION_0_DEGREES || this->is_clipping() || x_start < 0 ||
      y_start < 0 || w <= 0 || h <= 0 || x_start + w > width_ || y_start + h > height_ ||
      x_offset < 0 || y_offset < 0 || x_pad < 0) {
    if (!unsupported_logged_) {
      ESP_LOGE(TAG, "Unsupported draw path; flushed pixels dropped");
      unsupported_logged_ = true;
    }
    return;
  }
  // Dropping flushes while leaving the terminal prevents the ring from
  // filling with frames the transport will never submit.
  if (state_ != State::STREAM || !job_storage_ || exit_requested_) return;
  const size_t source_stride = size_t(x_offset + w + x_pad) * 2u;
  for (int first = 0; first < h; first += int(JOB_ROWS)) {
    const int rows = std::min<int>(JOB_ROWS, h - first);
    FrameJob *job = acquire_job_(lock);
    if (!job) {
      if (state_ == State::STREAM) drop_frame_("frame slot wait timed out");
      return;
    }
    const uint64_t generation = job_generation_;
    uint8_t *const cache = job->cache;
    const size_t capacity = job_capacity_;
    lock.unlock();
    const size_t size = encode_rows(ptr + size_t(y_offset + first) * source_stride, source_stride, x_offset,
                                    unsigned(w), unsigned(rows), cache, capacity);
    lock.lock();
    if (!job->finish_encoding(generation, job_generation_)) {
      drop_frame_("encode cancelled by stream reset");
      return;
    }
    if (state_ != State::STREAM || exit_requested_) {
      job->state = 0;
      return;
    }
    if (!size) {
      job->state = 0;
      drop_frame_("encode overflow");
      return;
    }
    job->x = uint16_t(x_start);
    job->y = uint16_t(y_start + first);
    job->w = uint16_t(w);
    job->h = uint16_t(rows);
    job->size = size;
    enqueue_job_(job);
  }
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
    auto *idf = idf_uart_();
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
  auto *idf = idf_uart_();
  if (uart_set_baudrate(uart_port_t(idf->get_hw_serial_number()), baud) != ESP_OK) return false;
  parent_->set_baud_rate(baud);
  return true;
}
bool NSPanelDisplay::identity_ok_(const char *reply) const {
  // The stock connect reply starts with the model, native version and MCU code:
  // NX4832F035_011C,<native>,61744,... . Our EU and US images both report
  // native revision 94 (protocol 8). Panel geometry is checked
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
  return version == 94 && record == 61744;
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
    if (!stream_.feed(bytes[i], millis(), r)) continue;
    if (r.status == protocol::READY || r.status == protocol::READY_BRIGHTNESS)
      ESP_LOGI(TAG, "Protocol %u (burst, content aware)", stream_.version());
    if (r.status == protocol::READY_BRIGHTNESS) {
      ESP_LOGI(TAG, "Live brightness capability confirmed");
      if (brightness_selected_) stream_.request_brightness(brightness_percent_);
    }
    if (r.status == protocol::BRIGHTNESS_DONE)
      ESP_LOGI(TAG, "Live brightness ACK received");
    if (r.freshness > 1 || (r.mask & 32)) {
      if ((stream_.touch_faults() & 0x3f) == 1)
        ESP_LOGW(TAG, "Touch controller fault ignored (freshness=%u, mask=0x%02x)", unsigned(r.freshness),
                 unsigned(r.mask));
    }
    if (touchscreen_ && r.freshness == 1 && !pending_touches_.push(r.mask & 1, r.x, r.y)) {
      ++dropped_touches_;
      if ((dropped_touches_ & 0x3f) == 1)
        ESP_LOGW(TAG, "Dropped touch sample because the main-loop queue is full (%u total)",
                 dropped_touches_);
    }
  }
  if (stream_.failed()) {
    fail_(stream_.error());
    return;
  }
  if (stream_.stage() == StreamEngine::Stage::STOPPED) {
    // mask bit 5 is the receiver's sticky touch fault; it must not suppress
    // restoration of the stock page after exit.
    restore_page_ = (stream_.mask() & 0x1f) == 0;
    terminal_active_ = false;
    high_frequency_.stop();
    state_ = State::RECOVER_WAIT;
    deadline_ = millis() + 500;
    return;
  }
  do {
    // Release a completed owner before submit() replaces snapshot_/active_job_.
    if (active_job_ && stream_.can_submit() && stream_.cache_releasable()) release_active_job_();
    if (stream_.can_submit() && !exit_requested_) {
      FrameJob *job = dequeue_job_();
      if (job) {
        const bool encoding = std::any_of(jobs_.begin(), jobs_.end(),
                                          [](const FrameJob &slot) { return slot.encoding(); });
        const bool final = (job_queue_empty_() && !encoding) || stream_.burst_rectangles() >= 255;
        if (!stream_.submit_cached(job->x, job->y, job->w, job->h, job->cache, job->cache, job->size, millis(),
                                   final)) {
          clear_jobs_();
          fail_("rectangle submit");
          return;
        }
        active_job_ = job;
      }
    }
    stream_.prepare(millis());
    if (active_job_ && stream_.stage() != StreamEngine::Stage::RECT &&
        stream_.stage() != StreamEngine::Stage::DATA && stream_.cache_releasable()) {
      // All DATA records for this rectangle are queued and no retry can still
      // read the slot; the slot can be reused.
      release_active_job_();
    }
    if (stream_.failed()) {
      fail_(stream_.error());
      return;
    }
    const size_t allowance = stream_.allowance();
    const size_t available = parent_->available_for_write();
    size_t count = std::min(allowance, available);
    if (!count) break;
    write_array(stream_.data(), count);
    stream_.sent(count, millis());
  } while (uint32_t(micros() - started) < 1500);
}
void NSPanelDisplay::fail_(const char *reason) {
  ESP_LOGE(TAG, "Transport stopped: %s", reason);
  restore_page_ = false;
  clear_jobs_();
  repaint_pending_ = true;
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
  clear_jobs_();
  repaint_pending_ = true;
  state_ = State::STOPPED;
  terminal_active_ = false;
  high_frequency_.stop();
  ESP_LOGI(TAG, "UART restored to 115200; terminal stopped");
  if (startup_retry_ && !exit_requested_) request_enter();
}
void NSPanelDisplay::request_enter() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (upload_running_) {
    ESP_LOGW(TAG, "TFT upload in progress; enter ignored");
    return;
  }
  // Frame slots are allocated when the stream starts, so a stopped terminal
  // must still be able to re-enter and retry the allocation.
  if (state_ != State::STOPPED) return;
  if (!startup_retry_) {
    startup_retry_ = true;
    startup_attempts_ = 0;
  }
  exit_requested_ = false;
  snapshots_ready_ = false;
  restore_page_ = false;
  clear_jobs_();
  repaint_pending_ = true;
  dirty_.all();
  rx_count_ = 0;
  state_ = State::BOOT_WAIT;
  wake_transport_();
  deadline_ = millis() + 300;
}
void NSPanelDisplay::request_tft_upload() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (tft_url_.empty() || upload_pending_ || upload_running_) {
    ESP_LOGE(TAG, "TFT upload unavailable or already running");
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
    wake_transport_();
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
  auto *idf = idf_uart_();
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
      if (n < 0) {
        ESP_LOGE(TAG, "TFT range read failed at %u", unsigned(start));
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        return false;
      }
      if (n > 0)
        read += n;
      else
        vTaskDelay(pdMS_TO_TICKS(1));
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
bool NSPanelDisplay::start_upload_task_() {
  if (upload_running_ || upload_finished_) return false;
  upload_running_ = true;
  // The upload runs in its own task so the ESPHome main loop can service the
  // API and OTA during the transfer. The transport task is already parked in
  // STOPPED and does not touch the UART.
  if (xTaskCreatePinnedToCore(upload_task_entry_, "nspanel_tft", 8192, this, 1, nullptr, 0) != pdPASS) {
    upload_running_ = false;
    return false;
  }
  return true;
}
void NSPanelDisplay::upload_task_entry_(void *arg) {
  auto *self = static_cast<NSPanelDisplay *>(arg);
  const bool ok = self->upload_tft_();
  self->upload_ok_ = ok;
  self->upload_finished_ = true;
  self->upload_running_ = false;
  vTaskDelete(nullptr);
}
void NSPanelDisplay::loop() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (upload_finished_) {
    upload_finished_ = false;
    const bool ok = upload_ok_;
    ESP_LOGI(TAG, "TFT upload %s", ok ? "complete" : "failed");
    brightness_pending_ = true;  // Upload wakes the display at 100%; restore the selected level.
    brightness_resume_ = true;
    state_ = State::BOOT_WAIT;
    exit_requested_ = false;
    deadline_ = millis() + (ok ? 3000 : 1000);
    return;
  }
  if (upload_running_) return;
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
  if (repaint_pending_ && state_ == State::STREAM && !refresh_active_ && !lvgl_refreshing_) {
    // A dropped flush area or job wait must not leave stale pixels until the
    // next unrelated redraw. Re-invalidate once the current refresh finishes.
    repaint_pending_ = false;
    request_repaint_();
  }
  if (touchscreen_) {
    if (touch_reset_pending_) {
      touchscreen_->reset_contacts();
      touch_reset_pending_ = false;
    }
    TouchSample sample;
    if (pending_touches_.pop(sample) && !touchscreen_->new_report(sample.down, sample.x, sample.y)) {
      ++dropped_touches_;
      if ((dropped_touches_ & 0x3f) == 1)
        ESP_LOGW(TAG, "Dropped invalid or queued touch sample (%u total)", dropped_touches_);
    }
  }
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
      if (!job_storage_ && !init_frame_jobs_()) {
        fail_("frame slots");
        break;
      }
      if (!change_baud_(4000000)) {
        fail_("baud switch");
        break;
      }
      stream_.begin(session_, now, width_, height_);
      if (exit_requested_) stream_.request_exit();
      if (repaint_pending_) {
        // The mirror is gone, so re-entering the stream needs a full LVGL
        // repaint; the flush areas are encoded and queued as they arrive.
        request_repaint_();
        repaint_pending_ = false;
      }
      state_ = State::STREAM;
      break;
    case State::STREAM:
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
          ESP_LOGI(TAG, "Verified terminal identity, baud, saved registers and page after exit");
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
        if (!start_upload_task_()) {
          ESP_LOGE(TAG, "TFT upload task creation failed");
          brightness_pending_ = true;
          brightness_resume_ = true;
          state_ = State::BOOT_WAIT;
          exit_requested_ = false;
          deadline_ = millis() + 1000;
        }
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
  wake_transport_();
  signal_slot_();
  while (task_running_) vTaskDelay(1);
  while (upload_running_) {
    App.feed_wdt();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
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
