import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaClimate = balboa_spa_ns.class_("BalboaClimate", climate.Climate, cg.Component)

# ESPHome 2026.x exposes climate_schema()/new_climate() (CLIMATE_SCHEMA is now private).
CONFIG_SCHEMA = (
    climate.climate_schema(BalboaClimate)
    .extend({cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
