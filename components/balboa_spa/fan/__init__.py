import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan
from esphome.const import CONF_INDEX, CONF_SPEED_COUNT, CONF_OUTPUT_ID
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaFan = balboa_spa_ns.class_("BalboaFan", fan.Fan, cg.Component)
SpaFanType = balboa_spa_ns.enum("SpaFanType")
FAN_TYPES = {
    "pump": SpaFanType.FAN_PUMP,
    "blower": SpaFanType.FAN_BLOWER,
}

CONF_TYPE = "type"

CONFIG_SCHEMA = fan.FAN_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(BalboaFan),
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(FAN_TYPES, lower=True),
        cv.Optional(CONF_INDEX, default=1): cv.int_range(min=1, max=6),
        cv.Optional(CONF_SPEED_COUNT, default=2): cv.int_range(min=1, max=2),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_fan_type(config[CONF_TYPE]))
    cg.add(var.set_index(config[CONF_INDEX] - 1))
    cg.add(var.set_speed_count(config[CONF_SPEED_COUNT]))
