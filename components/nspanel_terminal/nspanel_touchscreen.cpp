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
  if (down && panel_ && (x >= panel_->width() || y >= panel_->height())) return false;
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
  std::lock_guard<std::recursive_mutex> lock(panel_->transport_mutex());
  queue_.pop(current_);
  if (current_.down) add_raw_touch_position_(0, current_.x, current_.y);
  if (!queue_.empty()) store_.touched = true;
}

void NSPanelTouchscreen::dump_config() {
  ESP_LOGCONFIG("nspanel_terminal", "NSPanel protocol6 touchscreen, single pointer");
}

}  // namespace esphome::nspanel_terminal
