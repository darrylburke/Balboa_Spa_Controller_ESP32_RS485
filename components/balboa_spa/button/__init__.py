import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaButton = balboa_spa_ns.class_("BalboaButton", button.Button, cg.Component)
SpaButtonType = balboa_spa_ns.enum("SpaButtonType")
BUTTON_TYPES = {
    "normal_operation": SpaButtonType.BTN_NORMAL_OPERATION,
    "clear_notification": SpaButtonType.BTN_CLEAR_NOTIFICATION,
    "soak": SpaButtonType.BTN_SOAK,
}

CONFIG_SCHEMA = button.button_schema(BalboaButton).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(BUTTON_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_button_type(config[CONF_TYPE]))
