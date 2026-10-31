import logging

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import touchscreen
from esphome.const import CONF_DISPLAY, CONF_ID

from . import NSPanelDisplay, NSPanelTouchscreen

_LOGGER = logging.getLogger(__name__)

CONF_PANEL_ID = "panel_id"


def validate_panel(config):
    # display: is the standard touchscreen key and is now authoritative.
    # panel_id remains accepted so configs written for v0.7.0 keep working,
    # but it must point at the same display.
    if CONF_PANEL_ID in config and str(config[CONF_PANEL_ID]) != str(config[CONF_DISPLAY]):
        raise cv.Invalid("panel_id must match the touchscreen display: key")
    return config


CONFIG_SCHEMA = cv.All(
    touchscreen.touchscreen_schema("80ms").extend(
        {
            cv.GenerateID(): cv.declare_id(NSPanelTouchscreen),
            cv.Required(CONF_DISPLAY): cv.use_id(NSPanelDisplay),
            cv.Optional(CONF_PANEL_ID): cv.use_id(NSPanelDisplay),
        }
    ),
    validate_panel,
)


async def to_code(config):
    if CONF_PANEL_ID in config:
        _LOGGER.warning("panel_id is deprecated; the touchscreen display: key selects the panel")
    var = cg.new_Pvariable(config[CONF_ID])
    await touchscreen.register_touchscreen(var, config)
    panel = await cg.get_variable(config[CONF_DISPLAY])
    cg.add(var.set_panel(panel))
