# button_keyboard

An optional text-entry keyboard for ESPHome LVGL built from one `lv_button` per key. It provides four stock-style layouts and textarea editing, with individual objects for styling and small key redraws. It works independently of the NSPanel terminal.

## Try the stock keyboard first

The stock `lvgl.keyboard` can also redraw individual keys. With the default pressed-state styles, LVGL marks the whole matrix dirty when its pressed state changes. Removing those styles avoids that redraw; a draw-task callback restores visible feedback for the selected key. This needs no external component:

```yaml
esphome:
  on_boot:
    - priority: -100
      then:
        - lambda: |-
            lv_obj_t *kb = id(kb)->obj;
            lv_obj_remove_style(kb, nullptr,
                                (lv_style_selector_t) LV_PART_ANY | (lv_style_selector_t) LV_STATE_PRESSED);
            lv_obj_remove_flag(kb, LV_OBJ_FLAG_CLICK_FOCUSABLE);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
            lv_obj_add_event_cb(kb, [](lv_event_t *e) {
              lv_obj_t *obj = lv_event_get_target_obj(e);
              if (!lv_obj_has_state(obj, LV_STATE_PRESSED)) return;
              lv_draw_task_t *task = lv_event_get_draw_task(e);
              auto *base = (lv_draw_dsc_base_t *) lv_draw_task_get_draw_dsc(task);
              if (base->part != LV_PART_ITEMS ||
                  base->id1 != lv_buttonmatrix_get_selected_button(obj)) return;
              if (auto *fill = lv_draw_task_get_fill_dsc(task)) {
                fill->color = lv_color_hex(0x4B5563);
                fill->opa = LV_OPA_COVER;
              }
            }, LV_EVENT_DRAW_TASK_ADDED, nullptr);
```

Merge these entries into your existing configuration. This example targets ESPHome 2026.10.1 / LVGL 9.5.0; draw-task events use `LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS` in that version. The boot lambda must run after widget creation, and pressed-state styles added later can reintroduce broad redraws.

Use the custom component below when you want separate key objects, ordinary per-button styles or configurable repeat timing. It uses more memory, and is not required merely to avoid whole-matrix pressed-state redraws.

## Requirements

- ESPHome 2026.10.x.
- LVGL with the `button`, `label` and `textarea` widgets. The component enables those widgets when it is compiled.
- LVGL's built-in Montserrat 14 font is enabled (the default) so the action-key symbols render. If you disable it, enable a font that contains the FontAwesome symbol glyphs.

## Install

Load the component from the repository:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ptaru/nspanel-terminal
      ref: v0.8.0
    components: [button_keyboard]
```

For a local checkout, replace the source with:

```yaml
external_components:
  - source:
      type: local
      path: /path/to/nspanel-terminal/components
    components: [button_keyboard]
```

The component is a **top-level `button_keyboard:` block**, not an `lvgl:` widget. It creates its container on a page you specify. The top-level block must be present for ESPHome to compile the component.

## Example

This is a complete LVGL page and keyboard. Adjust the size and colours for your display.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ptaru/nspanel-terminal
      ref: v0.8.0
    components: [button_keyboard]

lvgl:
  style_definitions:
    - id: keyboard_background
      bg_color: 0x1F2937
      bg_opa: COVER
      border_width: 0
      radius: 12
      pad_all: 0
    - id: keyboard_character
      bg_opa: TRANSP
      border_width: 0
      radius: 8
      pad_all: 0
    - id: keyboard_character_pressed
      bg_color: 0x374151
      bg_opa: COVER
      border_width: 0
      radius: 8
      pad_all: 0
    - id: keyboard_action
      bg_color: 0x111827
      bg_opa: COVER
      border_width: 0
      radius: 8
      pad_all: 0
    - id: keyboard_action_pressed
      bg_color: 0x4B5563
      bg_opa: COVER
      border_width: 0
      radius: 8
      pad_all: 0

  pages:
    - id: text_page
      widgets:
        - textarea:
            id: text_input
            x: 10
            y: 10
            width: 434
            height: 64
            max_length: 64

button_keyboard:
  id: keyboard
  page: text_page
  textarea: text_input
  x: 0
  y: 82
  width: 454
  height: 230
  mode: TEXT_LOWER
  background_style: keyboard_background
  character_key_style: keyboard_character
  character_key_pressed_style: keyboard_character_pressed
  key_style: keyboard_action
  key_pressed_style: keyboard_action_pressed
  text_color: 0xF9FAFB
  on_ready:
    - logger.log: "accepted"
  on_cancel:
    - logger.log: "cancelled"
```

`font:` is optional. When set, it applies to character-key labels; action keys always use the built-in symbol font. Text labels such as `ABC`, `Space` and `1#` use the action-key font.

## Options

| Key | Default | Meaning |
| --- | --- | --- |
| `page` | required | LVGL page that owns the keyboard. |
| `textarea` | optional | Textarea to edit. Without one, mode, ready and cancel still work. |
| `x`, `y`, `width`, `height` | required | Position and size of the keyboard container. |
| `mode` | `TEXT_LOWER` | Initial mode: `TEXT_LOWER`, `TEXT_UPPER`, `SPECIAL`, `NUMBER`. |
| `key_gap` | `2` | Gap between keys and rows, in pixels. |
| `repeat_delay` | `400ms` | Hold time before a repeatable key starts repeating. |
| `repeat_interval` | `100ms` | Time between repeats after the initial delay. |
| `background_style` | none | Style for the keyboard container. |
| `character_key_style` | none | Style for character keys. |
| `character_key_pressed_style` | none | Pressed style for character keys. |
| `key_style` | none | Style for modifier and action keys. |
| `key_pressed_style` | none | Pressed style for modifier/action keys. Without it, the key dims. |
| `font` | none | Font for character-key labels. |
| `text_color` | none | Colour for all key labels. |
| `on_ready` | none | Runs when OK is pressed, or Enter in a one-line textarea. |
| `on_cancel` | none | Runs when Close is pressed. |

If `character_key_style` or `key_style` is omitted, that key group is transparent; the example supplies both styles.

## Actions

Switch modes from YAML or lambdas:

```yaml
- button_keyboard.set_mode:
    id: keyboard
    mode: NUMBER
```

The C++ object also exposes `press_key(row, column)`, `get_key_button(row, column)`, `get_mode()` and `set_textarea(lv_obj_t *)` for scripted tests and accessibility integrations.

## Layouts

The key maps follow LVGL 9's `lv_keyboard`:

| Mode | Rows |
| --- | --- |
| `TEXT_LOWER` | `1# q w e r t y u i o p Backspace`; `ABC a s d f g h j k l Enter`; `_ - z x c v b n m . , :`; `Close ← Space → OK` |
| `TEXT_UPPER` | Same with capitals and `abc` instead of `ABC`. |
| `SPECIAL` | `1..0 Backspace`; `abc + & / * = % ! ? # < >`; `\ @ $ ( ) { } [ ] ; " '`; `Close ← Space → OK` |
| `NUMBER` | `1 2 3 Close`; `4 5 6 OK`; `7 8 9 Backspace`; `+/- 0 . ← →` |

Key widths use LVGL's relative weights, so each row fills the configured container. Backspace, Enter, the cursor arrows, OK and Close use LVGL's stock symbols (`LV_SYMBOL_*`); character labels use the configured `font`.

## Behaviour

- Text is inserted at the textarea cursor with `lv_textarea_add_text`, so a selection is replaced and `max_length`, `accepted_chars` and `password_mode` are respected.
- Backspace deletes one character. The cursor arrows use LVGL's cursor calls.
- Enter inserts a newline, or fires `on_ready` when the textarea is one-line.
- `+/-` toggles the sign of the number at the cursor.
- Character, Backspace, Enter, cursor and sign keys repeat while held. Mode, OK and Close do not repeat. Releasing the key or losing the press stops the repeat.
- OK and Close fire `on_ready` / `on_cancel` and send `LV_EVENT_READY` / `LV_EVENT_CANCEL` to the textarea.
- The keys and container are not click-focusable, so using the keyboard does not move focus away from the textarea or hide its caret.

## Differences from `lvgl.keyboard`

| Capability | `lvgl.keyboard` | `button_keyboard` |
| --- | --- | --- |
| Four modes and mode switching | yes | yes, plus `set_mode` |
| Textarea binding | `textarea:` | `textarea:` |
| Insertion, selection, max length, accepted chars, password mode | yes | yes |
| Backspace, Enter, cursor, sign | yes | yes |
| Press-and-hold repeat | yes | yes, configurable |
| Ready/cancel events | LVGL events | LVGL events plus triggers |
| Damage per key press | whole matrix with default styles; key-sized with the workaround above | one button |
| Popovers | C API only | not implemented |
| Custom/user key maps | C API only | not implemented |

The main layout difference is configuration: this is a top-level component rather than an `lvgl:` widget.

## Memory

Each key is an `lv_button` plus an `lv_label`, so the component uses more LVGL object memory than the single matrix used by `lvgl.keyboard`. Individual objects offer direct per-key styling and avoid the matrix's extra invalidation padding. On a memory-constrained board, measure the LVGL heap and consider enabling PSRAM or reducing the keyboard size.

## License

MIT. See the repository [LICENSE](../../LICENSE).
