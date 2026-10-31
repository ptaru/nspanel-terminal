import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, uart
from esphome.components.esp32 import include_builtin_idf_component
from esphome.components.const import BYTE_ORDER_LITTLE
from esphome.const import (
    CONF_AUTO_CLEAR_ENABLED,
    CONF_HEIGHT,
    CONF_ID,
    CONF_LAMBDA,
    CONF_PAGES,
    CONF_ROTATION,
    CONF_WIDTH,
)

from . import NSPanelDisplay

DEPENDENCIES = ["uart"]

CONF_DIAGNOSTICS = "diagnostics"
CONF_TFT_URL = "tft_url"
CONF_BRIGHTNESS = "brightness"
CONF_FRAME_SLOTS_RAM = "frame_slots_ram"

PANEL_SIZES = {(454, 320), (320, 480)}


def http_tft_url(value):
    value = cv.url(value)
    if not value.startswith("http://"):
        raise cv.Invalid("tft_url must use HTTP")
    return value


def validate_config(config):
    size = (config[CONF_WIDTH], config[CONF_HEIGHT])
    if size not in PANEL_SIZES:
        raise cv.Invalid(
            f"Unsupported panel size {size[0]}x{size[1]}; use 454x320 for the "
            "EU landscape panel or 320x480 for the US portrait panel"
        )
    # 0.7.0 streams LVGL flush areas directly and no longer owns a framebuffer.
    # The DisplayBuffer writer path would silently render nothing, so reject it
    # here instead of exposing a config that cannot work.
    unsupported = []
    if CONF_LAMBDA in config:
        unsupported.append("lambda")
    if CONF_PAGES in config:
        unsupported.append("pages")
    if config.get(CONF_AUTO_CLEAR_ENABLED) is True:
        unsupported.append("auto_clear_enabled: true")
    if config.get(display.CONF_SHOW_TEST_CARD):
        unsupported.append("show_test_card")
    if unsupported:
        raise cv.Invalid(
            "nspanel_terminal only supports LVGL flush streaming; "
            f"{', '.join(unsupported)} would write to a framebuffer that no longer exists"
        )
    if config.get(CONF_ROTATION, 0):
        raise cv.Invalid(
            "nspanel_terminal only supports rotation 0; set rotation under lvgl: instead"
        )
    display.add_metadata(
        config[CONF_ID],
        config[CONF_WIDTH],
        config[CONF_HEIGHT],
        byte_order=BYTE_ORDER_LITTLE,
        has_writer=False,
        rotation=0,
    )
    return config


CONFIG_SCHEMA = cv.All(
    cv.only_on_esp32,
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(NSPanelDisplay),
            cv.Optional(CONF_WIDTH, default=454): cv.int_range(min=1, max=480),
            cv.Optional(CONF_HEIGHT, default=320): cv.int_range(min=1, max=480),
            cv.Optional(CONF_DIAGNOSTICS, default=False): cv.boolean,
            cv.Optional(CONF_FRAME_SLOTS_RAM, default="internal"): cv.one_of(
                "internal", "psram", lower=True
            ),
            cv.Optional(CONF_TFT_URL): http_tft_url,
            cv.Optional(CONF_BRIGHTNESS): cv.int_range(min=0, max=100),
        }
    ).extend(uart.UART_DEVICE_SCHEMA),
    validate_config,
)

# The terminal changes the UART to 4 Mbaud after entry and restores 115200
# during recovery. Recovery cannot work if the bus is configured differently.
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "nspanel_terminal",
    baud_rate=115200,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    include_builtin_idf_component("esp_http_client")
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_panel_size(config[CONF_WIDTH], config[CONF_HEIGHT]))
    cg.add(var.set_diagnostics(config[CONF_DIAGNOSTICS]))
    cg.add(var.set_frame_slots_ram(config[CONF_FRAME_SLOTS_RAM] == "psram"))
    if CONF_TFT_URL in config:
        cg.add(var.set_tft_url(config[CONF_TFT_URL]))
    if CONF_BRIGHTNESS in config:
        cg.add(var.set_initial_brightness(config[CONF_BRIGHTNESS]))
    await display.register_display(var, config)
    await uart.register_uart_device(var, config)
