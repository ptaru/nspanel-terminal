#include "button_keyboard.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace button_keyboard {

static const char *const TAG = "button_keyboard";
static constexpr lv_style_selector_t PRESSED_SELECTOR =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_PRESSED);

// Key maps follow LVGL 9.5's lv_keyboard default maps. The weights are the
// relative key widths from lv_keyboard.c; each row is scaled to the container
// width independently, so rows with fewer keys get wider keys.
static const KeyDef LOWER_ROW_0[] = {
    {"1#", 5, KeyAction::MODE_SPECIAL, nullptr},
    {"q", 4, KeyAction::INSERT, nullptr},
    {"w", 4, KeyAction::INSERT, nullptr},
    {"e", 4, KeyAction::INSERT, nullptr},
    {"r", 4, KeyAction::INSERT, nullptr},
    {"t", 4, KeyAction::INSERT, nullptr},
    {"y", 4, KeyAction::INSERT, nullptr},
    {"u", 4, KeyAction::INSERT, nullptr},
    {"i", 4, KeyAction::INSERT, nullptr},
    {"o", 4, KeyAction::INSERT, nullptr},
    {"p", 4, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_BACKSPACE, 7, KeyAction::BACKSPACE, nullptr},
};

static const KeyDef LOWER_ROW_1[] = {
    {"ABC", 6, KeyAction::MODE_UPPER, nullptr},
    {"a", 3, KeyAction::INSERT, nullptr},
    {"s", 3, KeyAction::INSERT, nullptr},
    {"d", 3, KeyAction::INSERT, nullptr},
    {"f", 3, KeyAction::INSERT, nullptr},
    {"g", 3, KeyAction::INSERT, nullptr},
    {"h", 3, KeyAction::INSERT, nullptr},
    {"j", 3, KeyAction::INSERT, nullptr},
    {"k", 3, KeyAction::INSERT, nullptr},
    {"l", 3, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_NEW_LINE, 7, KeyAction::NEWLINE, nullptr},
};

static const KeyDef LOWER_ROW_2[] = {
    {"_", 1, KeyAction::INSERT, nullptr},
    {"-", 1, KeyAction::INSERT, nullptr},
    {"z", 1, KeyAction::INSERT, nullptr},
    {"x", 1, KeyAction::INSERT, nullptr},
    {"c", 1, KeyAction::INSERT, nullptr},
    {"v", 1, KeyAction::INSERT, nullptr},
    {"b", 1, KeyAction::INSERT, nullptr},
    {"n", 1, KeyAction::INSERT, nullptr},
    {"m", 1, KeyAction::INSERT, nullptr},
    {".", 1, KeyAction::INSERT, nullptr},
    {",", 1, KeyAction::INSERT, nullptr},
    {":", 1, KeyAction::INSERT, nullptr},
};

static const KeyDef LOWER_ROW_3[] = {
    {LV_SYMBOL_CLOSE, 2, KeyAction::CANCEL, nullptr},
    {LV_SYMBOL_LEFT, 2, KeyAction::LEFT, nullptr},
    {"Space", 6, KeyAction::INSERT, " "},
    {LV_SYMBOL_RIGHT, 2, KeyAction::RIGHT, nullptr},
    {LV_SYMBOL_OK, 2, KeyAction::READY, nullptr},
};

static const KeyDef UPPER_ROW_0[] = {
    {"1#", 5, KeyAction::MODE_SPECIAL, nullptr},
    {"Q", 4, KeyAction::INSERT, nullptr},
    {"W", 4, KeyAction::INSERT, nullptr},
    {"E", 4, KeyAction::INSERT, nullptr},
    {"R", 4, KeyAction::INSERT, nullptr},
    {"T", 4, KeyAction::INSERT, nullptr},
    {"Y", 4, KeyAction::INSERT, nullptr},
    {"U", 4, KeyAction::INSERT, nullptr},
    {"I", 4, KeyAction::INSERT, nullptr},
    {"O", 4, KeyAction::INSERT, nullptr},
    {"P", 4, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_BACKSPACE, 7, KeyAction::BACKSPACE, nullptr},
};

static const KeyDef UPPER_ROW_1[] = {
    {"abc", 6, KeyAction::MODE_LOWER, nullptr},
    {"A", 3, KeyAction::INSERT, nullptr},
    {"S", 3, KeyAction::INSERT, nullptr},
    {"D", 3, KeyAction::INSERT, nullptr},
    {"F", 3, KeyAction::INSERT, nullptr},
    {"G", 3, KeyAction::INSERT, nullptr},
    {"H", 3, KeyAction::INSERT, nullptr},
    {"J", 3, KeyAction::INSERT, nullptr},
    {"K", 3, KeyAction::INSERT, nullptr},
    {"L", 3, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_NEW_LINE, 7, KeyAction::NEWLINE, nullptr},
};

static const KeyDef UPPER_ROW_2[] = {
    {"_", 1, KeyAction::INSERT, nullptr},
    {"-", 1, KeyAction::INSERT, nullptr},
    {"Z", 1, KeyAction::INSERT, nullptr},
    {"X", 1, KeyAction::INSERT, nullptr},
    {"C", 1, KeyAction::INSERT, nullptr},
    {"V", 1, KeyAction::INSERT, nullptr},
    {"B", 1, KeyAction::INSERT, nullptr},
    {"N", 1, KeyAction::INSERT, nullptr},
    {"M", 1, KeyAction::INSERT, nullptr},
    {".", 1, KeyAction::INSERT, nullptr},
    {",", 1, KeyAction::INSERT, nullptr},
    {":", 1, KeyAction::INSERT, nullptr},
};

static const KeyDef SPECIAL_ROW_0[] = {
    {"1", 1, KeyAction::INSERT, nullptr},
    {"2", 1, KeyAction::INSERT, nullptr},
    {"3", 1, KeyAction::INSERT, nullptr},
    {"4", 1, KeyAction::INSERT, nullptr},
    {"5", 1, KeyAction::INSERT, nullptr},
    {"6", 1, KeyAction::INSERT, nullptr},
    {"7", 1, KeyAction::INSERT, nullptr},
    {"8", 1, KeyAction::INSERT, nullptr},
    {"9", 1, KeyAction::INSERT, nullptr},
    {"0", 1, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_BACKSPACE, 2, KeyAction::BACKSPACE, nullptr},
};

static const KeyDef SPECIAL_ROW_1[] = {
    {"abc", 2, KeyAction::MODE_LOWER, nullptr},
    {"+", 1, KeyAction::INSERT, nullptr},
    {"&", 1, KeyAction::INSERT, nullptr},
    {"/", 1, KeyAction::INSERT, nullptr},
    {"*", 1, KeyAction::INSERT, nullptr},
    {"=", 1, KeyAction::INSERT, nullptr},
    {"%", 1, KeyAction::INSERT, nullptr},
    {"!", 1, KeyAction::INSERT, nullptr},
    {"?", 1, KeyAction::INSERT, nullptr},
    {"#", 1, KeyAction::INSERT, nullptr},
    {"<", 1, KeyAction::INSERT, nullptr},
    {">", 1, KeyAction::INSERT, nullptr},
};

static const KeyDef SPECIAL_ROW_2[] = {
    {"\\", 1, KeyAction::INSERT, nullptr},
    {"@", 1, KeyAction::INSERT, nullptr},
    {"$", 1, KeyAction::INSERT, nullptr},
    {"(", 1, KeyAction::INSERT, nullptr},
    {")", 1, KeyAction::INSERT, nullptr},
    {"{", 1, KeyAction::INSERT, nullptr},
    {"}", 1, KeyAction::INSERT, nullptr},
    {"[", 1, KeyAction::INSERT, nullptr},
    {"]", 1, KeyAction::INSERT, nullptr},
    {";", 1, KeyAction::INSERT, nullptr},
    {"\"", 1, KeyAction::INSERT, nullptr},
    {"'", 1, KeyAction::INSERT, nullptr},
};

static const KeyDef NUMBER_ROW_0[] = {
    {"1", 1, KeyAction::INSERT, nullptr},
    {"2", 1, KeyAction::INSERT, nullptr},
    {"3", 1, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_CLOSE, 2, KeyAction::CANCEL, nullptr},
};

static const KeyDef NUMBER_ROW_1[] = {
    {"4", 1, KeyAction::INSERT, nullptr},
    {"5", 1, KeyAction::INSERT, nullptr},
    {"6", 1, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_OK, 2, KeyAction::READY, nullptr},
};

static const KeyDef NUMBER_ROW_2[] = {
    {"7", 1, KeyAction::INSERT, nullptr},
    {"8", 1, KeyAction::INSERT, nullptr},
    {"9", 1, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_BACKSPACE, 2, KeyAction::BACKSPACE, nullptr},
};

static const KeyDef NUMBER_ROW_3[] = {
    {"+/-", 1, KeyAction::SIGN, nullptr},
    {"0", 1, KeyAction::INSERT, nullptr},
    {".", 1, KeyAction::INSERT, nullptr},
    {LV_SYMBOL_LEFT, 1, KeyAction::LEFT, nullptr},
    {LV_SYMBOL_RIGHT, 1, KeyAction::RIGHT, nullptr},
};

struct ModeLayout {
  const KeyDef *rows[4];
  uint8_t counts[4];
};

static const ModeLayout LAYOUTS[4] = {
    {{LOWER_ROW_0, LOWER_ROW_1, LOWER_ROW_2, LOWER_ROW_3}, {12, 11, 12, 5}},
    {{UPPER_ROW_0, UPPER_ROW_1, UPPER_ROW_2, LOWER_ROW_3}, {12, 11, 12, 5}},
    {{SPECIAL_ROW_0, SPECIAL_ROW_1, SPECIAL_ROW_2, LOWER_ROW_3}, {11, 12, 12, 5}},
    {{NUMBER_ROW_0, NUMBER_ROW_1, NUMBER_ROW_2, NUMBER_ROW_3}, {4, 4, 4, 5}},
};

[[maybe_unused]] static const char *const MODE_NAMES[4] = {"TEXT_LOWER", "TEXT_UPPER", "SPECIAL", "NUMBER"};
// The maximum number of used slots in each row across all four modes.
static const uint8_t MAX_SLOTS[4] = {12, 12, 12, 5};

uint16_t ButtonKeyboard::key_count_() const {
  const ModeLayout &layout = LAYOUTS[this->mode_];
  uint16_t count = 0;
  for (uint8_t row = 0; row < ROWS; row++) {
    count += layout.counts[row];
  }
  return count;
}

void ButtonKeyboard::setup() {
  if (this->page_ == nullptr || this->page_->obj == nullptr) {
    ESP_LOGE(TAG, "No page object for the keyboard; has the page been created?");
    this->mark_failed();
    return;
  }

  this->container_ = lv_obj_create(this->page_->obj);
  if (this->container_ == nullptr) {
    ESP_LOGE(TAG, "Could not create the keyboard container");
    this->mark_failed();
    return;
  }
  lv_obj_remove_style_all(this->container_);
  if (this->background_style_ != nullptr) {
    lv_obj_add_style(this->container_, this->background_style_, LV_PART_MAIN);
  }
  // The container style is decorative. Do not let its padding shift the keys.
  lv_obj_set_style_pad_all(this->container_, 0, LV_PART_MAIN);
  lv_obj_set_pos(this->container_, this->x_, this->y_);
  lv_obj_set_size(this->container_, this->width_, this->height_);
  lv_obj_clear_flag(this->container_, LV_OBJ_FLAG_SCROLLABLE);
  // Clicking the keyboard must not move LVGL's focus away from the textarea.
  lv_obj_clear_flag(this->container_, LV_OBJ_FLAG_CLICK_FOCUSABLE);

  lv_style_init(&this->dim_style_);
  lv_style_set_opa(&this->dim_style_, LV_OPA_70);

  this->create_keys_();
  this->built_ = true;
  this->apply_mode_();
  this->set_textarea(this->textarea_);

  ESP_LOGD(TAG, "Created %u key buttons on a %dx%d container", (unsigned) this->key_count_(), this->width_,
           this->height_);
}

void ButtonKeyboard::create_keys_() {
  for (uint8_t row = 0; row < ROWS; row++) {
    for (uint8_t col = 0; col < MAX_SLOTS[row]; col++) {
      lv_obj_t *button = lv_button_create(this->container_);
      lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
      // A key press must not defocus the textarea; the caret would vanish even
      // though the component still inserts text through the textarea pointer.
      lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
      // Start from no style: character keys stay transparent and action keys
      // receive the configured key style when the mode is applied.
      lv_obj_remove_style_all(button);
      lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

      lv_obj_t *label = lv_label_create(button);
      lv_obj_set_width(label, LV_PCT(100));
      lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
      if (this->has_text_color_) {
        lv_obj_set_style_text_color(label, this->text_color_, LV_PART_MAIN);
      }

      this->bindings_[row][col] = {this, row, col};
      lv_obj_add_event_cb(
          button,
          [](lv_event_t *event) {
            auto *binding = static_cast<KeyBinding *>(lv_event_get_user_data(event));
            const lv_event_code_t code = lv_event_get_code(event);
            if (code == LV_EVENT_PRESSED) {
              binding->self->on_press_(binding->row, binding->col);
            } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
              binding->self->stop_repeat_();
            }
          },
          LV_EVENT_ALL, &this->bindings_[row][col]);

      this->buttons_[row][col] = button;
      this->labels_[row][col] = label;
    }
  }
}

void ButtonKeyboard::apply_mode_() {
  const ModeLayout &layout = LAYOUTS[this->mode_];
  for (uint8_t row = 0; row < ROWS; row++) {
    for (uint8_t col = 0; col < MAX_SLOTS[row]; col++) {
      if (col < layout.counts[row]) {
        const KeyDef &key = layout.rows[row][col];
        lv_label_set_text(this->labels_[row][col], key.label);
        this->apply_key_style_(row, col, key);
        lv_obj_clear_flag(this->buttons_[row][col], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(this->buttons_[row][col], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  this->layout_();
}

void ButtonKeyboard::apply_key_style_(uint8_t row, uint8_t col, const KeyDef &key) {
  lv_obj_t *button = this->buttons_[row][col];
  if (this->key_style_ != nullptr) {
    lv_obj_remove_style(button, this->key_style_, LV_PART_MAIN);
  }
  if (this->character_key_style_ != nullptr) {
    lv_obj_remove_style(button, this->character_key_style_, LV_PART_MAIN);
  }
  if (this->key_pressed_style_ != nullptr) {
    lv_obj_remove_style(button, this->key_pressed_style_, PRESSED_SELECTOR);
  }
  if (this->character_key_pressed_style_ != nullptr) {
    lv_obj_remove_style(button, this->character_key_pressed_style_, PRESSED_SELECTOR);
  }
  lv_obj_remove_style(button, &this->dim_style_, PRESSED_SELECTOR);

  lv_obj_t *label = this->labels_[row][col];
  if (is_character_key_(key)) {
#ifdef USE_FONT
    if (this->font_ != nullptr) {
      lv_obj_set_style_text_font(label, this->font_->get_lv_font(), LV_PART_MAIN);
    } else
#endif
    {
      lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
    }
  } else {
    // The built-in Montserrat font contains the FontAwesome glyphs used by the
    // stock keyboard (backspace, return, arrows, OK, close).
#if LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
#else
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
#endif
  }

  if (is_character_key_(key)) {
    if (this->character_key_style_ != nullptr) {
      lv_obj_add_style(button, this->character_key_style_, LV_PART_MAIN);
    }
    if (this->character_key_pressed_style_ != nullptr) {
      lv_obj_add_style(button, this->character_key_pressed_style_, PRESSED_SELECTOR);
    } else {
      lv_obj_add_style(button, &this->dim_style_, PRESSED_SELECTOR);
    }
  } else {
    if (this->key_style_ != nullptr) {
      lv_obj_add_style(button, this->key_style_, LV_PART_MAIN);
    }
    if (this->key_pressed_style_ != nullptr) {
      lv_obj_add_style(button, this->key_pressed_style_, PRESSED_SELECTOR);
    } else {
      lv_obj_add_style(button, &this->dim_style_, PRESSED_SELECTOR);
    }
  }
}

bool ButtonKeyboard::is_character_key_(const KeyDef &key) {
  // Character keys use their label as the inserted text. Space is the only
  // INSERT key with explicit text, and is treated as an action key.
  return key.action == KeyAction::INSERT && key.text == nullptr;
}

void ButtonKeyboard::layout_() {
  const ModeLayout &layout = LAYOUTS[this->mode_];
  int available_height = this->height_ - this->gap_ * (ROWS - 1);
  if (available_height < ROWS) {
    available_height = ROWS;
  }
  int y = 0;
  int used_height = 0;
  for (uint8_t row = 0; row < ROWS; row++) {
    int row_height = (row == ROWS - 1) ? available_height - used_height
                                       : available_height / ROWS;
    if (row_height < 1) {
      row_height = 1;
    }
    const uint8_t count = layout.counts[row];
    if (count > 0) {
      int available_width = this->width_ - this->gap_ * (count - 1);
      if (available_width < count) {
        available_width = count;
      }
      int total_weight = 0;
      for (uint8_t col = 0; col < count; col++) {
        total_weight += layout.rows[row][col].weight;
      }
      int x = 0;
      int used_width = 0;
      for (uint8_t col = 0; col < count; col++) {
        int key_width;
        if (col == count - 1) {
          key_width = available_width - used_width;
        } else {
          key_width = available_width * layout.rows[row][col].weight / total_weight;
        }
        if (key_width < 1) {
          key_width = 1;
        }
        lv_obj_set_pos(this->buttons_[row][col], x, y);
        lv_obj_set_size(this->buttons_[row][col], key_width, row_height);
        x += key_width + this->gap_;
        used_width += key_width;
      }
    }
    y += row_height + this->gap_;
    used_height += row_height;
  }
}

void ButtonKeyboard::on_press_(uint8_t row, uint8_t col) {
  this->stop_repeat_();
  const ModeLayout &layout = LAYOUTS[this->mode_];
  if (row >= ROWS || col >= layout.counts[row]) {
    return;
  }
  const KeyDef &key = layout.rows[row][col];
  this->on_key_(row, col);
  if (is_repeatable_(key.action)) {
    this->repeat_row_ = row;
    this->repeat_col_ = col;
    this->repeat_at_ = millis() + this->repeat_delay_;
    this->repeat_active_ = true;
  }
}

void ButtonKeyboard::loop() {
  if (!this->repeat_active_) {
    return;
  }
  const ModeLayout &layout = LAYOUTS[this->mode_];
  if (this->repeat_row_ >= ROWS || this->repeat_col_ >= layout.counts[this->repeat_row_] ||
      !is_repeatable_(layout.rows[this->repeat_row_][this->repeat_col_].action)) {
    this->stop_repeat_();
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->repeat_at_) < 0) {
    return;
  }
  this->repeat_at_ = now + this->repeat_interval_;
  this->on_key_(this->repeat_row_, this->repeat_col_);
}

void ButtonKeyboard::stop_repeat_() { this->repeat_active_ = false; }

bool ButtonKeyboard::is_repeatable_(KeyAction action) {
  switch (action) {
    case KeyAction::INSERT:
    case KeyAction::BACKSPACE:
    case KeyAction::NEWLINE:
    case KeyAction::LEFT:
    case KeyAction::RIGHT:
    case KeyAction::SIGN:
      return true;
    default:
      return false;
  }
}

lv_obj_t *ButtonKeyboard::get_key_button(uint8_t row, uint8_t col) const {
  if (row >= ROWS || col >= MAX_SLOTS[row]) {
    return nullptr;
  }
  return this->buttons_[row][col];
}

void ButtonKeyboard::on_key_(uint8_t row, uint8_t col) {
  const ModeLayout &layout = LAYOUTS[this->mode_];
  if (row >= ROWS || col >= layout.counts[row]) {
    return;
  }
  const KeyDef &key = layout.rows[row][col];
  switch (key.action) {
    case KeyAction::INSERT:
      if (this->textarea_ != nullptr) {
        lv_textarea_add_text(this->textarea_, key.text != nullptr ? key.text : key.label);
      }
      break;
    case KeyAction::BACKSPACE:
      if (this->textarea_ != nullptr) {
        lv_textarea_delete_char(this->textarea_);
      }
      break;
    case KeyAction::NEWLINE:
      if (this->textarea_ != nullptr) {
        lv_textarea_add_char(this->textarea_, '\n');
        if (lv_textarea_get_one_line(this->textarea_)) {
          this->fire_ready_();
        }
      }
      break;
    case KeyAction::LEFT:
      if (this->textarea_ != nullptr) {
        lv_textarea_cursor_left(this->textarea_);
      }
      break;
    case KeyAction::RIGHT:
      if (this->textarea_ != nullptr) {
        lv_textarea_cursor_right(this->textarea_);
      }
      break;
    case KeyAction::SIGN: {
      if (this->textarea_ == nullptr) {
        break;
      }
      uint32_t cursor = lv_textarea_get_cursor_pos(this->textarea_);
      const char *text = lv_textarea_get_text(this->textarea_);
      if (text[0] == '-') {
        lv_textarea_set_cursor_pos(this->textarea_, 1);
        lv_textarea_delete_char(this->textarea_);
        lv_textarea_add_char(this->textarea_, '+');
        lv_textarea_set_cursor_pos(this->textarea_, cursor);
      } else if (text[0] == '+') {
        lv_textarea_set_cursor_pos(this->textarea_, 1);
        lv_textarea_delete_char(this->textarea_);
        lv_textarea_add_char(this->textarea_, '-');
        lv_textarea_set_cursor_pos(this->textarea_, cursor);
      } else {
        lv_textarea_set_cursor_pos(this->textarea_, 0);
        lv_textarea_add_char(this->textarea_, '-');
        lv_textarea_set_cursor_pos(this->textarea_, cursor + 1);
      }
      break;
    }
    case KeyAction::MODE_LOWER:
      this->set_mode(static_cast<uint8_t>(KeyboardMode::TEXT_LOWER));
      break;
    case KeyAction::MODE_UPPER:
      this->set_mode(static_cast<uint8_t>(KeyboardMode::TEXT_UPPER));
      break;
    case KeyAction::MODE_SPECIAL:
      this->set_mode(static_cast<uint8_t>(KeyboardMode::SPECIAL));
      break;
    case KeyAction::MODE_NUMBER:
      this->set_mode(static_cast<uint8_t>(KeyboardMode::NUMBER));
      break;
    case KeyAction::READY:
      this->fire_ready_();
      break;
    case KeyAction::CANCEL:
      this->fire_cancel_();
      break;
    case KeyAction::NONE:
      break;
  }
}

void ButtonKeyboard::fire_ready_() {
  // Send the LVGL event before running a YAML trigger: the trigger may hide the
  // page or otherwise change the widget tree.
  if (this->textarea_ != nullptr) {
    lv_obj_send_event(this->textarea_, LV_EVENT_READY, nullptr);
  }
  if (this->ready_trigger_ != nullptr) {
    this->ready_trigger_->trigger();
  }
}

void ButtonKeyboard::fire_cancel_() {
  if (this->textarea_ != nullptr) {
    lv_obj_send_event(this->textarea_, LV_EVENT_CANCEL, nullptr);
  }
  if (this->cancel_trigger_ != nullptr) {
    this->cancel_trigger_->trigger();
  }
}

void ButtonKeyboard::set_textarea(lv_obj_t *textarea) {
  if (this->textarea_ != nullptr) {
    lv_obj_remove_state(this->textarea_, LV_STATE_FOCUSED);
  }
  this->textarea_ = textarea;
  if (this->textarea_ != nullptr) {
    lv_obj_add_state(this->textarea_, LV_STATE_FOCUSED);
  }
}

void ButtonKeyboard::set_mode(uint8_t mode) {
  if (mode > static_cast<uint8_t>(KeyboardMode::NUMBER) || mode == this->mode_) {
    return;
  }
  this->mode_ = mode;
  this->stop_repeat_();
  if (this->built_) {
    this->apply_mode_();
  }
}

void ButtonKeyboard::dump_config() {
  ESP_LOGCONFIG(TAG, "Button keyboard:");
  ESP_LOGCONFIG(TAG, "  Page: %s", this->page_ != nullptr ? "set" : "missing");
  ESP_LOGCONFIG(TAG, "  Mode: %s", MODE_NAMES[this->mode_]);
  ESP_LOGCONFIG(TAG, "  Bounds: %dx%d at %d,%d, gap %d", this->width_, this->height_, this->x_,
                this->y_, this->gap_);
  ESP_LOGCONFIG(TAG, "  Textarea: %s", this->textarea_ != nullptr ? "set" : "none");
  ESP_LOGCONFIG(TAG, "  Ready/cancel triggers: %s/%s",
                this->ready_trigger_ != nullptr ? "set" : "none",
                this->cancel_trigger_ != nullptr ? "set" : "none");
}

}  // namespace button_keyboard
}  // namespace esphome
