"""A text keyboard made from individual LVGL buttons.

ESPHome's stock ``lvgl.keyboard`` is one ``lv_buttonmatrix`` object. LVGL's
base object handler marks that whole matrix pressed on every tap, so one key
invalidates the entire keyboard. This component creates one button per key
instead. A tap invalidates only the tapped button; the bound textarea
invalidates only the text that actually changes.

The key maps and text-editing behaviour follow LVGL's ``lv_keyboard``:
TEXT_LOWER, TEXT_UPPER, SPECIAL and NUMBER layouts, cursor movement,
backspace, newline/one-line ready, the numeric +/- toggle, and ready/cancel
events. Popovers and user maps are not implemented because the ESPHome
keyboard schema does not expose them either.
"""

import esphome.automation as automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.automation import Trigger
from esphome.components.font import Font
from esphome.components.lvgl.defines import add_lv_use
from esphome.components.lvgl.lv_validation import lv_color
from esphome.components.lvgl.types import lv_page_t, lv_style_t
from esphome.components.lvgl.widgets.textarea import lv_textarea_t
from esphome.const import (
    CONF_HEIGHT,
    CONF_ID,
    CONF_MODE,
    CONF_TRIGGER_ID,
    CONF_WIDTH,
    CONF_X,
    CONF_Y,
)

DEPENDENCIES = ["lvgl"]

CONF_PAGE = "page"
CONF_TEXTAREA = "textarea"
CONF_BACKGROUND_STYLE = "background_style"
CONF_KEY_STYLE = "key_style"
CONF_KEY_PRESSED_STYLE = "key_pressed_style"
CONF_CHARACTER_KEY_STYLE = "character_key_style"
CONF_CHARACTER_KEY_PRESSED_STYLE = "character_key_pressed_style"
CONF_KEY_GAP = "key_gap"
CONF_REPEAT_DELAY = "repeat_delay"
CONF_REPEAT_INTERVAL = "repeat_interval"
CONF_FONT = "font"
CONF_TEXT_COLOR = "text_color"
CONF_ON_READY = "on_ready"
CONF_ON_CANCEL = "on_cancel"
CONF_SET_MODE = "button_keyboard.set_mode"

button_keyboard_ns = cg.esphome_ns.namespace("button_keyboard")
ButtonKeyboard = button_keyboard_ns.class_("ButtonKeyboard", cg.Component)
ButtonKeyboardSetModeAction = button_keyboard_ns.class_(
    "ButtonKeyboardSetModeAction", automation.Action
)

# Keep in step with the C++ Mode enum and key-map table.
MODES = {
    "TEXT_LOWER": 0,
    "TEXT_UPPER": 1,
    "SPECIAL": 2,
    "NUMBER": 3,
}

def _validate(config):
    # LVGL generates its widget defines before external-component codegen, so
    # request the widgets here rather than in to_code().
    add_lv_use("button", "label", "textarea")
    return config


CONFIG_SCHEMA = cv.All(cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(ButtonKeyboard),
        cv.Required(CONF_PAGE): cv.use_id(lv_page_t),
        cv.Optional(CONF_TEXTAREA): cv.use_id(lv_textarea_t),
        cv.Required(CONF_X): cv.int_,
        cv.Required(CONF_Y): cv.int_,
        cv.Required(CONF_WIDTH): cv.int_range(min=1),
        cv.Required(CONF_HEIGHT): cv.int_range(min=1),
        cv.Optional(CONF_MODE, default="TEXT_LOWER"): cv.one_of(*MODES, upper=True),
        cv.Optional(CONF_KEY_GAP, default=2): cv.int_range(min=0, max=16),
        cv.Optional(
            CONF_REPEAT_DELAY, default="400ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_REPEAT_INTERVAL, default="100ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_BACKGROUND_STYLE): cv.use_id(lv_style_t),
        cv.Optional(CONF_KEY_STYLE): cv.use_id(lv_style_t),
        cv.Optional(CONF_KEY_PRESSED_STYLE): cv.use_id(lv_style_t),
        cv.Optional(CONF_CHARACTER_KEY_STYLE): cv.use_id(lv_style_t),
        cv.Optional(CONF_CHARACTER_KEY_PRESSED_STYLE): cv.use_id(lv_style_t),
        cv.Optional(CONF_FONT): cv.use_id(Font),
        cv.Optional(CONF_TEXT_COLOR): lv_color,
        cv.Optional(CONF_ON_READY): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Trigger.template()),
            }
        ),
        cv.Optional(CONF_ON_CANCEL): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Trigger.template()),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA), _validate)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_page(await cg.get_variable(config[CONF_PAGE])))
    if textarea := config.get(CONF_TEXTAREA):
        cg.add(var.set_textarea(await cg.get_variable(textarea)))
    cg.add(
        var.set_bounds(
            config[CONF_X],
            config[CONF_Y],
            config[CONF_WIDTH],
            config[CONF_HEIGHT],
        )
    )
    cg.add(var.set_mode(MODES[config[CONF_MODE]]))
    cg.add(var.set_key_gap(config[CONF_KEY_GAP]))
    cg.add(var.set_repeat_delay(config[CONF_REPEAT_DELAY].total_milliseconds))
    cg.add(var.set_repeat_interval(config[CONF_REPEAT_INTERVAL].total_milliseconds))
    if style := config.get(CONF_BACKGROUND_STYLE):
        cg.add(var.set_background_style(await cg.get_variable(style)))
    if style := config.get(CONF_KEY_STYLE):
        cg.add(var.set_key_style(await cg.get_variable(style)))
    if style := config.get(CONF_KEY_PRESSED_STYLE):
        cg.add(var.set_key_pressed_style(await cg.get_variable(style)))
    if style := config.get(CONF_CHARACTER_KEY_STYLE):
        cg.add(var.set_character_key_style(await cg.get_variable(style)))
    if style := config.get(CONF_CHARACTER_KEY_PRESSED_STYLE):
        cg.add(var.set_character_key_pressed_style(await cg.get_variable(style)))
    if font := config.get(CONF_FONT):
        cg.add(var.set_font(await cg.get_variable(font)))
    if color := config.get(CONF_TEXT_COLOR):
        cg.add(var.set_text_color(await lv_color.process(color)))

    for conf in config.get(CONF_ON_READY, ()):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.set_ready_trigger(trigger))
        await automation.build_automation(trigger, [], conf)
    for conf in config.get(CONF_ON_CANCEL, ()):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.set_cancel_trigger(trigger))
        await automation.build_automation(trigger, [], conf)


@automation.register_action(
    CONF_SET_MODE,
    ButtonKeyboardSetModeAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(ButtonKeyboard),
            cv.Required(CONF_MODE): cv.one_of(*MODES, upper=True),
        }
    ),
    synchronous=True,
)
async def set_mode_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(
        action_id, template_arg, parent, MODES[config[CONF_MODE]]
    )
