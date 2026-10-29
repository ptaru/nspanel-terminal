#pragma once
#include "esphome/components/touchscreen/touchscreen.h"
#include "stream_engine.h"
namespace esphome::nspanel_terminal {
class NSPanelDisplay;
class NSPanelTouchscreen : public touchscreen::Touchscreen {
 public:
  void set_panel(NSPanelDisplay* panel);
  bool new_report(bool down, uint16_t x, uint16_t y);
  unsigned touch_edges() const { return edges_; }
  unsigned touch_drops() const { return drops_; }
  void reset_contacts() {
    queue_ = TouchQueue();
    current_ = {};
    last_reported_ = false;
    store_.touched = true;
  }
  void dump_config() override;

 protected:
  void update_touches() override;

 private:
  NSPanelDisplay* panel_ = nullptr;
  TouchQueue queue_;
  TouchSample current_;
  unsigned edges_ = 0, drops_ = 0;
  bool last_reported_ = false;
};
}  // namespace esphome::nspanel_terminal
