import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaSelect = balboa_spa_ns.class_("BalboaSelect", select.Select, cg.Component)
SpaSelectType = balboa_spa_ns.enum("SpaSelectType")
SELECT_TYPES = {
    "heating_mode": (SpaSelectType.SEL_HEATING_MODE, ["ready", "rest"]),
    "temperature_range": (SpaSelectType.SEL_TEMP_RANGE, ["high", "low"]),
    "temperature_scale": (SpaSelectType.SEL_TEMP_SCALE, ["fahrenheit", "celsius"]),
}

CONFIG_SCHEMA = select.select_schema(BalboaSelect).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.one_of(*SELECT_TYPES.keys(), lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    enum_val, options = SELECT_TYPES[config[CONF_TYPE]]
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_select_type(enum_val))
