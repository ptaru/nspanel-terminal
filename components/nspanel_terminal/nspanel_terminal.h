#pragma once
#include "frame_slot.h"
#include <array>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/helpers.h"
#include "stream_engine.h"

namespace esphome::uart {
class IDFUARTComponent;
}

namespace esphome::nspanel_terminal {
class NSPanelTouchscreen;
class NSPanelDisplay : public display::DisplayBuffer, public uart::UARTDevice {
 public:
  ~NSPanelDisplay();
  void setup() override;
  void loop() override;
  void update() override;
  void draw_pixels_at(int, int, int, int, const uint8_t *, display::ColorOrder, display::ColorBitness, bool, int, int,
                      int) override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_COLOR; }
  void request_exit() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    exit_requested_ = true;
    stream_.request_exit();
    wake_transport_();
  }
  void request_enter();
  void request_tft_upload();
  void request_brightness(int percent);
  void set_initial_brightness(uint8_t percent) {
    initial_brightness_ = percent;
    brightness_percent_ = percent;
    brightness_selected_ = true;
  }
  void set_tft_url(const std::string &url) { tft_url_ = url; }
  // false keeps the four encoded frame slots in internal RAM (with an external
  // fallback); true allocates them in PSRAM and requires the psram component.
  void set_frame_slots_ram(bool psram) { frame_slots_psram_ = psram; }
  void set_panel_size(uint16_t width, uint16_t height) {
    width_ = width;
    height_ = height;
  }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  void set_touchscreen(NSPanelTouchscreen *t) { touchscreen_ = t; }
  void request_full_redraw() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    request_repaint_();
  }
  void set_refresh_active(bool active) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    refresh_active_ = active;
  }
  std::recursive_mutex &transport_mutex() { return mutex_; }

 protected:
  int get_width_internal() override { return width_; }
  int get_height_internal() override { return height_; }
  void draw_absolute_pixel_internal(int, int, Color) override;

 private:
  enum class State {
    BOOT_WAIT,
    CONNECT,
    PAGE,
    BKCMD,
    SYS0,
    SYS1,
    SYS2,
    CONFIG,
    SWITCHING,
    STREAM,
    RECOVER_WAIT,
    RECOVER_CONFIG,
    RECOVER_BAUD,
    VERIFY_SYS0,
    VERIFY_SYS1,
    VERIFY_SYS2,
    VERIFY_BKCMD,
    VERIFY_PAGE,
    VERIFY_IDENTITY,
    BRIGHTNESS_SETTLE,
    BRIGHTNESS_VERIFY,
    STOPPED
  };
  // Flush areas are PackBits-encoded into these slots; the mirror is gone.
  static constexpr unsigned JOB_ROWS = 16;
  static constexpr unsigned JOB_SLOTS = 4;
  struct FrameJob : FrameSlot {
    uint8_t *cache = nullptr;
    size_t size = 0;
    uint16_t x = 0, y = 0, w = 0, h = 0;
  };
  std::array<FrameJob, JOB_SLOTS> jobs_{};
  uint8_t *job_storage_ = nullptr;
  size_t job_capacity_ = 0;
  unsigned job_queue_[JOB_SLOTS]{};
  unsigned job_head_ = 0, job_tail_ = 0, job_queued_ = 0;
  FrameJob *active_job_ = nullptr;
  uint64_t job_generation_ = 0;
  bool repaint_pending_ = true;
  bool unsupported_logged_ = false;
  bool frame_slots_internal_ = false;
  bool frame_slots_psram_ = false;
  uart::IDFUARTComponent *idf_uart_() const;
  void send_command_(const char *);
  void send_query_(State, const char *);
  bool pump_command_();
  bool read_fixed_(size_t);
  bool read_terminated_();
  void drain_();
  bool change_baud_(uint32_t);
  bool identity_ok_(const char *reply) const;
  void fail_(const char *);
  void finish_recovery_();
  void wake_transport_() { if (work_signal_ != nullptr) xSemaphoreGive(work_signal_); }
  void signal_slot_() { if (slot_signal_ != nullptr) xSemaphoreGive(slot_signal_); }
  StaticSemaphore_t work_signal_storage_{};
  SemaphoreHandle_t work_signal_ = nullptr;
  StaticSemaphore_t slot_signal_storage_{};
  SemaphoreHandle_t slot_signal_ = nullptr;
  void service_stream_(int incoming = -1);
  bool init_frame_jobs_();
  FrameJob *acquire_job_(std::unique_lock<std::recursive_mutex> &lock);
  void enqueue_job_(FrameJob *job);
  FrameJob *dequeue_job_();
  void release_active_job_();
  void clear_jobs_();
  bool job_queue_empty_() const { return job_queued_ == 0; }
  void request_repaint_();
  void drop_frame_(const char *reason);
  void invalidate_screen_();
  bool upload_tft_();
  bool upload_ack_(uint32_t &position, uint32_t expected);
  bool start_upload_task_();
  static void upload_task_entry_(void *arg);
  State state_ = State::BOOT_WAIT;
  uint32_t deadline_ = 0, next_command_byte_ = 0;
  uint8_t config_step_ = 0, recovery_step_ = 0, page_ = 0, saved_bkcmd_ = 3;
  uint32_t saved_sys_[3]{};
  uint16_t session_ = 0;
  bool snapshots_ready_ = false, exit_requested_ = false, terminal_active_ = false, restore_page_ = false;
  uint8_t startup_attempts_ = 0;
  bool startup_retry_ = true;
  std::array<uint8_t, 128> rx_{};
  size_t rx_count_ = 0;
  std::vector<uint8_t> command_;
  size_t command_at_ = 0;
  uint16_t width_ = 454, height_ = 320;
  DirtyRows dirty_;
  StreamEngine stream_;
  HighFrequencyLoopRequester high_frequency_;
  NSPanelTouchscreen *touchscreen_ = nullptr;
  TouchQueue pending_touches_;
  bool touch_reset_pending_ = false;
  unsigned dropped_touches_ = 0, dropped_frames_ = 0;
  bool upload_pending_ = false;
  int16_t initial_brightness_ = -1;
  uint8_t brightness_percent_ = 100;
  bool brightness_selected_ = false;
  bool brightness_pending_ = false, brightness_resume_ = false;
  std::string tft_url_;
  // Track LVGL refreshes to gate frame snapshots until a refresh completes.
  void on_lvgl_refresh_start_();
  void on_lvgl_refresh_ready_();
  static void task_entry_(void *arg);
  void task_loop_();
  mutable std::recursive_mutex mutex_;
  std::atomic<bool> task_stop_{false};
  std::atomic<bool> task_running_{false};
  std::atomic<bool> upload_running_{false};
  std::atomic<bool> upload_finished_{false};
  std::atomic<bool> upload_ok_{false};
  TaskHandle_t task_ = nullptr;
  bool refresh_active_ = false;
  bool lvgl_refreshing_ = false;
  bool refresh_hooks_installed_ = false;
};
}  // namespace esphome::nspanel_terminal
