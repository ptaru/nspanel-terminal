import esphome.codegen as cg
from esphome.components import display, touchscreen, uart

nspanel_terminal_ns = cg.esphome_ns.namespace("nspanel_terminal")
NSPanelDisplay = nspanel_terminal_ns.class_(
    "NSPanelDisplay", display.DisplayBuffer, uart.UARTDevice
)
NSPanelTouchscreen = nspanel_terminal_ns.class_(
    "NSPanelTouchscreen", touchscreen.Touchscreen
)
