import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE, CONF_INDEX
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaSwitch = balboa_spa_ns.class_("BalboaSwitch", switch.Switch, cg.Component)
SpaSwitchType = balboa_spa_ns.enum("SpaSwitchType")
SWITCH_TYPES = {
    "light": SpaSwitchType.SW_LIGHT,
    "aux": SpaSwitchType.SW_AUX,
    "mister": SpaSwitchType.SW_MISTER,
    "hold": SpaSwitchType.SW_HOLD,
    "pump": SpaSwitchType.SW_PUMP_ONOFF,
    "blower": SpaSwitchType.SW_BLOWER_ONOFF,
}

CONFIG_SCHEMA = switch.switch_schema(BalboaSwitch).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(SWITCH_TYPES, lower=True),
        cv.Optional(CONF_INDEX, default=1): cv.int_range(min=1, max=6),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_switch_type(config[CONF_TYPE]))
    cg.add(var.set_index(config[CONF_INDEX] - 1))
