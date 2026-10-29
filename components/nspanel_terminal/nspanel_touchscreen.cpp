#include "nspanel_touchscreen.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "nspanel_terminal.h"

namespace esphome::nspanel_terminal {

void NSPanelTouchscreen::set_panel(NSPanelDisplay *panel) {
  this->panel_ = panel;
  panel->set_touchscreen(this);
}

bool NSPanelTouchscreen::new_report(bool down, uint16_t x, uint16_t y) {
  if (down && (x >= 454 || y >= 320)) return false;
  ++edges_;
  if (down != last_reported_) {
    last_reported_ = down;
    if (panel_) panel_->trace_edge(down, x, y);
  }
  if (!queue_.push(down, x, y)) {
    ++drops_;
    return false;
  }
  store_.touched = true;
  return true;
}

void NSPanelTouchscreen::update_touches() {
  queue_.pop(current_);
  if (current_.down) add_raw_touch_position_(0, current_.x, current_.y);
  if (!queue_.empty()) store_.touched = true;
}

void NSPanelTouchscreen::dump_config() {
  ESP_LOGCONFIG("nspanel_terminal", "NSPanel V92 protocol6 touchscreen, single pointer");
}

}  // namespace esphome::nspanel_terminal
