import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, uart
from esphome.components.const import BYTE_ORDER_LITTLE
from esphome.const import CONF_AUTO_CLEAR_ENABLED, CONF_ID, CONF_LAMBDA, CONF_PAGES, CONF_ROTATION, CONF_UART_ID

from . import NSPanelDisplay

DEPENDENCIES = ["uart", "psram"]

CONF_DIAGNOSTICS = "diagnostics"


def validate_config(config):
    display.add_metadata(
        config[CONF_ID],
        454,
        320,
        byte_order=BYTE_ORDER_LITTLE,
        has_writer=bool(config.get(CONF_LAMBDA) or config.get(CONF_PAGES))
        or config.get(CONF_AUTO_CLEAR_ENABLED) is True,
        rotation=config.get(CONF_ROTATION, 0),
    )
    return config


CONFIG_SCHEMA = cv.All(
    cv.only_on_esp32,
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(NSPanelDisplay),
            cv.Optional(CONF_DIAGNOSTICS, default=False): cv.boolean,
        }
    ).extend(uart.UART_DEVICE_SCHEMA),
    validate_config,
)


async def to_code(config):
    uart_parent = await cg.get_variable(config[CONF_UART_ID])
    cg.add(uart_parent.set_tx_buffer_size(2048))
    cg.add(uart_parent.set_rx_full_threshold(32))
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_diagnostics(config[CONF_DIAGNOSTICS]))
    await display.register_display(var, config)
    await uart.register_uart_device(var, config)
    if CONF_LAMBDA in config:
        writer = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(writer))
