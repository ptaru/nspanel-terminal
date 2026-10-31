#pragma once
#include <array>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/helpers.h"
#include "stream_engine.h"
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
    cancel_render_benchmark_("exit requested");
    exit_requested_ = true;
    stream_.request_exit();
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
  void set_panel_size(uint16_t width, uint16_t height) {
    width_ = width;
    height_ = height;
  }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  // Diagnostic: stop all stream records without EXIT so the display watchdog runs.
  void request_abandon();
  void set_touchscreen(NSPanelTouchscreen *t) { touchscreen_ = t; }
  // Diagnostic instrumentation; off by default so production images stay quiet.
  void set_diagnostics(bool enabled) { std::lock_guard<std::recursive_mutex> lock(mutex_); diagnostics_ = enabled; }
  bool diagnostics() const { return diagnostics_; }
  void request_full_redraw() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    request_repaint_();
  }
  void set_refresh_active(bool active) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    refresh_active_ = active;
  }
  std::recursive_mutex &transport_mutex() { return mutex_; }
  unsigned completed_rectangles() const { return stream_.completed(); }
  uint32_t bytes_sent() const { return stream_.sent_bytes(); }
  unsigned heartbeats() const { return stream_.heartbeats(); }
  unsigned resyncs() const { return resyncs_; }
  unsigned faults() const { return faults_; }
  unsigned loops() const { return loops_; }
  unsigned max_loop_gap_ms() const { return max_gap_us_ / 1000; }
  // RAM trace avoids per-event logging during a measurement.
  void trace_edge(bool down, uint16_t x, uint16_t y);
  void clear_trace() { trace_head_ = trace_count_ = 0; }
  void log_timings();
  void start_render_measurement();
  void report_render_measurement();
  // One action, N full-screen LVGL redraws through the normal dirty-rectangle
  // path. Each repeat is timed from the refresh start to the final DONE.
  void start_render_benchmark(unsigned repeats = 5);
  void start_protocol_benchmark();
  void start_legacy_protocol_benchmark();
  void start_legacy_protocol_benchmark_reuse();
  void start_legacy_protocol_benchmark_preencoded();

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
  struct FrameJob {
    uint8_t *cache = nullptr;
    size_t size = 0;
    uint16_t x = 0, y = 0, w = 0, h = 0;
    uint8_t state = 0;  // 0 free, nonzero reserved/queued/active
  };
  std::array<FrameJob, JOB_SLOTS> jobs_{};
  uint8_t *job_storage_ = nullptr;
  size_t job_capacity_ = 0;
  unsigned job_queue_[JOB_SLOTS]{};
  unsigned job_head_ = 0, job_tail_ = 0, job_queued_ = 0;
  FrameJob *active_job_ = nullptr;
  bool repaint_pending_ = true;
  bool unsupported_logged_ = false;
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
  void service_stream_(int incoming = -1);
  bool init_frame_jobs_();
  FrameJob *acquire_job_(std::unique_lock<std::recursive_mutex> &lock);
  void enqueue_job_(FrameJob *job);
  FrameJob *dequeue_job_();
  void release_active_job_();
  void clear_jobs_();
  bool job_queue_empty_() const { return job_queued_ == 0; }
  void request_repaint_();
  void invalidate_screen_();
  bool upload_tft_();
  bool upload_ack_(uint32_t &position, uint32_t expected);
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
  bool diagnostics_ = false;
  unsigned resyncs_ = 0, faults_ = 0;
  uint32_t submit_ms_ = 0, last_loop_us_ = 0, max_gap_us_ = 0, abandon_deadline_ = 0;
  bool abandon_requested_ = false;
  bool upload_pending_ = false;
  int16_t initial_brightness_ = -1;
  uint8_t brightness_percent_ = 100;
  bool brightness_selected_ = false;
  bool brightness_pending_ = false, brightness_resume_ = false;
  std::string tft_url_;
  unsigned loops_ = 0;
  struct TraceEvent {
    uint32_t t;
    uint16_t a, b, c, d;
    uint8_t type;
  };
  std::array<TraceEvent, 96> trace_buf_{};
  unsigned trace_head_ = 0, trace_count_ = 0;
  uint32_t measurement_bytes_ = 0, measurement_started_ms_ = 0;
  uint32_t measurement_started_us_ = 0, measurement_first_submit_us_ = 0, measurement_last_done_us_ = 0;
  unsigned measurement_rectangles_ = 0, measurement_faults_ = 0, measurement_resyncs_ = 0;
  bool measurement_active_ = false;
  bool benchmark_active_ = false;
  bool benchmark_legacy_ = false;
  bool benchmark_reuse_ = false;
  bool benchmark_preencoded_ = false;
  uint8_t *benchmark_cache_ = nullptr;
  size_t benchmark_cache_size_ = 0;
  uint8_t *benchmark_snapshot_ = nullptr;
  unsigned benchmark_mode_ = 0, benchmark_next_ = 0;
  uint32_t benchmark_started_ms_ = 0, benchmark_bytes_ = 0;
  unsigned benchmark_rectangles_ = 0, benchmark_faults_ = 0, benchmark_resyncs_ = 0;
  uint32_t benchmark_build_us_ = 0, benchmark_encode_us_ = 0;
  unsigned benchmark_service_calls_ = 0, benchmark_credit_stalls_ = 0, benchmark_uart_stalls_ = 0;
  unsigned benchmark_reports_ = 0, benchmark_highwater_ = 0;
  unsigned benchmark_idle_credit_stalls_ = 0;
  uint32_t benchmark_write_us_ = 0;
  struct RenderStats {
    uint32_t total_us = 0, refresh_us = 0, transfer_us = 0;
    uint32_t copy_us = 0, encode_us = 0, write_us = 0;
    uint32_t credit_wait_us = 0, uart_wait_us = 0, done_wait_us = 0;
    uint32_t bytes = 0;
    uint16_t rectangles = 0, credit_stalls = 0, uart_stalls = 0;
    uint16_t service_calls = 0, reports = 0, faults = 0, resyncs = 0;
  };
  void begin_render_repeat_();
  void finish_render_repeat_();
  void log_render_summary_();
  void close_render_wait_();
  void cancel_render_benchmark_(const char *reason);
  // Track the LVGL refresh independently of the snapshot gate. draw_pixels_at()
  // starts the gate on the first flush, but only while this is true, so a direct
  // bulk draw outside a refresh cannot leave refresh_active_ stuck.
  void on_lvgl_refresh_start_();
  void on_lvgl_refresh_ready_();
  bool render_benchmark_active_ = false, render_start_pending_ = false;
  bool render_repeat_active_ = false, render_repeat_complete_ = false;
  bool render_wait_refresh_ = false;
  uint32_t render_refresh_started_us_ = 0, render_refresh_deadline_ms_ = 0;
  unsigned render_repeats_ = 0, render_index_ = 0;
  uint32_t render_repeat_started_us_ = 0, render_first_write_us_ = 0, render_last_write_us_ = 0;
  uint32_t render_bytes_start_ = 0;
  unsigned render_rect_start_ = 0, render_fault_start_ = 0, render_resync_start_ = 0;
  uint8_t render_wait_kind_ = 0;
  uint32_t render_wait_started_us_ = 0;
  RenderStats render_stats_{};
  std::array<RenderStats, 8> render_results_{};
  void benchmark_report_();
  void trace_(uint8_t type, uint16_t a = 0, uint16_t b = 0, uint16_t c = 0, uint16_t d = 0);
  static void task_entry_(void *arg);
  void task_loop_();
  mutable std::recursive_mutex mutex_;
  std::atomic<bool> task_stop_{false};
  std::atomic<bool> task_running_{false};
  TaskHandle_t task_ = nullptr;
  bool refresh_active_ = false;
  bool lvgl_refreshing_ = false;
  bool refresh_hooks_installed_ = false;
};
}  // namespace esphome::nspanel_terminal
