#pragma once

#include <cstdint>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

#ifdef USE_FONT
#include "esphome/components/font/font.h"
#endif

namespace esphome {
namespace button_keyboard {

enum class KeyboardMode : uint8_t {
  TEXT_LOWER = 0,
  TEXT_UPPER = 1,
  SPECIAL = 2,
  NUMBER = 3,
};

enum class KeyAction : uint8_t {
  NONE,
  INSERT,
  BACKSPACE,
  NEWLINE,
  LEFT,
  RIGHT,
  SIGN,
  MODE_LOWER,
  MODE_UPPER,
  MODE_SPECIAL,
  MODE_NUMBER,
  READY,
  CANCEL,
};

struct KeyDef {
  const char *label;
  uint8_t weight;
  KeyAction action;
  const char *text;
};

class ButtonKeyboard : public Component {
 public:
  void set_page(lvgl::LvPageType *page) { this->page_ = page; }
  void set_textarea(lv_obj_t *textarea);
  void set_bounds(int x, int y, int width, int height) {
    this->x_ = x;
    this->y_ = y;
    this->width_ = width;
    this->height_ = height;
  }
  void set_key_gap(int gap) { this->gap_ = gap; }
  void set_repeat_delay(uint32_t delay) {
    this->repeat_delay_ = delay < 50 ? 50 : delay;
  }
  void set_repeat_interval(uint32_t interval) {
    this->repeat_interval_ = interval < 20 ? 20 : interval;
  }
  void set_mode(uint8_t mode);
  void set_background_style(lv_style_t *style) { this->background_style_ = style; }
  void set_key_style(lv_style_t *style) { this->key_style_ = style; }
  void set_key_pressed_style(lv_style_t *style) { this->key_pressed_style_ = style; }
  void set_character_key_style(lv_style_t *style) { this->character_key_style_ = style; }
  void set_character_key_pressed_style(lv_style_t *style) {
    this->character_key_pressed_style_ = style;
  }
  void set_text_color(lv_color_t color) {
    this->text_color_ = color;
    this->has_text_color_ = true;
  }
#ifdef USE_FONT
  void set_font(font::Font *font) { this->font_ = font; }
#endif
  void set_ready_trigger(Trigger<> *trigger) { this->ready_trigger_ = trigger; }
  void set_cancel_trigger(Trigger<> *trigger) { this->cancel_trigger_ = trigger; }

  // Press a key by its row/column slot. The touch path calls the same
  // handler; this is also useful for scripted tests and accessibility.
  void press_key(uint8_t row, uint8_t col) { this->on_key_(row, col); }
  uint8_t get_mode() const { return this->mode_; }
  lv_obj_t *get_key_button(uint8_t row, uint8_t col) const;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 10.0f; }

 protected:
  static constexpr uint8_t ROWS = 4;
  static constexpr uint8_t MAX_COLS = 12;

  struct KeyBinding {
    ButtonKeyboard *self;
    uint8_t row;
    uint8_t col;
  };

  void create_keys_();
  void apply_mode_();
  void apply_key_style_(uint8_t row, uint8_t col, const KeyDef &key);
  static bool is_character_key_(const KeyDef &key);
  void layout_();
  void on_key_(uint8_t row, uint8_t col);
  void on_press_(uint8_t row, uint8_t col);
  void stop_repeat_();
  static bool is_repeatable_(KeyAction action);
  void fire_ready_();
  void fire_cancel_();
  uint16_t key_count_() const;

  lvgl::LvPageType *page_{nullptr};
  lv_obj_t *textarea_{nullptr};
  lv_obj_t *container_{nullptr};
  lv_obj_t *buttons_[ROWS][MAX_COLS]{};
  lv_obj_t *labels_[ROWS][MAX_COLS]{};
  KeyBinding bindings_[ROWS][MAX_COLS]{};
  lv_style_t *background_style_{nullptr};
  lv_style_t *key_style_{nullptr};
  lv_style_t *key_pressed_style_{nullptr};
  lv_style_t *character_key_style_{nullptr};
  lv_style_t *character_key_pressed_style_{nullptr};
  lv_style_t dim_style_{};
#ifdef USE_FONT
  font::Font *font_{nullptr};
#endif
  Trigger<> *ready_trigger_{nullptr};
  Trigger<> *cancel_trigger_{nullptr};

  int x_{0};
  int y_{0};
  int width_{0};
  int height_{0};
  int gap_{2};
  uint8_t mode_{0};
  bool built_{false};
  bool repeat_active_{false};
  uint8_t repeat_row_{0};
  uint8_t repeat_col_{0};
  uint32_t repeat_delay_{400};
  uint32_t repeat_interval_{100};
  uint32_t repeat_at_{0};
  bool has_text_color_{false};
  lv_color_t text_color_{};
};

template<typename... Ts>
class ButtonKeyboardSetModeAction : public Action<Ts...> {
 public:
  explicit ButtonKeyboardSetModeAction(ButtonKeyboard *parent, uint8_t mode)
      : parent_(parent), mode_(mode) {}

  void play(Ts...) override { this->parent_->set_mode(this->mode_); }

 protected:
  ButtonKeyboard *parent_;
  uint8_t mode_;
};

}  // namespace button_keyboard
}  // namespace esphome
