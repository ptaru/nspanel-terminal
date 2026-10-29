import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import touchscreen
from esphome.const import CONF_ID

from . import NSPanelDisplay, NSPanelTouchscreen

CONF_PANEL_ID = "panel_id"

CONFIG_SCHEMA = touchscreen.touchscreen_schema("80ms").extend(
    {
        cv.GenerateID(): cv.declare_id(NSPanelTouchscreen),
        cv.Required(CONF_PANEL_ID): cv.use_id(NSPanelDisplay),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await touchscreen.register_touchscreen(var, config)
    panel = await cg.get_variable(config[CONF_PANEL_ID])
    cg.add(var.set_panel(panel))
