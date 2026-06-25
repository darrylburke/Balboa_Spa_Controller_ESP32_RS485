import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaNumber = balboa_spa_ns.class_("BalboaNumber", number.Number, cg.Component)
SpaNumberType = balboa_spa_ns.enum("SpaNumberType")
# type -> (enum, min, max, step)
NUMBER_TYPES = {
    "c1_start_hour": (SpaNumberType.C1_START_HOUR, 0, 23, 1),
    "c1_start_minute": (SpaNumberType.C1_START_MINUTE, 0, 59, 1),
    "c1_duration": (SpaNumberType.C1_DURATION, 0, 1439, 1),
    "c2_start_hour": (SpaNumberType.C2_START_HOUR, 0, 23, 1),
    "c2_start_minute": (SpaNumberType.C2_START_MINUTE, 0, 59, 1),
    "c2_duration": (SpaNumberType.C2_DURATION, 0, 1439, 1),
}

CONFIG_SCHEMA = number.number_schema(BalboaNumber).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.one_of(*NUMBER_TYPES.keys(), lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    enum_val, mn, mx, step = NUMBER_TYPES[config[CONF_TYPE]]
    var = await number.new_number(config, min_value=mn, max_value=mx, step=step)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_number_type(enum_val))
